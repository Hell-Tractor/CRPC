#include <cereal/archives/json.hpp>
#include <cereal/types/vector.hpp>

#include "logger.h"
#include <asio.hpp>
#include "client.hpp"

int main(int argc, char** argv) {
    LOGGER.add_stream(std::cout);
    crpc::client client(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 8080));
    try {
        int result = client.call<int>("add", 1, 2);
    } catch (std::exception& e) {
        LOGGER.log_error(e.what());
    }
    return 0;
}
