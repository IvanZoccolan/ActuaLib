set(PRODUCTS_HEADERS
    product.hpp
    va_policy.hpp
    va_base.hpp
    va_gmdb.hpp
    va_gmab.hpp
    va_gmmb.hpp
    va_gmwb.hpp
    va_gmib.hpp
    va_combo.hpp
)

add_library(products INTERFACE)
target_include_directories(products INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
