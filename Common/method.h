#pragma once
#include <string>
#include "cereal.h"

namespace crpc {

    class method_base_t {
    public:
        virtual ~method_base_t() = default;
        virtual std::string call(const std::string& serialized_data) = 0;
    };

    template <typename return_t, typename ...args_t>
    class method_t : public method_base_t {
    public:
        using funtion_t = std::function<return_t(args_t...)>;
        method_t(funtion_t func) : _func(func) {}

        std::string call(const std::string& serialized_data) {
            auto args = crpc::cereal::instance().deserialize_rpc_request_args<args_t...>(serialized_data);
            return_t ret;
            std::apply([&](auto&&... unpacked_args) {
                ret = _func(std::forward<args_t>(unpacked_args)...);
            }, args);
            return crpc::cereal::instance().serialize_rpc_response(ret);
        }

    private:
        funtion_t _func;
    };

}