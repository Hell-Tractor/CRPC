#include <iostream>
#include <vector>
#include <memory>
#include <functional>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include "serializer.h"
#include "logger.h"
#include "method.h"

int main() {
    LOGGER.add_stream(std::cout);
    asio::io_context io_context(1);

    crpc::awaitable_method_t<int, int, int> m([](int a, int b) -> asio::awaitable<int> {
		co_return a + b;
	});

    auto in_data = crpc::serializer::instance().serialize_rpc_request("", 1, 2);

    asio::co_spawn(io_context, [&]() -> asio::awaitable<void> {
        try {
			auto out = co_await m.call(in_data);
			LOGGER.log_info("{}", out);
		}
        catch (const std::exception& e) {
			LOGGER.log_error("exception: {}", e.what());
		}
	}, asio::detached);

    io_context.run();
    return 0;
}
