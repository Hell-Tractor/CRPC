#include "server.h"
#include <asio/redirect_error.hpp>
#include <iostream>

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

int main() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto server = std::make_shared<crpc::server>();

	try {
		server->connect_registry("127.0.0.1", 55554);

		server->register_service<int, int, int>("add", [](int a, int b) {
			std::this_thread::sleep_for(std::chrono::seconds(2));
			return a + b;
			});
		server->register_service<int, int, int>("mul", mul);
		server->register_service<int, std::string>("size", get_size{});

		server->register_service<int, int, int>("add2", [&](int a, int b) -> asio::awaitable<int> {
			asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(3));
			asio::error_code ec;
			co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
			co_return a + b;
		});
		server->register_service<int, int, int>("mul2", [&](int a, int b) -> asio::awaitable<int> {
			asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(2));
			asio::error_code ec;
			co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
			co_return a * b;
		});
		server->register_service<int, std::string>("size2", [&](std::string s) -> asio::awaitable<int> {
			asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(1));
			asio::error_code ec;
			co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
			co_return s.size();
		});

		server->push_service_update();

		server->unregister_method("mul")->unregister_method("size")->register_service<int, std::string>("size", get_size{})->push_service_update();
		server->register_service<int, int, int>("mul", mul)->push_service_update();

		server->start("127.0.0.1", 55555);
	}
	catch (const std::exception& e) {
		LOGGER.log_error("main exception: {}", e.what());
	}

	std::cin.get();
}