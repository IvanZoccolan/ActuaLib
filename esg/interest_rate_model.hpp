#pragma once

/*
    Interest Rate Scenario Generator — AIRG (Academy Interest Rate Generator)

    The AIRG is the regulatory scenario tool developed by the American Academy
    of Actuaries' Economic Scenario Work Group (ESWG).  It is used for
    C3 Phase I/II/III and VM-20 regulatory capital calculations in the United
    States.  Support: ESGhelp@SOA.org.  Workbooks: actuary.org.

    Three state variables evolve monthly:
      - Log-volatility (mean-reverting AR(1)):
        ln σₜ₊₁ = (1−β₃)ln σₜ + β₃ln τ₃ + σ₃ε₃,ₜ
      
      - Log-long-rate (mean-reverting, capped/floored):
        ln r₁,ₜ₊₁ = cap[ (1−β₁)ln r₁,ₜ + β₁ln τ₁ + ψ(τ₂ − dₜ) ] + e^(ln σₜ₊₁) ε₁,ₜ
      
      - Spread (short rate recovered as residual):
        dₜ₊₁ = (1−β₂)dₜ + β₂τ₂ + φ(ln r₁,ₜ − ln τ₁) + σ₂ε₂,ₜ r₁,ₜ₊₁^θ
    
    Three correlated shocks (ε₁, ε₂, ε₃) via 3×3 Cholesky factor.
    Nelson-Siegel 2-point interpolation generates full yield curve from short/long rates.
    Spot rates bootstrapped on demand.
    
    Generation is parallelised per scenario (outer loop), using project RNG abstraction.
*/

#include "yieldcurve.hpp"
#include "../math/matrix.hpp"
#include "../math/cholesky.hpp"
#include "../math/randomnumbers/genericrng.hpp"
#include "../math/randomnumbers/gaussians.hpp"
#include "../concurrency/threadpool.hpp"

#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <memory>

namespace ActuaLib {

    // ======================================================================
    //  Interest Rate Model Parameters
    // ======================================================================

    struct IRModelParams {
        // Mean-reversion levels (long-run rates)
        double tau1 = 0.04;    // long rate target
        double tau2 = 0.03;    // spread target
        double tau3 = 0.01;    // volatility target (baseline)

        // Mean-reversion speeds
        double beta1 = 0.1;    // long rate reversion
        double beta2 = 0.15;   // spread reversion
        double beta3 = 0.05;   // volatility reversion

        // Volatility parameters
        double sigma1 = 0.05;  // long rate shock magnitude
        double sigma2 = 0.02;  // spread shock magnitude
        double sigma3 = 0.20;  // volatility-of-vol

        // Curvature and dampening parameters
        double phi   = 0.1;    // spread sensitivity to level
        double psi   = 0.5;    // long-rate sensitivity to spread
        double theta = 0.5;    // rate elasticity in spread equation

        // Rate bounds
        double minLongRate  = 0.001;  // 0.1%
        double maxLongRate  = 0.15;   // 15%
        double minShortRate = 0.001;  // 0.1%
        double maxShortRate = 0.25;   // 25%

        // Volatility bounds (applied to exp(logVol))
        double minVol = 1.0e-4;
        double maxVol = 0.50;

        // Correlations between the three shocks
        double rho12 = 0.3;    // long-rate / spread correlation
        double rho13 = 0.1;    // long-rate / vol correlation
        double rho23 = -0.2;   // spread / vol correlation

        void validate() const
        {
            if (tau1 <= 0 || tau2 <= 0 || tau3 <= 0)
                throw std::runtime_error("IRModelParams: tau values must be positive");
            if (beta1 < 0 || beta2 < 0 || beta3 < 0)
                throw std::runtime_error("IRModelParams: beta values must be non-negative");
            if (sigma1 <= 0 || sigma2 <= 0 || sigma3 <= 0)
                throw std::runtime_error("IRModelParams: sigma values must be positive");
            if (minLongRate <= 0 || maxLongRate <= minLongRate)
                throw std::runtime_error("IRModelParams: long rate bounds invalid");
            if (minShortRate <= 0)
                throw std::runtime_error("IRModelParams: minShortRate must be positive");
            if (maxShortRate <= minShortRate)
                throw std::runtime_error("IRModelParams: short rate bounds invalid");
            if (minVol <= 0 || maxVol <= minVol)
                throw std::runtime_error("IRModelParams: volatility bounds invalid");
        }
    };

    // ======================================================================
    //  Interest Rate Scenario (one path)
    // ======================================================================

    struct IRScenario {
        std::vector<YieldCurve<double>> curves;  // [t=0..nMonths]
        std::vector<double> logVols;             // [t=0..nMonths]
        std::vector<double> shortRates;          // [t=0..nMonths]
        std::vector<double> longRates;           // [t=0..nMonths]
    };

    // ======================================================================
    //  Interest Rate Scenario Generator
    // ======================================================================

    class InterestRateScenarioGenerator {
    public:

        explicit InterestRateScenarioGenerator(const IRModelParams& params)
            : myParams(params)
        {
            myParams.validate();
            buildCholeskyFactor();
            precomputeConstants();
        }

        // ------------------------------------------------------------------
        //  generate() — produce nScenarios scenarios, each with nMonths steps
        //
        //  Initial short and long rates seeded from historical curve.
        // ------------------------------------------------------------------
        std::vector<IRScenario> generate(
            size_t nScenarios,
            size_t nMonths,
            double initialShortRate,
            double initialLongRate,
            double initialVol,
            const RNG& rng,
            uint64_t baseSeed = 42) const
        {
            std::vector<IRScenario> scenarios(nScenarios);

            ThreadPool* pool = ThreadPool::getInstance();
            const size_t nThreads = pool->numThreads();
            std::vector<std::unique_ptr<RNG>> rngs(nThreads + 1);

            for (size_t i = 0; i <= nThreads; ++i) {
                rngs[i] = rng.clone();
                rngs[i]->init(3);  // 3D random numbers
            }

            std::vector<TaskHandle> futures;
            futures.reserve(nScenarios);

            for (size_t s = 0; s < nScenarios; ++s) {
                futures.push_back(pool->spawnTask(
                    [&, s, nMonths, initialShortRate, initialLongRate, initialVol]() mutable {
                        const size_t threadId = ThreadPool::threadNum();
                        auto& rng = rngs[threadId];

                        rng->skipTo(static_cast<unsigned>(baseSeed + s * nMonths));
                        scenarios[s] = generateSingleScenario(
                            nMonths, initialShortRate, initialLongRate, initialVol, *rng);

                        return true;
                    }));
            }

            for (auto& f : futures)
                pool->activeWait(f);

            return scenarios;
        }

        const IRModelParams& params() const { return myParams; }

    private:
        IRModelParams       myParams;
        Matrix<double>      myChol;      // 3x3 Cholesky factor for shocks
        double              myConst1, myConst2, myConst3, myConst4, myConst5;

        void buildCholeskyFactor()
        {
            // 3×3 correlation matrix of shocks
            Matrix<double> corrMat(3, 3);
            corrMat[0][0] = 1.0;
            corrMat[0][1] = myParams.rho12;
            corrMat[0][2] = myParams.rho13;
            corrMat[1][0] = myParams.rho12;
            corrMat[1][1] = 1.0;
            corrMat[1][2] = myParams.rho23;
            corrMat[2][0] = myParams.rho13;
            corrMat[2][1] = myParams.rho23;
            corrMat[2][2] = 1.0;

            myChol = cholesky(corrMat);
        }

        void precomputeConstants()
        {
            myConst1 = myParams.beta1 * std::log(myParams.tau1);
            myConst2 = myParams.beta2 * myParams.tau2;
            myConst3 = myParams.beta3 * std::log(myParams.tau3);
            myConst4 = myParams.sigma3 * myParams.sigma3;  // for vol dynamics
            myConst5 = myParams.sigma1 * myParams.sigma1;  // for vol scaling
        }

        IRScenario generateSingleScenario(
            size_t nMonths,
            double initialShortRate,
            double initialLongRate,
            double initialVol,
            RNG& rng) const
        {
            const double dt = 1.0 / 12.0;  // monthly time step
            const double sqrtDt = std::sqrt(dt);

            IRScenario scenario;
            scenario.curves.resize(nMonths + 1);
            scenario.logVols.resize(nMonths + 1);
            scenario.shortRates.resize(nMonths + 1);
            scenario.longRates.resize(nMonths + 1);

            // Initial state
            double oldShortRate = std::max(initialShortRate, myParams.minShortRate);
            double oldLongRate = std::max(initialLongRate, myParams.minLongRate);
            oldLongRate = std::min(oldLongRate, myParams.maxLongRate);
            double oldLogVol = std::log(initialVol);
            double oldLogLongRate = std::log(oldLongRate);
            double oldDiff = oldLongRate - oldShortRate;

            // Build initial curve
            scenario.curves[0] = buildNelsonSiegelCurve(oldShortRate, oldLongRate);
            scenario.logVols[0] = oldLogVol;
            scenario.shortRates[0] = oldShortRate;
            scenario.longRates[0] = oldLongRate;

            const double logMinLongRate = std::log(myParams.minLongRate);
            const double logMaxLongRate = std::log(myParams.maxLongRate);
            const double logMinVol = std::log(myParams.minVol);
            const double logMaxVol = std::log(myParams.maxVol);

            // Monthly loop
            std::vector<double> randNums(3);
            for (size_t t = 1; t <= nMonths; ++t) {
                rng.nextG(randNums);

                // Correlate shocks via Cholesky
                double eps1 = myChol[0][0] * randNums[0];
                double eps2 = myChol[1][0] * randNums[0] + myChol[1][1] * randNums[1];
                double eps3 = myChol[2][0] * randNums[0] + myChol[2][1] * randNums[1] + myChol[2][2] * randNums[2];

                // Update log-volatility (AR(1), unbounded)
                double newLogVol = (1.0 - myParams.beta3) * oldLogVol
                                 + myConst3
                                 + myParams.sigma3 * eps3;
                newLogVol = std::max(newLogVol, logMinVol);
                newLogVol = std::min(newLogVol, logMaxVol);

                // Update log-long-rate (mean-reverting, capped/floored)
                double newLogLongRate = (1.0 - myParams.beta1) * oldLogLongRate
                                      + myConst1
                                      + myParams.psi * (myParams.tau2 - oldDiff);
                newLogLongRate += std::exp(newLogVol) * eps1;
                newLogLongRate = std::max(newLogLongRate, logMinLongRate);
                newLogLongRate = std::min(newLogLongRate, logMaxLongRate);

                double newLongRate = std::exp(newLogLongRate);
                if (!std::isfinite(newLongRate)) {
                    newLongRate = myParams.maxLongRate;
                    newLogLongRate = logMaxLongRate;
                }

                // Update spread
                double newDiff = (1.0 - myParams.beta2) * oldDiff
                               + myConst2
                               + myParams.phi * (oldLogLongRate - std::log(myParams.tau1))
                               + myParams.sigma2 * eps2 * std::pow(newLongRate, myParams.theta);
                if (!std::isfinite(newDiff)) {
                    newDiff = 0.0;
                }

                double newShortRate = newLongRate - newDiff;
                newShortRate = std::max(newShortRate, myParams.minShortRate);
                newShortRate = std::min(newShortRate, myParams.maxShortRate);
                if (!std::isfinite(newShortRate)) {
                    newShortRate = myParams.maxShortRate;
                }

                // Store state and build curve
                scenario.curves[t] = buildNelsonSiegelCurve(newShortRate, newLongRate);
                scenario.logVols[t] = newLogVol;
                scenario.shortRates[t] = newShortRate;
                scenario.longRates[t] = newLongRate;

                // Update for next iteration
                oldShortRate = newShortRate;
                oldLongRate = newLongRate;
                oldLogLongRate = newLogLongRate;
                oldDiff = newDiff;
                oldLogVol = newLogVol;
            }

            return scenario;
        }

        // Build Nelson-Siegel yield curve from short and long rates
        YieldCurve<double> buildNelsonSiegelCurve(double shortRate, double longRate) const
        {
            const double k = 0.4;
            const std::vector<double> maturities = {0.25, 0.5, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 20.0, 30.0};

            std::vector<double> rates(10);
            const double const1 = (1.0 - std::exp(-k * 1.0)) / (k * 1.0);
            const double const20 = (1.0 - std::exp(-k * 20.0)) / (k * 20.0);

            double b1 = (shortRate - longRate) / (const1 - const20);
            double b0 = shortRate - b1 * const1;

            for (size_t i = 0; i < 10; ++i) {
                double t = maturities[i];
                rates[i] = b0 + b1 * (1.0 - std::exp(-k * t)) / (k * t);
                rates[i] = std::max(rates[i], 0.0001);  // floor at 0.01%
            }

            YieldCurve<double> curve;
            std::vector<double> curveTenors;
            std::vector<double> dfs;
            curveTenors.reserve(maturities.size() + 1);
            dfs.reserve(maturities.size() + 1);

            curveTenors.push_back(0.0);
            dfs.push_back(1.0);

            for (size_t i = 0; i < 10; ++i) {
                double y = rates[i];
                double t = maturities[i];
                curveTenors.push_back(t);
                dfs.push_back(std::exp(-y * t));
            }

            curve.setDiscountCurve(curveTenors, dfs);
            return curve;
        }
    };

} // namespace ActuaLib
