#pragma once

/*
    Combination VA products — Death Benefit + Living Benefit riders.

    Each combo pairs the GMDB death-benefit with one of the living-benefit
    guarantee types.  All combos in Gan's dataset use Step-Up for the
    benefit-base update (this is handled centrally by VABase; the combo
    class is independent of the update rule).

    Classes defined here:
      • GMDB_AB<T>  — Death Benefit + Accumulation Benefit
      • GMDB_MB<T>  — Death Benefit + Maturity Benefit
      • GMDB_WB<T>  — Death Benefit + Withdrawal Benefit
      • GMDB_IB<T>  — Death Benefit + Income Benefit

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity
        Portfolios", 2017.
        PricerDBAB / PricerDBMB / PricerDBWB / PricerDBIB.
*/

#include "va_base.hpp"
#include <algorithm>

namespace ActuaLib {

    // =================================================================
    //  GMDB + GMAB  (Death + Accumulation)
    // =================================================================

    template <class T>
    class GMDB_AB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            using std::max;

            // Death benefit — every step
            T da = this->smoothMax0(s.gbAmt - s.totalAV);

            // Living benefit — at maturity / last step  (with renewal)
            T la(0.0);
            if (s.isMaturity || s.isLastStep) {
                la = this->smoothMax0(s.gbAmt - s.totalAV);

                // Renewal at maturity: top up AV, reset gbAmt
                if (s.isMaturity) {
                    if (s.gbAmt > s.totalAV) {
                        this->smoothRescaleFunds(s.fundValues,
                                                s.totalAV, s.gbAmt);
                        s.totalAV = s.gbAmt;
                    }
                    s.gbAmt = max(s.gbAmt, s.totalAV);
                }
            }

            return { std::move(da), std::move(la), s.riderFeeCollected };
        }

    public:
        GMDB_AB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMDB_AB<T>>(*this);
        }
    };

    // =================================================================
    //  GMDB + GMMB  (Death + Maturity)
    // =================================================================

    template <class T>
    class GMDB_MB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            // Death benefit — every step
            T da = this->smoothMax0(s.gbAmt - s.totalAV);

            // Living benefit — at maturity / last step only (no renewal)
            T la(0.0);
            if (s.isMaturity || s.isLastStep) {
                la = this->smoothMax0(s.gbAmt - s.totalAV);
            }

            return { std::move(da), std::move(la), s.riderFeeCollected };
        }

    public:
        GMDB_MB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMDB_MB<T>>(*this);
        }
    };

    // =================================================================
    //  GMDB + GMWB  (Death + Withdrawal)
    //
    //  DA = max(0, WA + gmwbBalance − AV)
    //       (insurer covers remaining guarantee on death)
    //  LA = max(0, WA − AV)         normally
    //     = max(0, WA + bal − AV)   at maturity / last step
    //
    //  Note: unlike standalone GMWB, the gbAmt is NOT zeroed when
    //  the balance is exhausted (death benefit remains active).
    // =================================================================

    template <class T>
    class GMDB_WB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            T withdrawalAmt(0.0);

            if (s.isAnniversary) {
                T wag = s.gbAmt * this->myPolicy.wbWithdrawalRate;
                withdrawalAmt = this->smoothMin(wag, s.gmwbBalance);

                s.gmwbBalance = s.gmwbBalance - withdrawalAmt;
                s.withdrawal  = s.withdrawal  + withdrawalAmt;

                // NOTE: no zeroing of gbAmt when balance exhausted
                //       (death benefit remains active)

                // Reduce AV (smooth)
                T newAV = this->smoothMax0(s.totalAV - withdrawalAmt);
                this->smoothRescaleFunds(s.fundValues, s.totalAV, newAV);
                s.totalAV = newAV;
            }

            // Death benefit: remaining guarantee obligation
            T da = this->smoothMax0(
                withdrawalAmt + s.gmwbBalance - s.totalAV);

            // Living benefit
            T la(0.0);
            if (s.isMaturity || s.isLastStep) {
                la = this->smoothMax0(
                    withdrawalAmt + s.gmwbBalance - s.totalAV);
            } else {
                la = this->smoothMax0(withdrawalAmt - s.totalAV);
            }

            return { std::move(da), std::move(la), s.riderFeeCollected };
        }

    public:
        GMDB_WB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMDB_WB<T>>(*this);
        }
    };

    // =================================================================
    //  GMDB + GMIB  (Death + Income)
    // =================================================================

    template <class T>
    class GMDB_IB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            // When aging along a real-world path, skip PV computation
            // (and the path access that marketAnnuityFactor requires).
            if (s.aging) {
                return { T(0.0), T(0.0), s.riderFeeCollected };
            }

            // Death benefit — every step
            T da = this->smoothMax0(s.gbAmt - s.totalAV);

            // Living benefit — annuity conversion at maturity / last step
            T la(0.0);
            if (s.isMaturity || s.isLastStep) {
                double gAF = this->guaranteedAnnuityFactor(
                    s.ageAtStep, this->myPolicy.guaranteedAnnuityRate);
                double rg = (gAF > 1e-8) ? 1.0 / gAF : 0.0;

                T mAF = this->marketAnnuityFactor(
                    s.path,
                    static_cast<size_t>(s.month),
                    s.ageAtStep);

                la = this->smoothMax0(
                    s.gbAmt * rg * mAF - s.totalAV);
            }

            return { std::move(da), std::move(la), s.riderFeeCollected };
        }

    public:
        GMDB_IB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMDB_IB<T>>(*this);
        }
    };

} // namespace ActuaLib
