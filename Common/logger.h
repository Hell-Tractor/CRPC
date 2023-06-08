#pragma once

#include "singleton.h"
#include <vector>
#include <ostream>
#include <format>
#include <chrono>
#include <string>
#include <thread>

#define LOGGER utils::logger::instance()

namespace utils {

class logger : public singleton<logger> {
    public:
        enum class level {
            trace,
            debug,
            info,
            warn,
            error,
            fatal
        };
    private:
        std::vector<std::pair<std::ostream*, level>> streams_;

        template <level Level>
        static std::string get_log_prefix() {
            std::string level;
            switch (Level) {
                case level::trace:
                    level = "TRACE";
                    break;
                case level::debug:
                    level = "DEBUG";
                    break;
                case level::info:
                    level = "INFO";
                    break;
                case level::warn:
                    level = "WARN";
                    break;
                case level::error:
                    level = "ERROR";
                    break;
                case level::fatal:
                    level = "FATAL";
                    break;
            }
            std::chrono::zoned_time now{ std::chrono::current_zone(), std::chrono::system_clock::now() };
            // auto thread_id = std::this_thread::get_id();
            // return std::format("[{:%Y-%m-%d %H:%M:%S}][{}][{}] ", now, level, (*reinterpret_cast<uint32_t*>(&thread_id)));
            return std::format("[{:%Y-%m-%d %H:%M:%S}][{}] ", now, level);
        }
    public:
        
        logger& add_stream(std::ostream& stream, level level = level::info) {
            streams_.emplace_back(&stream, level);
            return *this;
        }

        template <level Level, typename... Args>
        constexpr logger& log(const std::string& message, Args&&... args) {
            const std::string formatted_string = this->get_log_prefix<Level>() +
                std::vformat(message, std::make_format_args(std::forward<Args>(args)...));
            for (auto& [stream, level] : streams_) {
                if (level <= Level)
                    (*stream) << formatted_string << std::endl;
            }
            return *this;
        }

        template <typename... Args>
        constexpr logger& log_trace(const std::string& message, Args&&... args) {
            return log<level::trace>(message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        constexpr logger& log_debug(const std::string& message, Args&&... args) {
            return log<level::debug>(message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        constexpr logger& log_info(const std::string& message, Args&&... args) {
          return log<level::info>(message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        constexpr logger& log_warn(const std::string& message, Args&&... args) {
          return log<level::warn>(message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        constexpr logger& log_error(const std::string& message, Args&&... args) {
          return log<level::error>(message, std::forward<Args>(args)...);
        }

        template <typename... Args>
        constexpr logger& log_fatal(const std::string& message, Args&&... args) {
            return log<level::fatal>(message, std::forward<Args>(args)...);
        }
};

}