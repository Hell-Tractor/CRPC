#include "client.h"
#include <iostream>
#include <fstream>

int main() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto client = std::make_shared<crpc::client>();
	client->connect_server("127.0.0.1", 55555);

	try {
		/*
		auto fut1 = client->async_call<int, int, int>("add2", 1, 2);
		auto fut2 = client->async_call<int, int, int>("mul2", 2, 3);
		auto fut3 = client->async_call<int, std::string>("size2", "hello world");
		*/

		try { LOGGER.log_info("{}", client->call<int, int, int>("add2", 1, 2)); }
		catch (const std::exception& e) { LOGGER.log_error("exception: {}", e.what()); }
		try { LOGGER.log_info("{}", client->call<int, int, int>("mul2", 2, 3)); }
		catch (const std::exception& e) { LOGGER.log_error("exception: {}", e.what()); }
		try { LOGGER.log_info("{}", client->call<int, std::string>("size2", "hello world")); }
		catch (const std::exception& e) { LOGGER.log_error("exception: {}", e.what()); }

		/*
		try { LOGGER.log_info("{}", fut1.get()); }
		catch (const std::exception& e) { LOGGER.log_error("exception: {}", e.what()); }
		try { LOGGER.log_info("{}", fut2.get()); }
		catch (const std::exception& e) { LOGGER.log_error("exception: {}", e.what()); }
		try { LOGGER.log_info("{}", fut3.get()); }
		catch (const std::exception& e) { LOGGER.log_error("exception: {}", e.what()); }
		*/

	}
	catch (const std::exception& e) {
		LOGGER.log_error("exception: {}", e.what());
	}

	std::cin.get();
}