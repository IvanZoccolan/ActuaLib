add_executable(SimulateRider simulate_rider.cpp)

# Set output directory for example executables
set_target_properties(SimulateRider
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)

# Include project root so examples can include headers like "math/matrix.hpp"
target_include_directories(SimulateRider PRIVATE ${CMAKE_SOURCE_DIR})

# Locate HDF5 (C API only).
# On macOS/Homebrew: cmake finds it via HDF5_ROOT or the fallback hint below.
# On Linux (apt install libhdf5-dev): cmake finds it automatically.
if(APPLE)
    # Help cmake find Homebrew HDF5 when HDF5_ROOT is not set externally
    if(NOT DEFINED HDF5_ROOT AND EXISTS /opt/homebrew/opt/hdf5)
        set(HDF5_ROOT /opt/homebrew/opt/hdf5)
    endif()
endif()
find_package(HDF5 REQUIRED COMPONENTS C)

# Link libraries (use PRIVATE to avoid polluting consumers)
target_link_libraries(SimulateRider PRIVATE ActuaLib montecarlo products math randomnumbers esg concurrency aad
    ${HDF5_C_LIBRARIES})

# SimulateRider needs HDF5 headers
target_include_directories(SimulateRider PRIVATE ${HDF5_C_INCLUDE_DIRS})

# Ensure C++17 and disable compiler extensions for these targets
foreach(tgt IN ITEMS SimulateRider)
    target_compile_features(${tgt} PRIVATE cxx_std_17)
    set_target_properties(${tgt} PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endforeach()