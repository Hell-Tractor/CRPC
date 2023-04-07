#pragma once

namespace utils {

template <typename T>
class singleton {
    private:
        static T* instance_ = nullptr;
    protected:
        singleton() = default;
    public:
        singleton& operator = (const singleton&) const = delete;
        singleton& operator = (const singleton&&) const = delete;
        singleton(const singleton&) = delete;
        singleton(singleton&&) = delete;
        ~singleton() {
            this->destroy();
        }
    
        static T* instance() {
            if (instance_ == nullptr)
                instance_ = new T();
            return instance_;
        }

        static void destroy() {
            if (instance_ != nullptr) {
                delete instance_;
                instance_ = nullptr;
            }
        }
};

}