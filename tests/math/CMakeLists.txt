# Math module tests

# Matrix test executable
add_executable(matrix_test matrix_test.cpp)

target_include_directories(matrix_test PRIVATE 
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/math"
)

target_link_libraries(matrix_test 
    gtest 
    gtest_main
    math
)

# Link OpenMP if available
if(OpenMP_CXX_FOUND)
    target_link_libraries(matrix_test OpenMP::OpenMP_CXX)
endif()

# Register the test with CTest
add_test(NAME MatrixTest COMMAND matrix_test)
