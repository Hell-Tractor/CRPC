#pragma once

#include <mutex>

namespace utils {

template <typename T>
class singleton {
    protected:
        singleton() = default;
    public:
        singleton& operator = (const singleton&) const = delete;
        singleton& operator = (const singleton&&) const = delete;
        singleton(const singleton&) = delete;
        singleton(singleton&&) = delete;
        ~singleton() = default;
    
        static T& instance() {
            static T instance;
            return instance;
        }
};

}