#pragma once

/*
    Base class for Variable Annuity products.

    VABase<T> implements the Product<T> interface for generic VA contracts.
    It handles the common machinery shared by all guarantee types:

      1.  Monthly timeline construction
      2.  Defline: per-step forward prices for each fund + numeraire
      3.  Fund-return application and fee deduction
      4.  Benefit-base update (SU / RP / RU — parameterised by VAPolicy)
      5.  Mortality-weighted present-value aggregation

    Derived classes only implement the pure-virtual hook  project(),
    which receives the full mutable projection state via VAStepState<T>
    and returns the per-step cash-flow triplet (DA, LA, RC).

    Pricing formula  (per path, then averaged across MC paths):

        PV_DA  = Σ_j  _{(j-1)Δt}p_x · _{Δt}q_{x+(j-1)Δt} · DA_j · D(0,t_j)
        PV_LA  = Σ_j  _{jΔt}p_x                             · LA_j · D(0,t_j)
        PV_RC  = Σ_j  _{jΔt}p_x                             · RC_j · D(0,t_j)
        FMV    = (PV_DA + PV_LA − PV_RC) × survivorship

    where D(0,t) = 1 / numeraire(t).

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity Portfolios:
        Monte Carlo Simulation and Synthetic Datasets", 2017.
*/

#include "product.hpp"
#include "va_policy.hpp"
#include "../demographics/mortality_model.hpp"
#include "../types.hpp"
#include "../esg/rw_scenarios.hpp"

#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

namespace ActuaLib {

    template <class T>
    struct VALiabilityBreakdown {
        T deathBenefitPV;
        T livingBenefitPV;
        T riderChargesPV;
        T netLiabilityFMV;
    };

    enum class VAOutputIndex : size_t {
        DeathBenefitPV = 0,
        LivingBenefitPV = 1,
        RiderChargesPV = 2,
        NetLiabilityFMV = 3
    };

    template <class T>
    inline VALiabilityBreakdown<T> liabilityBreakdownFromOutputs(const std::vector<T>& outputs)
    {
        return {
            outputs[static_cast<size_t>(VAOutputIndex::DeathBenefitPV)],
            outputs[static_cast<size_t>(VAOutputIndex::LivingBenefitPV)],
            outputs[static_cast<size_t>(VAOutputIndex::RiderChargesPV)],
            outputs[static_cast<size_t>(VAOutputIndex::NetLiabilityFMV)]
        };
    }

    // -----------------------------------------------------------------
    //  Per-step cash-flow triplet returned by derived-class project()
    // -----------------------------------------------------------------

    template <class T>
    struct VAStepCashflows {
        T da;   // death-benefit payoff
        T la;   // living-benefit payoff
        T rc;   // rider charge collected

        VAStepCashflows()
            : da(T(0.0)), la(T(0.0)), rc(T(0.0)) {}
        VAStepCashflows(T da_, T la_, T rc_)
            : da(std::move(da_)), la(std::move(la_)), rc(std::move(rc_)) {}
    };

    // -----------------------------------------------------------------
    //  Mutable projection state passed to the project() hook
    // -----------------------------------------------------------------

    template <class T>
    struct VAStepState {
        // Mutable working variables (products may modify)
        T&                totalAV;
        T&                gbAmt;
        std::vector<T>&   fundValues;
        T&                gmwbBalance;
        T&                withdrawal;

        // Step information
        int               month;            // 1-based month index
        bool              isAnniversary;    // month % 12 == 0
        bool              isMaturity;       // at maturity boundary
        bool              isLastStep;       // month == N
        T                 riderFeeCollected;

        // Full path + age (for GMIB annuity factor)
        const Scenario<T>& path;
        double             ageAtStep;       // age at this monthly step

        // When true, project() should apply state mutations (fund top-ups,
        // gbAmt resets) but skip any PV cash-flow computation.
        // Used exclusively by ageAlongPath() to advance policy state along
        // a real-world trajectory before an inner AAD pricing call.
        bool               aging = false;
    };

    // -----------------------------------------------------------------
    //  VABase<T> — abstract base for all VA products
    // -----------------------------------------------------------------

    template <class T>
    class VABase : public Product<T> {

    protected:

        // ----- data -----
        VAPolicy                            myPolicy;
        std::unique_ptr<MortalityModel>     myMortality;

        std::vector<Time>                   myTimeline;
        std::vector<SampleDef>              myDefline;
        std::vector<std::string>            myLabels;

        // Sparse fund map: for each fund, list of (index, weight) pairs
        // with non-zero weight.  Precomputed in constructor to avoid
        // redundant work in the hot payoffs() loop.
        struct FundWeight { size_t idx; double wt; };
        std::vector<std::vector<FundWeight>> mySparseFM;

        // Pre-computed monthly survival probabilities:
        //   mySurvival[j] = _{j*dT}p_x  for j = 0 … N
        // Computed once in the constructor; eliminates expensive
        // virtual mortality calls from the per-path payoffs() loop.
        std::vector<double> mySurvival;

        // Pre-computed fund-fee multipliers:
        //   myFeeMultiplier[f] = 1 − fundFee_f × dT
        std::vector<double> myFeeMultiplier;

        static constexpr double dT = 1.0 / 12.0;

        // =============================================================
        //  Pure-virtual hook for derived classes
        // =============================================================

        /*  Called once per monthly step, AFTER:
                – fund returns have been applied
                – monthly fund-level fees deducted
                – annual rider fee & base fee deducted (on anniversaries)
                – benefit base updated (SU / RP / RU)

            The state struct is fully mutable.  Most products only read
            from it; GMWB and GMAB also modify fundValues / gmwbBalance.
        */
        virtual VAStepCashflows<T> project(VAStepState<T>& state) const = 0;

        // =============================================================
        //  Annuity-factor helpers  (used by GMIB)
        // =============================================================

        /// Market annuity factor at step j — T-valued for AAD.
        ///   äT(t_j) = Σ_{n≥0}  npx(age) · D(t_j, t_j + n years)
        /// where  D(t_j, t_j+n) = numeraire(t_j) / numeraire(t_j + 12n).
        ///
        /// When the annuity payment falls beyond the scenario horizon,
        /// the numeraire is extrapolated at a flat rate estimated from
        /// the last 12 months of the path.  This matches Gan's approach
        /// of using fc[numTimeStep-1] as a flat rate for extrapolation.
        T marketAnnuityFactor(const Scenario<T>& path,
                              size_t j, double age) const
        {
            T af(0.0);
            const size_t last = path.size() - 1;

            // Estimate annual forward rate from the last 12 months
            // of the scenario for extrapolation beyond the path.
            const size_t lb = std::min(last, static_cast<size_t>(12));
            double fwdRateEst = 0.0;
            if (lb > 0) {
                // Use plain-double extraction: log(B(last)/B(last-lb))
                double numLast = static_cast<double>(path[last].numeraire);
                double numPrev = static_cast<double>(path[last - lb].numeraire);
                if (numPrev > 1e-12 && numLast > 1e-12) {
                    fwdRateEst = std::log(numLast / numPrev)
                               / (static_cast<double>(lb) * dT);
                }
            }

            for (int n = 0; ; ++n) {
                double sp = myMortality->p(age, static_cast<double>(n));
                if (sp < 1e-4) break;
                size_t futIdx = j + static_cast<size_t>(n) * 12;
                T df;
                if (futIdx <= last) {
                    df = path[j].numeraire / path[futIdx].numeraire;
                } else {
                    // Extrapolate: B(futIdx) ≈ B(last) * exp(f * Δt)
                    double extraYrs = static_cast<double>(futIdx - last) * dT;
                    double extFactor = std::exp(fwdRateEst * extraYrs);
                    df = path[j].numeraire
                       / (path[last].numeraire * extFactor);
                }
                af = af + T(sp) * df;
            }
            return af;
        }

        /// Guaranteed annuity factor — plain double (no AAD).
        ///   ä(age, r) = Σ_{n≥0} npx(age) · exp(−r·n)
        double guaranteedAnnuityFactor(double age, double rate) const
        {
            double af = 0.0;
            for (int n = 0; ; ++n) {
                double sp = myMortality->p(age, static_cast<double>(n));
                if (sp < 1e-4) break;
                af += sp * std::exp(-rate * n);
            }
            return af;
        }

        // =============================================================
        //  AAD-friendly smooth payoff helpers
        //
        //  C¹ quadratic-spline replacement for max(x, 0), identical to
        //  the approach in barrier.hpp.  The spline is:
        //
        //      smoothMax0(x) =  0              if x <= −eps
        //                       (x+eps)²/(4eps) if −eps < x < eps
        //                       x               if x >= eps
        //
        //  where eps = myPolicy.smooth × myPolicy.gbAmt.
        //  When eps == 0 (default), falls back to the standard max.
        // =============================================================

        T smoothMax0(const T& x) const
        {
            using std::max;
            const double eps = myPolicy.smooth * myPolicy.gbAmt;
            if (eps <= 0.0) return max(x, T(0.0));

            // Branch on the runtime value of x (same pattern as barrier.hpp).
            // The tape records whichever branch is active for this path.
            if (x > eps) return x;
            if (x < -eps) return T(0.0);
            return (x + eps) * (x + eps) / (4.0 * eps);
        }

        /// Smooth min(a, b):  min(a,b) = b − max(b−a, 0)
        T smoothMin(const T& a, const T& b) const
        {
            return b - smoothMax0(b - a);
        }

        /// Smooth fund-value rescaling:  blends between proportional
        /// scaling (when AV >> 0) and uniform allocation (when AV ~ 0).
        /// Uses a sigmoid-style weight  w = AV² / (AV² + eps²)  so
        /// the blending is smooth and on-tape.
        void smoothRescaleFunds(std::vector<T>& fv, const T& totalAV,
                                const T& targetAV) const
        {
            const double eps = myPolicy.smooth * myPolicy.gbAmt;
            const size_t nF  = fv.size();
            if (eps <= 0.0) {
                // Sharp (original) behaviour
                if (totalAV > T(1e-4)) {
                    T ratio = targetAV / totalAV;
                    for (auto& f : fv) f = f * ratio;
                } else {
                    T perF = targetAV / T(static_cast<double>(nF));
                    for (auto& f : fv) f = perF;
                }
                return;
            }
            // Smooth blending weight w ∈ [0, 1]:
            //   w → 1 when AV is large (proportional scaling)
            //   w → 0 when AV is near zero (uniform allocation)
            T av2   = totalAV * totalAV;
            T eps2  = T(eps * eps);
            T w     = av2 / (av2 + eps2);
            T ratio = targetAV / (totalAV + T(eps * 1e-12));  // guard / 0
            T perF  = targetAV / T(static_cast<double>(nF));
            for (auto& f : fv) {
                f = w * (f * ratio) + (T(1.0) - w) * perF;
            }
        }

    public:

        // =============================================================
        //  Constructor
        // =============================================================

        VABase(const VAPolicy& policy,
               const MortalityModel& mortality)
            : myPolicy(policy),
              myMortality(mortality.clone()),
              myLabels({"GMDB", "GMLB", "RC", "FMV"})
        {
            myPolicy.validate();

            const int    N      = myPolicy.numMonths;
            const size_t nFunds = myPolicy.numFunds();

            // ----------------------------------------------------------
            //  Timeline:  t_0 = 0, t_1 = Δt, … , t_N = NΔt
            //  We need the first point (t_0) so that the model provides
            //  initial forward prices for computing step-1 returns.
            // ----------------------------------------------------------
            myTimeline.resize(static_cast<size_t>(N) + 1);
            for (int j = 0; j <= N; ++j) {
                myTimeline[static_cast<size_t>(j)] = j * dT;
            }

            // ----------------------------------------------------------
            //  Defline: at every step request
            //    • numAssets forwards   (index spot prices)
            //    • numeraire            (for discounting)
            //  numAssets = numIndices (not numFunds when fundMap is set)
            // ----------------------------------------------------------
            const size_t nIdx = myPolicy.numIndices();
            myDefline.resize(static_cast<size_t>(N) + 1);
            for (int j = 0; j <= N; ++j) {
                auto& def      = myDefline[static_cast<size_t>(j)];
                def.numeraire  = true;
                def.numAssets  = nIdx;
                def.forwardMats.push_back(myTimeline[static_cast<size_t>(j)]);
            }

            // ----------------------------------------------------------
            //  Sparse fund map: skip zero-weight entries in hot loop
            // ----------------------------------------------------------
            if (!myPolicy.fundMap.empty()) {
                mySparseFM.resize(nFunds);
                for (size_t f = 0; f < nFunds; ++f) {
                    for (size_t k = 0; k < nIdx; ++k) {
                        if (myPolicy.fundMap[f][k] != 0.0)
                            mySparseFM[f].push_back({k, myPolicy.fundMap[f][k]});
                    }
                }
            }

            // ----------------------------------------------------------
            //  Pre-compute mortality survival curve
            //
            //  mySurvival[j] = _{j*dT}p_x  is path-independent and
            //  was previously computed 3× per step per path (100K×N
            //  virtual calls).  Now computed once at construction.
            //
            //  In the payoffs() loop:
            //    survWt  = mySurvival[j]                 = s_j
            //    deathWt = mySurvival[j-1] - mySurvival[j] = s_{j-1} · q_j
            // ----------------------------------------------------------
            mySurvival.resize(static_cast<size_t>(N) + 1);
            mySurvival[0] = 1.0;
            for (int j = 1; j <= N; ++j) {
                mySurvival[static_cast<size_t>(j)] =
                    myMortality->p(policy.currentAge, j * dT);
            }

            // ----------------------------------------------------------
            //  Pre-compute fund fee multipliers: (1 − fee_f × Δt)
            // ----------------------------------------------------------
            myFeeMultiplier.resize(nFunds);
            for (size_t f = 0; f < nFunds; ++f) {
                myFeeMultiplier[f] = 1.0 - myPolicy.fundFees[f] * dT;
            }
        }

        // =============================================================
        //  Copy constructor  (deep-copies mortality model)
        // =============================================================

        VABase(const VABase& other)
            : myPolicy(other.myPolicy),
              myMortality(other.myMortality->clone()),
              myTimeline(other.myTimeline),
              myDefline(other.myDefline),
              myLabels(other.myLabels),
              mySparseFM(other.mySparseFM),
              mySurvival(other.mySurvival),
              myFeeMultiplier(other.myFeeMultiplier)
        {}

        // =============================================================
        //  Product<T> interface
        // =============================================================

        const std::vector<Time>& timeline() const override {
            return myTimeline;
        }

        const std::vector<SampleDef>& defline() const override {
            return myDefline;
        }

        const std::vector<std::string>& payoffLabels() const override {
            return myLabels;
        }

        const VAPolicy& policy() const {
            return myPolicy;
        }

        const MortalityModel& mortality() const {
            return *myMortality;
        }

        VALiabilityBreakdown<T> evaluateLiabilityBreakdown(const Scenario<T>& path) const
        {
            std::vector<T> outputs(4);
            payoffs(path, outputs);
            return liabilityBreakdownFromOutputs(outputs);
        }

        // -------------------------------------------------------------
        //  payoffs()  —  the main projection + PV engine
        // -------------------------------------------------------------
        void payoffs(
            const Scenario<T>& path,
            std::vector<T>&    pays) const override
        {
            using std::max;

            const size_t nFunds  = myPolicy.numFunds();
            const size_t nIdx    = myPolicy.numIndices();
            const bool   hasFM   = !myPolicy.fundMap.empty();
            const int    N       = myPolicy.numMonths;
            const double age     = myPolicy.currentAge;

            // Working copy of per-fund account values
            //
            // Linearisation trick: the fund value is exact at baseline,
            // but connected to the spots on the AAD tape so that
            //   ∂fv_f / ∂spot_k = fundValue_f * w_{fk} / PA_k
            //
            // A 1% shock to index k (i.e. PA_k → PA_k*(1+s)) raises
            // fund f by fundValue_f * w_{fk} * s.  To reproduce this:
            //
            //   fv[f] = fundValue_f
            //         + Σ_k (fundValue_f * w_{fk} / PA_k) * (spot_k − PA_k)
            //
            // At baseline (spot_k = PA_k) the perturbation vanishes.
            // The adjoint gives:
            //   DollarDelta_k = PA_k * Σ_f ∂V/∂fv_f * fv_f * w_{fk} / PA_k
            //                 = Σ_f ∂V/∂fv_f * fv_f * w_{fk}
            // which matches Gan's Eq.(27) bump-and-reprice.
            //
            // Without fundMap (identity): fv[k] = spot_k = fundValue_k.
            std::vector<T> fv(nFunds);
            if (hasFM) {
                // Pre-compute spotDev once per index (not per fund×index)
                std::vector<T> spotDev(nIdx);
                for (size_t k = 0; k < nIdx; ++k) {
                    spotDev[k] = path[0].forwards[k]
                        - T(static_cast<double>(path[0].forwards[k]));
                }
                for (size_t f = 0; f < nFunds; ++f) {
                    fv[f] = T(myPolicy.fundValues[f]);
                    for (const auto& fw : mySparseFM[f]) {
                        // scale = fundValue_f * w_{fk} / PA_k
                        double PA_k = static_cast<double>(
                            path[0].forwards[fw.idx]);
                        double scale = myPolicy.fundValues[f] * fw.wt / PA_k;
                        fv[f] = fv[f] + T(scale) * spotDev[fw.idx];
                    }
                }
            } else {
                for (size_t k = 0; k < nFunds; ++k) {
                    fv[k] = path[0].forwards[k];
                }
            }

            // Working copy of guaranteed benefit amount (on tape for AAD)
            T gbAmt = T(myPolicy.gbAmt);

            // GMWB working state
            T gmwbBal    = T(myPolicy.gmwbBalance);
            T withdrawal = T(0.0);

            // Accumulators for mortality-weighted PV
            T pvDA(0.0), pvLA(0.0), pvRC(0.0);

            // Pre-allocate index return buffer (avoid per-step heap alloc)
            std::vector<T> idxRet(hasFM ? nIdx : 0);

            for (int j = 1; j <= N; ++j) {
                const size_t jj = static_cast<size_t>(j);

                // -------------------------------------------------
                //  1. Fund returns  ×  monthly fund-level fee
                //
                //  If fundMap is present: index returns are blended
                //    indexRet_k = S_k(t_j) / S_k(t_{j-1})
                //    fundGrowth_f = Σ_k  fundMap[f][k] * indexRet_k
                //    fv[f] *= fundGrowth_f * (1 - fundFee_f * dt)
                //
                //  If no fundMap (identity): each fund IS its index
                //    fv[k] *= (S_k(t_j)/S_k(t_{j-1})) * (1 - fee_k*dt)
                // -------------------------------------------------
                if (hasFM) {
                    // Compute index returns first
                    for (size_t k = 0; k < nIdx; ++k) {
                        idxRet[k] = path[jj].forwards[k]
                                  / path[jj - 1].forwards[k];
                    }
                    // Blend into fund-level growth (sparse)
                    for (size_t f = 0; f < nFunds; ++f) {
                        T fundGrowth(0.0);
                        for (const auto& fw : mySparseFM[f]) {
                            fundGrowth = fundGrowth + T(fw.wt) * idxRet[fw.idx];
                        }
                        fv[f] = fv[f] * fundGrowth * myFeeMultiplier[f];
                    }
                } else {
                    for (size_t k = 0; k < nFunds; ++k) {
                        T ret = path[jj].forwards[k]
                              / path[jj - 1].forwards[k];
                        fv[k] = fv[k] * ret * myFeeMultiplier[k];
                    }
                }

                // -------------------------------------------------
                //  2. Annual rider fee + base fee  (on anniversaries)
                //
                //     Gan's anniversary condition is:
                //       nMonth % 12 == 0  &&  nMonth > 0
                //     where nMonth = monthsBetween(issueDate, currentDate).
                //     At projection step j:  nMonth = monthsSinceIssue + j.
                // -------------------------------------------------
                const bool isAnniv =
                    ((myPolicy.monthsSinceIssue + j) % 12 == 0)
                    && (myPolicy.monthsSinceIssue + j > 0);

                T riderFeeCollected(0.0);
                if (isAnniv) {
                    for (size_t k = 0; k < nFunds; ++k) {
                        T rf = fv[k] * myPolicy.riderFee;
                        T bf = fv[k] * myPolicy.baseFee;
                        fv[k] = fv[k] - rf - bf;
                        riderFeeCollected = riderFeeCollected + rf;
                    }
                }

                // -------------------------------------------------
                //  3. Total account value
                // -------------------------------------------------
                T totalAV(0.0);
                for (size_t k = 0; k < nFunds; ++k) {
                    totalAV = totalAV + fv[k];
                }

                // -------------------------------------------------
                //  4. Benefit-base update (on anniversaries)
                // -------------------------------------------------
                if (isAnniv) {
                    switch (myPolicy.updateRule) {
                    case BenefitBaseUpdate::StepUp:
                        gbAmt = max(gbAmt, totalAV);
                        break;
                    case BenefitBaseUpdate::ReturnOfPremium:
                        break;  // no change
                    case BenefitBaseUpdate::RollUp:
                        gbAmt = gbAmt * (1.0 + myPolicy.rollUpRate);
                        break;
                    }
                }

                // -------------------------------------------------
                //  5. Maturity detection
                //
                //  Renewable products (GMAB, GMDB_AB):
                //    first maturity at firstMaturityMonth,
                //    renewals every renewalPeriod months thereafter.
                //  Non-renewable products (GMMB, GMDB):
                //    single maturity at numMonths (= last step).
                // -------------------------------------------------
                bool isMaturity = false;
                if (myPolicy.firstMaturityMonth > 0) {
                    // New-style renewable maturity:
                    //   first at firstMaturityMonth,
                    //   then at firstMaturityMonth + k*renewalPeriod
                    if (j == myPolicy.firstMaturityMonth) {
                        isMaturity = true;
                    } else if (myPolicy.renewalPeriod > 0
                               && j > myPolicy.firstMaturityMonth) {
                        int past = j - myPolicy.firstMaturityMonth;
                        isMaturity = (past % myPolicy.renewalPeriod == 0);
                    }
                } else if (myPolicy.maturityPeriod > 0) {
                    // Legacy: simple periodic maturity
                    isMaturity = (j % myPolicy.maturityPeriod == 0);
                } else {
                    isMaturity = (j == N);
                }
                const bool isLast = (j == N);

                // -------------------------------------------------
                //  6. Derived-class projection hook
                // -------------------------------------------------
                VAStepState<T> state {
                    totalAV, gbAmt, fv, gmwbBal, withdrawal,
                    j, isAnniv, isMaturity, isLast,
                    riderFeeCollected,
                    path, age + j * dT
                };
                auto cf = project(state);

                // -------------------------------------------------
                //  7. Mortality-weighted present value
                //
                //     D(0,t_j) = 1 / numeraire(t_j)
                //     s_j      = _{jΔt}p_x
                //     q_j      = _{Δt}q_{x+(j-1)Δt}
                //
                //  DA weighted by  s_{j-1} · q_j  (die in interval)
                //  LA, RC weighted by  s_j         (survive to j)
                // -------------------------------------------------
                const double deathWt = mySurvival[jj - 1] - mySurvival[jj];
                const double survWt  = mySurvival[jj];

                const T dfj = T(1.0) / path[jj].numeraire;

                pvDA = pvDA + cf.da * T(deathWt) * dfj;
                pvLA = pvLA + cf.la * T(survWt)  * dfj;
                pvRC = pvRC + cf.rc * T(survWt)  * dfj;
            }

            // ----------------------------------------------------------
            //  Assemble payoffs
            // ----------------------------------------------------------
            const double surv = myPolicy.survivorship;

            pays[0] = pvDA * surv;                            // GMDB
            pays[1] = pvLA * surv;                            // GMLB
            pays[2] = pvRC * surv;                            // RC
            pays[3] = (pvDA + pvLA - pvRC) * surv;            // FMV
        }

        // clone() remains pure virtual — derived classes provide it.

        // =============================================================
        //  ageAlongPath()  —  advance policy state along a RW scenario
        //
        //  Drives fund values and benefit base through months 1..targetMonth
        //  using growth factors from rw(rwPath, t, k), applying fees and
        //  calling project() with aging=true so that derived classes perform
        //  state mutations (GMAB top-up, GMWB withdrawals, gbAmt resets)
        //  WITHOUT computing mortality-weighted PV cash flows.
        //
        //  Returns the aged VAPolicy ready to construct the inner pricing
        //  product.  Only meaningful when T = double (outer RW loop).
        // =============================================================
        VAPolicy ageAlongPath(const RWScenarioSet& rw,
                              size_t              rwPath,
                              size_t              targetMonth) const
        {
            using std::max;

            const size_t nFunds  = myPolicy.numFunds();
            const size_t nIdx    = myPolicy.numIndices();
            const bool   hasFM   = !myPolicy.fundMap.empty();
            const int    N       = myPolicy.numMonths;

            if (targetMonth > static_cast<size_t>(N)) {
                throw std::runtime_error(
                    "VABase::ageAlongPath: targetMonth exceeds policy remaining horizon");
            }

            // Working state (T-valued — no-op conversions when T = double)
            std::vector<T> fv(nFunds);
            for (size_t k = 0; k < nFunds; ++k)
                fv[k] = T(myPolicy.fundValues[k]);

            T gbAmt(myPolicy.gbAmt);
            T gmwbBal(myPolicy.gmwbBalance);
            T withdrawal(0.0);

            // Dummy path — single step; not accessed when aging=true,
            // but required as a const reference in VAStepState.
            Scenario<T> dummyPath(1);
            dummyPath[0].numeraire = T(1.0);
            dummyPath[0].forwards.assign(nIdx, T(1.0));

            // Per-step index return buffer
            std::vector<double> idxRet(hasFM ? nIdx : 0);

            for (size_t j = 1; j <= targetMonth; ++j) {
                const int jj = static_cast<int>(j);

                // 1. Fund returns from RW scenario
                if (hasFM) {
                    for (size_t k = 0; k < nIdx; ++k)
                        idxRet[k] = rw(rwPath, j, k);
                    for (size_t f = 0; f < nFunds; ++f) {
                        double fundGrowth = 0.0;
                        for (const auto& fw : mySparseFM[f])
                            fundGrowth += fw.wt * idxRet[fw.idx];
                        fv[f] = fv[f] * T(fundGrowth) * T(myFeeMultiplier[f]);
                    }
                } else {
                    for (size_t k = 0; k < nFunds; ++k)
                        fv[k] = fv[k] * T(rw(rwPath, j, k)) * T(myFeeMultiplier[k]);
                }

                // 2. Annual rider + base fees on anniversaries
                const bool isAnniv =
                    ((myPolicy.monthsSinceIssue + jj) % 12 == 0)
                    && (myPolicy.monthsSinceIssue + jj > 0);

                T rcFee(0.0);
                if (isAnniv) {
                    for (size_t k = 0; k < nFunds; ++k) {
                        T rf = fv[k] * T(myPolicy.riderFee);
                        T bf = fv[k] * T(myPolicy.baseFee);
                        fv[k] = fv[k] - rf - bf;
                        rcFee = rcFee + rf;
                    }
                }

                // 3. Total account value
                T totalAV(0.0);
                for (size_t k = 0; k < nFunds; ++k)
                    totalAV = totalAV + fv[k];

                // 4. Benefit base update on anniversaries
                if (isAnniv) {
                    switch (myPolicy.updateRule) {
                    case BenefitBaseUpdate::StepUp:
                        gbAmt = max(gbAmt, totalAV);
                        break;
                    case BenefitBaseUpdate::ReturnOfPremium:
                        break;
                    case BenefitBaseUpdate::RollUp:
                        gbAmt = gbAmt * T(1.0 + myPolicy.rollUpRate);
                        break;
                    }
                }

                // 5. Maturity detection
                bool isMaturity = false;
                if (myPolicy.firstMaturityMonth > 0) {
                    if (jj == myPolicy.firstMaturityMonth) {
                        isMaturity = true;
                    } else if (myPolicy.renewalPeriod > 0
                               && jj > myPolicy.firstMaturityMonth) {
                        int past = jj - myPolicy.firstMaturityMonth;
                        isMaturity = (past % myPolicy.renewalPeriod == 0);
                    }
                } else if (myPolicy.maturityPeriod > 0) {
                    isMaturity = (jj % myPolicy.maturityPeriod == 0);
                } else {
                    isMaturity = (jj == N);
                }
                const bool isLast = (jj == N);

                // 6. Project hook with aging=true
                //    State mutations (GMAB top-up, GMWB deductions, etc.)
                //    are applied; cash-flow PV accumulation is skipped.
                VAStepState<T> state {
                    totalAV, gbAmt, fv, gmwbBal, withdrawal,
                    jj, isAnniv, isMaturity, isLast,
                    std::move(rcFee),
                    dummyPath,
                    myPolicy.currentAge + j * dT,
                    /* aging = */ true
                };
                project(state);
            }

            // ----------------------------------------------------------
            //  Build aged VAPolicy
            // ----------------------------------------------------------
            VAPolicy aged = myPolicy;

            for (size_t k = 0; k < nFunds; ++k)
                aged.fundValues[k] = static_cast<double>(fv[k]);
            aged.gbAmt         = static_cast<double>(gbAmt);
            aged.gmwbBalance   = static_cast<double>(gmwbBal);

            // Advance time
            aged.numMonths        = N - static_cast<int>(targetMonth);
            aged.monthsSinceIssue = myPolicy.monthsSinceIssue
                                  + static_cast<int>(targetMonth);
            aged.currentAge       = myPolicy.currentAge + targetMonth * dT;

            // Re-anchor maturity schedule to the aged window
            const int tgt = static_cast<int>(targetMonth);
            if (myPolicy.firstMaturityMonth > 0) {
                int R = (myPolicy.renewalPeriod > 0) ? myPolicy.renewalPeriod
                                                     : myPolicy.maturityPeriod;
                int nextM = myPolicy.firstMaturityMonth;
                while (R > 0 && nextM <= tgt) nextM += R;
                aged.firstMaturityMonth = (nextM > tgt) ? (nextM - tgt) : 0;
                aged.renewalPeriod = R;
            } else if (myPolicy.maturityPeriod > 0) {
                const int R = myPolicy.maturityPeriod;
                const int nextM = ((tgt / R) + 1) * R;
                aged.firstMaturityMonth = (nextM > tgt && nextM <= N)
                                        ? (nextM - tgt) : 0;
                aged.renewalPeriod = R;
            }
            // else: single maturity at numMonths (ageAtStep falls to last step)

            return aged;
        }

        virtual ~VABase() = default;
    };

} // namespace ActuaLib
