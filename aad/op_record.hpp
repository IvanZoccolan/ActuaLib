#pragma once

#include <array>
#include <cstddef>

namespace ActuaLib {

struct op_record {
    std::size_t arity = 0;
    std::array<std::size_t, 2> parents{0, 0};
    std::array<double, 2> partials{0.0, 0.0};
};

} // namespace ActuaLib
