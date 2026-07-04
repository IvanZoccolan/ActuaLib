#pragma once

/*
    Hull-White one-factor (HW1F) interest-rate model — precomputed constants.

    Used as the risk-neutral (Q-measure) short-rate model inside the inner MC
    loop of nested stochastic simulation, calibrated at each outer scenario node
    to the AIRG-generated yield curve.

    Model dynamics (under Q):
        dr_t = [theta(t) - a * r_t] dt + sigma * dW_t

    where theta(t) is uniquely determined by the requirement that the model
    prices the outer yield curve exactly at t = 0.

    Exact discrete transition (no Euler error, monthly steps dt = 1/12):
        r_{i+1} = c1 * r_i + c2[i] + c3 * Z_i,    Z_i ~ N(0, 1)

    with precomputed constants (all off-tape doubles):
        c1       = exp(-a * dt)
        c3       = sigma * sqrt((1 - exp(-2*a*dt)) / (2*a))
        c2[i]    = alpha(t_{i+1}) - alpha(t_i) * c1
        alpha(t) = f^M(0,t) + [sigma^2 / (2*a^2)] * (1 - exp(-a*t))^2
        r0       = f^M(0, 0+) = -ln(D^M(dt)) / dt   (1-step forward rate)

    Analytic one-step discount factor P^HW(t_i, t_{i+1}) for numeraire accumulation:
        P^HW(t_i, t_{i+1}) = A_i * exp(-B_hw * r_i)

    where:
        B_hw       = (1 - exp(-a*dt)) / a
        corrFactor[i] = sigma^2 * B_hw^2 * (1 - exp(-2*a*t_i)) / (4*a)

    The A_i factor is computed on the AAD tape inside HullWhiteBlackScholes::init():
        logA_i = (1 - B_hw/dt) * (ln D^M(t_{i+1}) - ln D^M(t_i)) - corrFactor[i]

    so that IR Greeks (sensitivities to outer curve knots) flow correctly via AAD.

    Note: HW1F allows negative rates (unlike CIR or log-normal models).  For the
    short inner projection horizon (≤ 1 year), the probability of negative rates
    from a positive initial value is negligible at typical HW parameters.

    References:
        Hull, J. and White, A. (1990).  "Pricing Interest Rate Derivative
        Securities."  Review of Financial Studies, 3(4):573-592.

        Hull, J. (2022).  Options, Futures and Other Derivatives, 11th ed.,
        Chapter 32.  Pearson.
*/

#include "yieldcurve.hpp"
#include <vector>
#include <cmath>
#include <stdexcept>

namespace ActuaLib {

    // =========================================================================
    //  HW1FParams — user-facing parameters
    // =========================================================================

    struct HW1FParams {
        double a     = 0.10;   ///< Mean-reversion speed (yr^{-1}). Default: 10%.
        double sigma = 0.01;   ///< Short-rate volatility (yr^{-1/2}). Default: 1%.

        void validate() const {
            if (a <= 0.0)
                throw std::runtime_error("HW1FParams: a must be positive");
            if (sigma <= 0.0)
                throw std::runtime_error("HW1FParams: sigma must be positive");
        }
    };

    // =========================================================================
    //  HullWhite1F — precomputed constants for path simulation.
    //
    //  Scalar constants (c1, c3, bhw, corrFactor) are plain doubles and remain
    //  off-tape.  The curve-dependent parts (r0 and c2[i]) are available as
    //  doubles for reference, but the canonical on-tape versions are produced by
    //  computeOnTapeParts<U>() which should be called from
    //  HullWhiteBlackScholes::init() so that IR Greeks propagate through the
    //  full HW calibration (initial rate + theta function).
    // =========================================================================

    class HullWhite1F {
    public:
        // ---- scalar constants ----
        double a;           ///< mean-reversion speed (copy from params)
        double sigma;       ///< short-rate volatility (copy from params)
        double dt;          ///< step size in years (typically 1/12)
        double c1;          ///< exp(-a * dt)
        double c3;          ///< sigma * sqrt((1 - exp(-2*a*dt)) / (2*a))
        double bhw;         ///< B(dt) = (1 - exp(-a*dt)) / a
        double bhwOverDt;   ///< B(dt) / dt  — used for on-tape log(A_i) formula
        double r0;          ///< initial short rate = f^M(0, dt) from outer curve

        // ---- per-step arrays (size = nSteps) ----
        std::vector<double> c2;          ///< alpha(t_{i+1}) - alpha(t_i)*c1
        std::vector<double> corrFactor;  ///< sigma^2*B_hw^2*(1-exp(-2*a*t_i))/(4*a)
        std::vector<double> stepTimes;   ///< start time of each step (saved for computeOnTapeParts)

        HullWhite1F() = default;

        // -----------------------------------------------------------------
        //  build() — populate all constants from HW params and outer curve.
        //
        //  stepTimes[i] = t_i = start time of step i (years).
        //  stepTimes must have size = nSteps; the step size dt is inferred
        //  from stepTimes[1] - stepTimes[0] (or stepTimes[0] if nSteps == 1).
        // -----------------------------------------------------------------
        void build(const HW1FParams&          params,
                   const YieldCurve<double>&  outerCurve,
                   const std::vector<double>& stepTimes)
        {
            params.validate();
            const size_t nSteps = stepTimes.size();
            if (nSteps == 0)
                throw std::runtime_error("HullWhite1F::build: empty stepTimes");

            a     = params.a;
            sigma = params.sigma;

            // Infer dt from first pair of step times (or first time if single step)
            dt = (nSteps > 1)
                 ? (stepTimes[1] - stepTimes[0])
                 : stepTimes[0];
            if (dt <= 1.0e-10)
                throw std::runtime_error("HullWhite1F::build: dt must be positive");

            // Scalar constants
            c1        = std::exp(-a * dt);
            c3        = sigma * std::sqrt((1.0 - std::exp(-2.0 * a * dt)) / (2.0 * a));
            bhw       = (1.0 - c1) / a;            // B(dt) = (1-exp(-a dt))/a
            bhwOverDt = bhw / dt;

            // Initial short rate from the outer curve's first forward rate:
            //   r0 = f^M(0, 0+) ≈ [-ln D^M(dt)] / dt
            const double lnD_dt = std::log(outerCurve.discount(dt));
            r0 = -lnD_dt / dt;

            // Instantaneous forward rate at time t from outer curve, discretised as:
            //   f^M(0, t) ≈ [ln D^M(t) - ln D^M(t + dt)] / dt
            // For the final step, D^M(t + dt) uses the curve's flat-forward extrapolation.
            auto fwdRate = [&](double t) -> double {
                const double lnDt  = std::log(outerCurve.discount(t));
                const double lnDt1 = std::log(outerCurve.discount(t + dt));
                return (lnDt - lnDt1) / dt;
            };

            // alpha(t) = f^M(0,t) + [sigma^2/(2a^2)] * (1-exp(-at))^2
            auto alpha = [&](double t) -> double {
                const double f  = fwdRate(t);
                const double ex = 1.0 - std::exp(-a * t);
                return f + (sigma * sigma / (2.0 * a * a)) * ex * ex;
            };

            // Per-step precomputed values
            c2.resize(nSteps);
            corrFactor.resize(nSteps);

            for (size_t i = 0; i < nSteps; ++i) {
                const double ti = stepTimes[i];
                // Drift shift: alpha(t_{i+1}) - c1 * alpha(t_i)
                c2[i] = alpha(ti + dt) - c1 * alpha(ti);
                // Variance correction for A_i formula
                corrFactor[i] = (sigma * sigma * bhw * bhw *
                                 (1.0 - std::exp(-2.0 * a * ti))) / (4.0 * a);
            }

            // Save step times for use in computeOnTapeParts
            this->stepTimes.assign(stepTimes.begin(), stepTimes.end());
        }

        // -----------------------------------------------------------------
        //  evolve() — exact Gaussian step: r_{i+1} = c1*r + c2[step] + c3*z
        //  z must be N(0,1).  Off tape (returns plain double).
        // -----------------------------------------------------------------
        double evolve(double r, double z, size_t step) const
        {
            return c1 * r + c2[step] + c3 * z;
        }

        // -----------------------------------------------------------------
        //  computeOnTapeParts() — produce r0 and c2[i] as U-typed AAD nodes
        //  using the on-tape yield curve.  build() must be called first to
        //  populate c1, c3, bhw, dt, stepTimes, and corrFactor.
        //
        //  Mathematical basis:
        //    r0      = -ln D^M(dt) / dt
        //    c2[i]   = alpha(t_{i+1}) - c1 * alpha(t_i)
        //    alpha(t)= [ln D(t) - ln D(t+dt)] / dt  +  hwCorr(t)   ← D(t) first
        //    hwCorr  = sigma^2/(2a^2) * (1-exp(-at))^2   [pure double]
        //
        //  Substituting:
        //    c2[i] = [(1+c1)*ln D(ti1) - ln D(ti2) - c1*ln D(ti0)] / dt
        //            + [hwCorr(ti1) - c1*hwCorr(ti0)]
        //
        //  With r0 and c2[i] on the AAD tape, a bump to any outer curve
        //  knot propagates through every short-rate realisation r_k = c1^k*r0
        //  + sum c1^(k-j)*c2[j] into the accumulated path numeraire, giving
        //  the full HW IR Greek instead of only the logA_i contribution.
        // -----------------------------------------------------------------
        template <class U>
        void computeOnTapeParts(
            const YieldCurve<U>& curve,
            U&                   r0Out,
            std::vector<U>&      c2Out) const
        {
            using std::log;

            const size_t nSteps = stepTimes.size();

            // r0 = -ln D^M(dt) / dt
            r0Out = -log(curve.discount(dt)) / U(dt);

            // c2[i] on tape
            // hwCorr(t) = sigma^2/(2a^2) * (1-exp(-at))^2
            const double s2_2a2 = sigma * sigma / (2.0 * a * a);
            auto hwCorr = [this, s2_2a2](double t) -> double {
                const double ex = 1.0 - std::exp(-a * t);
                return s2_2a2 * ex * ex;
            };

            c2Out.resize(nSteps);
            for (size_t i = 0; i < nSteps; ++i) {
                const double ti0 = stepTimes[i];
                const double ti1 = ti0 + dt;
                const double ti2 = ti1 + dt;

                // ln D at three consecutive times (on tape)
                const U lnD0 = log(curve.discount(ti0));
                const U lnD1 = log(curve.discount(ti1));
                const U lnD2 = log(curve.discount(ti2));

                const double hwCorrDiff = hwCorr(ti1) - c1 * hwCorr(ti0);
                c2Out[i] = (U(1.0 + c1) * lnD1 - lnD2 - U(c1) * lnD0) / U(dt)
                           + U(hwCorrDiff);
            }
        }
    };

} // namespace ActuaLib
