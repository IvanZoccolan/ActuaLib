#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "math/randomnumbers/sobol.hpp"

TEST(RngSobolSanity, GaussianTailsStayBounded) {
    ActuaLib::Sobol rng;
    constexpr size_t dim = 64;
    constexpr size_t steps = 4096;
    constexpr double maxAbsAllowed = 6.0;

    rng.init(dim);
    std::vector<double> gauss(dim);

    double maxAbs = 0.0;
    for (size_t step = 0; step < steps; ++step) {
        rng.nextG(gauss);
        for (double value : gauss) {
            ASSERT_TRUE(std::isfinite(value));
            maxAbs = std::max(maxAbs, std::abs(value));
        }
    }

    EXPECT_LT(maxAbs, maxAbsAllowed);
}
