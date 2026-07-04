#pragma once

/*
    GMMB — Guaranteed Minimum Maturity Benefit.

    Cash flows are zero at all steps except maturity (and the last projection
    step if maturity falls beyond the scenario horizon):

        DA = 0
        LA = max(0, gbAmt − AV)   at maturity / last step
        RC = rider fee collected

    Unlike GMAB, there is NO renewal at maturity.

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity
        Portfolios", 2017.  PricerMBSU / PricerMBRP / PricerMBRU.
*/

#include "va_base.hpp"

namespace ActuaLib {

    template <class T>
    class GMMB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            T la(0.0);
            if (s.isMaturity || s.isLastStep) {
                la = this->smoothMax0(s.gbAmt - s.totalAV);
            }
            return { T(0.0), std::move(la), s.riderFeeCollected };
        }

    public:
        GMMB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMMB<T>>(*this);
        }
    };

} // namespace ActuaLib
