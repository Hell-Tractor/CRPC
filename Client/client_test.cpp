#include "client.h"
#include <iostream>
#include <fstream>

int main() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto io_context = std::make_shared<asio::io_context>();

	auto client = std::make_shared<crpc::client>(io_context);
	client->connect_server("127.0.0.1", 55555);

	try {
		LOGGER.log_info("{}", client->call<int, int, int>("add", 1, 2));
		LOGGER.log_info("{}", client->call<int, int, int>("mul", 2, 3));
		LOGGER.log_info("{}", client->call<int, std::string>("size", "hello world"));
	}
	catch (const std::exception& e) {
		LOGGER.log_error("exception: {}", e.what());
	}
	std::cin.get();
}