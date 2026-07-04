#pragma once

/*
    RWScenarioSet — Storage for real-world (P-measure) scenario paths.

    Each scenario is a sequence of monthly equity index growth factors.
    For path p at step t (1-based), index k:

        growthFactor(p, t, k) = S_k(t) / S_k(t-1)

    The cumulative spot level at month t relative to a given initial spot is:

        spotAt(p, t, k, spot0) = spot0 * Π_{j=1}^{t} growthFactor(p, j, k)

    Storage layout: flat row-major array
        [p * nSteps * nIndices + (t-1) * nIndices + k]
    with t 1-based.
*/

#include <vector>
#include <cstddef>
#include <stdexcept>

namespace ActuaLib {

    class RWScenarioSet {
    public:

        RWScenarioSet() = default;

        RWScenarioSet(size_t nPaths, size_t nSteps, size_t nIndices)
            : myNPaths(nPaths), myNSteps(nSteps), myNIndices(nIndices),
              myData(nPaths * nSteps * nIndices, 1.0)
        {}

        // ------------------------------------------------------------------
        //  Accessors
        // ------------------------------------------------------------------

        size_t nPaths()   const { return myNPaths; }
        size_t nSteps()   const { return myNSteps; }
        size_t nIndices() const { return myNIndices; }

        /// Growth factor at step t (1-based) for path p, index k.
        double& operator()(size_t p, size_t t, size_t k)
        {
            return myData[p * myNSteps * myNIndices + (t - 1) * myNIndices + k];
        }

        double operator()(size_t p, size_t t, size_t k) const
        {
            return myData[p * myNSteps * myNIndices + (t - 1) * myNIndices + k];
        }

        /// Cumulative spot price at month t (1-based), given initial spot0.
        ///   spotAt(p, t, k, spot0) = spot0 * Π_{j=1}^{t} g(p, j, k)
        double spotAt(size_t p, size_t t, size_t k, double spot0) const
        {
            double s = spot0;
            for (size_t j = 1; j <= t; ++j) {
                s *= (*this)(p, j, k);
            }
            return s;
        }

        /// Raw underlying storage (for direct writes during generation).
        std::vector<double>& data() { return myData; }
        const std::vector<double>& data() const { return myData; }

    private:
        size_t              myNPaths   = 0;
        size_t              myNSteps   = 0;
        size_t              myNIndices = 0;
        std::vector<double> myData;
    };

} // namespace ActuaLib
