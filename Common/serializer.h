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
#include "crpc_except.h"

namespace crpc {
    class serializer final : public utils::singleton<serializer> {
        using input_archive = ::cereal::JSONInputArchive;
        using output_archive = ::cereal::JSONOutputArchive;

    public:

        // 序列化普通数据
        template <typename... args_t>
        std::string serialize(args_t... args) {
            std::stringstream ss;
            {
                output_archive archive(ss);
                archive(std::make_tuple(args...));
            }
            return ss.str();
        }

        // 反序列化普通数据
        template <typename... args_t>
        std::tuple<args_t...> deserialize(const std::string& data) {
            std::stringstream ss(data);
            std::tuple<args_t...> args;
            input_archive archive(ss);
            archive(args);
            return args;
        }


        // 序列化rpc请求（方法名、参数）
        template <typename... args_t>
        std::string serialize_rpc_request(std::string method, args_t... args) {
            try {
                std::stringstream ss;
                {
                    output_archive archive(ss);
                    archive(::cereal::make_nvp("method", method), ::cereal::make_nvp("args", std::make_tuple(args...)));
                }
                return ss.str();
            }
            catch (const std::exception& e) {
                throw serialize_error("failed when serializing rpc request");
            }
        }

        // 反序列化rpc请求的方法名
        std::string deserialize_rpc_request_method(const std::string& data) {
            try {
                std::stringstream ss(data);
                input_archive archive(ss);
                std::string method;
                archive(::cereal::make_nvp("method", method));
                return method;
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing method name of rpc request");
            }
        }

        // 反序列化rpc请求的参数
        template <typename... args_t>
        std::tuple<args_t...> deserialize_rpc_request_args(const std::string& data) {
            try {
                std::stringstream ss(data);
                input_archive archive(ss);
                std::tuple<args_t...> args;
                archive(::cereal::make_nvp("args", args));
                return args;
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing args of rpc request");
            }
        }

        // 序列化rpc响应（结果、错误）
        template <typename return_t>
        std::string serialize_rpc_response(return_t result, std::string error) {
            try {
                std::stringstream ss;
                {
                    output_archive archive(ss);
                    archive(::cereal::make_nvp("result", result), ::cereal::make_nvp("error", error));
                }
                return ss.str();
            }
            catch (const std::exception& e) {
                throw serialize_error("failed when serializing rpc response");
            }
        }

        // 反序列化rpc响应的错误
        std::string deserialize_rpc_response_error(const std::string& data) {
            try {
                std::stringstream ss(data);
                input_archive archive(ss);
                std::string error;
                archive(::cereal::make_nvp("error", error));
                return error;
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing error of rpc response");
            }
        }

        // 反序列化rpc响应的结果
        template <typename return_t>
        return_t deserialize_rpc_response_result(const std::string& data) {
            try {
                std::stringstream ss(data);
                input_archive archive(ss);
                return_t result;
                archive(::cereal::make_nvp("result", result));
                return result;
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing result of rpc response");
            }
        }

        // 序列化rpc服务提供更新（vector<pair<服务名，状态>>）
        std::string serialize_provide_update(std::vector<std::pair<std::string, bool>> service_list) {
            try {
				std::stringstream ss;
                {
					output_archive archive(ss);
					archive(::cereal::make_nvp("service_list", service_list));
				}
				return ss.str();
			}
            catch (const std::exception& e) {
				throw serialize_error("failed when serializing provide update");
			}
		}

        // 反序列化rpc服务提供更新
        std::vector<std::pair<std::string, bool>> deserialize_provide_update(const std::string& data) {
            try {
				std::stringstream ss(data);
				input_archive archive(ss);
				std::vector<std::pair<std::string, bool>> service_list;
				archive(::cereal::make_nvp("service_list", service_list));
				return service_list;
			}
            catch (const std::exception& e) {
				throw deserialize_error("failed when deserializing provide update");
			}
		}

        // 序列化rpc服务订阅更新（vector<pair<服务名，状态>>）
        std::string serialize_subscribe_update(std::vector<std::pair<std::string, bool>> subscribe_list) {
            try {
                std::stringstream ss;
                {
                    output_archive archive(ss);
                    archive(::cereal::make_nvp("subscribe_list", subscribe_list));
                }
                return ss.str();
            }
            catch (const std::exception& e) {
                throw serialize_error("failed when serializing subscribe update");
            }
        }

        // 反序列化rpc服务订阅更新
        std::vector<std::pair<std::string, bool>> deserialize_subscribe_update(const std::string& data) {
            try {
                std::stringstream ss(data);
                input_archive archive(ss);
                std::vector<std::pair<std::string, bool>> subscribe_list;
                archive(::cereal::make_nvp("subscribe_list", subscribe_list));
                return subscribe_list;
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing subscribe update");
            }
        }

        // 序列化rpc服务更新（vector<tuple<服务名，地址，状态>>）
        std::string serialize_service_update(std::vector<std::tuple<std::string, std::string, bool>> subscribe_list) {
            try {
                std::stringstream ss;
                {
                    output_archive archive(ss);
                    archive(::cereal::make_nvp("service_list", subscribe_list));
                }
                return ss.str();
            }
            catch (const std::exception& e) {
                throw serialize_error("failed when serializing rpc service update");
            }
        }

        // 反序列化rpc服务更新
        std::vector<std::tuple<std::string, std::string, bool>> deserialize_service_update(const std::string& data) {
			try {
				std::stringstream ss(data);
				input_archive archive(ss);
				std::vector<std::tuple<std::string, std::string, bool>> service_list;
				archive(::cereal::make_nvp("service_list", service_list));
				return service_list;
			}
			catch (const std::exception& e) {
				throw deserialize_error("failed when deserializing rpc service update");
			}
		}

    };
}