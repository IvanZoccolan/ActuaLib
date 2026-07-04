#pragma once

/*
    Yield curve bootstrapped from par swap rates
    with log-linear interpolation of discount factors.

    Templated on T for AAD compatibility — swap rates can be
    Number type so that rate sensitivities propagate through
    the bootstrap automatically.

    Bootstrap assumes:
      - sorted tenors T_1 < T_2 < ... < T_n
      - payment dates for swap k are T_1, T_2, ..., T_k
      - day-count fraction delta_i = T_i - T_{i-1}  (T_0 = 0)

    Log-linear interpolation:
      ln D(t) = linear interp of { ln D(T_i) }
    with flat forward-rate extrapolation beyond the last tenor.
*/

#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include "../types.hpp"

namespace ActuaLib {

    template <class T>
    class YieldCurve {
    private:
        // Tenors including t = 0 (plain time points, not on tape)
        std::vector<double> myTenors;
        // Bootstrapped discount factors (including D(0) = 1)
        std::vector<T> myDiscountFactors;
        // ln D(T_i) for log-linear interpolation
        std::vector<T> myLogDiscounts;

    public:
        YieldCurve() = default;

        // -----------------------------------------------------------
        //  Rebuild cached ln D(T_i) from current discount factors
        //  (needed after putParametersOnTape() in AAD runs)
        // -----------------------------------------------------------
        void refreshLogDiscountsFromDiscountFactors()
        {
            using std::log;
            if (myDiscountFactors.size() != myLogDiscounts.size()) {
                myLogDiscounts.resize(myDiscountFactors.size());
            }
            if (myDiscountFactors.empty()) {
                return;
            }

            // D(0) is structurally 1.0 and intentionally off-tape. Setting
            // ln D(0) explicitly avoids constructing a graph edge through a
            // non-parameter knot after tape resets.
            myLogDiscounts[0] = T(0.0);

            for (size_t i = 1; i < myDiscountFactors.size(); ++i) {
                myLogDiscounts[i] = log(myDiscountFactors[i]);
            }
        }

        // -----------------------------------------------------------
        //  Set curve directly from discount factors
        // -----------------------------------------------------------
        void setDiscountCurve(
            const std::vector<double>& tenors,
            const std::vector<T>& discountFactors)
        {
            using std::log;

            if (tenors.size() != discountFactors.size()) {
                throw std::runtime_error(
                    "YieldCurve::setDiscountCurve: tenors/discountFactors size mismatch");
            }
            if (tenors.size() < 2) {
                throw std::runtime_error(
                    "YieldCurve::setDiscountCurve: curve must contain at least t=0 and one tenor");
            }
            if (std::fabs(tenors.front()) > 1.0e-12) {
                throw std::runtime_error(
                    "YieldCurve::setDiscountCurve: first tenor must be 0");
            }

            myTenors = tenors;
            myDiscountFactors = discountFactors;
            myLogDiscounts.resize(discountFactors.size());

            for (size_t i = 0; i < discountFactors.size(); ++i) {
                myLogDiscounts[i] = log(myDiscountFactors[i]);
            }
        }

        // -----------------------------------------------------------
        //  Bootstrap from par swap rates
        // -----------------------------------------------------------
        void bootstrap(
            const std::vector<double>& tenors,
            const std::vector<T>& swapRates)
        {
            using std::log;

            const size_t n = tenors.size();
            if (n != swapRates.size()) {
                throw std::runtime_error(
                    "YieldCurve::bootstrap: tenors/swapRates size mismatch");
            }
            if (n == 0) {
                throw std::runtime_error(
                    "YieldCurve::bootstrap: empty input");
            }

            // Reserve n+1 entries: index 0 → t=0, indices 1..n → swap tenors
            myTenors.resize(n + 1);
            myDiscountFactors.resize(n + 1);
            myLogDiscounts.resize(n + 1);

            // D(0) = 1
            myTenors[0] = 0.0;
            myDiscountFactors[0] = T(1.0);
            myLogDiscounts[0] = T(0.0);

            // Iterative bootstrap
            //   s_k * sum_{i=1}^{k} delta_i * D(T_i) = 1 - D(T_k)
            // => D(T_k) = (1 - s_k * sum_{i=1}^{k-1} delta_i * D(T_i))
            //              / (1 + s_k * delta_k)
            for (size_t k = 0; k < n; ++k) {
                myTenors[k + 1] = tenors[k];

                const double delta_k =
                    (k == 0) ? tenors[0] : (tenors[k] - tenors[k - 1]);

                // PV of the annuity for coupons 1..k-1
                T pvAnnuity = T(0.0);
                for (size_t i = 0; i < k; ++i) {
                    const double delta_i =
                        (i == 0) ? tenors[0] : (tenors[i] - tenors[i - 1]);
                    pvAnnuity = pvAnnuity + T(delta_i) * myDiscountFactors[i + 1];
                }

                myDiscountFactors[k + 1] =
                    (T(1.0) - swapRates[k] * pvAnnuity) /
                    (T(1.0) + swapRates[k] * T(delta_k));

                myLogDiscounts[k + 1] = log(myDiscountFactors[k + 1]);
            }
        }

        // -----------------------------------------------------------
        //  Discount factor at arbitrary maturity via log-linear interp
        // -----------------------------------------------------------
        T discount(double t) const {
            using std::exp;

            if (t <= 0.0) return T(1.0);

            const size_t n = myTenors.size();

            // Locate the interval [T_i, T_{i+1}) containing t
            auto it = std::upper_bound(myTenors.begin(), myTenors.end(), t);

            if (it == myTenors.begin()) {
                return T(1.0);
            }

            if (it == myTenors.end()) {
                // Flat forward-rate extrapolation beyond last tenor
                const size_t last = n - 1;
                T fwdRate =
                    (myLogDiscounts[last - 1] - myLogDiscounts[last]) /
                    T(myTenors[last] - myTenors[last - 1]);
                return exp(myLogDiscounts[last] -
                           fwdRate * T(t - myTenors[last]));
            }

            const size_t i =
                static_cast<size_t>(std::distance(myTenors.begin(), it)) - 1;

            // Log-linear: ln D(t) = (1-w)*ln D(T_i) + w*ln D(T_{i+1})
            const double w =
                (t - myTenors[i]) / (myTenors[i + 1] - myTenors[i]);
            T logD = myLogDiscounts[i] * T(1.0 - w) +
                     myLogDiscounts[i + 1] * T(w);
            return exp(logD);
        }

        // -----------------------------------------------------------
        //  Accessors
        // -----------------------------------------------------------
        const std::vector<double>& tenors() const { return myTenors; }
        const std::vector<T>& discountFactors() const {
            return myDiscountFactors;
        }
        std::vector<T>& discountFactorsMutable() {
            return myDiscountFactors;
        }
        const std::vector<T>& logDiscounts() const {
            return myLogDiscounts;
        }
    };

} // namespace ActuaLib
