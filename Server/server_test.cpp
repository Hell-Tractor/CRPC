#include "server.h"
#include <asio/redirect_error.hpp>
#include <iostream>

/*
int mul(int a, int b) { 
	std::this_thread::sleep_for(std::chrono::seconds(3));
	return a * b;  
}

struct get_size {
	int operator()(std::string s) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		return s.size();
	}
};
*/

int main(int argc, char* argv[]) {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	uint16_t port = 0;
	std::cout << "input port: ";
	std::cin >> port;

	auto server = crpc::server::create("127.0.0.1", port);

	try {
		server->register_service<std::string, int, int>("add1", [port](int a, int b) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			return std::format("server on port{} add1 return {}", port, a + b);
		});
		server->register_service<std::string, int, int>("add2", [&](int a, int b) -> asio::awaitable<std::string> {
			asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(1));
			asio::error_code ec;
			co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
			co_return std::format("server on port{} add2 return {}", port, a + b);
		});
		server->register_service<std::string, int, int>("mul1", [port](int a, int b) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			return std::format("server on port{} mul1 return {}", port, a * b);
			});
		server->register_service<std::string, int, int>("mul2", [&](int a, int b) -> asio::awaitable<std::string> {
			asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(1));
			asio::error_code ec;
			co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
			co_return std::format("server on port{} mul2 return {}", port, a * b);
		});

		server->connect_registry("127.0.0.1", 55554);
		server->push_service_update();
		server->start();
	}
	catch (const std::exception& e) {
		LOGGER.log_error("main exception: {}", e.what());
	}

	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}