#include "registry.h"
#include <iostream>

int main() {
	LOGGER.add_stream(std::cout, utils::logger::level::debug);

	auto reg = std::make_shared<crpc::registry>();

	reg->start("127.0.0.1", 55554);

	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}