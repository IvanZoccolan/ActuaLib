#pragma once

/*
    GMAB — Guaranteed Minimum Accumulation Benefit.

    Like GMMB, the living benefit pays max(0, gbAmt − AV) at maturity.
    Unlike GMMB, the contract RENEWS at each maturity:
      – the account value is topped up to gbAmt if AV < gbAmt
      – a new accumulation period begins
      – gbAmt is reset to max(gbAmt, AV)

    Requires VAPolicy.maturityPeriod > 0  (e.g. 120 = 10-year term).

    At each step:
        DA = 0
        LA = max(0, gbAmt − AV)   at maturity boundaries / last step
        RC = rider fee collected

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity
        Portfolios", 2017.  PricerABSU / PricerABRP / PricerABRU.
*/

#include "va_base.hpp"
#include <algorithm>

namespace ActuaLib {

    template <class T>
    class GMAB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            using std::max;
            T la(0.0);

            if (s.isMaturity || s.isLastStep) {
                la = this->smoothMax0(s.gbAmt - s.totalAV);

                // Renewal at maturity (not at last step unless it coincides):
                // top up fund values so that AV = max(gbAmt, AV).
                if (s.isMaturity) {
                    if (s.gbAmt > s.totalAV) {
                        this->smoothRescaleFunds(s.fundValues,
                                                s.totalAV, s.gbAmt);
                        s.totalAV = s.gbAmt;
                    }
                    // Reset gbAmt to current (possibly topped-up) AV
                    s.gbAmt = max(s.gbAmt, s.totalAV);
                }
            }

            return { T(0.0), std::move(la), s.riderFeeCollected };
        }

    public:
        GMAB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMAB<T>>(*this);
        }
    };

} // namespace ActuaLib
