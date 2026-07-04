#pragma once

#include <algorithm>
#include <iterator>

#define EPS 1.0e-08

namespace ActuaLib {

template <class CONT, class T, class IT = T*>
inline CONT fillData(
    const CONT& original,
    const T& maxDx,
    const T& minDx = T(0.0),
    IT addBegin = nullptr,
    IT addEnd = nullptr) {

    if (original.empty()) {
        return CONT{};
    }

    CONT filled;
    CONT sequence = original;
    const bool has_additional = addBegin != nullptr && addEnd != nullptr;

    if (has_additional) {
        CONT merged;
        std::set_union(
            original.begin(), original.end(),
            addBegin, addEnd,
            std::back_inserter(merged),
            [minDx](const T& x, const T& y) { return x < y - minDx; });
        if (!merged.empty()) {
            sequence.swap(merged);
        }
    }

    auto it = sequence.begin();
    filled.push_back(*it);
    ++it;
    while (it != sequence.end()) {
        const T current = filled.back();
        const T next = *it;
        if (next > current + maxDx) {
            const int num_points = static_cast<int>((next - current) / maxDx - EPS) + 1;
            const T spacing = (next - current) / static_cast<T>(num_points);
            T t = current + spacing;
            while (t < next - minDx) {
                filled.push_back(t);
                t += spacing;
            }
        }
        filled.push_back(*it);
        ++it;
    }
    return filled;
}

} // namespace ActuaLib
