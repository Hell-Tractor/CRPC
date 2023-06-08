#pragma once
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>
#include <format>
#include "logger.h"
#include "protocol.h"
#include <iostream>

namespace crpc {

    template<class T> using awaitable = asio::awaitable<T>;

    awaitable<void> client(asio::io_context& io_context, const char* host, const char* port)
    {
        LOGGER.log_info("client started");

        asio::ip::tcp::resolver resolver(io_context);
        asio::ip::tcp::socket socket(io_context);

        // 连接
        co_await socket.async_connect(*co_await resolver.async_resolve(host, port, asio::use_awaitable), asio::use_awaitable);
        LOGGER.log_info("connected to {}:{}", host, port);

        uint32_t current_seq_id = 0;

        while (true) {
            // 发送请求
            std::string msg; std::cin >> msg;
            proto::package request(proto::request_type::RPC_METHOD_REQUEST, current_seq_id++, msg);
            co_await request.await_write_to(socket);
            LOGGER.log_debug("send package: {}", request.brief_info());

            // 接受响应
            proto::package response;
            co_await response.await_read_from(socket);
            LOGGER.log_debug("recv package: {}", response.brief_info());
            LOGGER.log_info("response: {}", response.data());
        }
    }

    void run_client()
    {
        try {
            asio::io_context io_context(1);

            asio::signal_set signals(io_context, SIGINT, SIGTERM);
            signals.async_wait([&](auto, auto) { io_context.stop(); });

            asio::co_spawn(io_context, client(io_context, "127.0.0.1", "55555"), asio::detached);

            io_context.run();
        }
        catch (std::exception& e) {
            LOGGER.log_error(e.what());
        }
    }

}
