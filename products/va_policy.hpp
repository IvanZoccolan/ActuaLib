#pragma once

/*
    Variable Annuity policy data.

    Plain-data struct that mirrors the fields in Gan's Java Policy class:
      – fund allocation (values, fees)
      – guarantee parameters (benefit amount, roll-up rate, …)
      – fee structure (base/MER, rider charge)
      – policyholder demographics (age, gender, survivorship)

    All values are plain doubles; they are never placed on the AAD tape.

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity Portfolios:
        Monte Carlo Simulation and Synthetic Datasets", 2017.
*/

#include <vector>
#include <stdexcept>
#include <numeric>

namespace ActuaLib {

    // ==================================================================
    //  Benefit-base update rule
    // ==================================================================

    enum class BenefitBaseUpdate {
        StepUp,           // SU: gbAmt = max(gbAmt, AV) on anniversaries
        ReturnOfPremium,  // RP: gbAmt stays fixed
        RollUp            // RU: gbAmt *= (1 + rollUpRate) on anniversaries
    };

    struct VAPolicy {

        // ==================================================================
        //  Policyholder demographics
        // ==================================================================

        double currentAge   = 65.0;   // current age in years
        int    numMonths    = 360;    // months from valuation to maturity
        bool   female       = true;   // true → female mortality table
        double survivorship = 1.0;    // survivorship scaling factor

        /// Months from policy issue to valuation date, used for anniversary
        /// timing.  Anniversaries occur when (monthsSinceIssue + j) is a
        /// multiple of 12, where j is the projection step.  Set to 0
        /// (default) if issue date is unknown — anniversaries then fall
        /// at j = 12, 24, 36, … as before.
        int    monthsSinceIssue = 0;

        // ==================================================================
        //  Guarantee parameters
        // ==================================================================

        BenefitBaseUpdate updateRule = BenefitBaseUpdate::ReturnOfPremium;

        double gbAmt            = 0.0;  // guaranteed benefit amount
        double rollUpRate       = 0.0;  // annual roll-up rate  (RU products)
        double gmwbBalance      = 0.0;  // GMWB remaining balance
        double wbWithdrawalRate = 0.0;  // GMWB annual withdrawal rate

        /// Maturity period in months (0 = single maturity at numMonths,
        /// >0 = renewable every maturityPeriod months, used by GMAB)
        int    maturityPeriod   = 0;

        /// For renewable products (GMAB): month index of first maturity
        /// within the projection.  Subsequent maturities occur at
        ///   firstMaturityMonth + k * renewalPeriod  (k = 1, 2, …)
        /// If firstMaturityMonth == 0, it is taken from maturityPeriod
        /// (for backward compatibility).
        int    firstMaturityMonth = 0;

        /// Renewal period for GMAB (months between consecutive maturities
        /// after the first).  Typically = original term length.
        /// If 0, defaults to maturityPeriod.
        int    renewalPeriod   = 0;

        /// Guaranteed annuity rate for GMIB (e.g. 0.05 = 5%)
        double guaranteedAnnuityRate = 0.05;

        // ==================================================================
        //  AAD smoothing
        // ==================================================================

        /// Smoothing fraction for max(0, x) payoffs.
        /// The smoothing half-width is  eps = smooth × gbAmt.
        ///   0   →  sharp (standard max, as in European payoffs)
        ///   >0  →  C¹ quadratic-spline approximation in [−eps, eps]
        ///          for better higher-order AAD sensitivities.
        /// Typical value: 0.02 (2% of guarantee amount).
        double smooth = 0.0;

        // ==================================================================
        //  Fee parameters  (annual rates)
        // ==================================================================

        double baseFee  = 0.0;  // base / MER fee (to fund manager)
        double riderFee = 0.0;  // rider charge   (to insurer)

        // ==================================================================
        //  Fund information
        // ==================================================================

        std::vector<double> fundValues;  // initial dollar value per fund
        std::vector<double> fundFees;    // annual fee rate per fund

        // ==================================================================
        //  Fund-to-index mapping  (optional)
        //
        //  When a policy's funds are blends of underlying equity indices
        //  (as in Gan's 10-fund / 5-index setup), fundMap provides the
        //  mapping weights:   fundMap[f][k] = weight of index k in fund f.
        //
        //  If fundMap is empty, each fund IS its own index (identity map)
        //  and the model must provide numFunds() assets.
        //
        //  If fundMap is provided (nFunds x nIndices), the model provides
        //  numIndices() assets and fund growth is computed as:
        //      fundGrowth_f = sum_k  fundMap[f][k] * indexGrowth_k
        // ==================================================================

        std::vector<std::vector<double>> fundMap;  // nFunds x nIndices

        // ==================================================================
        //  Convenience
        // ==================================================================

        /// Number of underlying funds
        size_t numFunds() const { return fundValues.size(); }

        /// Number of model indices (assets the ESG must simulate)
        size_t numIndices() const {
            if (fundMap.empty()) return numFunds();
            return fundMap.empty() ? 0 : fundMap[0].size();
        }

        /// Total initial account value
        double totalAV() const {
            double s = 0.0;
            for (auto v : fundValues) s += v;
            return s;
        }

        /// Basic validation
        void validate() const {
            if (fundValues.size() != fundFees.size()) {
                throw std::runtime_error(
                    "VAPolicy: fundValues and fundFees must have the same size");
            }
            if (fundValues.empty()) {
                throw std::runtime_error(
                    "VAPolicy: at least one fund is required");
            }
            if (numMonths <= 0) {
                throw std::runtime_error(
                    "VAPolicy: numMonths must be positive");
            }
            // Validate fundMap dimensions if provided
            if (!fundMap.empty()) {
                if (fundMap.size() != numFunds()) {
                    throw std::runtime_error(
                        "VAPolicy: fundMap rows must equal numFunds()");
                }
                const size_t nIdx = fundMap[0].size();
                for (size_t f = 0; f < fundMap.size(); ++f) {
                    if (fundMap[f].size() != nIdx) {
                        throw std::runtime_error(
                            "VAPolicy: all fundMap rows must have the same size");
                    }
                    // Check weights sum to ~1
                    double wSum = std::accumulate(
                        fundMap[f].begin(), fundMap[f].end(), 0.0);
                    if (std::abs(wSum - 1.0) > 0.01) {
                        throw std::runtime_error(
                            "VAPolicy: fundMap weights for fund "
                            + std::to_string(f) + " sum to "
                            + std::to_string(wSum) + " (expected ~1.0)");
                    }
                }
            }
        }
    };

} // namespace ActuaLib
