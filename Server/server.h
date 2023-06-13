#pragma once
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/redirect_error.hpp>
#include <asio/write.hpp>
#include <format>
#include <unordered_map>
#include <memory>
#include <queue>
#include "logger.h"
#include "method.h"
#include "serializer.h"
#include "protocol.h"

namespace crpc {

    static const bool DEFAULT_SERVER_AUTO_PUSH_SERVICE = false;

    // RPC服务器
    class server final : public std::enable_shared_from_this<server> {
    private:
        struct registry_session {
            asio::ip::tcp::resolver resolver;
            asio::ip::tcp::socket socket;
            asio::steady_timer timer;
            std::queue<proto::package> send_queue;
            bool closed = false;

            void push_send(proto::package package) {
                send_queue.push(std::move(package));
                timer.cancel_one();
            }
        };

        struct client_session {
            asio::ip::tcp::socket socket;
            asio::steady_timer timer;
            std::queue<proto::package> send_queue;
            bool closed;

            void push_send(proto::package package) {
				send_queue.push(std::move(package));
                timer.cancel_one();
			}
        };

        std::string _host;
        uint16_t _port;
        
        asio::io_context _io_context;
        asio::executor_work_guard<asio::io_context::executor_type> _work_guard;
        std::jthread _io_thread;
        std::unique_ptr<asio::ip::tcp::acceptor> _acceptor;

        std::unordered_map<std::string, std::unique_ptr<crpc::method_base_t>> _methods;
        std::unordered_map<std::string, std::unique_ptr<crpc::awaitable_method_base_t>> _awaitable_methods;

        std::unordered_set<std::string> _last_available_services;
        std::unordered_set<std::string> _current_available_services;

        std::shared_ptr<registry_session> _registry_session = nullptr;

        uint32_t _current_seq_id = 0;

        bool _auto_push_service = DEFAULT_SERVER_AUTO_PUSH_SERVICE;


        // 监听协程
        asio::awaitable<void> _listener()
        {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            LOGGER.log_info("server listening on port {}", _port);
            while (true) {
                // 接受客户端连接 生成客户端会话协程
                asio::ip::tcp::socket socket = co_await _acceptor->async_accept(asio::use_awaitable);
                auto session = std::make_shared<client_session>(client_session{
                    std::move(socket),
                    asio::steady_timer(self->get_io_context()),
                    std::queue<proto::package>()
                    });
                session->timer.expires_at(std::chrono::steady_clock::time_point::max());
                asio::co_spawn(executor, [self, &session] { return self->_client_session_recv(session); }, asio::detached);
                asio::co_spawn(executor, [self, &session] { return self->_client_session_send(session); }, asio::detached);
            }
        }

        // 处理单个客户端接受的协程
        asio::awaitable<void> _client_session_recv(std::shared_ptr<client_session> session) {
            auto& [socket, timer, send_queue, closed] = *session;
            const std::string addr_str = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
            LOGGER.log_info("<{}> client session recv started", addr_str);

            try {
                while (socket.is_open()) {
                    // 接受请求
                    proto::package request;
                    co_await request.await_read_from(socket);
                    LOGGER.log_debug("<{}> recv from client: {}", addr_str, request.brief_info());

                    if (request.type() == proto::request_type::RPC_METHOD_REQUEST) {
                        // 方法调用请求
                        std::string data = request.data();
                        std::string name = crpc::serializer::instance().deserialize_rpc_request_method(data);
                        LOGGER.log_info("<{}> rpc request #{} ({}) from client", addr_str, request.seq_id(), name);

                        std::string response_data;
                        auto it1 = _methods.find(name);
                        auto it2 = _awaitable_methods.find(name);

                        if (it1 != _methods.end()) {
                            // 调用方法
                            response_data = it1->second->call(data);
                            LOGGER.log_info("<{}> method {} done for rpc request #{}", addr_str, name, request.seq_id());
                            // 响应入队
                            proto::package response(proto::request_type::RPC_METHOD_RESPONSE, request.seq_id(), response_data);
                            session->push_send(std::move(response));
                        }
                        else if (it2 != _awaitable_methods.end()) {
                            asio::co_spawn(_io_context, [self = shared_from_this(), &socket, &name, &addr_str, &session, req = std::move(request)]()
                            -> asio::awaitable<void> {
                                // 调用协程方法
                                auto response_data = co_await self->_awaitable_methods[name]->call(req.data());
                                LOGGER.log_info("<{}> method {} done for rpc request #{}", addr_str, name, req.seq_id());
                                // 响应入队
                                proto::package response(proto::request_type::RPC_METHOD_RESPONSE, req.seq_id(), response_data);
                                session->push_send(std::move(response));
                            }, asio::detached);
                        }
                        else {
                            LOGGER.log_info("<{}> method {} no found for rpc request #{}", addr_str, name, request.seq_id());
                            // 响应入队
                            auto response_data = serializer::instance().serialize_rpc_response<int>({}, "rpc method no found");
                            proto::package response(proto::request_type::RPC_METHOD_RESPONSE, request.seq_id(), response_data);
                            session->push_send(std::move(response));
                        }
                    }
                }
            }
            catch (std::exception& e) {
                LOGGER.log_error("<{}> exception in client session recv: {}", addr_str, e.what());
                disconnect_client(session);
            }

            LOGGER.log_info("<{}> client session recv closed", addr_str);
        }

        // 处理单个客户端发送的协程
        asio::awaitable<void> _client_session_send(std::shared_ptr<client_session> session) {
            auto& [socket, timer, send_queue, closed] = *session;
            const std::string addr_str = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
            LOGGER.log_info("<{}> client session send started", addr_str);

            try {
                while (socket.is_open()) {
                    if (send_queue.empty()) {
                        asio::error_code ec;
                        co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    }
                    else {
                        // 发送响应
                        proto::package& response = send_queue.front();
                        co_await response.await_write_to(socket);
                        LOGGER.log_debug("<{}> send to client: {}", addr_str, response.brief_info());
                        send_queue.pop();
                    }
                }
            }
            catch (std::exception& e) {
                LOGGER.log_error("<{}> exception in client session send: {}", addr_str, e.what());
                disconnect_client(session);
            }

            LOGGER.log_info("<{}> client session send closed", addr_str);
        }

        // 关闭客户端连接
        void disconnect_client(std::shared_ptr<client_session> session) {
            if (session->closed) return;
            LOGGER.log_info("<{}:{}> disconnect client", session->socket.remote_endpoint().address().to_string(), std::to_string(session->socket.remote_endpoint().port()));
            session->socket.close();
            session->timer.cancel();
            session->closed = true;
        }


        // 处理注册中心接受的协程
        asio::awaitable<void> _registry_session_recv(std::shared_ptr<registry_session> session) {
            LOGGER.log_info("registry session recv started");

            try {
                while (session->socket.is_open()) {
                    // 接受包
                    proto::package pack;
                    co_await pack.await_read_from(session->socket);
                    LOGGER.log_debug("recv from registry : {}", pack.brief_info());

                    if (pack.type() == proto::request_type::HEARTBEAT) {
                        // 心跳包
                        LOGGER.log_debug("recv heartbeat from registry");
                        proto::package response(proto::request_type::HEARTBEAT_RESPONSE, pack.seq_id());
                        session->send_queue.push(std::move(response));
                    }
                }
            }
            catch (std::exception& e) {
                LOGGER.log_error("exception in registry send: {}", e.what());
                _disconnect_registry(session);
            }

            LOGGER.log_info("registry session recv closed");
        }

        // 处理注册中心发送的协程
        asio::awaitable<void> _registry_session_send(std::shared_ptr<registry_session> session) {
            LOGGER.log_info("registry session send started");

            try {
                while (session->socket.is_open()) {
                    if (session->send_queue.empty()) {
                        asio::error_code ec;
                        co_await session->timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    }
                    else {
                        // 发送响应
                        proto::package& response = session->send_queue.front();
                        co_await response.await_write_to(session->socket);
                        LOGGER.log_debug("send to registry : {}", response.brief_info());
                        session->send_queue.pop();
                    }
                }
            }
            catch (std::exception& e) {
                LOGGER.log_error("exception in registry send: {}", e.what());
                _disconnect_registry(session);
            }

            LOGGER.log_info("registry session send closed");
        }

        // 关闭注册中心连接
        void _disconnect_registry(std::shared_ptr<registry_session> session) {
			if (session->closed) return;
			LOGGER.log_info("disconnect registry");
			session->socket.close();
            session->resolver.cancel();
			session->timer.cancel();
			session->closed = true;
            _registry_session = nullptr;
            _last_available_services.clear();
		}

    public:
        server(const std::string& host, uint16_t port) 
            : _host(host), _port(port), _io_context(1), _work_guard(asio::make_work_guard(_io_context.get_executor())) {
            _io_thread = std::jthread([&io_context = this->_io_context] {
                LOGGER.log_debug("server io thread started");
                io_context.run();
                LOGGER.log_debug("server io thread stopped");
            });
        }

        ~server() { stop(); }

        server(const server&) = delete;
        server& operator=(const server&) = delete;

        // 获取io_context
        asio::io_context& get_io_context() {
			return _io_context;
		}

        // 设置是否在更新服务时自动推送服务到注册中心
        void set_auto_push_service(bool flag) {
			_auto_push_service = flag;
		}

        // 连接注册中心
        void connect_registry(const std::string& host, uint16_t port) {
            // 启动初始化协程 并阻塞等待
            try {
                _registry_session = std::make_shared<registry_session>(registry_session{
                    std::move(asio::ip::tcp::resolver(_io_context)),
                    std::move(asio::ip::tcp::socket(_io_context)),
                    std::move(asio::steady_timer(_io_context)),
                    std::queue<proto::package>()
                });

                // 连接
                auto session = _registry_session;
                auto endpoints = session->resolver.resolve(host, std::to_string(port));
                session->socket.connect(*endpoints.begin());
                LOGGER.log_info("connect to registry {}:{}", host, port);

                // 发送初始化包
                std::vector<std::pair<std::string, bool>> updates{};
                for (auto& name : _current_available_services) updates.push_back(std::make_pair(name, true));
                auto addr_str = host + ":" + std::to_string(_port);
                auto data = serializer::instance().serialize_server_online(updates, addr_str);
                proto::package pack(proto::request_type::RPC_SERVER_ONLINE, _current_seq_id++, data);
                pack.write_to(session->socket);
                LOGGER.log_debug("online request send to registry");

                // 等待初始化响应 TODO 超时处理
                proto::package response;
                response.read_from(session->socket);
                if (response.type() != proto::request_type::RPC_SERVER_ONLINE_RESPONSE) {
                    throw std::runtime_error("invalid response from registry");
                }
                LOGGER.log_debug("recv online response from registry");

                _last_available_services = _current_available_services;

                // 生成会话
                asio::co_spawn(_io_context, [self = shared_from_this()] { return self->_registry_session_recv(self->_registry_session); }, asio::detached);
                asio::co_spawn(_io_context, [self = shared_from_this()] { return self->_registry_session_send(self->_registry_session); }, asio::detached);
            }
            catch (const std::exception& e) {
                _registry_session = nullptr;
                throw e;
            }
        }

        // 启动服务端
        void start() {
            _acceptor = std::make_unique<asio::ip::tcp::acceptor>(_io_context, asio::ip::tcp::endpoint(asio::ip::make_address(_host), _port));
            asio::co_spawn(_io_context, [self = shared_from_this()] { return self->_listener(); }, asio::detached);
        }

        // 停止服务端
        void stop() {
            if(_registry_session) _disconnect_registry(_registry_session);
			_acceptor->close();
			_work_guard.reset();
			_io_context.stop();
			_io_thread.join();
		}

        // 注册一个服务
        template<class return_t, class... args_t>
        std::shared_ptr<server> register_service(const std::string& name, std::function<return_t(args_t...)> method) {
            unregister_service(name);
            _methods[name] = std::make_unique<method_t<return_t, args_t...>>(method);
            _current_available_services.insert(name);
            LOGGER.log_info("method {} registered", name);
            if (_auto_push_service) push_service_update();
            return shared_from_this();
        }

        // 注册一个协程服务
        template<class return_t, class... args_t>
        std::shared_ptr<server> register_service(const std::string& name, std::function<asio::awaitable<return_t>(args_t...)> method) {
            unregister_service(name);
            _awaitable_methods[name] = std::make_unique<awaitable_method_t<return_t, args_t...>>(method);
            _current_available_services.insert(name);
            LOGGER.log_info("awaitable method {} registered", name);
            if (_auto_push_service) push_service_update();
            return shared_from_this();
        }

        // 删除一个服务
        std::shared_ptr<server> unregister_service(const std::string& name) {
            if (_methods.contains(name)) {
                _methods.erase(name);
                _current_available_services.erase(name);
                LOGGER.log_info("method {} unregistered", name);
                if (_auto_push_service) push_service_update();
            }
            else if (_awaitable_methods.contains(name)) {
                _awaitable_methods.erase(name);
                _current_available_services.erase(name);
                LOGGER.log_info("awaitable method {} unregistered", name);
                if (_auto_push_service) push_service_update();
            }
            return shared_from_this();
        }

        // 推送服务更新到注册中心
        std::shared_ptr<server> push_service_update() {
            if (!_registry_session) {
                throw std::runtime_error("registry not connected");
            }

            std::vector<std::pair<std::string, bool>> updates{};
            size_t add_count{}, del_count{};
            for (auto& name : _current_available_services)
                if (!_last_available_services.contains(name)) {
                    updates.emplace_back(name, true);
                    ++add_count;
                }
            for (auto& name : _last_available_services)
                if (!_current_available_services.contains(name)) {
                    updates.emplace_back(name, false);
                    ++del_count;
                }
            auto data = serializer::instance().serialize_provide_update(updates);

            proto::package pack(proto::request_type::RPC_SERVICE_PROVIDE_UPDATE, _current_seq_id++, data);
            _registry_session->send_queue.push(std::move(pack));
           
            _last_available_services = _current_available_services;

            LOGGER.log_info("push service update to registry: {} adds {} dels", add_count, del_count);
            return shared_from_this();
		}

    };

}