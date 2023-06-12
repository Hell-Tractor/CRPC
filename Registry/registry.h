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
#include "protocol.h"
#include "serializer.h"

namespace crpc {

    static const std::chrono::milliseconds DEFAULT_SERVER_HEARTBEAT_INTERVAL = std::chrono::seconds(5);
    static const std::chrono::milliseconds DEFAULT_SERVER_HEARTBEAT_TIMEOUT = std::chrono::seconds(5);

    // RPC注册中心类型
	class registry final : public std::enable_shared_from_this<registry> {
    private:
        // 与服务器会话维护的信息
        struct server_session {
            uint32_t id;
            asio::ip::tcp::socket socket;
            asio::steady_timer heartbeat_timer;
            asio::steady_timer timeout_timer;
            std::unordered_set<std::string> services;
            bool closed = false;
        };
        // 与客户端会话维护的信息
        struct client_session {
            asio::ip::tcp::socket socket;
            std::string address_str;
            asio::steady_timer timer;
            std::unordered_set<std::string> subscribes;
            std::queue<proto::package> send_queue;
        };

        std::string _host;
        uint16_t _port;

        std::unique_ptr<asio::io_context> _io_context;
        std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> _work_guard;
        std::unique_ptr<std::jthread> _io_thread;
        std::unique_ptr<asio::ip::tcp::acceptor> _acceptor;

        std::chrono::milliseconds _heartbeat_interval = DEFAULT_SERVER_HEARTBEAT_INTERVAL;
        std::chrono::milliseconds _heartbeat_timeout = DEFAULT_SERVER_HEARTBEAT_TIMEOUT;

        uint32_t _current_server_id = 0;

        uint32_t _current_client_id = 0;
        // 客户端id -> 客户端信息
        std::unordered_map<uint32_t, std::shared_ptr<client_session>> _clients;   
        // 服务名 -> 订阅客户端id列表
        std::unordered_map<std::string, std::unordered_set<uint32_t>> _service_subscribes;

        uint32_t _current_seq_id = 0;

        // 监听协程
        asio::awaitable<void> _listener()
        {
            LOGGER.log_info("registry listening on port {}", _port);
            
            while (true) {
                // 接受连接
                asio::ip::tcp::socket socket = co_await _acceptor->async_accept(asio::use_awaitable);
                LOGGER.log_info("accept connection from {}:{}", socket.remote_endpoint().address().to_string(), socket.remote_endpoint().port());
                // 启动初始化会话协程
                asio::co_spawn(*_io_context, [self = shared_from_this(), socket = std::move(socket)]() mutable -> asio::awaitable<void> {
                    return self->_init_session(std::move(socket));
                }, asio::detached);
            }
        }

        // 初始化会话协程
        asio::awaitable<void> _init_session(asio::ip::tcp::socket socket) {
            // 等待第一个包 TODO 超时处理
            proto::package fst_pack;
            try {
                co_await fst_pack.await_read_from(socket);
            }
            catch (const std::exception& e) {
				LOGGER.log_error("init session failed: {}", e.what());
				co_return;
			}

            // 服务端上线
            if (fst_pack.type() == proto::request_type::RPC_SERVER_ONLINE) {
                auto server = std::make_shared<server_session>(server_session{
                    _current_server_id++,
                    std::move(socket), 
                    asio::steady_timer(*_io_context), asio::steady_timer(*_io_context),
                    {}
                });
                LOGGER.log_info("{}:{} server#{} online", server->socket.remote_endpoint().address().to_string(), std::to_string(server->socket.remote_endpoint().port()), server->id);

                try {
                    // 解析更新服务信息
                    auto updates = serializer::instance().deserialize_serivce_update(fst_pack.data());
                    LOGGER.log_debug("recv first service update from server#{}", server->id);
                    _update_serive(server, std::move(updates));
                }
                catch (const std::exception& e) {
                    LOGGER.log_error("server#{} first service update failed: {}", server->id, e.what());
                    disconnect_server(server);
                    co_return;
                }

                try {
                    // 发送响应
                    proto::package online_response(proto::request_type::RPC_SERVER_ONLINE_RESPONSE, _current_seq_id++);
                    co_await online_response.await_write_to(server->socket);
                    LOGGER.log_debug("online response send to server#{}", server->id);
                }
                catch (const std::exception& e) {
                    LOGGER.log_error("server#{} online response failed: {}", server->id, e.what());
					disconnect_server(server);
					co_return;
                }

                // 启动服务端会话
                asio::co_spawn(*_io_context, [self = shared_from_this(), server] { return self->_server_send(server); }, asio::detached);
                asio::co_spawn(*_io_context, [self = shared_from_this(), server] { return self->_server_recv(server); }, asio::detached);
            }
            // 客户端上线
            else if (fst_pack.type() == proto::request_type::RPC_CLIENT_ONLINE) {

                // TODO
            }
		}

        // 更新服务
        void _update_serive(std::shared_ptr<server_session> server, std::vector<std::pair<std::string, bool>>&& update) {
            std::vector<std::pair<std::string, bool>> real_update{};
            // 更新服务器服务信息
            for (auto& [name, state] : update) {
                if (state && !server->services.contains(name)) {
                    server->services.insert(name);
                    real_update.emplace_back(name, state);
                }
                else if (!state && server->services.contains(name)) {
					server->services.erase(name);
					real_update.emplace_back(name, state);
				}
            }
            for (auto& [name, state] : real_update) {
                LOGGER.log_info("server#{} service update: {} -> {}", server->id, name, state ? "online" : "offline");
            }
            // 向订阅的客户端的服务推送更新
            for (auto& [name, state] : real_update) {
                for (auto& client_id : _service_subscribes[name]) {
					auto client = _clients[client_id];
					// TODO 推送更新
				}
            }
        }

        // 服务端发送协程
        asio::awaitable<void> _server_send(std::shared_ptr<server_session> server) {
            LOGGER.log_debug("server#{} send start", server->id);

            try {
                while (server->socket.is_open()) {
                    // 定时发送心跳
                    server->heartbeat_timer.expires_after(_heartbeat_interval);
                    co_await server->heartbeat_timer.async_wait(asio::use_awaitable);

                    proto::package heartbeat(proto::request_type::HEARTBEAT, _current_seq_id++);
                    co_await heartbeat.await_write_to(server->socket);
                    LOGGER.log_debug("send heartbeat to server#{}", server->id);

                    // 检查超时
                    server->timeout_timer.expires_after(_heartbeat_timeout);
                    asio::error_code ec;
                    co_await server->timeout_timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    if (ec != asio::error::operation_aborted) {
						throw crpc::timeout_error("heartbeat timeout");
					}
                }
            }
            catch (const std::exception& e) {
                LOGGER.log_error("server#{} send failed: {}", server->id, e.what());
                disconnect_server(server);
            }

            LOGGER.log_debug("server#{} send end", server->id);
		}

        // 服务端接收协程
        asio::awaitable<void> _server_recv(std::shared_ptr<server_session> server) {
            LOGGER.log_debug("server#{} recv start", server->id);

            try {
                while (server->socket.is_open()) {
                    proto::package pack;
                    co_await pack.await_read_from(server->socket);
                    if (pack.type() == proto::request_type::RPC_SERVICE_UPDATE) {    
                        // 服务更新
                        LOGGER.log_info("recv service update from server#{}", server->id);
                        auto updates = serializer::instance().deserialize_serivce_update(pack.data());
                        _update_serive(server, std::move(updates));
                    }
                    else if (pack.type() == proto::request_type::HEARTBEAT_RESPONSE) {   
                        // 心跳响应
                        LOGGER.log_info("recv heartbeat response from server#{}", server->id);
                        server->timeout_timer.cancel();
                    }
                }
            }
            catch (const std::exception& e) {
                LOGGER.log_error("server#{} send failed: {}", server->id, e.what());
                disconnect_server(server);
            }

            LOGGER.log_debug("server#{} recv end", server->id);
        }

        // 从服务端断开
        void disconnect_server(std::shared_ptr<server_session> server) {
            if (server->closed) return;
		    LOGGER.log_info("disconnect server#{}", server->id);
            server->closed = true;
			server->heartbeat_timer.cancel();
			server->timeout_timer.cancel();
			server->socket.close();
            // 下线所有服务
            std::vector<std::pair<std::string, bool>> update{};
            for (auto& name : server->services) 
				update.emplace_back(name, false);
            _update_serive(server, std::move(update));
		}

    public:
        registry() {}
        ~registry() { stop(); }

        registry(const registry&) = delete;
        registry& operator=(const registry&) = delete;

        // 获取io_context
        asio::io_context& get_io_context() {
            return *_io_context;
        }

        // 在指定端口上启动注册中心
        void start(const std::string& host, uint16_t port) {
            _host = host;
            _port = port;
            _io_context = std::make_unique<asio::io_context>(1);
            _work_guard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(_io_context->get_executor());
            _acceptor = std::make_unique<asio::ip::tcp::acceptor>(*_io_context, asio::ip::tcp::endpoint(asio::ip::make_address(host), port));
            asio::co_spawn(*_io_context, [self = shared_from_this()] { return self->_listener(); }, asio::detached);
            _io_thread = std::make_unique<std::jthread>([self = shared_from_this()] {
                LOGGER.log_debug("registry io thread started");
                self->get_io_context().run();
                LOGGER.log_debug("registry io thread stopped");
            });
            _current_server_id = 0;
            _current_client_id = 0;
            _current_seq_id = 0;
            _clients.clear();
            _service_subscribes.clear();
        }

        // 停止注册中心
        void stop() {
            _acceptor->close();
            _work_guard.reset();
            _io_context->stop();
            _io_thread->join();
        }


	};

}