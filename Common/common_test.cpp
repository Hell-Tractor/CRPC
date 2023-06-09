#include <iostream>
#include <vector>
#include <memory>
#include <functional>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include "cereal.h"
#include "logger.h"

class method_base_t {
public:
    virtual ~method_base_t() = default;
    virtual std::string call(const std::string& serialized_data) = 0;
};

template <typename return_t, typename ...args_t>
class method_t : public method_base_t {
public:
    using function_t = std::function<return_t(args_t...)>;
    method_t(function_t func) : _func(func) {}
    
    std::string call(const std::string& serialized_data) {
        auto args = crpc::cereal::instance().deserialize_rpc_request_args<args_t...>(serialized_data);
        return_t ret;
        std::apply([&](auto... unpacked_args) {
            ret = _func(unpacked_args...);
        }, args);
        return crpc::cereal::instance().serialize_rpc_response(ret);
    }

private:
    function_t _func;
};

int main() {
    LOGGER.add_stream(std::cout);
    asio::io_context io_context;


   
    io_context.run();
    return 0;
}
