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

namespace crpc {

    static constexpr std::chrono::milliseconds DEFAULT_RPC_CALL_TIMEOUT = std::chrono::seconds(10);

    // RPC客户端类型
    class client final : public std::enable_shared_from_this<client> {
    private:
        std::shared_ptr<asio::io_context> _io_context;
        bool _use_self_io_context;   // 是否使用自身的io_context
        asio::executor_work_guard<asio::io_context::executor_type> _work_guard;
        std::unique_ptr<std::jthread> _io_thread;

        asio::ip::tcp::resolver _resolver;
        asio::ip::tcp::socket _socket;
        asio::steady_timer _timer;

        std::queue<proto::package> _send_queue;

        struct request_status {
            std::promise<std::string> promise;
            std::unique_ptr<asio::steady_timer> timer;
            bool done = false;
        };
        std::unordered_map<uint32_t, request_status> _request_status;
        
        uint32_t _current_seq_id = 0;


        client() :
            _io_context(std::make_shared<asio::io_context>(1)),
            _use_self_io_context(true),
            _work_guard(asio::make_work_guard(get_io_context())),
            _resolver(get_io_context()),
            _socket(get_io_context()),
            _timer(get_io_context()) {

            _timer.expires_at(std::chrono::steady_clock::time_point::max());
            _io_thread = std::make_unique<std::jthread>([this] {
                LOGGER.log_debug("client io thread started");
                get_io_context().run();
                LOGGER.log_debug("client io thread stopped");
                });
        }

        client(std::shared_ptr<asio::io_context> io_context) :
            _io_context(io_context),
            _use_self_io_context(false),
            _work_guard(asio::make_work_guard(get_io_context())),
            _resolver(get_io_context()),
            _socket(get_io_context()),
            _timer(get_io_context()) {

            _timer.expires_at(std::chrono::steady_clock::time_point::max());
        }


        // RPC调用超时时间
        std::chrono::milliseconds _rpc_call_timeout = DEFAULT_RPC_CALL_TIMEOUT;

        // 发送协程
        asio::awaitable<void> _handle_send() {
            LOGGER.log_info("send coroutine started");
            try {
                while (_socket.is_open()) {
                    if (_send_queue.empty()) {
                        asio::error_code ec;
                        co_await _timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    }
                    else {
                        auto& request = _send_queue.front();
                        co_await request.await_write_to(_socket);
                        LOGGER.log_debug("send package: {}", request.brief_info());
                        _send_queue.pop();
                    }
                }
            }
            catch (const std::exception& e) {
				LOGGER.log_error("send coroutine error: {}", e.what());
			}

            disconnet();
            LOGGER.log_info("send coroutine stopped");
		}

        // 接受协程
        asio::awaitable<void> _handle_recv() {
            LOGGER.log_info("recv coroutine started");
            try {
                while (_socket.is_open()) {
                    proto::package response{};
                    co_await response.await_read_from(_socket);
                    LOGGER.log_debug("recv package: {}", response.brief_info());
                    if (response.type() == proto::request_type::RPC_METHOD_RESPONSE) {
                        _handle_rpc_response(std::move(response));
                    }
                }
            }
            catch (const std::exception& e) {
                LOGGER.log_error("recv coroutine error: {}", e.what());
            }

            disconnet();
            LOGGER.log_info("recv coroutine stopped");
        }

        // 添加发送
        void _push_send(proto::package request) {
			_send_queue.push(std::move(request));
            _timer.cancel_one();
		}

        // 处理接受
        void _handle_rpc_response(proto::package response) {
            auto id = response.seq_id();
            if (_request_status.contains(id)) {
				auto& [promise, timer, done] = _request_status[id];
				promise.set_value(std::move(response.data()));
                timer->cancel();
                done = true;
                LOGGER.log_info("rpc #{} done", id);
            }
            else {
                // ignore
            }
        } 

    public:
       
        static std::shared_ptr<client> create() {
            return std::shared_ptr<client>(new client());
		}

        static std::shared_ptr<client> create(std::shared_ptr<asio::io_context> io_context) {
            return std::shared_ptr<client>(new client(io_context));
        }

        ~client() {
            if(_use_self_io_context) {
                _io_context->stop();
                _io_thread->join();
			}
		}

        asio::io_context& get_io_context() {
			return *_io_context;
		}

        client(const client&) = delete;
        client& operator=(const client&) = delete;

        // 连接服务器（阻塞）
        void connect_server(const std::string& host, uint16_t port) {
            LOGGER.log_info("waiting for connection to {}:{}", host, port);
            auto endpoints = _resolver.resolve(host, std::to_string(port));
            _socket.connect(*endpoints.begin());
            LOGGER.log_info("connected to {}:{}", host, port);

            auto self = shared_from_this();
            asio::co_spawn(get_io_context(), [self] { return self->_handle_send(); }, asio::detached);
            asio::co_spawn(get_io_context(), [self] { return self->_handle_recv(); }, asio::detached);
        }

        // 异步rpc请求
        template<class return_t, class ... args_t>
        rpc_future<return_t> async_call(const std::string& name, args_t... args) {
            if (!_socket.is_open()) {
                throw std::runtime_error("connection closed");
            }
            auto seq_id = _current_seq_id++;
            LOGGER.log_info("rpc call #{}: {}", seq_id, name);
            auto request_data = serializer::instance().serialize_rpc_request<args_t...>(name, args...);
            proto::package request(proto::request_type::RPC_METHOD_REQUEST, seq_id, request_data);
            _push_send(std::move(request));

            _request_status[seq_id] = request_status{
                std::promise<std::string>{},
                std::make_unique<asio::steady_timer>(get_io_context(), _rpc_call_timeout),
                false
            };

            // 处理超时
            asio::co_spawn(get_io_context(), [seq_id, self = shared_from_this()]() -> asio::awaitable<void> {
                auto& [promise, timer, done] = self->_request_status[seq_id];
                asio::error_code ec;
                co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
                if (!self->_socket.is_open()) {
                    LOGGER.log_error("rpc #{} timeout", seq_id);
                    promise.set_exception(std::make_exception_ptr(timeout_error("connection closed")));
                } else if (!done) {
                    LOGGER.log_error("rpc #{} timeout", seq_id);
                    promise.set_exception(std::make_exception_ptr(timeout_error("rpc timeout")));
				}
                timer->cancel();
				self->_request_status.erase(seq_id);
			}, asio::detached);

            return rpc_future<return_t>(_request_status[seq_id].promise.get_future());
        }

        // 同步rpc请求
        template<class return_t, class ... args_t>
        return_t call(const std::string& name, args_t... args) {
			return async_call<return_t>(name, args...).get();
		}

		// 关闭到服务器的连接
        void disconnet() {
			_socket.close();
            for (auto& [k, v] : _request_status) 
                v.timer->cancel();
		}
    };
}
