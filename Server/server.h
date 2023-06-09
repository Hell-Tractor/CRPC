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
#include "method.h"
#include "cereal.h"
#include "protocol.h"

namespace crpc {

    template<class T> using awaitable = asio::awaitable<T>; 

    // RPC������
    class server final : public std::enable_shared_from_this<server> {
    private:
        uint16_t _port;
        
        std::shared_ptr<asio::io_context> _io_context;
        std::unique_ptr<asio::ip::tcp::acceptor> _acceptor;

        std::unordered_map<std::string, std::unique_ptr<crpc::method_base_t>> _methods;

    public:
        server(std::shared_ptr<asio::io_context> io_context)
            : _io_context(io_context) {}

        server(const server&) = delete;
        server& operator=(const server&) = delete;

        // ע��һ������
        template<class return_t, class... args_t>
        std::shared_ptr<server> register_method(const std::string& name, std::function<return_t(args_t...)> method) {
            _methods[name] = std::make_unique<method_t<return_t, args_t...>>(method);
            LOGGER.log_info("method {} registered", name);
            return shared_from_this();
        }

        // ɾ��һ������
        std::shared_ptr<server> unregister_method(const std::string& name) {
            _methods.erase(name);
            LOGGER.log_info("method {} unregistered", name);
			return shared_from_this();
		}

        // �������ͻ��˻Ự��Э��
        awaitable<void> client_session(asio::ip::tcp::socket socket) {
            const std::string addr_str = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
            LOGGER.log_info("<{}> client session started", addr_str);

            try {
                while (true) {
                    // ��������
                    proto::package request;
                    co_await request.await_read_from(socket);
                    LOGGER.log_debug("<{}> recv package from client: {}", addr_str, request.brief_info());

                    if (request.type() == proto::request_type::RPC_METHOD_REQUEST) {
                        // ������������
                        std::string data = request.data();
                        std::string name = crpc::cereal::instance().deserialize_rpc_request_method(data);
                        LOGGER.log_info("<{}> method request from client: {}", addr_str, name);

                        std::string response_data;
                        auto it = _methods.find(name);

                        if (it != _methods.end()) {
                            response_data = it->second->call(data);
                            LOGGER.log_info("<{}> method done for client: {}", addr_str, name);
                        } 
                        else {
                            // TODO
                            LOGGER.log_info("<{}> method no found for client: {}", addr_str, name);
                        }

                        // ��Ӧ
                        proto::package response(proto::request_type::RPC_METHOD_RESPONSE, request.seq_id(), response_data);
                        co_await response.await_write_to(socket);
                        LOGGER.log_debug("<{}> send package to client: {}", addr_str, response.brief_info());
                    }
                }
            }
            catch (std::exception& e) {
                LOGGER.log_error("<{}> exception in client session: {}", addr_str, e.what());
            }

            LOGGER.log_info("<{}> client session closed", addr_str);
        }

        // ����Э��
        awaitable<void> listener()
        {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            LOGGER.log_info("server listening on port {}", _port);
            while (true) {
                // ���ܿͻ������� ���ɿͻ��˻ỰЭ��
                asio::ip::tcp::socket socket = co_await _acceptor->async_accept(asio::use_awaitable);
                asio::co_spawn(executor, [self, &socket] { return self->client_session(std::move(socket)); }, asio::detached);
            }
        }

        // �ڶ˿������������
        void start(uint16_t port) {
            auto self = shared_from_this();
            _port = port;
            _acceptor = std::make_unique<asio::ip::tcp::acceptor>(*_io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), _port));
            asio::co_spawn(*_io_context, [self] { return self->listener(); }, asio::detached);
        }
    };

}