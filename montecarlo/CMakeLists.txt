set(MONTECARLO_HEADERS
    scenario.hpp
)

add_library(montecarlo INTERFACE)
target_include_directories(montecarlo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
