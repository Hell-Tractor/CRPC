#pragma once

#include <stdexcept>

namespace crpc {

class unimplemented_error final : public std::runtime_error {
  public:
    explicit unimplemented_error(const std::string& message) : runtime_error(message) {}
    explicit unimplemented_error(const char* message) : runtime_error(message) {}
};

}