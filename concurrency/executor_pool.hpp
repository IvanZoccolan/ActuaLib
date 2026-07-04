#pragma once

#include <chrono>
#include <cstddef>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "task_handle.hpp"
#include "task_queue.hpp"

namespace ActuaLib {

class executor_pool {
public:
    using task = std::packaged_task<bool(void)>;

    static executor_pool& instance();
    static std::size_t worker_index();

    void start(std::size_t workers);
    void stop();
    std::size_t worker_count() const;

    template <class Callable>
    task_handle submit(Callable&& func) {
        task work(std::forward<Callable>(func));
        task_handle handle = work.get_future();
        queue_.push(std::move(work));
        return handle;
    }

    bool wait_assist(const task_handle& handle) {
        bool executed_task = false;
        task work;
        while (handle.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            if (queue_.try_pop(work)) {
                work();
                executed_task = true;
            } else {
                handle.wait();
            }
        }
        return executed_task;
    }

    executor_pool(const executor_pool&) = delete;
    executor_pool& operator=(const executor_pool&) = delete;
    executor_pool(executor_pool&&) = delete;
    executor_pool& operator=(executor_pool&&) = delete;

private:
    executor_pool() = default;
    ~executor_pool() = default;

    void worker_loop(std::size_t worker_id);

    task_queue<task> queue_;
    std::vector<std::thread> workers_;
    mutable std::mutex lifecycle_mutex_;
    bool active_ = false;
    std::size_t worker_count_ = 0;

    static thread_local std::size_t worker_index_;
};

} // namespace ActuaLib
