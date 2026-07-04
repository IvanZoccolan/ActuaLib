/*
 * Adapted from: A. Savine, "Modern Computational Finance: AAD and Parallel
 * Simulations", Wiley, 2018. Used and modified under the book's license.
 * Public API (singleton, start/stop/spawnTask/activeWait) preserved;
 * internals split into executor_pool + task_queue.
 */

#pragma once

#include <future>
#include <thread>
#include <utility>

#include "executor_pool.hpp"
#include "task_handle.hpp"

namespace ActuaLib {

using Task = std::packaged_task<bool(void)>;

class ThreadPool {
    static ThreadPool instance_;

    // Non-constructible outside getInstance()
    ThreadPool() = default;

public:
    static ThreadPool* getInstance() {
        return &instance_;
    }

    size_t numThreads() const {
        return executor_pool::instance().worker_count();
    }

    static size_t threadNum() {
        return executor_pool::worker_index();
    }

    void start(const size_t nThread = std::thread::hardware_concurrency() - 1) {
        const size_t requested = (nThread == 0) ? 1 : nThread;
        executor_pool::instance().start(requested);
    }

    void stop() {
        executor_pool::instance().stop();
    }

    template <typename Callable>
    TaskHandle spawnTask(Callable&& func) {
        return executor_pool::instance().submit(std::forward<Callable>(func));
    }

    bool activeWait(const TaskHandle& handle) {
        if (std::getenv("ACTUALIB_DISABLE_ACTIVE_STEAL") != nullptr) {
            handle.wait();
            return false;
        }
        return executor_pool::instance().wait_assist(handle);
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
};

} // namespace ActuaLib
