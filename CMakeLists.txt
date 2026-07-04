cmake_minimum_required(VERSION 3.22)

project(
    ActuaLib
    VERSION 1.0.0
    LANGUAGES C CXX
)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "Build type not specified, defaulting to Release")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories for built artifacts
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Compiler-specific optimizations
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-deprecated-declarations)
    
    # On macOS, help find OpenMP installed via Homebrew
    if(APPLE)
        set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
        set(OpenMP_C_LIB_NAMES "omp")
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "/opt/homebrew/opt/libomp/lib/libomp.dylib")
        
        # Enable AVX2 and SIMD with proper Clang flags
        add_compile_options(-march=native -mtune=native)
    else()
        # Linux Clang
        add_compile_options(-mavx2 -mfma)
    endif()
    
    # Enable aggressive optimizations for Release builds
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto=thin")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto=thin")
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-deprecated-declarations)
    # Enable AVX2 and SIMD optimizations
    add_compile_options(-mavx2 -mfma)
    # Enable aggressive optimizations for Release builds
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")
    
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # MSVC AVX2 support
    add_compile_options(/arch:AVX2)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /GL /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "/O2 /GL /DNDEBUG")
endif()

# Find OpenMP after setting the hints
find_package(OpenMP)

#set(CMAKE_CXX_FLAGS_DEBUG "-g -O0") # Ensure debug symbols and disable optimizations
#set(CMAKE_C_FLAGS_DEBUG "-g -O0")

# Set the library name
set(LIBRARY_NAME ActuaLib)

# Add core source files
set(ACTUALIB_SOURCES
    types.cpp
)

# Add the library with source files
add_library(${LIBRARY_NAME} STATIC ${ACTUALIB_SOURCES})

# Include directories for the library
target_include_directories(${LIBRARY_NAME} PUBLIC
    "${CMAKE_SOURCE_DIR}"
)

# Add subdirectories
add_subdirectory(products)
add_subdirectory(montecarlo)
add_subdirectory(esg)
add_subdirectory(demographics)
add_subdirectory(math)
add_subdirectory(examples)
add_subdirectory(concurrency)
add_subdirectory(aad)

# Link sub-libraries to ActuaLib
target_link_libraries(${LIBRARY_NAME} PUBLIC products)
target_link_libraries(${LIBRARY_NAME} PUBLIC montecarlo)
target_link_libraries(${LIBRARY_NAME} PUBLIC esg)
target_link_libraries(${LIBRARY_NAME} PUBLIC demographics)
target_link_libraries(${LIBRARY_NAME} PUBLIC concurrency)
target_link_libraries(${LIBRARY_NAME} PUBLIC math)
target_link_libraries(${LIBRARY_NAME} PUBLIC aad)

