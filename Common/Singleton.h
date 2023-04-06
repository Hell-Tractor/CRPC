#pragma once

namespace utils {

template <typename T>
class Singleton {
    private:
        static T* _instance = nullptr;
    protected:
        Singleton() = default;
    public:
        Singleton& operator = (const Singleton&) const = delete;
        Singleton& operator = (const Singleton&&) const = delete;
        Singleton(const Singleton&) = delete;
        Singleton(Singleton&&) = delete;
    
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