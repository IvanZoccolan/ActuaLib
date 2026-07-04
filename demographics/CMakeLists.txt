add_library(demographics INTERFACE)
target_include_directories(demographics INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(demographics INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/mortality_model.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mortality.hpp
)
