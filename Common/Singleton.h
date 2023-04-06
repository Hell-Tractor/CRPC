#pragma once

namespace utils {

template <typename T>
class singleton {
    private:
        static T* _instance = nullptr;
    protected:
        singleton() = default;
    public:
        singleton& operator = (const singleton&) const = delete;
        singleton& operator = (const singleton&&) const = delete;
        singleton(const singleton&) = delete;
        singleton(singleton&&) = delete;
    
        static T* instance() {
            if (_instance == nullptr)
                _instance = new T();
            return _instance;
        }

        static void destroy() {
            if (_instance != nullptr) {
                delete _instance;
                _instance = nullptr;
            }
        }
};

}