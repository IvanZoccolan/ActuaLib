#pragma once

/*
    GMDB — Guaranteed Minimum Death Benefit.

    At each monthly step:
        DA = max(0, gbAmt − AV)      (insurer pays shortfall on death)
        LA = 0
        RC = rider fee collected

    The benefit-base update rule (SU / RP / RU) is handled by VABase.

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity
        Portfolios", 2017.  PricerDBSU / PricerDBRP / PricerDBRU.
*/

#include "va_base.hpp"

namespace ActuaLib {

    template <class T>
    class GMDB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            T da = this->smoothMax0(s.gbAmt - s.totalAV);
            return { std::move(da), T(0.0), s.riderFeeCollected };
        }

    public:
        GMDB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMDB<T>>(*this);
        }
    };

} // namespace ActuaLib
