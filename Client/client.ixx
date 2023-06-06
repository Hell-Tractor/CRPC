export module client;

import <asio.hpp>;
import <future>;
import <optional>;
import <string>;

import "cereal.h";
import "crpc_except.h";

using asio_tcp = asio::ip::tcp;

namespace crpc {
    export class client final {
        asio::io_context io_context_;
        asio_tcp::endpoint server_endpoint_;
        asio_tcp::socket server_socket_;
    public:
        explicit client(asio_tcp::endpoint&& server_endpoint) : server_endpoint_(std::move(server_endpoint)), server_socket_(io_context_) {}
        void connect_registry() {
            throw unimplemented_error("connect_registry not implemented");
        }
        void connect_server() {
            if (server_socket_.is_open())
                return;
            server_socket_.connect(server_endpoint_);
        }
        template <typename ReturnType, typename... Args>
        ReturnType call(const std::string& method, Args&&... args) {
            this->connect_server();

            std::string data = cereal::instance().serialize(method, std::forward<Args>(args)...);
            this->server_socket_.send(asio::buffer(data));

            std::string response;
            this->server_socket_.receive(asio::buffer(response));
            return cereal::instance()->deserialize<ReturnType>(response);
        }
        template <typename ReturnType, typename... Args>
        std::future<ReturnType> async_call(const std::string& method, Args&&... args) {
            this->connect_server();
            std::promise<ReturnType> promise;
            std::string data = cereal::instance().serialize(method, std::forward<Args>(args)...);
            this->server_socket_.async_send(asio::buffer(data), [&promise, this](const std::error_code& error_code, auto bytes_transferred) {
                if (error_code) {
                    promise.set_exception(std::make_exception_ptr(std::runtime_error("Failed to send message: " + error_code.message())));
                    return;
                }
                std::string response;
                this->server_socket_.async_receive(asio::buffer(response), [&promise, &response](const std::error_code& error_code, auto bytes_transferred) {
                if (error_code)
                    promise.set_exception(std::make_exception_ptr(std::runtime_error("Failed to receive message: " + error_code.message())));
                else
                    promise.set_value(cereal::instance().deserialize<ReturnType>(response));
                });
            });
            return promise.get_future();
        }
        template <typename ReturnType, typename... Args>
        void async_call(const std::string& method, std::function<void(std::error_code, std::optional<ReturnType>)> callback, Args&&... args) {
            this->connect_server();
            std::string data = cereal::instance().serialize(method, std::forward<Args>(args)...);
            this->server_socket_.async_send(asio::buffer(data), [&callback, this](const std::error_code error_code, auto bytes_transferred) {
                if (error_code) {
                    callback(error_code, std::optional<ReturnType>());
                    return;
                }
                std::string response;
                this->server_socket_.async_receive(asio::buffer(response), [&response, &callback](const std::error_code& error_code, auto bytes_transferred) {
                    if (error_code)
                        callback(error_code, std::optional<ReturnType>());
                    else
                        callback(error_code, std::optional<ReturnType>(cereal::instance().deserialize<ReturnType>(response)));
                });
            });
        }
        void run() {
            throw unimplemented_error("run not implemented");
        }
    };
}
