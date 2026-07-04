#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

namespace ActuaLib {

template <class T>
class task_queue {
public:
    task_queue() = default;

    task_queue(const task_queue&) = delete;
    task_queue& operator=(const task_queue&) = delete;

    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        while (queue_.empty() && !terminated_) {
            cond_var_.wait(lock);
        }
        if (terminated_) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    template <class U>
    void push(U&& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (terminated_) {
                return;
            }
            queue_.push(std::forward<U>(item));
        }
        cond_var_.notify_one();
    }

    void terminate() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            terminated_ = true;
        }
        cond_var_.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        terminated_ = false;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
    bool terminated_ = false;
};

} // namespace ActuaLib
