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

	server->register_method<int, int, int>("add", [](int a, int b) { 
		std::this_thread::sleep_for(std::chrono::seconds(2));  
		return a + b; 
	});
	server->register_method<int, int, int>("mul", mul);
	server->register_method<int, std::string>("size", get_size{});

	server->register_method<int, int, int>("add2", [&](int a, int b) -> asio::awaitable<int> {
		asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(3));
		asio::error_code ec;
		co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
		co_return a + b;
	});
	server->register_method<int, int, int>("mul2", [&](int a, int b) -> asio::awaitable<int> {
		asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(2));
		asio::error_code ec;
		co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
		co_return a * b;
	});
	server->register_method<int, std::string>("size2", [&](std::string s) -> asio::awaitable<int> {
		asio::steady_timer timer(server->get_io_context(), std::chrono::seconds(1));
		asio::error_code ec;
		co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
		co_return s.size();
	});

	server->start(55555);

	std::cin.get();
}