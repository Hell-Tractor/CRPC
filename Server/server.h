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
#include "cereal.h"
#include "protocol.h"

namespace crpc {

    // RPC服务器
    class server final : public std::enable_shared_from_this<server> {
    private:
        struct client_session {
            asio::ip::tcp::socket socket;
            asio::steady_timer timer;
            std::queue<proto::package> send_queue;
            bool closed = false;

            void push_send(proto::package package) {
				send_queue.push(std::move(package));
                timer.cancel_one();
			}
        };

        uint16_t _port;
        
        std::unique_ptr<asio::io_context> _io_context;
        std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> _work_guard;
        std::unique_ptr<std::jthread> _io_thread;
        std::unique_ptr<asio::ip::tcp::acceptor> _acceptor;

        std::unordered_map<std::string, std::unique_ptr<crpc::method_base_t>> _methods;
        std::unordered_map<std::string, std::unique_ptr<crpc::awaitable_method_base_t>> _awaitable_methods;

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

        // 处理单个客户端接受会话的协程
        asio::awaitable<void> _client_session_recv(std::shared_ptr<client_session> session) {
            auto& [socket, timer, send_queue, closed] = *session;
            const std::string addr_str = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
            LOGGER.log_info("<{}> client session recv started", addr_str);

            try {
                while (!closed) {
                    // 接受请求
                    proto::package request;
                    co_await request.await_read_from(socket);
                    LOGGER.log_debug("<{}> recv from client: {}", addr_str, request.brief_info());

                    if (request.type() == proto::request_type::RPC_METHOD_REQUEST) {
                        // 方法调用请求
                        std::string data = request.data();
                        std::string name = crpc::cereal::instance().deserialize_rpc_request_method(data);
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
                            asio::co_spawn(*_io_context, [self = shared_from_this(), &socket, &name, &addr_str, &session, req = std::move(request)]()
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
                            auto response_data = cereal::instance().serialize_rpc_response<int>({}, "rpc method no found");
                            proto::package response(proto::request_type::RPC_METHOD_RESPONSE, request.seq_id(), response_data);
                            session->push_send(std::move(response));
                        }
                    }
                }
            }
            catch (std::exception& e) {
                LOGGER.log_error("<{}> exception in client session recv: {}", addr_str, e.what());
                session->closed = true;
            }

            LOGGER.log_info("<{}> client session recv closed", addr_str);
        }

        // 处理单个客户端发送会话的协程
        asio::awaitable<void> _client_session_send(std::shared_ptr<client_session> session) {
            auto& [socket, timer, send_queue, closed] = *session;
            const std::string addr_str = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
            LOGGER.log_info("<{}> client session send started", addr_str);

            try {
                while (!closed) {
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
                session->closed = true;
            }

            LOGGER.log_info("<{}> client session send closed", addr_str);
        }

    public:
        server() {}
        ~server() { stop(); }

        server(const server&) = delete;
        server& operator=(const server&) = delete;

        // 获取io_context
        asio::io_context& get_io_context() {
			return *_io_context;
		}

        // 在指定端口上启动服务端
        void start(uint16_t port) {
            _port = port;
            _io_context = std::make_unique<asio::io_context>();
            _work_guard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(_io_context->get_executor());
            _acceptor = std::make_unique<asio::ip::tcp::acceptor>(*_io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), _port));
            asio::co_spawn(*_io_context, [self = shared_from_this()] { return self->_listener(); }, asio::detached);
            _io_thread = std::make_unique<std::jthread>([self = shared_from_this()] { 
                LOGGER.log_debug("server io thread started");
                self->get_io_context().run(); 
                LOGGER.log_debug("server io thread stopped");
            });
        }

        // 停止服务端
        void stop() {
			_acceptor->close();
			_work_guard.reset();
			_io_context->stop();
			_io_thread->join();
		}

        // 注册一个方法
        template<class return_t, class... args_t>
        std::shared_ptr<server> register_method(const std::string& name, std::function<return_t(args_t...)> method) {
            unregister_method(name);
            _methods[name] = std::make_unique<method_t<return_t, args_t...>>(method);
            LOGGER.log_info("method {} registered", name);
            return shared_from_this();
        }

        // 注册一个协程方法
        template<class return_t, class... args_t>
        std::shared_ptr<server> register_method(const std::string& name, std::function<asio::awaitable<return_t>(args_t...)> method) {
            unregister_method(name);
            _awaitable_methods[name] = std::make_unique<awaitable_method_t<return_t, args_t...>>(method);
            LOGGER.log_info("awaitable method {} registered", name);
            return shared_from_this();
        }

        // 删除一个方法
        std::shared_ptr<server> unregister_method(const std::string& name) {
            if (_methods.contains(name)) {
                _methods.erase(name);
                LOGGER.log_info("method {} unregistered", name);
            } else if (_awaitable_methods.contains(name)) {
				_awaitable_methods.erase(name);
				LOGGER.log_info("awaitable method {} unregistered", name);
			}
			return shared_from_this();
		}

    };

}