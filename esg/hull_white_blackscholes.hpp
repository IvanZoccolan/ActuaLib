#pragma once

/*
    HullWhiteBlackScholes<T> — inner Q-measure model combining:
      - d correlated GBM equity assets (same as MultivariateBlackScholes)
      - Hull-White one-factor stochastic short rate (calibrated to the outer curve)

    Design
    ------
    At each outer AIRG scenario node, the outer yield curve becomes the initial
    term structure for the inner HW1F model.  The short rate r_t evolves as:

        r_{i+1} = c1 * r_i + c2T[i] + c3 * Z_i^{rate}

    where c1 and c3 are off-tape doubles (depend only on HW params a and sigma)
    and c2T[i] and r0T are on-tape T-typed values produced by
    HullWhite1F::computeOnTapeParts() from the on-tape outer curve.  This means
    r is evolved on the AAD tape and IR Greeks flow through the full HW
    calibration: dFMV/dD(T_i) captures both the logA_i channel (partial,
    coefficient ~ 1-B/dt ~ 0.004) and the dominant HW-drift channel (r0 and
    theta(t) dependence on the curve, coefficient ~ 1/D ~ 1).

    The money-market numeraire B(t_m) = 1/D^path(0,t_m) is accumulated on the
    AAD tape step by step using the analytic HW one-step discount:

        P^HW(t_i, t_{i+1}) = exp(logA_i - B_hw * r_i)

    where logA_i is precomputed on tape in init() via:

        logA_i = (1 - B_hw/dt) * [ln D^M(t_{i+1}) - ln D^M(t_i)] - corrFactor_i

    The curve discount factors D^M(t) are the model parameters put on the AAD
    tape.  Adjoints flow back through r_i (via r0T and c2T) and logA_i to give
    full IR Greeks (rho sensitivities) to the outer curve knots.

    The equity drift at step i uses the realised short rate r_i (on tape),
    replacing the static-curve forward rate used in MultivariateBlackScholes.
    Dividend yields q_k are on tape.

    Parameters on tape (same layout as MultivariateBlackScholes):
        spots[d], divs[d], curveKnots[nCurve]

    Off-tape:
        vols[d], corrMatrix[d x d], HW1FParams { a, sigma }

    Simulation dimension:
        simDim() = (d + 1) * nSteps
        gaussVec[step * (d+1) + k], k = 0..d-1  →  equity draw for asset k
        gaussVec[step * (d+1) + d]               →  rate draw Z^{rate}

    Forward factors and discount mats in the Sample use the static outer curve
    (same as MultivariateBlackScholes).  The numeraire is path-dependent.

    References:
        Hull, J. and White, A. (1990).  Review of Financial Studies, 3(4):573-592.
        Hull, J. (2022).  Options, Futures and Other Derivatives, 11th ed., ch. 32.
*/

#include "model.hpp"
#include "yieldcurve.hpp"
#include "hull_white_1f.hpp"
#include "../types.hpp"
#include "../math/matrix.hpp"
#include "../math/cholesky.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace ActuaLib {

    template <class T>
    class HullWhiteBlackScholes : public Model<T> {
    private:
        // ---- dimensions ----
        size_t myNumAssets;       // d

        // ---- model parameters on AAD tape ----
        std::vector<T>  mySpots;
        std::vector<T>  myDivs;
        YieldCurve<T>   myYieldCurve;   // outer curve; knots are on tape

        // ---- off tape ----
        std::vector<double> myVols;
        Matrix<double>      myCorrMatrix;
        HW1FParams          myHWParams;
        HullWhite1F         myHW;        // precomputed off-tape HW constants

        // ---- derived (on tape, computed in init()) ----
        Matrix<T>   myCovMatrix;
        Matrix<T>   myCholesky;

        // ---- simulation timeline ----
        std::vector<Time>              myTimeline;  // today + future product dates
        bool                           myTodayOnTimeline;
        const std::vector<SampleDef>*  myDefline;

        // ---- per-step precomputed (on tape) ----
        std::vector<T>      mySqrtDts;     // sqrt(dt_i) for equity stochastic term
        std::vector<T>      myHWLogA;      // log(A_i) on tape — carries partial IR Greek
        std::vector<double> myVolSqHalf;   // 0.5 * vol_k^2 (off tape, per asset)

        // ---- on-tape HW calibration (full IR Greek via r0 + theta channel) ----
        T              myHWr0T;   // initial short rate from outer curve (on tape)
        std::vector<T> myHWc2T;  // per-step drift shift c2[i] from outer curve (on tape)

        // ---- per-event precomputed (on tape) ----
        // NOTE: numeraire is PATH-DEPENDENT → computed in generatePath(), not here
        std::vector<std::vector<T>>  myForwardFactors;  // [nEvents][d * nFwdMats]
        std::vector<std::vector<T>>  myDiscounts;        // [nEvents][nDiscMats]
        std::vector<std::vector<T>>  myLibors;           // [nEvents][nLibors]

        // ---- parameter export ----
        std::vector<T*>          myParameters;
        std::vector<std::string> myParameterLabels;

        // =================================================================
        //  Private helpers
        // =================================================================

        T curveDiscount(double t) const { return myYieldCurve.discount(t); }

        void buildParameterLabels()
        {
            const size_t d      = myNumAssets;
            const size_t nCurve = myYieldCurve.tenors().size() - 1;  // skip t=0
            myParameterLabels.resize(2 * d + nCurve);
            size_t p = 0;
            for (size_t k = 0; k < d; ++k)
                myParameterLabels[p++] = "spot_" + std::to_string(k);
            for (size_t k = 0; k < d; ++k)
                myParameterLabels[p++] = "div_"  + std::to_string(k);
            for (size_t i = 1; i <= nCurve; ++i)
                myParameterLabels[p++] = "disc_" + std::to_string(myYieldCurve.tenors()[i]);
        }

        void setParamPointers()
        {
            const size_t d      = myNumAssets;
            const size_t nCurve = myYieldCurve.tenors().size() - 1;
            myParameters.resize(2 * d + nCurve);
            size_t p = 0;
            for (size_t k = 0; k < d; ++k)
                myParameters[p++] = &mySpots[k];
            for (size_t k = 0; k < d; ++k)
                myParameters[p++] = &myDivs[k];
            auto& dfs = myYieldCurve.discountFactorsMutable();
            for (size_t i = 1; i <= nCurve; ++i)
                myParameters[p++] = &dfs[i];
        }

        // Fill one sample in the scenario path.
        // discountAcc = product of P^HW(t_j, t_{j+1}) for j=0..step-1
        //             = D^path(0, t_event) = 1/B(t_event)
        inline void fillScen(
            const size_t          evIdx,
            const std::vector<T>& spots,
            Sample<T>&            scen,
            const SampleDef&      def,
            const T&              discountAcc) const
        {
            const size_t d    = myNumAssets;
            const size_t dEff = def.numAssets;

            // Numeraire = B(t) = 1 / D^path(0, t)
            if (def.numeraire) {
                scen.numeraire = T(1.0) / discountAcc;
            }

            // Forward factors: F(t_ev, T_mat) = D^M(t_ev)/D^M(T_mat) * exp(-q_k * tau)
            // (static outer curve approximation — same as MultivariateBlackScholes V1)
            const size_t nFwds = def.forwardMats.size();
            for (size_t j = 0; j < nFwds; ++j) {
                for (size_t k = 0; k < dEff; ++k) {
                    scen.forwards[j * dEff + k] =
                        myForwardFactors[evIdx][j * d + k] * spots[k];
                }
            }

            // Discount factors (asset-independent, static outer curve)
            std::copy(myDiscounts[evIdx].begin(), myDiscounts[evIdx].end(),
                      scen.discounts.begin());

            // Libors (static outer curve)
            std::copy(myLibors[evIdx].begin(), myLibors[evIdx].end(),
                      scen.libors.begin());
        }

    public:
        // =================================================================
        //  Constructor
        // =================================================================

        HullWhiteBlackScholes(
            const std::vector<T>&      spots,
            const std::vector<T>&      divs,
            const std::vector<double>& vols,
            const Matrix<double>&      corrMatrix,
            const YieldCurve<T>&       yieldCurve,
            const HW1FParams&          hwParams)
            : myNumAssets(spots.size()),
              mySpots(spots),
              myDivs(divs),
              myVols(vols),
              myCorrMatrix(corrMatrix),
              myYieldCurve(yieldCurve),
              myHWParams(hwParams),
              myTodayOnTimeline(false),
              myDefline(nullptr)
        {
            if (divs.size() != myNumAssets)
                throw std::runtime_error(
                    "HullWhiteBlackScholes: spots/divs size mismatch");
            if (vols.size() != myNumAssets)
                throw std::runtime_error(
                    "HullWhiteBlackScholes: spots/vols size mismatch");
            if (corrMatrix.rows() != myNumAssets || corrMatrix.cols() != myNumAssets)
                throw std::runtime_error(
                    "HullWhiteBlackScholes: corrMatrix size mismatch");
            if (myYieldCurve.tenors().size() < 2)
                throw std::runtime_error(
                    "HullWhiteBlackScholes: yield curve must have at least 2 knots");
            if (std::fabs(myYieldCurve.tenors().front()) > 1.0e-12)
                throw std::runtime_error(
                    "HullWhiteBlackScholes: curve must start at t = 0");
            myHWParams.validate();
            buildParameterLabels();
            setParamPointers();
        }

        // =================================================================
        //  Accessors
        // =================================================================

        size_t           numAssets()  const { return myNumAssets; }
        const HW1FParams& hwParams()  const { return myHWParams;  }
        const HullWhite1F& hw()       const { return myHW;         }

        // =================================================================
        //  Model<T> interface
        // =================================================================

        const std::vector<T*>& parameters() const override {
            return myParameters;
        }
        const std::vector<std::string>& parameterLabels() const override {
            return myParameterLabels;
        }

        std::unique_ptr<Model<T>> clone() const override {
            auto cl = std::make_unique<HullWhiteBlackScholes<T>>(*this);
            cl->setParamPointers();
            return cl;
        }

        // -----------------------------------------------------------------
        //  allocate() — set up storage for the product's timeline
        // -----------------------------------------------------------------
        void allocate(
            const std::vector<Time>& prodTimeline,
            const std::vector<SampleDef>& defline) override
        {
            const size_t d = myNumAssets;

            myTimeline.clear();
            myTimeline.push_back(SystemTime);
            for (const auto t : prodTimeline) {
                if (t > SystemTime)
                    myTimeline.push_back(t);
            }

            myTodayOnTimeline =
                !prodTimeline.empty() &&
                std::fabs(prodTimeline[0] - SystemTime) < 1.0e-12;

            myDefline = &defline;

            const size_t nSteps  = myTimeline.size() - 1;
            const size_t nEvents = prodTimeline.size();

            mySqrtDts.resize(nSteps);
            myHWLogA.resize(nSteps);
            myHWc2T.resize(nSteps);
            myVolSqHalf.resize(d);

            myForwardFactors.resize(nEvents);
            myDiscounts.resize(nEvents);
            myLibors.resize(nEvents);

            for (size_t i = 0; i < nEvents; ++i) {
                myDiscounts[i].resize(defline[i].discountMats.size());
                myForwardFactors[i].resize(d * defline[i].forwardMats.size());
                myLibors[i].resize(defline[i].liborDefs.size());
            }
        }

        // -----------------------------------------------------------------
        //  init() — precompute on-tape quantities (called after parameters
        //  are placed on tape by lifeSimulationAAD)
        // -----------------------------------------------------------------
        void init(
            const std::vector<Time>& productTimeline,
            const std::vector<SampleDef>& defline) override
        {
            using std::sqrt; using std::exp; using std::log;

            const size_t d      = myNumAssets;
            const size_t nSteps = myTimeline.size() - 1;

            // Refresh cached log-discounts after tape placement
            myYieldCurve.refreshLogDiscountsFromDiscountFactors();

            // ---- 1. Covariance matrix and Cholesky (equity, off-tape vols) ----
            myCovMatrix = Matrix<T>(d, d);
            for (size_t i = 0; i < d; ++i) {
                myCovMatrix[i][i] = T(myVols[i] * myVols[i]);
                for (size_t j = 0; j < i; ++j) {
                    T cov = T(myVols[i] * myVols[j]) * myCorrMatrix[i][j];
                    myCovMatrix[i][j] = cov;
                    myCovMatrix[j][i] = cov;
                }
            }
            myCholesky = cholesky(myCovMatrix);

            // ---- 2. Off-tape half-vol^2 for each asset ----
            for (size_t k = 0; k < d; ++k)
                myVolSqHalf[k] = 0.5 * myVols[k] * myVols[k];

            // ---- 3. Build off-tape HW constants from the outer curve ----
            // We extract double discount factors from the on-tape T parameters.
            // static_cast<double>(T) uses T::operator double() which returns the
            // primal value (correct for off-tape constant precomputation).
            {
                const auto& tenors = myYieldCurve.tenors();
                const auto& dfsT   = myYieldCurve.discountFactors();
                std::vector<double> dfsD(dfsT.size());
                for (size_t i = 0; i < dfsT.size(); ++i)
                    dfsD[i] = static_cast<double>(dfsT[i]);

                YieldCurve<double> curveDouble;
                curveDouble.setDiscountCurve(tenors, dfsD);

                std::vector<double> stepTimes(nSteps);
                for (size_t i = 0; i < nSteps; ++i)
                    stepTimes[i] = myTimeline[i];

                myHW.build(myHWParams, curveDouble, stepTimes);
            }

            // ---- 3b. On-tape HW calibration (r0T and c2T) ----
            // These carry the dominant IR Greek channel: a shift in any outer
            // curve knot changes r0T and each c2T[i], propagating through every
            // short-rate realisation r_k = c1^k*r0T + sum c1^(k-j)*c2T[j] into
            // the accumulated path numeraire.  This gives ~250x more IR signal
            // than the logA_i channel alone (which has coefficient 1-B/dt ~ 0.004).
            myHW.computeOnTapeParts(myYieldCurve, myHWr0T, myHWc2T);

            // ---- 4. Per-step precomputed values (on tape where noted) ----
            for (size_t i = 0; i < nSteps; ++i) {
                const double t1 = myTimeline[i];
                const double t2 = myTimeline[i + 1];
                const double dt = t2 - t1;

                mySqrtDts[i] = T(sqrt(dt));

                // On-tape log(A_i) for HW one-step discount:
                //   logA_i = (1 - B_hw/dt) * [ln D^M(t2) - ln D^M(t1)] - corrFactor_i
                //
                // IR Greeks flow through D^M(t1) and D^M(t2), which are the curve
                // parameter knots put on tape by lifeSimulationAAD.
                const T D1    = curveDiscount(t1);
                const T D2    = curveDiscount(t2);
                const T lnD1  = log(D1);
                const T lnD2  = log(D2);
                const double alpha_exp = 1.0 - myHW.bhwOverDt;  // (1 - B_hw/dt)
                myHWLogA[i] = T(alpha_exp) * (lnD2 - lnD1) - T(myHW.corrFactor[i]);
            }

            // ---- 5. Per-event: forward factors, discounts, libors (static outer curve) ----
            const size_t nEvents = productTimeline.size();
            for (size_t i = 0; i < nEvents; ++i) {
                const double t_ev = productTimeline[i];

                // Forward factors: F_k(t_ev, T) = D^M(t_ev)/D^M(T) * exp(-q_k*(T-t_ev))
                const size_t nFwds = defline[i].forwardMats.size();
                for (size_t j = 0; j < nFwds; ++j) {
                    const double T_mat = defline[i].forwardMats[j];
                    const double tau   = T_mat - t_ev;
                    T D_ev  = curveDiscount(t_ev);
                    T D_mat = curveDiscount(T_mat);
                    T ratio = D_ev / D_mat;
                    for (size_t k = 0; k < d; ++k)
                        myForwardFactors[i][j * d + k] = ratio * exp(-myDivs[k] * T(tau));
                }

                // Discount factors: D^M(T) / D^M(t_ev)
                const size_t nDiscs = defline[i].discountMats.size();
                for (size_t j = 0; j < nDiscs; ++j) {
                    const double T_mat = defline[i].discountMats[j];
                    myDiscounts[i][j] = curveDiscount(T_mat) / curveDiscount(t_ev);
                }

                // Libors: L(T1, T2) = (D(T1)/D(T2) - 1) / (T2 - T1)
                const size_t nLibors = defline[i].liborDefs.size();
                for (size_t j = 0; j < nLibors; ++j) {
                    const double T1  = defline[i].liborDefs[j].start;
                    const double T2  = defline[i].liborDefs[j].end;
                    const double tau = T2 - T1;
                    myLibors[i][j] =
                        (curveDiscount(T1) / curveDiscount(T2) - T(1.0)) / T(tau);
                }
            }
        }

        // -----------------------------------------------------------------
        //  simDim() = (d + 1) * nSteps
        //    Gaussians: [step*(d+1)+k] for asset k, [step*(d+1)+d] for rate
        // -----------------------------------------------------------------
        size_t simDim() const override {
            return (myNumAssets + 1) * (myTimeline.size() - 1);
        }

        // -----------------------------------------------------------------
        //  generatePath() — simulate one scenario
        //
        //  State:
        //    r  : short rate (on tape via myHWr0T / myHWc2T — full IR Greek)
        //    discountAcc : accumulated prod_i P^HW(t_i, t_{i+1}) (on tape)
        //
        //  At each step i:
        //    1. Compute P^HW(t_i, t_{i+1}) = exp(logA_i - B_hw * r_i)  [on tape; r_i on tape]
        //    2. Accumulate discountAcc *= P_step
        //    3. Evolve rate: r_{i+1} = c1*r_i + c2T[i] + c3*Z_rate       [on tape via c2T]
        //    4. Equity drift uses r_i (on tape) and on-tape divs
        //    5. Evolve equity spots
        //    6. Fill sample with discountAcc (numeraire = 1/discountAcc)
        // -----------------------------------------------------------------
        void generatePath(
            const std::vector<double>& gaussVec,
            Scenario<T>& path) const override
        {
            using std::exp;

            const size_t d      = myNumAssets;
            const size_t nSteps = myTimeline.size() - 1;
            const size_t stride = d + 1;   // Gaussians per step

            // Working state
            std::vector<T> spots(mySpots);
            std::vector<T> corrGauss(d);

            // Short rate — on tape, initialised from calibrated r0T
            // Carries dFMV/d(outer curve knots) through the full HW calibration.
            T r = myHWr0T;

            // Accumulated path discount factor:  prod_i P^HW(t_i, t_{i+1})
            // This equals D^path(0, t_event) = 1/B(t_event) on this path.
            T discountAcc = T(1.0);

            size_t evIdx = 0;
            if (myTodayOnTimeline) {
                // At t=0: discountAcc = 1, numeraire B(0) = 1
                fillScen(evIdx, spots, path[evIdx], (*myDefline)[evIdx], T(1.0));
                ++evIdx;
            }

            for (size_t i = 0; i < nSteps; ++i) {
                const double dt      = myTimeline[i + 1] - myTimeline[i];
                const double* z_eq   = &gaussVec[i * stride];
                const double  z_rate =  gaussVec[i * stride + d];

                // ---- Step 1: one-step HW discount (on tape via myHWLogA and r) ----
                // P^HW(t_i, t_{i+1}) = exp(logA_i - B_hw * r_i)
                // r is now T-typed, so the full derivative dP/d(curve knot) is captured.
                const T P_step = exp(myHWLogA[i] - T(myHW.bhw) * r);
                discountAcc    = discountAcc * P_step;

                // ---- Step 2: equity drift using r_i (before rate evolution) ----
                // drift_k = (r_i - q_k - 0.5*vol_k^2) * dt
                // r_i is on tape; the equity paths now also carry IR sensitivity.
                const T r_dt = r * T(dt);

                // ---- Step 3: evolve short rate (on tape via myHWc2T) ----
                r = T(myHW.c1) * r + myHWc2T[i] + T(myHW.c3 * z_rate);

                // ---- Step 4: correlated equity draws L · Z ----
                for (size_t k = 0; k < d; ++k) {
                    T sum = T(0.0);
                    for (size_t l = 0; l <= k; ++l)
                        sum = sum + myCholesky[k][l] * T(z_eq[l]);
                    corrGauss[k] = sum;
                }

                // ---- Step 5: evolve equity spots ----
                // S_k *= exp((r_i - q_k - 0.5*vol_k^2)*dt + (L·Z)_k * sqrt(dt))
                for (size_t k = 0; k < d; ++k) {
                    // r_dt is T (on tape); equity paths carry IR sensitivity via drift
                    const T drift = r_dt - T(myVolSqHalf[k] * dt) - myDivs[k] * T(dt);
                    spots[k] = spots[k] * exp(drift + corrGauss[k] * mySqrtDts[i]);
                }

                // ---- Step 6: fill path sample ----
                fillScen(evIdx, spots, path[evIdx], (*myDefline)[evIdx], discountAcc);
                ++evIdx;
            }
        }
    };

} // namespace ActuaLib
