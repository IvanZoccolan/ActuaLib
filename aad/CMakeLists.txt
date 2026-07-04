set(AAD_HEADERS
    AAD.hpp
    number.hpp
    ad_scalar.hpp
    gradient_tape.hpp
    op_record.hpp
    tape_arena.hpp
    reverse_pass.hpp
    multi_output_scope.hpp
)

set(AAD_SOURCES
    AAD.cpp
)

add_library(aad ${AAD_SOURCES})
target_include_directories(aad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
