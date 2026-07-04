#pragma once

/*
        Multivariate Black-Scholes model for d correlated assets.

        A full term-structure is provided externally as YieldCurve<T>
        (e.g. from an outer scenario engine). This class does NOT bootstrap
        from swap rates.

        On tape (T = Number):
            - spots[d]               initial asset prices
            - divs[d]                continuous dividend yields per asset
            - curve discount knots   D(T_i), i >= 1 (all tenor risk factors)

        Off tape (plain double):
            - vols[d]                annual volatilities σ_k
            - corrMatrix[d x d]      correlation matrix ρ

        In init(), covariance is built with off-tape vols and on-tape
        correlations:
            - Σ_{ij} = σ_i σ_j ρ_{ij}
            - L = cholesky(Σ)

        AAD gives sensitivities to spots, dividends, and all curve tenors;
        NOT to volatilities, correlations, or variances.

    simDim() = d * nSteps  (d independent Gaussians per step)

    Forwards convention (multi-asset):
      forwards[ j * d + k ]  =  forward of asset k at forwardMats[j]
    Products must set  SampleDef::numAssets = d  so that the
    path allocator reserves the right amount of space.
*/

#include "model.hpp"
#include "yieldcurve.hpp"
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
    class MultivariateBlackScholes : public Model<T> {
    private:
        // ----- dimensions -----
        size_t myNumAssets;            // d

        // ----- model parameters on AAD tape -----
        std::vector<T>  mySpots;       // d
        std::vector<T>  myDivs;        // d
        Matrix<double>  myCorrMatrix;  // d x d, correlation matrix ρ (off tape)
        YieldCurve<T>   myYieldCurve;  // curve knots are on tape

        // ----- off tape -----
        std::vector<double> myVols;        // d, annual volatilities σ_k
        bool                myUseNSTail = false;
        double              myNSShortRate = 0.0;
        double              myNSLongRate = 0.0;

        // ----- derived (computed in init, on tape) -----
        Matrix<T>       myCovMatrix;   // d x d, Σ_{ij} = σ_i σ_j ρ_{ij}
        Matrix<T>       myCholesky;    // d x d, L s.t. Σ = L Lᵀ (on tape)

        // ----- simulation timeline -----
        std::vector<Time> myTimeline;  // today + future product dates
        bool myTodayOnTimeline;
        const std::vector<SampleDef>* myDefline;

        // ----- pre-computed per simulation step (nSteps) -----
        std::vector<T>              mySqrtDts;   // sqrt(dt)
        std::vector<std::vector<T>> myDrifts;    // nSteps x d

        // ----- pre-computed per event (nEvents) -----
        std::vector<T>              myNumeraires;
        std::vector<std::vector<T>> myDiscounts;      // per disc mat
        std::vector<std::vector<T>> myForwardFactors; // per (fwdMat x d)
        std::vector<std::vector<T>> myLibors;

        // ----- parameter export -----
        std::vector<T*>         myParameters;
        std::vector<std::string> myParameterLabels;

        // ===================================================================
        //  Private helpers
        // ===================================================================

        void buildParameterLabels() {
            const size_t d = myNumAssets;
            const size_t nCurve = myYieldCurve.tenors().size() - 1; // skip t=0
            const size_t nParams = 2 * d + nCurve;
            //  spots(d) + divs(d) + curve knots(nCurve)

            myParameterLabels.resize(nParams);
            size_t p = 0;
            for (size_t i = 0; i < d; ++i)
                myParameterLabels[p++] = "spot_" + std::to_string(i);
            for (size_t i = 0; i < d; ++i)
                myParameterLabels[p++] = "div_"  + std::to_string(i);
            for (size_t i = 1; i <= nCurve; ++i)
                myParameterLabels[p++] = "disc_" + std::to_string(myYieldCurve.tenors()[i]);
        }

        void setParamPointers() {
            const size_t d = myNumAssets;
            const size_t nCurve = myYieldCurve.tenors().size() - 1; // skip t=0
            const size_t nParams = 2 * d + nCurve;

            myParameters.resize(nParams);
            size_t p = 0;
            for (size_t i = 0; i < d; ++i)
                myParameters[p++] = &mySpots[i];
            for (size_t i = 0; i < d; ++i)
                myParameters[p++] = &myDivs[i];
            auto& dfs = myYieldCurve.discountFactorsMutable();
            for (size_t i = 1; i <= nCurve; ++i)
                myParameters[p++] = &dfs[i];
        }

        // Nelson-Siegel zero rate calibrated from the outer state
        // represented by short and long rates.
        double nsZeroRate(double t) const
        {
            if (t <= 0.0) return myNSShortRate;

            constexpr double k = 0.4;
            const double c1 = (1.0 - std::exp(-k * 1.0)) / (k * 1.0);
            const double c20 = (1.0 - std::exp(-k * 20.0)) / (k * 20.0);
            const double b1 = (myNSShortRate - myNSLongRate) / (c1 - c20);
            const double b0 = myNSShortRate - b1 * c1;
            const double ct = (1.0 - std::exp(-k * t)) / (k * t);
            return b0 + b1 * ct;
        }

        // Discount factor with NS-informed tail extrapolation.
        // For t <= last curve knot: use on-tape log-linear interpolation.
        // For t > last knot: anchor the NS tail to the last on-tape knot so
        // the curve stays continuous and retains curve-risk sensitivity.
        T curveDiscount(double t) const
        {
            const auto& ten = myYieldCurve.tenors();
            const double lastTenor = ten.back();
            if (!myUseNSTail || t <= lastTenor) {
                return myYieldCurve.discount(t);
            }

            const double nsLast = std::exp(-nsZeroRate(lastTenor) * lastTenor);
            const double nsT = std::exp(-nsZeroRate(t) * t);
            if (nsLast <= 0.0) {
                return myYieldCurve.discount(t);
            }

            const T scale = myYieldCurve.discount(lastTenor) / T(nsLast);
            return scale * T(nsT);
        }

        // Fill one sample in the scenario path
        inline void fillScen(
            const size_t     idx,
            const std::vector<T>& spots,
            Sample<T>&       scen,
            const SampleDef& def) const
        {
            const size_t d = myNumAssets;
            // Number of assets the product actually requests
            const size_t dEff = def.numAssets;

            // Numeraire
            if (def.numeraire) {
                scen.numeraire = myNumeraires[idx];
            }

            // Forwards:  forwards[ j*dEff + k ] = ff * spot_k
            // Only write as many assets as the product expects
            const size_t nFwds = def.forwardMats.size();
            for (size_t j = 0; j < nFwds; ++j) {
                for (size_t k = 0; k < dEff; ++k) {
                    scen.forwards[j * dEff + k] =
                        myForwardFactors[idx][j * d + k] * spots[k];
                }
            }

            // Discounts (asset-independent)
            std::copy(
                myDiscounts[idx].begin(),
                myDiscounts[idx].end(),
                scen.discounts.begin());

            // Libors (asset-independent)
            std::copy(
                myLibors[idx].begin(),
                myLibors[idx].end(),
                scen.libors.begin());
        }

    public:

        // ===================================================================
        //  Constructor
        // ===================================================================
        MultivariateBlackScholes(
            const std::vector<T>&      spots,
            const std::vector<T>&      divs,
            const std::vector<double>& vols,
            const Matrix<double>&      corrMatrix,
                        const YieldCurve<T>&       yieldCurve,
                        bool                       useNSTail = false,
                        double                     nsShortRate = 0.0,
                        double                     nsLongRate = 0.0)
            : myNumAssets(spots.size()),
              mySpots(spots),
              myDivs(divs),
              myVols(vols),
                            myUseNSTail(useNSTail),
                            myNSShortRate(nsShortRate),
                            myNSLongRate(nsLongRate),
              myCorrMatrix(corrMatrix),
              myYieldCurve(yieldCurve),
              myTodayOnTimeline(false),
              myDefline(nullptr)
        {
            // Validate dimensions
            if (divs.size() != myNumAssets) {
                throw std::runtime_error(
                    "MultivariateBlackScholes: spots/divs dimension mismatch");
            }
            if (vols.size() != myNumAssets) {
                throw std::runtime_error(
                    "MultivariateBlackScholes: spots/vols dimension mismatch");
            }
            if (corrMatrix.rows() != myNumAssets ||
                corrMatrix.cols() != myNumAssets) {
                throw std::runtime_error(
                    "MultivariateBlackScholes: correlation matrix size mismatch");
            }
            if (myYieldCurve.tenors().size() != myYieldCurve.discountFactors().size() ||
                myYieldCurve.tenors().size() < 2) {
                throw std::runtime_error(
                    "MultivariateBlackScholes: invalid input yield curve");
            }
            if (std::fabs(myYieldCurve.tenors().front()) > 1.0e-12) {
                throw std::runtime_error(
                    "MultivariateBlackScholes: curve must include t=0 knot");
            }
            if (myUseNSTail && (myNSShortRate <= 0.0 || myNSLongRate <= 0.0)) {
                throw std::runtime_error(
                    "MultivariateBlackScholes: NS tail requires positive short/long rates");
            }

            buildParameterLabels();
            setParamPointers();
        }

        // ===================================================================
        //  Accessors
        // ===================================================================
        size_t              numAssets()  const { return myNumAssets; }
        const std::vector<T>&  spots()  const { return mySpots; }
        const std::vector<double>& vols() const { return myVols; }
        const std::vector<T>&  divs()   const { return myDivs; }
        const Matrix<double>& corrMatrix() const { return myCorrMatrix; }
        const Matrix<T>&    covMatrix()  const { return myCovMatrix; }
        const YieldCurve<T>& yieldCurve()const { return myYieldCurve; }

        // ===================================================================
        //  Model interface
        // ===================================================================

        const std::vector<T*>& parameters() const override {
            return myParameters;
        }
        const std::vector<std::string>& parameterLabels() const override {
            return myParameterLabels;
        }

        std::unique_ptr<Model<T>> clone() const override {
            auto cl = std::make_unique<MultivariateBlackScholes<T>>(*this);
            cl->setParamPointers();
            return cl;
        }

        // -----------------------------------------------------------------
        //  Allocate storage
        // -----------------------------------------------------------------
        void allocate(
            const std::vector<Time>& prodTimeline,
            const std::vector<SampleDef>& defline) override
        {
            const size_t d = myNumAssets;

            // Build simulation timeline: today + future product dates
            myTimeline.clear();
            myTimeline.push_back(SystemTime);
            for (const auto t : prodTimeline) {
                if (t > SystemTime) {
                    myTimeline.push_back(t);
                }
            }

            myTodayOnTimeline =
                !prodTimeline.empty() &&
                std::fabs(prodTimeline[0] - SystemTime) < 1.0e-12;

            myDefline = &defline;

            // Per-step storage
            const size_t nSteps = myTimeline.size() - 1;
            mySqrtDts.resize(nSteps);
            myDrifts.resize(nSteps);
            for (size_t i = 0; i < nSteps; ++i) {
                myDrifts[i].resize(d);
            }

            // Per-event storage
            const size_t nEvents = prodTimeline.size();
            myNumeraires.resize(nEvents);
            myDiscounts.resize(nEvents);
            myForwardFactors.resize(nEvents);
            myLibors.resize(nEvents);

            for (size_t i = 0; i < nEvents; ++i) {
                myDiscounts[i].resize(defline[i].discountMats.size());
                myForwardFactors[i].resize(
                    d * defline[i].forwardMats.size());
                myLibors[i].resize(defline[i].liborDefs.size());
            }
        }

        // -----------------------------------------------------------------
        //  Pre-compute (on tape when T = Number)
        // -----------------------------------------------------------------
        void init(
            const std::vector<Time>& productTimeline,
            const std::vector<SampleDef>& defline) override
        {
            using std::sqrt;
            using std::exp;
            using std::log;

            const size_t d = myNumAssets;

            // Re-link curve interpolation cache to current discount knots
            // (discount factors are model parameters put on tape each run).
            myYieldCurve.refreshLogDiscountsFromDiscountFactors();

            // 1. Build covariance matrix from vols + correlation
            //    Σ_{ij} = σ_i · σ_j · ρ_{ij}
            //
            //    vols and correlations are off tape by design.
            myCovMatrix = Matrix<T>(d, d);
            for (size_t i = 0; i < d; ++i) {
                myCovMatrix[i][i] = T(myVols[i] * myVols[i]);
                for (size_t j = 0; j < i; ++j) {
                    T cov = T(myVols[i] * myVols[j]) * myCorrMatrix[i][j];
                    myCovMatrix[i][j] = cov;
                    myCovMatrix[j][i] = cov;
                }
            }

            // 2. Cholesky of covariance matrix
            myCholesky = cholesky(myCovMatrix);

            // 3. Per-step: drift and sqrt(dt)
            const size_t nSteps = myTimeline.size() - 1;
            for (size_t i = 0; i < nSteps; ++i) {
                const double t1 = myTimeline[i];
                const double t2 = myTimeline[i + 1];
                const T dt = T(t2 - t1);

                mySqrtDts[i] = sqrt(dt);

                // Log D(t1) - Log D(t2) gives the forward rate * dt
                T D1 = curveDiscount(t1);
                T D2 = curveDiscount(t2);
                T logFwdDt = log(D1) - log(D2);

                for (size_t k = 0; k < d; ++k) {
                    // drift_k = log(D1/D2)  - (q_k + ½ σ_k²) dt
                    myDrifts[i][k] =
                        logFwdDt -
                        (myDivs[k] + T(0.5 * myVols[k] * myVols[k])) * dt;
                }
            }

            // 4. Per-event: numeraire, discounts, forward factors, libors
            const size_t nEvents = productTimeline.size();
            for (size_t i = 0; i < nEvents; ++i) {
                const double t_ev = productTimeline[i];

                // Numeraire = 1 / D(0, t)     (money-market measure)
                if (defline[i].numeraire) {
                    myNumeraires[i] = T(1.0) / curveDiscount(t_ev);
                }

                // Forward factors:  F_k(t,T) = [D(t)/D(T)] exp(-q_k (T-t))
                //   at simulation time t_ev for forward maturity T
                const size_t nFwds = defline[i].forwardMats.size();
                for (size_t j = 0; j < nFwds; ++j) {
                    const double T_mat = defline[i].forwardMats[j];
                    const double tau   = T_mat - t_ev;
                    T D_ev  = curveDiscount(t_ev);
                    T D_mat = curveDiscount(T_mat);
                    T ratioD = D_ev / D_mat;

                    for (size_t k = 0; k < d; ++k) {
                        myForwardFactors[i][j * d + k] =
                            ratioD * exp(-myDivs[k] * T(tau));
                    }
                }

                // Discounts:  D(t_ev, T) = D(T) / D(t_ev)
                const size_t nDiscs = defline[i].discountMats.size();
                for (size_t j = 0; j < nDiscs; ++j) {
                    const double T_mat = defline[i].discountMats[j];
                    myDiscounts[i][j] =
                        curveDiscount(T_mat) /
                        curveDiscount(t_ev);
                }

                // Libors: L(T1,T2) = (D(T1)/D(T2) - 1) / tau
                const size_t nLibors = defline[i].liborDefs.size();
                for (size_t j = 0; j < nLibors; ++j) {
                    const double T1  = defline[i].liborDefs[j].start;
                    const double T2  = defline[i].liborDefs[j].end;
                    const double tau = T2 - T1;
                    myLibors[i][j] =
                        (curveDiscount(T1) /
                         curveDiscount(T2) - T(1.0)) / T(tau);
                }
            }
        }

        // -----------------------------------------------------------------
        //  Simulation dimension = d * nSteps
        // -----------------------------------------------------------------
        size_t simDim() const override {
            return myNumAssets * (myTimeline.size() - 1);
        }

        // -----------------------------------------------------------------
        //  Generate one scenario path
        //  gaussVec has simDim() entries:
        //    gaussVec[step * d + asset]  (d independent N(0,1) per step)
        // -----------------------------------------------------------------
        void generatePath(
            const std::vector<double>& gaussVec,
            Scenario<T>& path) const override
        {
            using std::exp;

            const size_t d      = myNumAssets;
            const size_t nSteps = myTimeline.size() - 1;

            // Working copy of spots
            std::vector<T> spots(mySpots);
            // Temp buffer for correlated draws
            std::vector<T> corrGauss(d);

            size_t idx = 0;
            if (myTodayOnTimeline) {
                fillScen(idx, spots, path[idx], (*myDefline)[idx]);
                ++idx;
            }

            for (size_t i = 0; i < nSteps; ++i) {
                // Pointer to this step's d independent Gaussians
                const double* z = &gaussVec[i * d];

                // Correlate: X = L · Z  (L = cholesky(Σ), on tape → AAD tracks ∂/∂Σ)
                // (L·Z)_k already incorporates both σ_k and correlations
                for (size_t k = 0; k < d; ++k) {
                    T sum = T(0.0);
                    for (size_t l = 0; l <= k; ++l) {
                        sum = sum + myCholesky[k][l] * T(z[l]);
                    }
                    corrGauss[k] = sum;
                }

                // Evolve each asset:
                //   S_k(t+dt) = S_k(t) exp[ (r - q_k - ½σ_k²)dt + (L·Z)_k √dt ]
                for (size_t k = 0; k < d; ++k) {
                    spots[k] = spots[k] * exp(
                        myDrifts[i][k] +
                        corrGauss[k] * mySqrtDts[i]);
                }

                // Fill the sample
                fillScen(idx, spots, path[idx], (*myDefline)[idx]);
                ++idx;
            }
        }
    };

} // namespace ActuaLib
