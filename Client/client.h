#pragma once
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>
#include <format>
#include <memory>
#include <future>
#include <queue>
#include "logger.h"
#include "protocol.h"
#include "cereal.h"

namespace crpc {

    template<class T> using awaitable = asio::awaitable<T>;

    // RPC客户端类型
    class client final : public std::enable_shared_from_this<client> {
    private:

        std::shared_ptr<asio::io_context> _io_context;
        std::unique_ptr<asio::ip::tcp::resolver> _resolver;
        std::unique_ptr<asio::ip::tcp::socket> _socket;
        
        uint32_t _current_seq_id = 0;

    public:
        client(std::shared_ptr<asio::io_context> io_context)
            : _io_context(io_context)
            , _resolver(std::make_unique<asio::ip::tcp::resolver>(*_io_context))
            , _socket(std::make_unique<asio::ip::tcp::socket>(*_io_context)) {}

        client(const client&) = delete;
        client& operator=(const client&) = delete;

        // 连接服务器
        void connect_server(const std::string& host, uint16_t port) {
            LOGGER.log_info("waiting for connection to {}:{}", host, port);
            auto endpoints = _resolver->resolve(host, std::to_string(port));
            _socket->connect(*endpoints.begin());
            LOGGER.log_info("connected to {}:{}", host, port);
        }

        // 同步rpc请求
        template<class return_t, class ... args_t>
        return_t call(const std::string& name, args_t... args) {
            LOGGER.log_info("call {}", name);
            auto request_data = cereal::instance().serialize_rpc_request<args_t...>(name, args...);
            proto::package request(proto::request_type::RPC_METHOD_REQUEST, _current_seq_id++, request_data);
            request.write_to(*_socket);
            LOGGER.log_debug("call request package sent: {}", request.brief_info());

            proto::package response{};
            response.read_from(*_socket);
            LOGGER.log_debug("call response package recv: {}", response.brief_info());

            if (response.type() == proto::request_type::RPC_METHOD_RESPONSE && response.seq_id() == request.seq_id()) {
                return cereal::instance().deserialize_rpc_response<return_t>(response.data());
            } else {
                throw std::runtime_error(std::format("call {} failed", name));
            }
        }

    };
}
