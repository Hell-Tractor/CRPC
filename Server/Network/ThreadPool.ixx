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
        // ���̳߳��ύ����
        template <typename ReturnType>
        std::future<ReturnType> submit(std::function<ReturnType()> task) throw(thread_pool_inactive);
        // �ر��̳߳ز��ȴ������������
        void shutdown();
        // ǿ�ƹر��̳߳�
        void shutdown_now();
        // �̳߳��Ƿ��Ѿ��ر�
        bool is_shutdown() noexcept;
        // �̳߳������������Ƿ��Ѿ�ִ�����
        bool is_terminated() noexcept;
        // ��ȡ����ִ�е��߳�����
        int get_active_thread_count() noexcept;
        // ��ȡ��ǰ�߳�����
        int get_size() noexcept;
        // ���õ�ǰ�߳�������������߳�����ȡmin
        thread_pool* set_size() noexcept;
};

}