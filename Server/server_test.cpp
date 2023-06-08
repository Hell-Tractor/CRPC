#include "server.h"
#include <iostream>

int main() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto server = std::make_shared<crpc::server>();

	server->register_method("f1", []() -> crpc::awaitable<std::string> {
		co_return "f1_result";
	});
	server->register_method("f2", []() -> crpc::awaitable<std::string> {
		co_return "f2_result";
	});

	server->start(55555);
}