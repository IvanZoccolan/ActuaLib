#pragma once

/*
    GMWB — Guaranteed Minimum Withdrawal Benefit.

    On each policy anniversary the policyholder withdraws a guaranteed
    amount:   WA = min(gbAmt × wbWithdrawalRate, gmwbBalance).

    The account value is reduced by the withdrawal.  If the AV is
    insufficient to cover the full withdrawal, the insurer pays the
    shortfall — that is the living benefit  LA = max(0, WA − AV).

    When gmwbBalance is exhausted (< 1e-3), gbAmt is set to 0.

    At maturity / last step, the insurer also pays the remaining
    gmwb balance that hasn't been withdrawn yet:
        LA = max(0, WA + gmwbBalance − AV).

    Cash flows:
        DA = 0
        LA = insurer's shortfall on withdrawal
        RC = rider fee collected

    Reference:
        G. Gan & E. Valdez, "Valuation of Large Variable Annuity
        Portfolios", 2017.  PricerWBSU / PricerWBRP / PricerWBRU.
*/

#include "va_base.hpp"
#include <algorithm>

namespace ActuaLib {

    template <class T>
    class GMWB : public VABase<T> {
    protected:
        VAStepCashflows<T> project(VAStepState<T>& s) const override
        {
            T withdrawalAmt(0.0);

            // Annual withdrawal on anniversaries
            if (s.isAnniversary) {
                T wag = s.gbAmt * this->myPolicy.wbWithdrawalRate;
                // Smooth min: WA = min(wag, gmwbBalance)
                withdrawalAmt = this->smoothMin(wag, s.gmwbBalance);

                s.gmwbBalance = s.gmwbBalance - withdrawalAmt;
                s.withdrawal  = s.withdrawal  + withdrawalAmt;

                // Balance exhausted → guarantee expires
                // Smooth: scale gbAmt by a sigmoid weight that
                // transitions from 1 to 0 as gmwbBalance → 0.
                {
                    const double eps = this->myPolicy.smooth
                                     * this->myPolicy.gbAmt;
                    if (eps > 0.0) {
                        // w = bal² / (bal² + eps²),  w→0 as bal→0
                        T b2 = s.gmwbBalance * s.gmwbBalance;
                        T e2 = T(eps * eps);
                        T w  = b2 / (b2 + e2);
                        s.gbAmt = s.gbAmt * w;
                    } else {
                        if (s.gmwbBalance < T(1e-3)) {
                            s.gbAmt = T(0.0);
                        }
                    }
                }

                // Reduce AV by withdrawal amount (smooth)
                T newAV = this->smoothMax0(s.totalAV - withdrawalAmt);
                this->smoothRescaleFunds(s.fundValues, s.totalAV, newAV);
                s.totalAV = newAV;
            }

            // Living benefit = insurer's shortfall
            T la(0.0);
            if (s.isMaturity || s.isLastStep) {
                la = this->smoothMax0(
                    withdrawalAmt + s.gmwbBalance - s.totalAV);
            } else {
                la = this->smoothMax0(withdrawalAmt - s.totalAV);
            }

            return { T(0.0), std::move(la), s.riderFeeCollected };
        }

    public:
        GMWB(const VAPolicy& policy, const MortalityModel& mortality)
            : VABase<T>(policy, mortality) {}

        std::unique_ptr<Product<T>> clone() const override {
            return std::make_unique<GMWB<T>>(*this);
        }
    };

} // namespace ActuaLib
