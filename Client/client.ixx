export module client;

import <asio.hpp>;
import <future>;
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
        throw unimplemented_error("async_call not implemented");
        // ? 如何将回调变为future
        //this->connect_server();

        //std::string data = cereal::instance()->serialize(method, std::forward<Args>(args)...);
        //this->server_socket_.async_send(asio::buffer(data), [](auto a, auto b) {
        //  
        //});
      }
      template <typename ReturnType, typename... Args>
      void async_call(const std::string& method, std::function<void(ReturnType)> callback, Args&&... args) {
        throw unimplemented_error("async_call not implemented");
      }
      void run() {
        throw unimplemented_error("run not implemented");
      }
  };
}
