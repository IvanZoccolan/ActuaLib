set(MATH_HEADERS
    matrix.hpp
    interp.hpp
    cholesky.hpp
    utils.hpp
)

add_library(math INTERFACE)
target_include_directories(math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(randomnumbers)
