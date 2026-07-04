#pragma once

/*
    Nested stochastic simulation for a single VA product over a single
    outer real-world aging period.

    Design:
      - Outer loop only is parallelised, following the resource pattern of
        mcParallelSimulation(): batched tasks, one RNG/tape slot per thread,
        and activeWait() on the project ThreadPool singleton.
      - Inner pricing is always serial on the same worker thread via
        mcSimulationAAD().
      - The product is passed directly as a concrete VABase-derived object;
        the same product type is rebuilt on the inner loop after aging the
        policy along the outer RW path.
      - The interest-rate curve is identical across all outer scenarios for now.

    Usage example:
      GMAB<double> outerProduct(policy, mortality);
      auto res = NestedSimulation::runSinglePeriod<GMAB>(
          config, outerProduct, innerRng, 24);
*/

#include "mcsimulationAAD.hpp"
#include "../esg/multivariate_blackscholes.hpp"
#include "../esg/hull_white_blackscholes.hpp"
#include "../esg/regime_switching.hpp"
#include "../esg/rw_scenarios.hpp"
#include "../esg/yieldcurve.hpp"
#include "../esg/interest_rate_model.hpp"
#include "../esg/hull_white_1f.hpp"
#include "../products/va_base.hpp"
#include "../math/matrix.hpp"
#include "../math/randomnumbers/genericrng.hpp"
#include "../concurrency/threadpool.hpp"

#include <vector>
#include <memory>
#include <cstddef>
#include <stdexcept>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <type_traits>

namespace ActuaLib {

    struct NestedSimConfig {
        // --- RW scenario generation (equity) --------------------------------
        size_t              nRWPaths;         // number of outer P-measure paths
        RSParams            rsParams;         // regime-switching model
        uint64_t            rwSeed = 42;      // seed for RW generator

        // --- Interest rate generation — AIRG outer P-measure model (AAA ESWG) --
        IRModelParams       irParams;         // AIRG: 3-factor log-normal (long rate, spread, stoch. vol)
        double              initialIRShort = 0.02;
        double              initialIRLong = 0.04;
        double              initialIRVol = 0.01;
        uint64_t            irSeed = 100;     // seed for IR generator

        // --- Initial market state -----------------------------------------
        std::vector<double> initialSpots;     // S_k(0)

        // --- Inner (Q-measure) model parameters ---------------------------
        std::vector<double> vols;             // off-tape implied vols
        std::vector<double> divs;             // continuous dividend yields
        Matrix<double>      corrMatrix;       // off-tape correlation matrix

        // --- Inner MC settings --------------------------------------------
        size_t nRNPaths = 1000;
        bool   useNSTail = true;         // keep 10-knot risks, improve tail discounting

        // --- Inner interest-rate model ------------------------------------
        // When false (default): static outer yield curve used for discounting
        //   (MultivariateBlackScholes, same behaviour as before).
        // When true: Hull-White one-factor model calibrated to the outer curve
        //   simulates stochastic inner rates (HullWhiteBlackScholes).  The
        //   path-dependent numeraire and realised-rate equity drift capture
        //   interest-rate risk in the inner pricing loop.
        bool       useHullWhiteInner = false;
        HW1FParams hwParams;             // a = 0.05, sigma = 0.01 by default

        // --- Outer batching controls --------------------------------------
        size_t outerBatchMax = 256;
        size_t outerBatchTargetInnerPaths = 256 * 1000;
        size_t outerBatchSinglePathInnerThreshold = 64 * 1000;
        size_t outerBatchTargetTasksPerThread = 4;

        void validate() const
        {
            const size_t d = initialSpots.size();
            if (d == 0)
                throw std::runtime_error("NestedSimConfig: empty initialSpots");
            if (vols.size() != d)
                throw std::runtime_error("NestedSimConfig: vols size != nIndices");
            if (divs.size() != d)
                throw std::runtime_error("NestedSimConfig: divs size != nIndices");
            if (corrMatrix.rows() != d || corrMatrix.cols() != d)
                throw std::runtime_error("NestedSimConfig: corrMatrix size mismatch");
            if (initialIRShort <= 0 || initialIRLong <= 0 || initialIRVol <= 0)
                throw std::runtime_error("NestedSimConfig: initial IR rates must be positive");
            rsParams.validate();
            irParams.validate();
            if (rsParams.nIndices != d)
                throw std::runtime_error("NestedSimConfig: rsParams.nIndices mismatch");
            if (nRNPaths == 0)
                throw std::runtime_error("NestedSimConfig: nRNPaths must be >= 1");
            if (useHullWhiteInner)
                hwParams.validate();
            if (outerBatchMax == 0)
                throw std::runtime_error("NestedSimConfig: outerBatchMax must be >= 1");
            if (outerBatchTargetInnerPaths == 0)
                throw std::runtime_error("NestedSimConfig: outerBatchTargetInnerPaths must be >= 1");
            if (outerBatchSinglePathInnerThreshold == 0)
                throw std::runtime_error("NestedSimConfig: outerBatchSinglePathInnerThreshold must be >= 1");
            if (outerBatchTargetTasksPerThread == 0)
                throw std::runtime_error("NestedSimConfig: outerBatchTargetTasksPerThread must be >= 1");
        }
    };

    struct NestedResult {
        double              fmv = 0.0;
        std::vector<double> dollarDeltas;
        std::vector<double> rho;
        YieldCurve<double>  irCurve;          // yield curve at valuation timestep
        std::vector<double> outerSpots;       // equity spot prices at outer pricing date
        std::vector<double> outerDiscFactors; // IR discount factors (excl. t=0 anchor)
        std::vector<double> outerFundValues;  // aged fund values at outer pricing date
    };

    class NestedSimulation {
    public:
        template <template<class> class VAProduct>
        static std::vector<NestedResult> runSinglePeriod(
            const NestedSimConfig&     config,
            const VAProduct<double>&   outerProduct,
            const RNG&                 equityRng,
            const RNG&                 rateRng,
            size_t                     outerMonths,
            const RWScenarioSet*       preGeneratedRW = nullptr)
        {
            static_assert(std::is_base_of_v<LifeProduct<double>, VAProduct<double>>,
                "VAProduct<double> must derive from LifeProduct<double>");
            return runSinglePeriod(
                config,
                outerProduct,
                equityRng,
                rateRng,
                outerMonths,
                [](const std::vector<Number>& pays) { return pays[3]; },
                preGeneratedRW);
        }

        template <template<class> class VAProduct, class FAgg>
        static std::vector<NestedResult> runSinglePeriod(
            const NestedSimConfig&     config,
            const VAProduct<double>&   outerProduct,
            const RNG&                 equityRng,
            const RNG&                 rateRng,
            size_t                     outerMonths,
            const FAgg&                fmvAggregator,
            const RWScenarioSet*       preGeneratedRW = nullptr)
        {
            static_assert(std::is_base_of_v<LifeProduct<double>, VAProduct<double>>,
                "VAProduct<double> must derive from LifeProduct<double>");
            config.validate();
            if (outerMonths == 0)
                throw std::runtime_error("NestedSimulation: outerMonths must be >= 1");
            if (outerMonths > static_cast<size_t>(outerProduct.policy().numMonths)) {
                throw std::runtime_error(
                    "NestedSimulation: outerMonths exceeds policy remaining horizon");
            }

            // Generate equity RW scenarios
            const RWScenarioSet* rwPtr = nullptr;
            RWScenarioSet generatedRW;
            if (preGeneratedRW) {
                if (preGeneratedRW->nPaths() < config.nRWPaths)
                    throw std::runtime_error("NestedSimulation: preGeneratedRW has too few paths");
                if (preGeneratedRW->nSteps() < outerMonths)
                    throw std::runtime_error("NestedSimulation: preGeneratedRW has too few steps");
                rwPtr = preGeneratedRW;
            } else {
                RegimeSwitchingGenerator rsGen(config.rsParams);
                generatedRW = rsGen.generate(config.nRWPaths, outerMonths, config.rwSeed);
                rwPtr = &generatedRW;
            }

            // Generate interest rate scenarios
            InterestRateScenarioGenerator irGen(config.irParams);
            auto irScenarios = irGen.generate(
                config.nRWPaths,
                outerMonths,
                config.initialIRShort,
                config.initialIRLong,
                config.initialIRVol,
                rateRng,
                config.irSeed);

            const size_t d = config.initialSpots.size();
            const size_t nCurve = irScenarios[0].curves[0].tenors().size() - 1;

            std::vector<NestedResult> results(config.nRWPaths);
            for (auto& r : results) {
                r.dollarDeltas.assign(d, 0.0);
                r.rho.assign(nCurve, 0.0);
            }

            runOuterParallel<VAProduct>(
                config, outerProduct, equityRng, *rwPtr, irScenarios, outerMonths,
                fmvAggregator, results);

            return results;
        }

    private:
        static size_t chooseOuterBatchSize(
            const NestedSimConfig& config,
            size_t totalPaths,
            size_t innerPaths,
            size_t nThreads)
        {
            if (totalPaths == 0) {
                return 1;
            }

            if (innerPaths >= config.outerBatchSinglePathInnerThreshold) {
                return 1;
            }

            const size_t safeInnerPaths = std::max<size_t>(innerPaths, 1);
            const size_t safeThreads = std::max<size_t>(nThreads, 1);
            const size_t desiredTasks = std::min(
                totalPaths,
                safeThreads * config.outerBatchTargetTasksPerThread);

            const size_t batchForParallelism = std::max<size_t>(
                1,
                (totalPaths + desiredTasks - 1) / desiredTasks);
            const size_t batchForWork = std::max<size_t>(
                1,
                config.outerBatchTargetInnerPaths / safeInnerPaths);

            return std::min({
                totalPaths,
                config.outerBatchMax,
                batchForParallelism,
                batchForWork,
            });
        }

        static std::unique_ptr<LifeModel<Number>> buildInnerModel(
            const NestedSimConfig& config,
            const RWScenarioSet&   rw,
            const IRScenario&      irScen,
            size_t                 p,
            size_t                 t)
        {
            const size_t d = config.initialSpots.size();

            std::vector<Number> spotsT(d);
            for (size_t k = 0; k < d; ++k)
                spotsT[k] = Number(rw.spotAt(p, t, k, config.initialSpots[k]));

            std::vector<Number> divsN(config.divs.begin(), config.divs.end());

            // Convert the outer yield curve at time t to Number for AAD
            const YieldCurve<double>& curveDouble = irScen.curves[t];
            const auto& tenors = curveDouble.tenors();
            const auto& dfs    = curveDouble.discountFactors();

            std::vector<Number> dfsN;
            dfsN.reserve(dfs.size());
            for (double df : dfs)
                dfsN.emplace_back(df);

            YieldCurve<Number> curveN;
            curveN.setDiscountCurve(tenors, dfsN);

            if (config.useHullWhiteInner) {
                // Hull-White one-factor inner model:
                // The outer AIRG curve becomes the initial term structure.
                // HW is calibrated to it exactly; stochastic r_t drives the
                // path-dependent numeraire and equity drift inside the inner loop.
                return std::make_unique<HullWhiteBlackScholes<Number>>(
                    std::move(spotsT),
                    std::move(divsN),
                    config.vols,
                    config.corrMatrix,
                    curveN,
                    config.hwParams);
            }

            // Default: static outer curve (MultivariateBlackScholes)
            const double nsShortRate = irScen.shortRates[t];
            const double nsLongRate  = irScen.longRates[t];

            return std::make_unique<MultivariateBlackScholes<Number>>(
                std::move(spotsT),
                std::move(divsN),
                config.vols,
                config.corrMatrix,
                curveN,
                config.useNSTail,
                nsShortRate,
                nsLongRate);
        }

        static NestedResult extractResult(
            const LifeAADResults& aar,
            const NestedSimConfig& config,
            const RWScenarioSet&   rw,
            const IRScenario&      irScen,
            size_t                 p,
            size_t                 t,
            const VAPolicy&        agedPolicy)
        {
            const size_t d = config.initialSpots.size();
            const size_t nCurve = irScen.curves[0].tenors().size() - 1;

            NestedResult res;
            double sum = 0.0;
            for (double v : aar.pathLiability) sum += v;
            res.fmv = sum / static_cast<double>(aar.pathLiability.size());

            res.dollarDeltas.resize(d);
            res.outerSpots.resize(d);
            for (size_t k = 0; k < d; ++k) {
                const double spotT = rw.spotAt(p, t, k, config.initialSpots[k]);
                res.dollarDeltas[k] = aar.parameterSensitivities[k] * spotT;
                res.outerSpots[k]   = spotT;
            }

            res.rho.resize(nCurve);
            for (size_t i = 0; i < nCurve; ++i) {
                res.rho[i] = aar.parameterSensitivities[2 * d + i];
            }

            res.irCurve = irScen.curves[t];

            // Discount factors at outer pricing date (skip the t=0 anchor at index 0)
            const auto& dfsDouble = irScen.curves[t].discountFactors();
            res.outerDiscFactors.assign(dfsDouble.begin() + 1, dfsDouble.end());

            // Fund values after aging along the outer path
            res.outerFundValues = agedPolicy.fundValues;

            return res;
        }

        template <template<class> class VAProduct, class FAgg>
        static void runOuterParallel(
            const NestedSimConfig&           config,
            const VAProduct<double>&         outerProduct,
            const RNG&                       innerRng,
            const RWScenarioSet&             rw,
            const std::vector<IRScenario>&   irScenarios,
            size_t                           outerMonths,
            const FAgg&                      fmvAggregator,
            std::vector<NestedResult>&       results)
        {
            ThreadPool* pool = ThreadPool::getInstance();
            const size_t nThreads = pool->numThreads();
            std::vector<Tape> taskTapes(nThreads + 1);

            const size_t totalPaths = config.nRWPaths;
            const size_t updateEvery = std::max<size_t>(1, totalPaths / 40);
            std::atomic<size_t> completed{0};
            std::atomic<size_t> lastPrinted{0};
            std::atomic<long long> totalInnerMicros{0};

            auto printProgress = [&](size_t done) {
                const size_t barWidth = 36;
                const double progress = static_cast<double>(done) / static_cast<double>(totalPaths);
                const size_t filled = static_cast<size_t>(progress * static_cast<double>(barWidth));
                const double meanMs = done > 0
                    ? static_cast<double>(totalInnerMicros.load()) / (1000.0 * static_cast<double>(done))
                    : 0.0;

                std::cout << "\r  Outer progress [";
                for (size_t i = 0; i < barWidth; ++i) {
                    std::cout << (i < filled ? '=' : ' ');
                }
                std::cout << "] "
                          << std::setw(6) << std::fixed << std::setprecision(1)
                          << (100.0 * progress) << "%"
                          << "  (" << done << "/" << totalPaths << ")"
                          << "  mean inner: " << std::setprecision(2) << meanMs << " ms"
                          << std::flush;

                if (done == totalPaths) {
                    std::cout << std::endl;
                }
            };

            printProgress(0);

            std::vector<TaskHandle> futures;
            const size_t outerBatchSize = chooseOuterBatchSize(
                config,
                config.nRWPaths,
                config.nRNPaths,
                nThreads);
            futures.reserve(config.nRWPaths / outerBatchSize + 1);

            size_t firstPath = 0;
            size_t pathsLeft = config.nRWPaths;
            while (pathsLeft > 0) {
                const size_t batchSize = std::min(pathsLeft, outerBatchSize);

                futures.push_back(pool->spawnTask(
                    [&, firstPath, batchSize]() mutable {
                        const size_t threadId = ThreadPool::threadNum();
                        Number::tape = &taskTapes[threadId];

                        for (size_t i = 0; i < batchSize; ++i) {
                            const size_t p = firstPath + i;

                            VAPolicy agedPolicy = outerProduct.ageAlongPath(rw, p, outerMonths);
                            VAProduct<Number> innerProduct(agedPolicy, outerProduct.mortality());
                            auto innerModel = buildInnerModel(config, rw, irScenarios[p], p, outerMonths);
                            auto innerRngClone = innerRng.clone();

                            const auto innerStart = std::chrono::steady_clock::now();
                            auto aar = lifeSimulationAAD(
                                innerProduct,
                                *innerModel,
                                *innerRngClone,
                                config.nRNPaths,
                                fmvAggregator);
                            const auto innerEnd = std::chrono::steady_clock::now();
                            const auto innerMicros = std::chrono::duration_cast<std::chrono::microseconds>(innerEnd - innerStart).count();
                            totalInnerMicros.fetch_add(innerMicros);

                            results[p] = extractResult(aar, config, rw, irScenarios[p], p, outerMonths, agedPolicy);

                            const size_t done = completed.fetch_add(1) + 1;
                            size_t expected = lastPrinted.load();
                            if (done == totalPaths || done >= expected + updateEvery) {
                                while (done > expected) {
                                    if (lastPrinted.compare_exchange_weak(expected, done)) {
                                        printProgress(done);
                                        break;
                                    }
                                }
                            }
                        }
                        return true;
                    }));

                firstPath += batchSize;
                pathsLeft -= batchSize;
            }

            for (auto& future : futures)
                pool->activeWait(future);
        }
    };

} // namespace ActuaLib
