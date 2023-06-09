#pragma once
#include <mutex>
#include <queue>

namespace crpc {

	template<class T>
	class thread_safe_queue {
	public:
		void push(const T& data) {
			std::lock_guard<std::mutex> lock(mutex_);
			queue_.push(data);
			cv_.notify_one();
		}

		void push(T&& data) {
			std::lock_guard<std::mutex> lock(mutex_);
			queue_.push(std::move(data));
			cv_.notify_one();
		}

		T pop() {
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [this] { return !queue_.empty(); });
			auto data = queue_.front();
			queue_.pop();
			return data;
		}

	private:
		std::queue<std::string> queue_;
		std::mutex mutex_;
		std::condition_variable cv_;
	};

}