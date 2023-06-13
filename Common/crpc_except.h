#pragma once

#include <stdexcept>

namespace crpc {

    class unimplemented_error final : public std::runtime_error {
    public:
        explicit unimplemented_error(const std::string& message) : runtime_error(message) {}
        explicit unimplemented_error(const char* message) : runtime_error(message) {}
    };

    class serialize_error final : public std::runtime_error {
    public:
        explicit serialize_error(const std::string& message) : runtime_error("serialize error: " + message) {}
		explicit serialize_error(const char* message) : runtime_error("serialize error: " + std::string(message)) {}
	};

    class deserialize_error final : public std::runtime_error {
    public:
        explicit deserialize_error(const std::string& message) : runtime_error("deserialize error: " + message) {}
        explicit deserialize_error(const char* message) : runtime_error("deserialize error: " + std::string(message)) {}
    };

    class timeout_error final : public std::runtime_error {
    public:
        explicit timeout_error(const std::string& message) : runtime_error(message) {}
        explicit timeout_error(const char* message) : runtime_error(message) {}
    };

    class service_not_available_error final : public std::runtime_error {
    public:
        explicit service_not_available_error(const std::string& message) : runtime_error(message) {}
        explicit service_not_available_error(const char* message) : runtime_error(message) {}
    };

    class route_error final : public std::runtime_error {
    public:
		explicit route_error(const std::string& message) : runtime_error(message) {}
		explicit route_error(const char* message) : runtime_error(message) {}
	};

}