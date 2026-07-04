#pragma once

/*
    GMIB — Guaranteed Minimum Income Benefit.

    At maturity, the policyholder may convert the guaranteed benefit
    to a life annuity at a guaranteed rate.  The insurer's cost is the
    excess of the guaranteed annuity value over the account value:

        LA = max( gbAmt × rg × äT  −  AV ,  0 )

    where
        rg  = 1 / ä(age, r_guaranteed)   (guaranteed annuity rate)
        äT  = Σ_{n≥0} npx(age) · D(t_mat, t_mat + n years)
              (market annuity factor using the prevailing yield curve)

    Cash flows at each step:
        DA = 0
        LA = annuity conversion excess at maturity / last step only
        RC = rider fee collected

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity
        Portfolios", 2017.  PricerIBSU / PricerIBRP / PricerIBRU.
*/

#include "va_base.hpp"
#include <algorithm>

namespace ActuaLib {

    template <class T>
    class GMIB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            T la(0.0);

            // When aging along a real-world path, skip PV computation
            // (and the path access that marketAnnuityFactor requires).
            if (s.aging) {
                return { T(0.0), T(0.0), s.riderFeeCollected };
            }

            if (s.isMaturity || s.isLastStep) {
                // Guaranteed annuity rate
                double gAF = this->guaranteedAnnuityFactor(
                    s.ageAtStep, this->myPolicy.guaranteedAnnuityRate);
                double rg = (gAF > 1e-8) ? 1.0 / gAF : 0.0;

                // Market annuity factor (T-valued for AAD)
                T mAF = this->marketAnnuityFactor(
                    s.path,
                    static_cast<size_t>(s.month),
                    s.ageAtStep);

                // LA = smoothMax0(gbAmt × rg × äT − AV)
                la = this->smoothMax0(
                    s.gbAmt * rg * mAF - s.totalAV);
            }

            return { T(0.0), std::move(la), s.riderFeeCollected };
        }

    public:
        GMIB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMIB<T>>(*this);
        }
    };

} // namespace ActuaLib
