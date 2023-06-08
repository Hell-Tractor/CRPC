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

    // RPC服务器
    class server final : public std::enable_shared_from_this<server> {
    private:
        uint16_t _port;
        
        std::unique_ptr<asio::io_context> _io_context;
        std::unique_ptr<asio::ip::tcp::acceptor> _acceptor;
        std::unique_ptr<asio::signal_set> _signals;

        std::unordered_map<std::string, std::function<awaitable<std::string>()>> _methods;

    public:
        // 注册一个方法
        std::shared_ptr<server> register_method(std::string name, std::function<awaitable<std::string>()> method) {
            _methods[name] = method;
            return shared_from_this();
        }

        // 删除一个方法
        std::shared_ptr<server> unregister_method(std::string name) {
            _methods.erase(name);
			return shared_from_this();
		}

        // 处理单个客户端会话的协程
        awaitable<void> client_session(asio::ip::tcp::socket socket) {
            const std::string addr_str = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
            LOGGER.log_info("<{}> client session started", addr_str);

            uint32_t current_seq_id = 0;

            try {
                while (true) {
                    // 接受请求
                    proto::package request;
                    co_await request.await_read_from(socket);
                    LOGGER.log_debug("<{}> recv package from client: {}", addr_str, request.brief_info());

                    if (request.type() == proto::request_type::RPC_METHOD_REQUEST) {
                        // 方法调用请求
                        std::string name = request.data();
                        LOGGER.log_info("<{}> method request from client: {}", addr_str, name);

                        std::string response_msg;
                        auto it = _methods.find(name);
                        if (it == _methods.end()) {
                            response_msg = std::format("no handler for {}", name);
                            LOGGER.log_info("<{}> method no found for client: {}", addr_str, name);
                        } else {
                            response_msg = co_await it->second();
                            LOGGER.log_info("<{}> method done for client: {}", addr_str, name);
                        }
                        // 响应
                        proto::package response(proto::request_type::RPC_METHOD_RESPONSE, request.seq_id() + 1u, response_msg);
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

        // 监听协程
        awaitable<void> listener()
        {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            LOGGER.log_info("server listening on port {}", _port);
            while (true) {
                // 接受客户端连接 生成客户端会话协程
                asio::ip::tcp::socket socket = co_await _acceptor->async_accept(asio::use_awaitable);
                asio::co_spawn(executor, [self, &socket] { return self->client_session(std::move(socket)); }, asio::detached);
            }
        }

        // 启动服务端
        void start(uint16_t port) {
            auto self = shared_from_this();
            _port = port;
            _io_context = std::make_unique<asio::io_context>(1);
            _acceptor = std::make_unique<asio::ip::tcp::acceptor>(*_io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), _port));
            _signals = std::make_unique<asio::signal_set>(*_io_context, SIGINT, SIGTERM);

            _signals->async_wait([&](auto, auto) { _io_context->stop(); });
            asio::co_spawn(*_io_context, [self] { return self->listener(); }, asio::detached);
            _io_context->run();
        }
    };

}