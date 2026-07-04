#pragma once

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace ActuaLib {

    template<class ITX, class ITY, class T>
    inline auto interp(const ITX xBegin, const ITX xEnd, const ITY yBegin, const ITY yEnd, const T& x0)
        -> std::remove_reference_t<decltype(*yBegin)> {
        if (xBegin == xEnd || yBegin == yEnd) {
            throw std::invalid_argument("interp requires non-empty x and y ranges");
        }

        const auto nx = std::distance(xBegin, xEnd);
        const auto ny = std::distance(yBegin, yEnd);
        if (nx != ny) {
            throw std::invalid_argument("interp requires x and y ranges with identical length");
        }

        auto it = std::upper_bound(xBegin, xEnd, x0);
        if (it == xBegin) {
            return *yBegin; // Extrapolate to the left
        }
        if (it == xEnd) {
            return *(yEnd - 1); // Extrapolate to the right
        }

        const size_t i = static_cast<size_t>(std::distance(xBegin, it) - 1);
        const auto x1 = *(xBegin + i);
        const auto x2 = *(xBegin + i + 1);
        const auto y1 = *(yBegin + i);
        const auto y2 = *(yBegin + i + 1);

        if (x2 == x1) {
            return y1;
        }
        return y1 + (y2 - y1) * (x0 - x1) / (x2 - x1);
    }

} // namespace ActuaLib
