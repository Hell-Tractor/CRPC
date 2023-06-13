#pragma once
#include <unordered_set>
#include <unordered_map>
#include <random>
#include "logger.h"

namespace crpc {

	namespace route {

		// 路由策略
		class strategy {
		public:
			strategy() = default;
			virtual ~strategy() = default;
			virtual std::string get_route_address(
				const std::string& client_addr, 
				const std::string& service_name, 
				const std::unordered_set<std::string>& addr_set
			) = 0;
		};

		// 轮询
		class polling : public strategy {
		private:
			// 每个服务目前的轮询索引
			std::unordered_map<std::string, size_t> _service_indices;
		public:
			~polling() = default;
			std::string get_route_address(
				const std::string& client_addr,
				const std::string& service_name,
				const std::unordered_set<std::string>& addr_set
			) override {
				if (addr_set.empty()) throw route_error("no address for route");
				auto it = _service_indices.find(service_name);
				if(it == _service_indices.end()) {
					_service_indices[service_name] = 0;
					it = _service_indices.find(service_name);
				}
				auto& index = it->second;
				index %= addr_set.size();
				auto& addr = *std::next(addr_set.begin(), index);
				index = (index + 1) % addr_set.size();
				return addr;
			}
		};

		// 随机
		class random : public strategy {
		private:
			std::random_device _rd;
			std::mt19937 _gen;
		public:
			~random() = default;
			std::string get_route_address(
				const std::string& client_addr,
				const std::string& service_name,
				const std::unordered_set<std::string>& addr_set
			) override {
				if (addr_set.empty()) throw route_error("no address for route");
				std::uniform_int_distribution<> dis(0, addr_set.size() - 1);
				auto index = dis(_gen);
				auto& addr = *std::next(addr_set.begin(), index);
				return addr;
			}
		};

		// 地址哈希
		class hash : public strategy {
		private:
		public:
			~hash() = default;
			std::string get_route_address(
				const std::string& client_addr,
				const std::string& service_name,
				const std::unordered_set<std::string>& addr_set
			) override {
				if (addr_set.empty()) throw route_error("no address for route");
				std::string server_ip = client_addr.substr(0, client_addr.find(':'));
				std::hash<std::string> hash_fn;
				auto index = hash_fn(server_ip) % addr_set.size();
				auto& addr = *std::next(addr_set.begin(), index);
				return addr;
			}
		};

		// 路由策略工厂
		class strategy_factory {
		private:
		public:
			static std::shared_ptr<strategy> create(const std::string& name) {
				if (name == "polling") {
					return std::make_shared<polling>();
				}
				else if (name == "random") {
					return std::make_shared<random>();
				}
				else if (name == "hash") {
					return std::make_shared<hash>();
				}
				else {
					throw route_error("unknown route strategy");
				}
			}
		};
	}

}