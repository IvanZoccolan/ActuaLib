#pragma once

#include <cstddef>

namespace ActuaLib {

    using Real = double;
    using Size = std::size_t;
    using Time = Real;
    using DiscountFactor = Real;
    using Rate = Real;
    using Probability = Real;

    // Global system time
    extern Time SystemTime;

} // namespace ActuaLib
