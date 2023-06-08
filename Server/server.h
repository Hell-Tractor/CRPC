#pragma once
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>
#include <format>
#include <unordered_map>
#include <memory>
#include "logger.h"
#include "protocol.h"

namespace crpc {

    template<class T> using awaitable = asio::awaitable<T>; 

    // RPC������
    class server final : public std::enable_shared_from_this<server> {
    private:
        uint16_t _port;
        std::unordered_map<std::string, std::function<awaitable<std::string>()>> _methods;

    public:
        // ע��һ������
        std::shared_ptr<server> register_method(std::string name, std::function<awaitable<std::string>()> method) {
            _methods[name] = method;
            return shared_from_this();
        }

        // ɾ��һ������
        std::shared_ptr<server> unregister_method(std::string name) {
            _methods.erase(name);
			return shared_from_this();
		}

        // �������ͻ��˻Ự��Э��
        awaitable<void> client_session(asio::ip::tcp::socket socket) {
            const std::string addr_str = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
            LOGGER.log_info("client({}) session started", addr_str);

            uint32_t current_seq_id = 0;

            try {
                while (true) {
                    // ��������
                    proto::package request;
                    co_await request.await_read_from(socket);
                    LOGGER.log_debug("recv package from client({}): {}", addr_str, request.brief_info());

                    if (request.type() == proto::request_type::RPC_METHOD_REQUEST) {
                        // ������������
                        std::string name = request.data();
                        LOGGER.log_info("method request from client({}): {}", addr_str, name);

                        std::string response_msg;
                        auto it = _methods.find(name);
                        if (it == _methods.end()) {
                            response_msg = std::format("no handler for {}", name);
                        } else {
                            response_msg = co_await it->second();
                        }
                        LOGGER.log_debug("method done for client({}): {}", addr_str, name);
                        // ��Ӧ
                        proto::package response(proto::request_type::RPC_METHOD_RESPONSE, request.seq_id() + 1u, response_msg);
                        co_await response.await_write_to(socket);
                    }
                }
            }
            catch (std::exception& e) {
                LOGGER.log_error("exception in client session: {}", e.what());
            }

            LOGGER.log_info("client({}) session closed", addr_str);
        }

        // ����Э��
        awaitable<void> listener()
        {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            asio::ip::tcp::acceptor acceptor(executor, { asio::ip::tcp::v4(), _port });
            LOGGER.log_info("server listening on port {}", _port);
            while (true) {
                // ���ܿͻ������� ���ɿͻ��˻ỰЭ��
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
                asio::co_spawn(executor, [self, &socket] { return self->client_session(std::move(socket)); }, asio::detached);
            }
        }

        // ���������
        void start(uint16_t port) {
            auto self = shared_from_this();
            _port = port;
            asio::io_context io_context(1);
            asio::signal_set signals(io_context, SIGINT, SIGTERM);
            signals.async_wait([&](auto, auto) { io_context.stop(); });
            asio::co_spawn(io_context, [self] { return self->listener(); }, asio::detached);
            io_context.run();
        }
    };

}