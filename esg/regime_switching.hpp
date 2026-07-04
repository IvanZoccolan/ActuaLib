#pragma once

/*
    Regime-Switching GBM generator for real-world (P-measure) equity scenarios.

    Implements a 2-state Markov-modulated GBM.  Each month the state
    transitions according to a 2×2 Markov matrix:

         Regime 1 (Bull)  →  Regime 1 with prob (1 − p12 * dt)
                          →  Regime 2 with prob  p12 * dt
         Regime 2 (Bear)  →  Regime 1 with prob  p21 * dt
                          →  Regime 2 with prob (1 − p21 * dt)

    Within each regime r = {1, 2} the d-dimensional return is:

        Z   ~ N(0, I_d)
        eps = L_r × Z            (cholesky of covariance matrix for regime r)

        growthFactor_k = exp[ (mu_r[k] − 0.5 * sigma_r[k]²) * dt
                              + sigma_r[k] * sqrt(dt) * eps_k ]

    Covariance matrices:
        Σ_r[i][j] = sigma_r[i] * sigma_r[j] * correlation[i][j]

    Generation is embarrassingly parallel (independent paths).
    Uses the project RNG abstraction and ThreadPool pattern from the
    Monte Carlo engine (thread-local RNG clones + skipTo for batches).

    Reference architecture: docs/nested_stochastic_architecture.md
*/

#include "rw_scenarios.hpp"
#include "../math/matrix.hpp"
#include "../math/cholesky.hpp"
#include "../math/randomnumbers/mrg32k3a.hpp"
#include "../math/randomnumbers/gaussians.hpp"
#include "../concurrency/threadpool.hpp"

#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>

#define RW_BATCHSIZE 1024

namespace ActuaLib {

    // ======================================================================
    //  Regime-switching parameters
    // ======================================================================

    struct RSParams {
        size_t nIndices = 1;        // number of equity indices / assets

        // Per-regime drift and volatility (each has nIndices elements)
        std::vector<double> mu1;    // annual drift,    regime 1 (bull)
        std::vector<double> sigma1; // annual vol,      regime 1
        std::vector<double> mu2;    // annual drift,    regime 2 (bear)
        std::vector<double> sigma2; // annual vol,      regime 2

        // d × d correlation matrix (same for both regimes for simplicity)
        // corrMatrix[i * nIndices + j] = ρ_{ij}
        std::vector<double> correlation;  // flat row-major, d×d

        double p12 = 0.1;   // annualised transition rate regime 1 → 2
        double p21 = 0.5;   // annualised transition rate regime 2 → 1

        // Optional: initial regime probability  (0.0 → start in regime 1)
        double initProb2 = 0.0;   // probability of starting in regime 2

        // Validate basic consistency
        void validate() const
        {
            if (mu1.size() != nIndices) throw std::runtime_error("RSParams: mu1 size mismatch");
            if (sigma1.size() != nIndices) throw std::runtime_error("RSParams: sigma1 size mismatch");
            if (mu2.size() != nIndices) throw std::runtime_error("RSParams: mu2 size mismatch");
            if (sigma2.size() != nIndices) throw std::runtime_error("RSParams: sigma2 size mismatch");
            if (correlation.size() != nIndices * nIndices)
                throw std::runtime_error("RSParams: correlation matrix size mismatch");
            if (p12 < 0.0 || p21 < 0.0)
                throw std::runtime_error("RSParams: transition rates must be non-negative");
        }
    };


    // ======================================================================
    //  RegimeSwitchingGenerator
    // ======================================================================

    class RegimeSwitchingGenerator {
    public:

        explicit RegimeSwitchingGenerator(const RSParams& params)
            : myParams(params)
        {
            myParams.validate();
            buildCholeskyFactors();
        }

        // ------------------------------------------------------------------
        //  generate()  —  produce nPaths × nSteps scenario set
        //
        //  Each path is generated independently (embarrassingly parallel).
        //  seed is used as the base: path p uses seed + p.
        // ------------------------------------------------------------------
        RWScenarioSet generate(size_t nPaths, size_t nSteps,
                               uint64_t seed = 42) const
        {
            RWScenarioSet rws(nPaths, nSteps, myParams.nIndices);

            const size_t d     = myParams.nIndices;
            const double dt    = 1.0 / 12.0;  // monthly steps
            const double sqrtDt = std::sqrt(dt);

            // Monthly transition probabilities (from annualised rates)
            const double prob12 = myParams.p12 * dt;
            const double prob21 = myParams.p21 * dt;

            // Cholesky factors (precomputed)
            const Matrix<double>& L1 = myChol1;
            const Matrix<double>& L2 = myChol2;

            // Per-regime pre-computed drift adjustments:
            //   driftAdj_r[k] = (mu_r[k] - 0.5 * sigma_r[k]^2) * dt
            std::vector<double> driftAdj1(d), driftAdj2(d);
            for (size_t k = 0; k < d; ++k) {
                driftAdj1[k] = (myParams.mu1[k]
                    - 0.5 * myParams.sigma1[k] * myParams.sigma1[k]) * dt;
                driftAdj2[k] = (myParams.mu2[k]
                    - 0.5 * myParams.sigma2[k] * myParams.sigma2[k]) * dt;
            }

            ThreadPool* pool = ThreadPool::getInstance();
            const size_t nThreads = pool->numThreads();
            const size_t rngDim = d + 1;              // one uniform for regime + d uniforms for normals
            const size_t drawsPerPath = nSteps + 1;   // initial regime draw + one draw vector per month

            mrg32k3a baseRng(static_cast<unsigned>(seed), static_cast<unsigned>(seed + 1));

            std::vector<std::vector<double>> uVecs(nThreads + 1);
            std::vector<std::vector<double>> zVecs(nThreads + 1);
            std::vector<std::unique_ptr<RNG>> rngs(nThreads + 1);
            for (size_t i = 0; i <= nThreads; ++i) {
                uVecs[i].resize(rngDim);
                zVecs[i].resize(d);
                rngs[i] = baseRng.clone();
                rngs[i]->init(rngDim);
            }

            std::vector<TaskHandle> futures;
            futures.reserve(nPaths / RW_BATCHSIZE + 1);

            size_t firstPath = 0;
            size_t pathLeft = nPaths;
            while (pathLeft > 0) {
                const size_t batchSize = std::min(pathLeft, static_cast<size_t>(RW_BATCHSIZE));
                futures.push_back(pool->spawnTask(
                    [&, firstPath, batchSize]() mutable {
                        const size_t threadId = ThreadPool::threadNum();
                        auto& rng = rngs[threadId];
                        auto& uVec = uVecs[threadId];
                        auto& zVec = zVecs[threadId];

                        rng->skipTo(static_cast<unsigned>(firstPath * drawsPerPath));

                        for (size_t local = 0; local < batchSize; ++local) {
                            const size_t p = firstPath + local;

                            // Initial regime draw
                            rng->nextU(uVec);
                            int regime = (myParams.initProb2 > 0.0 && uVec[0] < myParams.initProb2) ? 2 : 1;

                            for (size_t t = 1; t <= nSteps; ++t) {
                                rng->nextU(uVec);

                                // Markov transition
                                const double u = uVec[0];
                                if (regime == 1) {
                                    if (u < prob12) regime = 2;
                                } else {
                                    if (u < prob21) regime = 1;
                                }

                                // Convert uniforms to independent standard normals
                                for (size_t k = 0; k < d; ++k) {
                                    zVec[k] = invNormalCdf(uVec[k + 1]);
                                }

                                // Correlated normals via covariance Cholesky: eps = L * Z
                                const Matrix<double>& L = (regime == 1) ? L1 : L2;
                                const auto& driftAdj = (regime == 1) ? driftAdj1 : driftAdj2;

                                for (size_t k = 0; k < d; ++k) {
                                    double eps = 0.0;
                                    for (size_t j = 0; j <= k; ++j) {
                                        eps += L[k][j] * zVec[j];
                                    }
                                    const double gf = std::exp(driftAdj[k] + sqrtDt * eps);
                                    rws(p, t, k) = gf;
                                }
                            }
                        }
                        return true;
                    }));

                firstPath += batchSize;
                pathLeft -= batchSize;
            }

            for (auto& f : futures) pool->activeWait(f);

            return rws;
        }

        const RSParams& params() const { return myParams; }

    private:
        RSParams         myParams;
        Matrix<double>   myChol1;
        Matrix<double>   myChol2;

        // Build the Cholesky factor of the per-regime covariance matrices
        // Σ_r[i][j] = sigma_r[i] * sigma_r[j] * rho[i][j]
        void buildCholeskyFactors()
        {
            const size_t d = myParams.nIndices;
            Matrix<double> cov1(d, d), cov2(d, d);
            for (size_t i = 0; i < d; ++i) {
                for (size_t j = 0; j < d; ++j) {
                    double rho = myParams.correlation[i * d + j];
                    cov1[i][j] = myParams.sigma1[i] * myParams.sigma1[j] * rho;
                    cov2[i][j] = myParams.sigma2[i] * myParams.sigma2[j] * rho;
                }
            }
            myChol1 = cholesky(cov1);
            myChol2 = cholesky(cov2);
        }
    };

} // namespace ActuaLib
