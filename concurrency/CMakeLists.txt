set(CONCURRENCY_SOURCES
    executor_pool.cpp
    threadpool.cpp
)

set(CONCURRENCY_HEADERS
    executor_pool.hpp
    task_handle.hpp
    task_queue.hpp
    threadpool.hpp
    worker_context.hpp
)

add_library(concurrency STATIC ${CONCURRENCY_SOURCES} ${CONCURRENCY_HEADERS})
target_include_directories(concurrency PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
