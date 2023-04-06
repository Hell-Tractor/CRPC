export module Network : ThreadPool;

#include "../../Common/Singleton.h"
#include <functional>
#include <thread>
#include <future>
#include <exception>
#include <vector>
#include <queue>
#include <mutex>

namespace network {

class thread_pool_inactive final : public std::runtime_error {
    public:
        explicit thread_pool_inactive(const std::string& message) : runtime_error(message) {}
        explicit thread_pool_inactive(const char* message) : runtime_error(message) {}
};

class thread_pool final : public utils::singleton<thread_pool> {
    private:
        enum class state {
            running, stopping, stopped
        } state_;
        const static int MAX_THREADS;
        std::vector<std::thread> threads_;
        std::queue<std::function<void()>> tasks_;
        std::mutex mutex_;
        std::condition_variable condition_variable_;
    public:
        thread_pool();
        // 向线程池提交任务
        template <typename ReturnType>
        std::future<ReturnType> submit(std::function<ReturnType()> task) throw(thread_pool_inactive);
        // 关闭线程池并等待所有任务结束
        void shutdown();
        // 强制关闭线程池
        void shutdown_now();
        // 线程池是否已经关闭
        bool is_shutdown() noexcept;
        // 线程池中所有任务是否已经执行完毕
        bool is_terminated() noexcept;
        // 获取正在执行的线程数量
        int get_active_thread_count() noexcept;
        // 获取当前线程数量
        int get_size() noexcept;
        // 设置当前线程数量，与最大线程数量取min
        thread_pool* set_size() noexcept;
};

}