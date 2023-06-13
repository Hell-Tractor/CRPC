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

    // RPCע����������
	class registry final : public std::enable_shared_from_this<registry> {
    private:
        // ��������Ựά������Ϣ
        struct server_session {
            uint32_t id;
            asio::ip::tcp::socket socket;
            asio::steady_timer heartbeat_timer;
            asio::steady_timer timeout_timer;
            std::unordered_set<std::string> services;
            std::string addr_str;
            bool closed = false;
        };
        // ��ͻ��˻Ựά������Ϣ
        struct client_session {
            uint32_t id;
            asio::ip::tcp::socket socket;
            asio::steady_timer timer;
            std::unordered_set<std::string> subscribes;
            std::queue<proto::package> send_queue;
            bool closed = false;

            void push_send(proto::package pack) {
                send_queue.push(std::move(pack));
                timer.cancel_one();
            }
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
        // �����id -> �������Ϣ
        std::unordered_map<uint32_t, std::shared_ptr<server_session>> _servers;
        // ������ -> �ṩ����ķ����id�б�
        std::unordered_map<std::string, std::unordered_set<uint32_t>> _service_providers;

        uint32_t _current_client_id = 0;
        // �ͻ���id -> �ͻ�����Ϣ
        std::unordered_map<uint32_t, std::shared_ptr<client_session>> _clients;   
        // ������ -> ���Ŀͻ���id�б�
        std::unordered_map<std::string, std::unordered_set<uint32_t>> _service_subscribers;

        uint32_t _current_seq_id = 0;


        registry() {}

        // ����Э��
        asio::awaitable<void> _listener()
        {
            LOGGER.log_info("registry listening on port {}", _port);
            
            while (true) {
                // ��������
                asio::ip::tcp::socket socket = co_await _acceptor->async_accept(asio::use_awaitable);
                LOGGER.log_info("accept connection from {}:{}", socket.remote_endpoint().address().to_string(), socket.remote_endpoint().port());
                // ������ʼ���ỰЭ��
                asio::co_spawn(*_io_context, [self = shared_from_this(), socket = std::move(socket)]() mutable -> asio::awaitable<void> {
                    return self->_init_session(std::move(socket));
                }, asio::detached);
            }
        }

        // ��ʼ���ỰЭ��
        asio::awaitable<void> _init_session(asio::ip::tcp::socket socket) {
            // �ȴ���һ���� TODO ��ʱ����
            proto::package fst_pack;
            try {
                co_await fst_pack.await_read_from(socket);
            }
            catch (const std::exception& e) {
				LOGGER.log_error("init session failed: {}", e.what());
				co_return;
			}

            // ���������
            if (fst_pack.type() == proto::request_type::RPC_SERVER_ONLINE) {
                auto server = std::make_shared<server_session>(server_session{
                    _current_server_id++,
                    std::move(socket), 
                    asio::steady_timer(*_io_context), 
                    asio::steady_timer(*_io_context),
                    {},
                    {}
                });
                _servers[server->id] = server;
                LOGGER.log_info("server#{} online", server->id);

                try {
                    // �������·�����Ϣ
                    auto updates = serializer::instance().deserialize_server_online_service_list(fst_pack.data());
                    auto addr = serializer::instance().deserialize_server_online_addr(fst_pack.data());
                    _servers[server->id]->addr_str = addr;
                    _update_provide(server, std::move(updates));
                    LOGGER.log_debug("recv first service update from server#{}", server->id);
                }
                catch (const std::exception& e) {
                    LOGGER.log_error("server#{} first service update failed: {}", server->id, e.what());
                    _disconnect_server(server);
                    co_return;
                }

                try {
                    // ������Ӧ
                    proto::package online_response(proto::request_type::RPC_SERVER_ONLINE_RESPONSE, fst_pack.seq_id());
                    co_await online_response.await_write_to(server->socket);
                    LOGGER.log_debug("online response send to server#{}", server->id);
                }
                catch (const std::exception& e) {
                    LOGGER.log_error("server#{} online response failed: {}", server->id, e.what());
                    _disconnect_server(server);
					co_return;
                }

                // ��������˻Ự
                asio::co_spawn(*_io_context, [self = shared_from_this(), server] { return self->_server_send(server); }, asio::detached);
                asio::co_spawn(*_io_context, [self = shared_from_this(), server] { return self->_server_recv(server); }, asio::detached);
            }
            // �ͻ�������
            else if (fst_pack.type() == proto::request_type::RPC_CLIENT_ONLINE) {
                auto client = std::make_shared<client_session>(client_session{
                    _current_client_id++,
                    std::move(socket),
                    asio::steady_timer(*_io_context), 
                    {}, {}
                });
                _clients[client->id] = client;
                LOGGER.log_info("{}:{} client#{} online", client->socket.remote_endpoint().address().to_string(), std::to_string(client->socket.remote_endpoint().port()), client->id);

                try {
                    // ������������Ϣ
                    auto updates = serializer::instance().deserialize_subscribe_update(fst_pack.data());
                    LOGGER.log_debug("recv first subscribe update from client#{}", client->id);
                    _update_subscribe(client, std::move(updates));
                }
                catch (const std::exception& e) {
                    LOGGER.log_error("client#{} first subscribe update failed: {}", client->id, e.what());
                    _disconnect_client(client);
                    co_return;
                }

                try {
                    // ������Ӧ
                    std::vector<std::tuple<std::string, std::string, bool>> services{};
                    for(auto& name : client->subscribes) 
                        if(_service_providers.contains(name)) 
                            for (auto& id : _service_providers[name]) {
                                auto addr = _servers[id]->addr_str;
                                services.emplace_back(std::make_tuple(name, addr, true));
                            }
                    auto data = serializer::instance().serialize_service_update(std::move(services));
                    proto::package online_response(proto::request_type::RPC_CLIENT_ONLINE_RESPONSE, fst_pack.seq_id(), data);
                    co_await online_response.await_write_to(client->socket);
					LOGGER.log_debug("online response send to client#{}", client->id);
                }
                catch (const std::exception& e) {
                    LOGGER.log_error("client#{} online response failed: {}", client->id, e.what());
                    _disconnect_client(client);
                }

                // �����ͻ��˻Ự
                asio::co_spawn(*_io_context, [self = shared_from_this(), client] { return self->_client_send(client); }, asio::detached);
				asio::co_spawn(*_io_context, [self = shared_from_this(), client] { return self->_client_recv(client); }, asio::detached);
			}
			else {
				LOGGER.log_error("connection abandoned: invalid first package type: {}", proto::request_type_str[fst_pack.type()]);
            }
		}


        // ����˸����ṩ�ķ���
        void _update_provide(std::shared_ptr<server_session> server, std::vector<std::pair<std::string, bool>>&& update) {
            std::vector<std::pair<std::string, bool>> real_update{};
            // ���·�����������Ϣ
            for (auto& [name, state] : update) {
                if (state && !server->services.contains(name)) {
                    server->services.insert(name);
                    _service_providers[name].insert(server->id);
                    real_update.emplace_back(name, state);
                }
                else if (!state && server->services.contains(name)) {
					server->services.erase(name);
                    _service_providers[name].erase(server->id);
					real_update.emplace_back(name, state);
				}
            }
            for (auto& [name, state] : real_update) {
                LOGGER.log_info("server#{} service update: {} -> {}", server->id, name, state ? "online" : "offline");
            }
            // ���ĵĿͻ��˵ķ������͸���
            std::unordered_map<uint32_t, std::vector<std::pair<std::string, bool>>> client_update{};
            for (auto& [name, state] : real_update) 
                for (auto& client_id : _service_subscribers[name]) 
					client_update[client_id].emplace_back(name, state);
            for (auto& [id, updates] : client_update) {
                auto& client = _clients[id];
                std::vector<std::tuple<std::string, std::string, bool>> update_with_addr{};
                for(auto& [name, state] : updates)
					update_with_addr.emplace_back(name, server->addr_str, state);
                if (update_with_addr.size()) {
                    _push_service_update(client, std::move(update_with_addr));
                }
            }
        }

        // �ͻ��˸��¶��ĵķ���
        void _update_subscribe(std::shared_ptr<client_session> client, std::vector<std::pair<std::string, bool>>&& update) {
            std::vector<std::pair<std::string, bool>> real_update{};
            // ���¿ͻ��˶�����Ϣ
            for (auto& [name, state] : update) {
                if (state && !client->subscribes.contains(name)) {
                    client->subscribes.insert(name);
                    _service_subscribers[name].insert(client->id);
                    real_update.emplace_back(name, state);
                }
                else if (!state && client->subscribes.contains(name)) {
                    client->subscribes.erase(name);
                    _service_subscribers[name].erase(client->id);
                    real_update.emplace_back(name, state);
                }
            }
            for (auto& [name, state] : real_update) {
                LOGGER.log_info("client#{} subscribe update: {} -> {}", client->id, name, state ? "sub" : "unsub");
            }
            // ������������ģ���ͻ���������ظ���
            std::vector<std::tuple<std::string, std::string, bool>> update_with_addr{};
            for(auto& [name, state] : real_update) if (state) { 
                for (auto& id : _service_providers[name]) {
                    auto& server = _servers[id];
                    update_with_addr.emplace_back(name, server->addr_str, true);
                }
		    }
            if (update_with_addr.size()) {
				_push_service_update(client, std::move(update_with_addr));
			}
        }

        // ���ͷ�����µ��ͻ���
        void _push_service_update(std::shared_ptr<client_session> client, std::vector<std::tuple<std::string, std::string, bool>>&& update) {
            auto data = serializer::instance().serialize_service_update(update);
            proto::package pack(proto::request_type::RPC_SERVICE_UPDATE, _current_seq_id++, std::move(data));
            client->push_send(std::move(pack));
		}


        // ����˷���Э��
        asio::awaitable<void> _server_send(std::shared_ptr<server_session> server) {
            LOGGER.log_debug("server#{} send start", server->id);

            try {
                while (server->socket.is_open()) {
                    // ��ʱ��������
                    server->heartbeat_timer.expires_after(_heartbeat_interval);
                    co_await server->heartbeat_timer.async_wait(asio::use_awaitable);

                    proto::package heartbeat(proto::request_type::HEARTBEAT, _current_seq_id++);
                    co_await heartbeat.await_write_to(server->socket);
                    LOGGER.log_debug("send heartbeat to server#{}", server->id);

                    // ��鳬ʱ
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
            }

            _disconnect_server(server);
            LOGGER.log_debug("server#{} send end", server->id);
		}

        // ����˽���Э��
        asio::awaitable<void> _server_recv(std::shared_ptr<server_session> server) {
            LOGGER.log_debug("server#{} recv start", server->id);

            try {
                while (server->socket.is_open()) {
                    proto::package pack;
                    co_await pack.await_read_from(server->socket);
                    if (pack.type() == proto::request_type::RPC_SERVICE_PROVIDE_UPDATE) {    
                        // �������
                        LOGGER.log_info("recv service update from server#{}", server->id);
                        auto updates = serializer::instance().deserialize_provide_update(pack.data());
                        _update_provide(server, std::move(updates));
                    }
                    else if (pack.type() == proto::request_type::HEARTBEAT_RESPONSE) {   
                        // ������Ӧ
                        LOGGER.log_info("recv heartbeat response from server#{}", server->id);
                        server->timeout_timer.cancel();
                    }
                }
            }
            catch (const std::exception& e) {
                LOGGER.log_error("server#{} send failed: {}", server->id, e.what());
            }

            _disconnect_server(server);
            LOGGER.log_debug("server#{} recv end", server->id);
        }

        // �ӷ���˶Ͽ�
        void _disconnect_server(std::shared_ptr<server_session> server) {
            if (server->closed) return;
		    LOGGER.log_info("disconnect server#{}", server->id);
            server->closed = true;
			server->heartbeat_timer.cancel();
			server->timeout_timer.cancel();
			server->socket.close();
            // �������з���
            std::vector<std::pair<std::string, bool>> update{};
            for (auto& name : server->services) 
				update.emplace_back(name, false);
            _update_provide(server, std::move(update));
            _servers.erase(server->id);
		}


        // �ͻ��˷���Э��
        asio::awaitable<void> _client_send(std::shared_ptr<client_session> client) {
            LOGGER.log_debug("client#{} send start", client->id);

            client->timer.expires_at(std::chrono::steady_clock::time_point::max());
            try {
                while (client->socket.is_open()) {
                    if (client->send_queue.empty()) {
                        asio::error_code ec;
                        co_await client->timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    }
					else {
						auto& pack = client->send_queue.front();
						co_await pack.await_write_to(client->socket);
                        LOGGER.log_debug("send package to client#{}: {}", client->id, pack.brief_info());
						client->send_queue.pop();
					}
                }
            }
            catch (const std::exception& e) {
				LOGGER.log_error("client#{} send failed: {}", client->id, e.what());
			}

            _disconnect_client(client);
	        LOGGER.log_debug("client#{} send end", client->id);
        }

        // �ͻ��˽���Э��
        asio::awaitable<void> _client_recv(std::shared_ptr<client_session> client) {
            LOGGER.log_debug("client#{} recv start", client->id);

            try {
                while (client->socket.is_open()) {
                    proto::package pack;
			        co_await pack.await_read_from(client->socket);

                    if (pack.type() == proto::request_type::RPC_SERVICE_SUBSCRIBE_UPDATE) {
                        // ���ĸ���
                        LOGGER.log_info("recv subscribe update from client#{}", client->id);
                        auto updates = serializer::instance().deserialize_subscribe_update(pack.data());
                        _update_subscribe(client, std::move(updates));
                    }
                }
            }
            catch (const std::exception& e) {
                LOGGER.log_error("client#{} recv failed: {}", client->id, e.what());
			}

            _disconnect_client(client);
	        LOGGER.log_debug("client#{} recv end", client->id);
		}

        // �ӿͻ��˶Ͽ�
        void _disconnect_client(std::shared_ptr<client_session> client) {
            if (client->closed) return;
            LOGGER.log_info("disconnect client#{}", client->id);
            client->closed = true;
            client->timer.cancel();
            client->socket.close();
            // ɾ�����ж���
            for (auto& name : client->subscribes) 
				_service_subscribers[name].erase(client->id);
            _clients.erase(client->id);
        }


    public:
        
        static std::shared_ptr<registry> create() {
            return std::shared_ptr<registry>(new registry());
		}

        ~registry() { stop(); }

        registry(const registry&) = delete;
        registry& operator=(const registry&) = delete;

        // ��ȡio_context
        asio::io_context& get_io_context() {
            return *_io_context;
        }

        // ��ָ���˿�������ע������
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
            _service_subscribers.clear();
        }

        // ֹͣע������
        void stop() {
            _acceptor->close();
            _work_guard.reset();
            _io_context->stop();
            _io_thread->join();
        }


	};

}