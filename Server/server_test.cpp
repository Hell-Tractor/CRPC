#include "server.h"
#include <iostream>

int mul(int a, int b) { return a * b;  }

struct get_size {
	int operator()(std::string s) {
		return s.size();
	}
};

int main() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto io_context = std::make_shared<asio::io_context>();
	auto server = std::make_shared<crpc::server>(io_context);

	server->register_method<int, int, int>("add", [](int a, int b) { return a + b; });
	server->register_method<int, int, int>("mul", mul);
	server->register_method<int, std::string>("size", get_size{});

	server->start(55555);

	try {
		asio::signal_set signals(*io_context, SIGINT, SIGTERM);
		signals.async_wait([&](auto, auto) { io_context->stop(); });
		io_context->run();
	}
	catch (const std::exception& e) {
		LOGGER.log_error("exception: {}", e.what());
	}
}