#include "executor_pool.hpp"

namespace ActuaLib {

thread_local std::size_t executor_pool::worker_index_ = 0;

executor_pool& executor_pool::instance() {
    static executor_pool pool;
    return pool;
}

std::size_t executor_pool::worker_index() {
    return worker_index_;
}

void executor_pool::worker_loop(const std::size_t worker_id) {
    worker_index_ = worker_id;
    task work;
    while (queue_.pop(work)) {
        work();
    }
}

void executor_pool::start(const std::size_t workers) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (active_) {
        return;
    }

    const std::size_t requested = workers == 0 ? 1 : workers;
    workers_.reserve(requested);
    queue_.reset();

    for (std::size_t i = 0; i < requested; ++i) {
        workers_.emplace_back(&executor_pool::worker_loop, this, i + 1);
    }

    worker_count_ = requested;
    active_ = true;
}

void executor_pool::stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (!active_) {
        return;
    }

    queue_.terminate();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    queue_.clear();
    queue_.reset();

    worker_count_ = 0;
    active_ = false;
}

std::size_t executor_pool::worker_count() const {
    return worker_count_;
}

} // namespace ActuaLib
