#pragma once
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/redirect_error.hpp>
#include <asio/write.hpp>
#include <format>
#include <memory>
#include <future>
#include <queue>
#include <set>
#include "logger.h"
#include "method.h"
#include "protocol.h"
#include "serializer.h"
#include "crpc_except.h"
#include "client.h"
#include "route.h"

namespace crpc {

    static constexpr bool DEFAULT_CLIENT_AUTO_PUSH_SUBSCRIBE = false;

    // RPC连接池类型
    class connection_pool final : public std::enable_shared_from_this<connection_pool> {
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

        struct connection {
            std::shared_ptr<client> client;
            int64_t last_use_time;
            std::string addr;
            // 更新时间
            void update() {
                last_use_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            }
        };

        struct lru_comparer {
		    bool operator()(const std::shared_ptr<connection>& a, const std::shared_ptr<connection>& b) const {
				return a->last_use_time < b->last_use_time;
			}
		};

        std::shared_ptr<asio::io_context> _io_context;
        asio::executor_work_guard<asio::io_context::executor_type> _work_guard;
        std::jthread _io_thread;

        std::unordered_set<std::string> _last_subscribes;
        std::unordered_set<std::string> _current_subscribes;

        std::shared_ptr<registry_session> _registry_session = nullptr;

        std::function<void()> _registry_disconnect_callback;

        uint32_t _current_seq_id = 0;

        bool _auto_push_subscribe = DEFAULT_CLIENT_AUTO_PUSH_SUBSCRIBE;

        std::unordered_map<std::string, std::shared_ptr<connection>> _pool;
        std::set<std::shared_ptr<connection>, lru_comparer> _lru_queue;

        std::shared_ptr<route::strategy> _route_strategy;
        std::string _local_address;
        size_t _pool_size;

        // 服务名 -> 服务地址
        std::unordered_map<std::string, std::unordered_set<std::string>> _service_address_map;


        // 更新服务
        void _update_service(std::vector<std::tuple<std::string, std::string, bool>> updates) {
            for (auto& [name, addr, state] : updates) {
                auto& mp = _service_address_map[name];
                if (state && !mp.contains(addr)) {
                    mp.insert(addr);
                    LOGGER.log_info("service {}: address {} -> online", name, addr);
                }
                else if (!state && mp.contains(addr)) {
					mp.erase(addr);
					LOGGER.log_info("service {}: address {} -> offline", name, addr);
				}
            }
        }


        // 处理注册中心接收的协程
        asio::awaitable<void> _registry_session_recv(std::shared_ptr<registry_session> session) {
            LOGGER.log_info("registry session recv started");

            try {
                while (session->socket.is_open()) {
                    // 接受包
                    proto::package pack;
                    co_await pack.await_read_from(session->socket);
                    LOGGER.log_debug("recv from registry : {}", pack.brief_info());

                    if (pack.type() == proto::request_type::RPC_SERVICE_UPDATE) {
                        // 更新服务
                        LOGGER.log_info("recv rpc service update");
                        auto updates = serializer::instance().deserialize_service_update(pack.data());
                        _update_service(updates);
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
                        // 发送
                        proto::package& pack = session->send_queue.front();
                        co_await pack.await_write_to(session->socket);
                        LOGGER.log_debug("send to registry : {}", pack.brief_info());
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
            _last_subscribes.clear();
            _registry_disconnect_callback();
        }


        // 创建新连接
        std::shared_ptr<connection> _create_connection(const std::string& addr) {
            auto con = std::make_shared<connection>();
            con->client = std::make_shared<client>(_io_context);
            con->addr = addr;
            std::string host = addr.substr(0, addr.find(':'));
            uint16_t port = std::stoi(addr.substr(addr.find(':') + 1));
            con->client->connect_server(host, port);
            con->update();
            LOGGER.log_info("create new connection to {}", addr);
            return con;
        }

        // 根据路由策略获取服务相关的连接
        std::shared_ptr<connection> _find_connection(const std::string& name) {
            if (_service_address_map[name].empty())
                throw service_not_available_error("service " + name + " not found");

            std::string addr = _route_strategy->get_route_address(_local_address, name, _service_address_map[name]);
                
            if (_pool.contains(addr)) {
                return _pool[addr];
            }
            else {
                auto con = _create_connection(addr);
                if(_pool.size() == _pool_size) {
                    // LRU 删除最久未使用的连接
                    auto it = _lru_queue.begin();
                    _pool.erase((*it)->addr);
                    _lru_queue.erase(it);
				}
                _pool[addr] = con;
                _lru_queue.insert(con);
                return con;
            }
        }

    public:
        connection_pool(size_t pool_size, const std::string& route_strategy = "polling")
            : _io_context(std::make_shared<asio::io_context>(1))
            , _work_guard(asio::make_work_guard(get_io_context().get_executor()))
            , _pool_size(pool_size)
            , _route_strategy(route::strategy_factory::create(route_strategy)) {

            _io_thread = std::jthread([io_context = this->_io_context] {
                LOGGER.log_debug("connection_pool io thread started");
                io_context->run();
                LOGGER.log_debug("connection_pool io thread stopped");
            });
        }

        ~connection_pool() {
            if(_registry_session) _disconnect_registry(_registry_session);
            _pool.clear();
            _work_guard.reset();
            get_io_context().stop();
            _io_thread.request_stop();
        }

        connection_pool(const connection_pool&) = delete;
        connection_pool& operator=(const connection_pool&) = delete;

        // 获取io_context
        asio::io_context& get_io_context() {
            return *_io_context;
        }

        // 设置是否在更新订阅时自动推送订阅到注册中心
        void set_auto_push_subscribe(bool flag) {
            _auto_push_subscribe = flag;
        }


        // 连接注册中心
        void connect_registry(const std::string& host, uint16_t port) {
            try {
                _registry_session = std::make_shared<registry_session>(registry_session{
                    std::move(asio::ip::tcp::resolver(get_io_context())),
                    std::move(asio::ip::tcp::socket(get_io_context())),
                    std::move(asio::steady_timer(get_io_context())),
                    std::queue<proto::package>()
                });

                // 连接
                auto session = _registry_session;
                auto endpoints = session->resolver.resolve(host, std::to_string(port));
                session->socket.connect(*endpoints.begin());
                auto local_endpoint = session->socket.local_endpoint();
                _local_address = local_endpoint.address().to_string() + ":" + std::to_string(local_endpoint.port());
                LOGGER.log_info("connect to registry {}:{}", host, port); 

                // 发送初始化包
                std::vector<std::pair<std::string, bool>> updates{};
                for (auto& name : _current_subscribes) updates.push_back(std::make_pair(name, true));
                auto data = serializer::instance().serialize_subscribe_update(updates);
                proto::package pack(proto::request_type::RPC_CLIENT_ONLINE, _current_seq_id++, data);
                pack.write_to(session->socket);
                LOGGER.log_debug("online request send to registry");

                // 等待初始化响应 TODO 超时处理
                proto::package response;
                response.read_from(session->socket);
                if (response.type() != proto::request_type::RPC_CLIENT_ONLINE_RESPONSE) {
                    throw std::runtime_error("invalid response from registry");
                }
                auto services = serializer::instance().deserialize_service_update(response.data());
                _service_address_map.clear();
                _update_service(services);
                LOGGER.log_debug("recv online response from registry");

                _last_subscribes = _current_subscribes;

                // 生成会话
                asio::co_spawn(get_io_context(), [self = shared_from_this()] { return self->_registry_session_recv(self->_registry_session); }, asio::detached);
                asio::co_spawn(get_io_context(), [self = shared_from_this()] { return self->_registry_session_send(self->_registry_session); }, asio::detached);
            }
            catch (const std::exception& e) {
                _registry_session = nullptr;
                throw e;
            }
        }

        // 设置注册中心掉线回调
        void set_registry_disconnect_callback(std::function<void()> callback) {
            _registry_disconnect_callback = callback;
		}


        // 添加订阅
        std::shared_ptr<connection_pool> subscribe_service(const std::string& name) {
            if (!_current_subscribes.contains(name)) {
                _current_subscribes.insert(name);
                LOGGER.log_info("subscribe {}", name);
                if (_auto_push_subscribe) {
                    push_subscribe_update();
                }
            }
            return shared_from_this();
        }

        // 取消订阅
        std::shared_ptr<connection_pool> unsubscribe_service(const std::string& name) {
            if (_current_subscribes.contains(name)) {
                _current_subscribes.erase(name);
                LOGGER.log_info("unsubscribe {}", name);
                if (_auto_push_subscribe) {
					push_subscribe_update();
				}
            }
            return shared_from_this();
        }

        // 推送订阅更新
        std::shared_ptr<connection_pool> push_subscribe_update() {
            if (!_registry_session) {
                throw std::runtime_error("registry not connected");
            }

            std::vector<std::pair<std::string, bool>> updates{};
            for (auto& name : _current_subscribes) 
                if(!_last_subscribes.contains(name)) 
                    updates.push_back(std::make_pair(name, true));
            for (auto& name : _last_subscribes)
                if (!_current_subscribes.contains(name)) 
                    updates.push_back(std::make_pair(name, false));
            
            for (auto& [name, state] : updates) if(!state) {
                // 订阅取消不会接收到服务器回应，在本地实现
                _service_address_map.erase(name);
            }

            auto data = serializer::instance().serialize_subscribe_update(updates);
            proto::package pack(proto::request_type::RPC_SERVICE_SUBSCRIBE_UPDATE, _current_seq_id++, data);
            _registry_session->push_send(std::move(pack));

            _last_subscribes = _current_subscribes;
            LOGGER.log_info("push subscribe update");
            return shared_from_this();
		}
        

        // 异步调用
        template<class return_t, class ... args_t>
        rpc_future<return_t> async_call(const std::string& name, args_t&&... args) {
            auto con = _find_connection(name);
            return con->client->async_call<return_t, args_t...>(name, std::forward<args_t>(args)...);
        }

        // 同步调用
        template<class return_t, class ... args_t>
        return_t call(const std::string& name, args_t&&... args) {
			auto con = _find_connection(name);
			return con->client->call<return_t, args_t...>(name, std::forward<args_t>(args)...);
		}
    
    };
}
