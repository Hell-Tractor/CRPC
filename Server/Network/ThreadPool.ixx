export module Network : ThreadPool;

#include "singleton.h"
#include "logger.h"
#include <functional>
#include <thread>
#include <future>
#include <vector>
#include <queue>
#include <mutex>

#define UNIQUE_LOCK(mutex) std::unique_lock lock(mutex)

namespace network {

class thread_pool_inactive final : public std::runtime_error {
    public:
        explicit thread_pool_inactive(const std::string& message) : runtime_error(message) {}
        explicit thread_pool_inactive(const char* message) : runtime_error(message) {}
};

class thread_pool final : public utils::singleton<thread_pool> {
    private:
        enum class state {
            running, stopping, force_stopping, stopped
        } state_;
        const static int MAX_THREADS;
        std::vector<std::thread> threads_;
        std::queue<std::function<void()>> tasks_;
        std::mutex mutex_;
        std::condition_variable condition_variable_;
    public:
        thread_pool() {
            this->state_ = state::running;
            threads_.reserve(MAX_THREADS);

            for (int i = 0; i < MAX_THREADS; ++i) {
                threads_.emplace_back([this]() {
                   while (true) {
                       std::function<void()> task;
                       {
                           UNIQUE_LOCK(this->mutex_);
                           this->condition_variable_.wait(lock, [this]() {
                               if (this->state_ == state::force_stopping)
                                   return true;
                               return !this->tasks_.empty() || this->state_ == state::stopping;
                           });

                           if (this->state_ == state::force_stopping)
                               break;
                           if (this->state_ == state::stopping && this->tasks_.empty())
                               break;

                           task = std::move(this->tasks_.front());
                           this->tasks_.pop();
                       }
                       task();
                   }
                });
            }

            LOGGER->log_info("Thread pool initialized, {} thread(s) created.", MAX_THREADS);
        }

        ~thread_pool() {
            this->shutdown();
        }

        // 向线程池提交任务
        template <typename ReturnType>
        std::future<ReturnType> submit(std::function<ReturnType()> func) throw(thread_pool_inactive) {
            auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::move(func));
            std::future<ReturnType> result = task->get_future();
            {
                std::unique_lock lock(this->mutex_);
                this->tasks_.emplace([task]() { *task(); });
            }
            this->condition_variable_.notify_one();
            LOGGER->log_debug("Task submitted.");
            return result;
        }
        // 关闭线程池并等待所有任务结束
        void shutdown() {
            LOGGER->log_info("Shutting down thread pool...");
            {
                UNIQUE_LOCK(this->mutex_);
                this->state_ = state::stopping;
            }
            condition_variable_.notify_all();
            for (auto& thread : this->threads_)
                thread.join();
            this->state_ = state::stopped;
            LOGGER->log_info("Thread pool shut down.");
        }
        // 强制关闭线程池
        void shutdown_now() {
            LOGGER->log_info("Force shutting down thread pool...");
            {
                UNIQUE_LOCK(this->mutex_);
                this->state_ = state::force_stopping;
            }
            condition_variable_.notify_all();
            for (auto& thread : this->threads_)
                thread.join();
            this->state_ = state::stopped;
            LOGGER->log_info("Thread pool force shut down.");
        }
        // 线程池是否已经关闭
        bool is_shutdown() const noexcept {
            return this->state_ == state::stopped;
        }
};

}