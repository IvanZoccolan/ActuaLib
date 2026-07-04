#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>

#include "number.hpp"
#include "ad_scalar.hpp"
#include "gradient_tape.hpp"

namespace ActuaLib {

    struct numResultsResetterForAAD
    {
        ~numResultsResetterForAAD() = default;
    };

    inline auto setNumResultsForAAD(const bool /*multi*/ = false, const size_t numResults = 1)
    {
        Node::numAdj = numResults;
        return std::make_unique<numResultsResetterForAAD>();
    }

    template <class IT>
    inline void putOnTape(IT begin, IT end)
    {
        std::for_each(begin, end, [](Number& n) { n.putOnTape(); });
    }

    template<class It1, class It2>
    inline void convertCollection(It1 srcBegin, It1 srcEnd, It2 destBegin)
    {
        using destType = std::remove_reference_t<decltype(*destBegin)>;
        std::transform(srcBegin, srcEnd, destBegin,
            [](const auto& source) { return destType(source); });
    }

} // namespace ActuaLib
