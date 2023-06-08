#include "client.h"
#include <iostream>

int main() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	crpc::run_client();
}