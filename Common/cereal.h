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
      template <typename... Args>
      std::string serialize(const std::string& method, Args&&... args) {
        std::stringstream ss;
        output_archive archive(ss);
        archive(::cereal::make_nvp("method", method), ::cereal::make_nvp("args", std::make_tuple(std::forward<Args>(args)...)));
        return ss.str();
      }
      template <typename ReturnType>
      ReturnType deserialize(const std::string& data) {
        std::stringstream ss(data);
        input_archive archive(ss);
        ReturnType result;
        archive(result);
        return result;
      }
  };
}