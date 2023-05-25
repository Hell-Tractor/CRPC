#pragma once

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
    public:
      template <typename... Args>
      std::string serialize(const std::string& method, Args&&... args) {
        std::stringstream ss;
        ::cereal::JSONOutputArchive archive(ss);
        archive(::cereal::make_nvp("method", method), ::cereal::make_nvp("args", std::make_tuple(std::forward<Args>(args)...)));
        return ss.str();
      }
      template <typename ReturnType>
      ReturnType deserialize(const std::string& data) {
        std::stringstream ss(data);
        ::cereal::JSONInputArchive archive(ss);
        ReturnType result;
        archive(result);
        return result;
      }
  };
}