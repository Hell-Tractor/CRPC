#include "connection_pool.h"
#include "client.h"
#include <iostream>
#include <fstream>

void connection_pool_test() {
	// LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto con = std::make_shared<crpc::connection_pool>(10, "polling");

	con->subscribe_service("add1")->subscribe_service("mul1")->subscribe_service("add2")->subscribe_service("mul2");
	con->connect_registry("127.0.0.1", 55554);

	for (int i = 0; ; i++) {
		try {
			std::cout << con->call<std::string>("add1", 0, i) << std::endl;
			std::cout << con->call<std::string>("mul1", 1, i) << std::endl;
		}
		catch (const std::exception& e) {
			std::cerr << std::format("exception: {}", e.what()) << std::endl;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void client_test() {
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
}

int main() {
	try {
		connection_pool_test();
		// client_test();
	}
	catch (const std::exception& e) {
		std::cerr << std::format("main exception: {}", e.what()) << std::endl;
	}

	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}