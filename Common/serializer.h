#pragma once

#include <sstream>
#include <string>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
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
        using input_archive = ::cereal::BinaryInputArchive;
        using output_archive = ::cereal::BinaryOutputArchive;

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
                    archive(std::make_tuple(args...));
                }
                return method + '\n' + ss.str();
            }
            catch (const std::exception& e) {
                throw serialize_error("failed when serializing rpc request");
            }
        }

        // 反序列化rpc请求的方法名
        std::string deserialize_rpc_request_method(const std::string& data) {
            try {
                auto it = std::find(data.begin(), data.end(), '\n');
                return std::string(data.begin(), it);
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing method name of rpc request");
            }
        }

        // 反序列化rpc请求的参数
        template <typename... args_t>
        std::tuple<args_t...> deserialize_rpc_request_args(const std::string& data) {
            try {
                auto it = std::find(data.begin(), data.end(), '\n');
                std::stringstream ss(std::string(it + 1, data.end()));
                input_archive archive(ss);
                std::tuple<args_t...> args;
                archive(args);
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
                    archive(result, error);
                }
                return ss.str();
            }
            catch (const std::exception& e) {
                throw serialize_error("failed when serializing rpc response");
            }
        }

        // 反序列化rpc响应
        template <typename return_t>
        std::tuple<return_t, std::string> deserialize_rpc_response(const std::string& data) {
            try {
                std::stringstream ss(data);
                input_archive archive(ss);
                return_t result;
                std::string error;
                archive(result, error);
                return std::tuple<return_t, std::string>(result, error);
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing rpc response");
            }
        }

        // 序列化服务器上线（vector<pair<服务名，状态>>, 地址）
        std::string serialize_server_online(std::vector<std::pair<std::string, bool>> service_list, std::string addr) {
            try {
                std::stringstream ss;
                {
                    output_archive archive(ss);
                    archive(service_list, addr);
                }
                return ss.str();
            }
            catch (const std::exception& e) {
                throw serialize_error("failed when serializing server online info");
            }
        }

        // 反序列化服务器上线
        std::tuple<std::vector<std::pair<std::string, bool>>, std::string> deserialize_server_online(const std::string& data) {
			try {
				std::stringstream ss(data);
				input_archive archive(ss);
				std::vector<std::pair<std::string, bool>> service_list;
                std::string addr;
				archive(service_list, addr);
				return std::tuple<std::vector<std::pair<std::string, bool>>, std::string>(service_list, addr);
			}
			catch (const std::exception& e) {
				throw deserialize_error("failed when deserializing server online info");
			}
		}

        // 序列化rpc服务提供更新（vector<pair<服务名，状态>>）
        std::string serialize_provide_update(std::vector<std::pair<std::string, bool>> service_list) {
            try {
				std::stringstream ss;
                {
					output_archive archive(ss);
					archive(service_list);
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
				archive(service_list);
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
                    archive(subscribe_list);
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
                archive(subscribe_list);
                return subscribe_list;
            }
            catch (const std::exception& e) {
                throw deserialize_error("failed when deserializing subscribe update");
            }
        }

        // 序列化rpc服务更新（vector<tuple<服务名，地址，状态>>）
        std::string serialize_service_update(std::vector<std::tuple<std::string, std::string, bool>> service_list) {
            try {
                std::stringstream ss;
                {
                    output_archive archive(ss);
                    archive(service_list);
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
				archive(service_list);
				return service_list;
			}
			catch (const std::exception& e) {
				throw deserialize_error("failed when deserializing rpc service update");
			}
		}

    };
}