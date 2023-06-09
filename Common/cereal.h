#pragma once

#include <sstream>
#include <string>
#include <cereal/archives/json.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>

#include "singleton.h"

namespace crpc {
    class cereal final : public utils::singleton<cereal> {
        using input_archive = ::cereal::JSONInputArchive;
        using output_archive = ::cereal::JSONOutputArchive;

    public:
        template <typename... args_t>
        std::string serialize(args_t... args) {
            std::stringstream ss;
            {
                output_archive archive(ss);
                archive(std::make_tuple(args...));
            }
            return ss.str();
        }

        template <typename... args_t>
        std::tuple<args_t...> deserialize(const std::string& data) {
            std::stringstream ss(data);
            std::tuple<args_t...> args;
            input_archive archive(ss);
            archive(args);
            return args;
        }

        template <typename... args_t>
        std::string serialize_rpc_request(std::string method, args_t... args) {
            std::stringstream ss;
            {
                output_archive archive(ss);
                archive(::cereal::make_nvp("method", method), ::cereal::make_nvp("args", std::make_tuple(args...)));
            }
            return ss.str();
        }

        std::string deserialize_rpc_request_method(const std::string& data) {
            std::stringstream ss(data);
            input_archive archive(ss);
            std::string method;
            archive(::cereal::make_nvp("method", method));
            return method;
        }

        template <typename... args_t>
        std::tuple<args_t...> deserialize_rpc_request_args(const std::string& data) {
			std::stringstream ss(data);
			input_archive archive(ss);
			std::tuple<args_t...> args;
			archive(::cereal::make_nvp("args", args));
			return args;
		}

        template <typename return_t>
        std::string serialize_rpc_response(return_t result) {
            std::stringstream ss;
            {
                output_archive archive(ss);
                archive(::cereal::make_nvp("result", result));
            }
            return ss.str();
        }

        template <typename return_t>
        return_t deserialize_rpc_response(const std::string& data) {
            std::stringstream ss(data);
            input_archive archive(ss);
            return_t result;
            archive(::cereal::make_nvp("result", result));
            return result;
        }
    };
}