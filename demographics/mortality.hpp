#pragma once

/*
    Mortality curve / table for actuarial simulations.

    Accepts an annual (one-year) mortality table  q(x)  indexed by
    integer age x, such as the IAM 1996 Basic table or SOA tables.

    Fractional-age hypothesis: Uniform Distribution of Deaths (UDD).
    Under UDD, for integer age x and 0 <= s < 1:

        _sp_x  = 1 - s * q(x)          survival for fraction s of a year
        _sq_x  = s * q(x)              death probability for fraction s

    For arbitrary real age x and duration t the survival probability is

        _tp_x  = prod_{k=x0}^{x0+n-1} (1 - q(k))
                 * [1 - (x+t - floor(x+t)) * q(floor(x+t))]
                 / [1 - (x   - floor(x))   * q(floor(x))  ]

    where x0 = floor(x), n = floor(x+t) - x0.

    This matches the implementation in Gan's VAMC codebase
    (va.curve.MortalityCurve).

    Usage:
        MortalityTable table({0,1,2,...,120}, {q0,q1,...,q120});
        // or
        MortalityTable table;
        table.set(ages, qxValues);

        double px = table.p(65.5, 10.25);   // _{10.25}p_{65.5}
        double qx = table.q(65.5, 10.25);   // _{10.25}q_{65.5}

    All types are plain double / Probability — not on the AAD tape.

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity Portfolios:
        Monte Carlo Simulation and Synthetic Datasets", 2017.
*/

#include "mortality_model.hpp"
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace ActuaLib {

    class MortalityTable : public MortalityModel {
    private:

        // One-year death probabilities stored in a flat vector.
        // myQxVec[age - myMinAge] holds q(age).
        std::vector<Probability> myQxVec;

        int myMinAge = 0;
        int myMaxAge = 0;

        // ------------------------------------------------------------------
        //  Look up q(age).  If age > maxAge → 1.0 (certain death).
        //  If age < minAge → q(minAge).
        // ------------------------------------------------------------------
        Probability getQx(int age) const {
            if (age > myMaxAge) return 1.0;
            if (age < myMinAge) return myQxVec.front();
            return myQxVec[static_cast<size_t>(age - myMinAge)];
        }

        // Update min/max age bookkeeping
        void updateMinMax() {
            if (myQxVec.empty()) {
                myMinAge = 0;
                myMaxAge = 0;
            }
            // min/max are set in set()
        }

    public:

        // ==================================================================
        //  Constructors
        // ==================================================================

        MortalityTable() = default;

        // Construct from parallel vectors of ages and q(x) values
        MortalityTable(
            const std::vector<int>& ages,
            const std::vector<Probability>& qxValues)
        {
            set(ages, qxValues);
        }

        // ==================================================================
        //  Setters
        // ==================================================================

        // Set the table from parallel vectors
        void set(
            const std::vector<int>& ages,
            const std::vector<Probability>& qxValues)
        {
            if (ages.size() != qxValues.size()) {
                throw std::runtime_error(
                    "MortalityTable::set: ages and qx vectors must have the same length");
            }
            if (ages.empty()) {
                myQxVec.clear();
                myMinAge = myMaxAge = 0;
                return;
            }
            myMinAge = *std::min_element(ages.begin(), ages.end());
            myMaxAge = *std::max_element(ages.begin(), ages.end());
            myQxVec.assign(static_cast<size_t>(myMaxAge - myMinAge + 1), 0.0);
            for (size_t i = 0; i < ages.size(); ++i) {
                myQxVec[static_cast<size_t>(ages[i] - myMinAge)] = qxValues[i];
            }
        }

        // ==================================================================
        //  Accessors
        // ==================================================================

        int minAge() const override { return myMinAge; }
        int maxAge() const override { return myMaxAge; }
        size_t size() const { return myQxVec.size(); }
        bool empty() const { return myQxVec.empty(); }

        // Raw one-year q(x) for integer age
        Probability qx(int age) const override { return getQx(age); }

        // ==================================================================
        //  Survival and death probabilities  (UDD fractional-age assumption)
        // ==================================================================

        // _tp_x :  probability that (x) survives at least t years
        //
        //  Under UDD:
        //    _tp_x = prod_{k=floor(x)}^{floor(x+t)-1} (1 - q(k))
        //          * [1 - frac(x+t) * q(floor(x+t))]
        //          / [1 - frac(x)   * q(floor(x))  ]
        //
        Probability p(double x, double t) const override {
            if (t <= 0.0) {
                return 1.0;
            }

            const int x0 = static_cast<int>(std::floor(x));
            const int xt = static_cast<int>(std::floor(x + t));

            // Product of full-year survivals from floor(x) to floor(x+t)-1
            Probability pFull = 1.0;
            for (int k = x0; k < xt; ++k) {
                pFull *= (1.0 - getQx(k));
            }

            // Fractional-year adjustments (UDD)
            const double fracEnd   = (x + t) - xt;   // fractional part at end
            const double fracStart = x - x0;          // fractional part at start

            const Probability numerator   = 1.0 - fracEnd   * getQx(xt);
            const Probability denominator = 1.0 - fracStart * getQx(x0);

            return pFull * numerator / denominator;
        }

        // _tq_x :  probability that (x) dies within t years
        Probability q(double x, double t) const override {
            return 1.0 - p(x, t);
        }

        // Deep copy
        std::unique_ptr<MortalityModel> clone() const override {
            return std::make_unique<MortalityTable>(*this);
        }
    };

} // namespace ActuaLib
