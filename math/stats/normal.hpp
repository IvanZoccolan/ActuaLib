#pragma once

#include <cmath>

namespace ActuaLib {

// Mathematical constant: 1/sqrt(2*pi)
constexpr double INV_SQRT_2PI = 0.3989422804014327;

inline double normalDens(const double x) {
    if (x < -10.0 || x > 10.0) {
        return 0.0;
    }
    return std::exp(-0.5 * x * x) * INV_SQRT_2PI;
}

// Zelen and Severo approximation for N(x).
inline double normalCdf(const double x) {
    if (x < -10.0) return 0.0;
    if (x > 10.0) return 1.0;
    if (x < 0.0) return 1.0 - normalCdf(-x);

    static constexpr double p = 0.2316419;
    static constexpr double b1 = 0.319381530;
    static constexpr double b2 = -0.356563782;
    static constexpr double b3 = 1.781477937;
    static constexpr double b4 = -1.821255978;
    static constexpr double b5 = 1.330274429;

    const double t = 1.0 / (1.0 + p * x);
    const double pol = t * (b1 + t * (b2 + t * (b3 + t * (b4 + t * b5))));
    return 1.0 - normalDens(x) * pol;
}

// Beasley-Springer-Moro inverse normal CDF.
inline double invNormalCdf(const double p) {
    const bool sup = p > 0.5;
    const double up = sup ? 1.0 - p : p;

    static constexpr double a0 = 2.50662823884;
    static constexpr double a1 = -18.61500062529;
    static constexpr double a2 = 41.39119773534;
    static constexpr double a3 = -25.44106049637;

    static constexpr double b0 = -8.47351093090;
    static constexpr double b1 = 23.08336743743;
    static constexpr double b2 = -21.06224101826;
    static constexpr double b3 = 3.13082909833;

    static constexpr double c0 = 0.3374754822726147;
    static constexpr double c1 = 0.9761690190917186;
    static constexpr double c2 = 0.1607979714918209;
    static constexpr double c3 = 0.0276438810333863;
    static constexpr double c4 = 0.0038405729373609;
    static constexpr double c5 = 0.0003951896511919;
    static constexpr double c6 = 0.0000321767881768;
    static constexpr double c7 = 0.0000002888167364;
    static constexpr double c8 = 0.0000003960315187;

    const double x = up - 0.5;
    if (std::fabs(x) < 0.42) {
        const double r = x * x;
        return (sup ? -1.0 : 1.0) * x * (((a3 * r + a2) * r + a1) * r + a0)
            / ((((b3 * r + b2) * r + b1) * r + b0) * r + 1.0);
    }

    double r = std::log(-std::log(up));
    r = c0 + r * (c1 + r * (c2 + r * (c3 + r * (c4 + r * (c5 + r * (c6 + r * (c7 + r * c8)))))));
    return sup ? r : -r;
}

} // namespace ActuaLib