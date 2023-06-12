#include "connection_pool.h"
#include "client.h"
#include <iostream>
#include <fstream>

void connection_pool_test() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto con = std::make_shared<crpc::connection_pool>();
	con->connect_registry("127.0.0.1", 55554);

	con->subscribe_service("add")->subscribe_service("mul")->subscribe_service("size")->push_subscribe_update();

	con->unsubscribe_service("add")->subscribe_service("add")->unsubscribe_service("mul")->push_subscribe_update();

	con->subscribe_service("mul")->push_subscribe_update();
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
		LOGGER.log_error("main exception: {}", e.what());
	}
	std::cin.get();
}