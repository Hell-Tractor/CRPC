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
#include "logger.h"
#include "method.h"
#include "protocol.h"
#include "serializer.h"
#include "crpc_except.h"
#include "client.h"

namespace crpc {

    static constexpr bool DEFAULT_CLIENT_AUTO_PUSH_SUBSCRIBE = false;

    // RPC���ӳ�����
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

        asio::io_context _io_context;
        asio::executor_work_guard<asio::io_context::executor_type> _work_guard;
        std::jthread _io_thread;

        std::unordered_set<std::string> _last_subscribes;
        std::unordered_set<std::string> _current_subscribes;

        std::shared_ptr<registry_session> _registry_session = nullptr;

        std::function<void()> _registry_disconnect_callback;

        uint32_t _current_seq_id = 0;

        bool _auto_push_subscribe = DEFAULT_CLIENT_AUTO_PUSH_SUBSCRIBE;

        // ������ -> �����ַ
        std::unordered_map<std::string, std::unordered_set<std::string>> _service_address_map;


        // ���·���
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

        // ����ע�����Ľ��յ�Э��
        asio::awaitable<void> _registry_session_recv(std::shared_ptr<registry_session> session) {
            LOGGER.log_info("registry session recv started");

            try {
                while (session->socket.is_open()) {
                    // ���ܰ�
                    proto::package pack;
                    co_await pack.await_read_from(session->socket);
                    LOGGER.log_debug("recv from registry : {}", pack.brief_info());

                    if (pack.type() == proto::request_type::RPC_SERVICE_UPDATE) {
                        // ���·���
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

        // ����ע�����ķ��͵�Э��
        asio::awaitable<void> _registry_session_send(std::shared_ptr<registry_session> session) {
            LOGGER.log_info("registry session send started");

            try {
                while (session->socket.is_open()) {
                    if (session->send_queue.empty()) {
                        asio::error_code ec;
                        co_await session->timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    }
                    else {
                        // ����
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

        // �ر�ע����������
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

    public:
        connection_pool()
            : _io_context(1), _work_guard(asio::make_work_guard(_io_context.get_executor())) {
            _io_thread = std::jthread([&io_context = this->_io_context] {
                LOGGER.log_debug("connection_pool io thread started");
                io_context.run();
                LOGGER.log_debug("connection_pool io thread stopped");
            });
        }

        ~connection_pool() {
            if(_registry_session) _disconnect_registry(_registry_session);
            _work_guard.reset();
            _io_context.stop();
            _io_thread.request_stop();
        }

        connection_pool(const connection_pool&) = delete;
        connection_pool& operator=(const connection_pool&) = delete;

        // ��ȡio_context
        asio::io_context& get_io_context() {
            return _io_context;
        }

        // �����Ƿ��ڸ��¶���ʱ�Զ����Ͷ��ĵ�ע������
        void set_auto_push_subscribe(bool flag) {
            _auto_push_subscribe = flag;
        }

        // ����ע������
        void connect_registry(const std::string& host, uint16_t port) {
            // ������ʼ��Э�� �������ȴ�
            try {
                _registry_session = std::make_shared<registry_session>(registry_session{
                    std::move(asio::ip::tcp::resolver(_io_context)),
                    std::move(asio::ip::tcp::socket(_io_context)),
                    std::move(asio::steady_timer(_io_context)),
                    std::queue<proto::package>()
                });

                // ����
                auto session = _registry_session;
                auto endpoints = session->resolver.resolve(host, std::to_string(port));
                session->socket.connect(*endpoints.begin());
                LOGGER.log_info("connect to registry {}:{}", host, port);

                // ���ͳ�ʼ����
                std::vector<std::pair<std::string, bool>> updates{};
                for (auto& name : _current_subscribes) updates.push_back(std::make_pair(name, true));
                auto data = serializer::instance().serialize_subscribe_update(updates);
                proto::package pack(proto::request_type::RPC_CLIENT_ONLINE, _current_seq_id++, data);
                pack.write_to(session->socket);
                LOGGER.log_debug("online request send to registry");

                // �ȴ���ʼ����Ӧ TODO ��ʱ����
                proto::package response;
                response.read_from(session->socket);
                if (response.type() != proto::request_type::RPC_CLIENT_ONLINE_RESPONSE) {
                    throw std::runtime_error("invalid response from registry");
                }
                LOGGER.log_debug("recv online response from registry");

                _last_subscribes = _current_subscribes;
                _service_address_map.clear();

                // ���ɻỰ
                asio::co_spawn(_io_context, [self = shared_from_this()] { return self->_registry_session_recv(self->_registry_session); }, asio::detached);
                asio::co_spawn(_io_context, [self = shared_from_this()] { return self->_registry_session_send(self->_registry_session); }, asio::detached);
            }
            catch (const std::exception& e) {
                _registry_session = nullptr;
                throw e;
            }
        }

        // ����ע�����ĵ��߻ص�
        void set_registry_disconnect_callback(std::function<void()> callback) {
            _registry_disconnect_callback = callback;
		}

        // ��Ӷ���
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

        // ȡ������
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

        // ���Ͷ��ĸ���
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
                // ����ȡ��������յ���������Ӧ���ڱ���ʵ��
                _service_address_map.erase(name);
            }

            auto data = serializer::instance().serialize_subscribe_update(updates);
            proto::package pack(proto::request_type::RPC_SERVICE_SUBSCRIBE_UPDATE, _current_seq_id++, data);
            _registry_session->push_send(std::move(pack));

            _last_subscribes = _current_subscribes;
            LOGGER.log_info("push subscribe update");
            return shared_from_this();
		}
    };
}
