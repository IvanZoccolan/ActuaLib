set(RANDOMNUMBERS_HEADERS
    genericrng.hpp
    gaussians.hpp
    mrg32k3a.hpp
    sobol.hpp
)

add_library(randomnumbers INTERFACE)
target_include_directories(randomnumbers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
