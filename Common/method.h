#pragma once
#include <string>
#include <future>
#include "serializer.h"

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
            try {
                auto args = crpc::serializer::instance().deserialize_rpc_request_args<args_t...>(serialized_data);
                return_t ret;
                std::apply([&](auto&&... unpacked_args) {
                    ret = _func(std::forward<args_t>(unpacked_args)...);
                    }, args);
                return crpc::serializer::instance().serialize_rpc_response<return_t>(ret, {});
            }
            catch (std::exception& e) {
				return crpc::serializer::instance().serialize_rpc_response<return_t>({}, e.what());
			}
        }

    private:
        funtion_t _func;
    };

    class awaitable_method_base_t {
    public:
        virtual ~awaitable_method_base_t() = default;
        virtual asio::awaitable<std::string> call(const std::string& serialized_data) = 0;
    };

    template <typename return_t, typename ...args_t>
    class awaitable_method_t : public awaitable_method_base_t {
    public:
        using funtion_t = std::function<asio::awaitable<return_t>(args_t...)>;
		awaitable_method_t(funtion_t func) : _func(func) {}

        asio::awaitable<std::string> call(const std::string& serialized_data) {
            try {
                auto args = crpc::serializer::instance().deserialize_rpc_request_args<args_t...>(serialized_data);
                return_t ret = co_await std::apply([&](auto&&... unpacked_args) {
                    return _func(std::forward<args_t>(unpacked_args)...);
                }, args);
                co_return crpc::serializer::instance().serialize_rpc_response<return_t>(ret, {});
            }
            catch (std::exception& e) {
                co_return crpc::serializer::instance().serialize_rpc_response<return_t>({}, e.what());
			}
		}
    private:
        funtion_t _func;
    };

    template<class return_t>
    class rpc_future : public std::future<std::string> {
    public:
        return_t get() {
            auto serialized_ret = std::future<std::string>::get();
            std::string error = crpc::serializer::instance().deserialize_rpc_response_error(serialized_ret);
            if (!error.empty()) {
				throw std::runtime_error(error);
			}
            return crpc::serializer::instance().deserialize_rpc_response_result<return_t>(serialized_ret);
        }
    };

}