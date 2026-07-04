set(ESG_HEADERS
    model.hpp
    interest_rate_model.hpp
    regime_switching.hpp
    rw_scenarios.hpp
    yieldcurve.hpp
    multivariate_blackscholes.hpp
    hull_white_1f.hpp
    hull_white_blackscholes.hpp
)

add_library(esg INTERFACE)
target_include_directories(esg INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
