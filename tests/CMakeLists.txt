# Tests CMakeLists.txt

# Add subdirectories for each module's tests
add_subdirectory(math)

# Component-level timing benchmark (serial): RNG vs ESG vs Cash-Flow
add_executable(BenchComponents benchComponents.cpp)
target_include_directories(BenchComponents PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(BenchComponents PRIVATE ActuaLib montecarlo products math randomnumbers esg concurrency)
target_compile_features(BenchComponents PRIVATE cxx_std_17)
set_target_properties(BenchComponents PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(RngSobolSanity rng_sobol_sanity.cpp)
target_include_directories(RngSobolSanity PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(RngSobolSanity PRIVATE gtest_main ActuaLib math randomnumbers)
target_compile_features(RngSobolSanity PRIVATE cxx_std_17)
set_target_properties(RngSobolSanity PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME RngSobolSanityTest COMMAND RngSobolSanity)

add_executable(NestedAadSinglePolicy nested_aad_single_policy.cpp)
target_include_directories(NestedAadSinglePolicy PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(NestedAadSinglePolicy PRIVATE gtest_main ActuaLib montecarlo products math randomnumbers esg concurrency aad)
target_compile_features(NestedAadSinglePolicy PRIVATE cxx_std_17)
set_target_properties(NestedAadSinglePolicy PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME NestedAadSinglePolicyTest COMMAND NestedAadSinglePolicy)
