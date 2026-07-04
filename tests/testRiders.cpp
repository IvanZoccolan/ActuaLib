/*
    Rider-by-rider comparison with Gan's reference FMVs.

    Prices one representative policy from each of Gan's product types
    using the datasets/inforce.csv fund values and compares against
    the reference FMV from datasets/Greek.csv.

    Product types tested (14 of 19 — RollUp types DBRU/MBRU/ABRU/WBRU/IBRU
    are identical in structure to StepUp, only differing in update rule):
      Standalone GMDB:  DBRP (70001), DBSU (130001)
      Standalone GMMB:  MBRP (20001), MBSU (80001)
      Standalone GMAB:  ABRP (1),     ABSU (140001)
      Standalone GMWB:  WBRP (40001), WBSU (50001)
      Standalone GMIB:  IBRP (90001), IBSU (180001)
      Combo GMDB+MB:    DBMB (160001)
      Combo GMDB+AB:    DBAB (30001)
      Combo GMDB+WB:    DBWB (100001)
      Combo GMDB+IB:    DBIB (110001)

    Results summary (100K Sobol paths):
      < 2% difference :  DBRP (-0.4%), WBRP (1.6%), WBSU (1.4%), DBWB (1.3%)
      3–7% difference :  ABSU (3.7%), DBAB (5.1%), DBMB (6.0%), MBRP (6.5%),
                         MBSU (6.8%)
      7–9% difference :  IBRP (8.8%), DBIB (7.9%), ABRP (9.0%)
      Known Gan bugs  :  DBSU (-81.8%): Gan zeroes DA (see PricerDBSU.java)
                         IBSU (-17.0%): Gan missing *dT on fund fee

    Systematic 3–9% bias sources:
      1. Yield curve bootstrap: Gan uses semi-annual coupons with actual
         business-day dates (ACT/ACT day count); we use simplified annual
         coupon bootstrap with exact-year tenors.
      2. MC methodology: Gan uses 1000 pseudo-random scenarios (high
         variance, ~3% SE); we use 100,000 Sobol quasi-random paths.
      3. GMIB annuity factor: Gan uses flat forward-rate approximation
         exp(-f(t+n*12)*n); we extrapolate numeraire from the last
         12 months of the path.

    Usage:
        ./TestRiders
*/

#include <esg/multivariate_blackscholes.hpp>
#include <montecarlo/parallelmcsimulation.hpp>
#include <products/va_gmdb.hpp>
#include <products/va_gmmb.hpp>
#include <products/va_gmab.hpp>
#include <products/va_gmwb.hpp>
#include <products/va_gmib.hpp>
#include <products/va_combo.hpp>
#include <demographics/mortality.hpp>
#include <math/randomnumbers/sobol.hpp>
#include <math/matrix.hpp>

#include <iostream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <numeric>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <tuple>

using namespace ActuaLib;

// ================================================================
//  Mortality tables — IAM 1996  (ages 5–115)
// ================================================================

static MortalityTable makeFemaleTable() {
    std::vector<int> ages(111);
    std::iota(ages.begin(), ages.end(), 5);
    // clang-format off
    std::vector<double> qx = {
        0.000159, 0.000131, 0.000110, 0.000111, 0.000114,
        0.000119, 0.000127, 0.000136, 0.000147, 0.000159,
        0.000172, 0.000186, 0.000200, 0.000215, 0.000230,
        0.000245, 0.000260, 0.000276, 0.000291, 0.000307,
        0.000322, 0.000336, 0.000350, 0.000362, 0.000373,
        0.000383, 0.000392, 0.000400, 0.000407, 0.000415,
        0.000426, 0.000439, 0.000457, 0.000481, 0.000512,
        0.000549, 0.000593, 0.000646, 0.000706, 0.000775,
        0.000853, 0.000941, 0.001039, 0.001149, 0.001270,
        0.001403, 0.001548, 0.001705, 0.001876, 0.002060,
        0.002260, 0.002477, 0.002713, 0.002970, 0.003252,
        0.003566, 0.003916, 0.004308, 0.004746, 0.005231,
        0.005762, 0.006339, 0.006963, 0.007637, 0.008390,
        0.009256, 0.010268, 0.011459, 0.012859, 0.014484,
        0.016345, 0.018454, 0.020822, 0.023469, 0.026439,
        0.029786, 0.033560, 0.037814, 0.042605, 0.047995,
        0.054057, 0.060857, 0.068464, 0.076911, 0.086087,
        0.095846, 0.106039, 0.116521, 0.127149, 0.137798,
        0.148351, 0.158684, 0.168680, 0.178961, 0.190149,
        0.202865, 0.217733, 0.235373, 0.256408, 0.281459,
        0.311150, 0.346100, 0.386933, 0.434271, 0.488734,
        0.550947, 0.621529, 0.701104, 0.790292, 0.889717,
        1.000000
    };
    // clang-format on
    return MortalityTable(ages, qx);
}

static MortalityTable makeMaleTable() {
    std::vector<int> ages(111);
    std::iota(ages.begin(), ages.end(), 5);
    // clang-format off
    std::vector<double> qx = {
        0.000310, 0.000288, 0.000274, 0.000307, 0.000335,
        0.000358, 0.000376, 0.000392, 0.000405, 0.000417,
        0.000427, 0.000438, 0.000451, 0.000465, 0.000481,
        0.000500, 0.000520, 0.000543, 0.000567, 0.000593,
        0.000618, 0.000642, 0.000664, 0.000682, 0.000697,
        0.000709, 0.000718, 0.000724, 0.000729, 0.000735,
        0.000747, 0.000770, 0.000807, 0.000862, 0.000937,
        0.001034, 0.001155, 0.001301, 0.001473, 0.001669,
        0.001887, 0.002124, 0.002377, 0.002643, 0.002922,
        0.003213, 0.003516, 0.003829, 0.004153, 0.004487,
        0.004833, 0.005190, 0.005560, 0.005947, 0.006365,
        0.006834, 0.007372, 0.007997, 0.008728, 0.009579,
        0.010564, 0.011696, 0.012989, 0.014456, 0.016096,
        0.017913, 0.019903, 0.022068, 0.024414, 0.026967,
        0.029761, 0.032829, 0.036205, 0.039919, 0.043993,
        0.048449, 0.053305, 0.058582, 0.064299, 0.070462,
        0.077080, 0.084158, 0.091701, 0.099715, 0.108196,
        0.117140, 0.126540, 0.136392, 0.146691, 0.157432,
        0.168615, 0.180232, 0.192282, 0.205218, 0.219494,
        0.235563, 0.253878, 0.274893, 0.299061, 0.326834,
        0.358668, 0.395014, 0.436326, 0.483057, 0.535662,
        0.594592, 0.660302, 0.733244, 0.813872, 0.902640,
        1.000000
    };
    // clang-format on
    return MortalityTable(ages, qx);
}

// ================================================================
//  Market model builder
// ================================================================

static const size_t d = 5;  // number of equity indices

// Standard fund-to-index mapping (Gan's FundMap.csv)
//              LC     SC     IE     FI     M
static const std::vector<std::vector<double>> standardFundMap = {
    { 1.0,   0.0,   0.0,   0.0,   0.0 },   // Fund 1 -> LC 100%
    { 0.0,   1.0,   0.0,   0.0,   0.0 },   // Fund 2 -> SC 100%
    { 0.0,   0.0,   1.0,   0.0,   0.0 },   // Fund 3 -> IE 100%
    { 0.0,   0.0,   0.0,   1.0,   0.0 },   // Fund 4 -> FI 100%
    { 0.0,   0.0,   0.0,   0.0,   1.0 },   // Fund 5 -> M  100%
    { 0.6,   0.4,   0.0,   0.0,   0.0 },   // Fund 6 -> LC 60% + SC 40%
    { 0.5,   0.0,   0.5,   0.0,   0.0 },   // Fund 7 -> LC 50% + IE 50%
    { 0.5,   0.0,   0.0,   0.5,   0.0 },   // Fund 8 -> LC 50% + FI 50%
    { 0.0,   0.3,   0.7,   0.0,   0.0 },   // Fund 9 -> SC 30% + IE 70%
    { 0.2,   0.2,   0.2,   0.2,   0.2 }    // Fund 10 -> 20% each
};

static const std::vector<double> standardFundFees = {
    0.003, 0.005, 0.006, 0.008, 0.001, 0.0038, 0.0045, 0.0055, 0.0057, 0.0046
};

static const double corr[5][5] = {
    {  1.000000,  0.761332,  0.556299,  0.238114, -0.025552 },
    {  0.761332,  1.000000,  0.443120,  0.131246, -0.024576 },
    {  0.556299,  0.443120,  1.000000,  0.153277, -0.023841 },
    {  0.238114,  0.131246,  0.153277,  1.000000,  0.062975 },
    { -0.025552, -0.024576, -0.023841,  0.062975,  1.000000 }
};

static const double vols[5] = {
    0.151255, 0.205336, 0.170563, 0.042894, 0.005663
};

static const std::vector<double> swapTenors = {
    1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0, 20.0, 25.0, 30.0
};

static const double swapRatesBase[10] = {
    0.00280228, 0.00579118, 0.01012285, 0.01768415, 0.02283698,
    0.02751166, 0.03111376, 0.03286486, 0.03388387, 0.03453971
};

// Build partial account values from fund values + fund map
static std::vector<double> computePA(const std::vector<double>& fundValues) {
    std::vector<double> PA(d, 0.0);
    for (size_t f = 0; f < fundValues.size(); ++f)
        for (size_t k = 0; k < d; ++k)
            PA[k] += standardFundMap[f][k] * fundValues[f];
    // Guard against zero PA: set to a small positive value so the
    // GBM simulation doesn't produce 0/0 = NaN.  Indices with zero
    // PA don't contribute to any fund growth (all fund-map weights
    // for that index are zero for funds with value), so the choice
    // of fallback value is immaterial.
    for (auto& pa : PA) {
        if (pa < 1e-8) pa = 1.0;
    }
    return PA;
}

// Build MultivariateBlackScholes<double> model from fund values
static MultivariateBlackScholes<double> makeModel(
    const std::vector<double>& fundValues)
{
    auto PA = computePA(fundValues);

    std::vector<double> spots(d), divs(d, 0.0), volsVec(d);
    Matrix<double> corrMatrix(d, d);
    std::vector<double> swapRates(10);

    for (size_t k = 0; k < d; ++k) {
        spots[k] = PA[k];
        volsVec[k] = vols[k];
    }
    for (size_t i = 0; i < d; ++i)
        for (size_t j = 0; j < d; ++j)
            corrMatrix[i][j] = corr[i][j];
    for (size_t k = 0; k < 10; ++k)
        swapRates[k] = swapRatesBase[k];

    YieldCurve<double> curve;
    curve.bootstrap(swapTenors, swapRates);

    return MultivariateBlackScholes<double>(
        spots, divs, volsVec, corrMatrix, curve);
}

// ================================================================
//  Pricing helper
// ================================================================

struct TestResult {
    std::string label;
    int         recordID;
    double      ganFMV;
    double      ourFMV;
    double      ourGMDB;
    double      ourGMLB;
    double      ourRC;
    double      mcSE;
    double      elapsed_ms;
};

static TestResult pricePolicy(
    const std::string& label,
    int recordID,
    double ganFMV,
    Product<double>& product,
    const std::vector<double>& fundValues,
    size_t nPaths)
{
    auto model = makeModel(fundValues);
    Sobol rng;

    auto t0 = std::chrono::high_resolution_clock::now();
    auto results = mcParallelSimulation(product, model, rng, nPaths);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);

    double sumGMDB = 0, sumGMLB = 0, sumRC = 0, sumFMV = 0, sumFMV2 = 0;
    const double N = static_cast<double>(nPaths);
    for (size_t p = 0; p < nPaths; ++p) {
        sumGMDB += results[p][0];
        sumGMLB += results[p][1];
        sumRC   += results[p][2];
        sumFMV  += results[p][3];
        sumFMV2 += results[p][3] * results[p][3];
    }
    double priceFMV  = sumFMV / N;
    double varFMV    = sumFMV2 / N - priceFMV * priceFMV;
    double seFMV     = std::sqrt(std::max(varFMV, 0.0) / N);

    return TestResult{
        label, recordID, ganFMV,
        priceFMV, sumGMDB / N, sumGMLB / N, sumRC / N,
        seFMV, static_cast<double>(ms.count())
    };
}

static void printResult(const TestResult& r) {
    double diff = r.ourFMV - r.ganFMV;
    double pctDiff = (std::abs(r.ganFMV) > 1.0)
                   ? diff / r.ganFMV * 100.0
                   : 0.0;

    std::cout << std::fixed;
    std::cout << "  " << r.label << "  (policy " << r.recordID << ")"
              << std::endl;
    std::cout << "    PV(DA)=" << std::setprecision(2) << r.ourGMDB
              << "  PV(LA)=" << r.ourGMLB
              << "  PV(RC)=" << r.ourRC << std::endl;
    std::cout << "    FMV:  Ours=" << std::setprecision(2) << r.ourFMV
              << "  +/- " << r.mcSE
              << "  |  Gan=" << r.ganFMV
              << "  |  diff=" << diff
              << "  (" << std::setprecision(1) << pctDiff << "%)"
              << "  |  " << static_cast<int>(r.elapsed_ms) << " ms"
              << std::endl;
}

// ================================================================
//  Main
// ================================================================

int main() {

    ThreadPool::getInstance()->start(7);

    auto femaleMort = makeFemaleTable();
    auto maleMort   = makeMaleTable();

    const size_t nPaths = 100000;

    std::cout << "==========================================================="
              << std::endl;
    std::cout << "  Rider-by-Rider Comparison with Gan's Reference FMVs"
              << std::endl;
    std::cout << "  Policies from datasets/inforce.csv"
              << std::endl;
    std::cout << "  Reference from datasets/Greek.csv"
              << std::endl;
    std::cout << "  Paths: " << nPaths << " (Sobol quasi-MC)"
              << std::endl;
    std::cout << "==========================================================="
              << std::endl;

    std::vector<TestResult> allResults;

    // ================================================================
    //  1. GMDB — Guaranteed Minimum Death Benefit
    // ================================================================

    std::cout << "\n--- GMDB (Death Benefit) ---\n" << std::endl;

    // DBRP: policy 70001, Female, age=34.77, 119 months, RP
    //   Reference FMV: -2299.61 (DB charges exceed expected death benefit)
    {
        VAPolicy pol;
        pol.currentAge        = 34.773;     // (41791-29099)/365.0
        pol.numMonths         = 119;        // monthsBetween(2014-06, 2024-05)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 109;        // monthsBetween(2005-05, 2014-06)
        pol.gbAmt             = 143299.596476;
        pol.updateRule        = BenefitBaseUpdate::ReturnOfPremium;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.0025;
        pol.smooth            = 0.0;
        pol.fundValues = {
            17305.777556, 0.0, 10249.159843, 0.0, 15163.152030,
            17039.858265, 13435.714669, 12769.149239, 11952.472558, 13720.639418
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB<double> product(pol, femaleMort);
        auto r = pricePolicy("DBRP", 70001, -2299.614583,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // DBSU: policy 130001, Female, age=56.62, 191 months, StepUp
    //   Reference FMV: -8623.30
    {
        VAPolicy pol;
        pol.currentAge        = 56.619;     // (41791-21125)/365.0
        pol.numMonths         = 191;        // monthsBetween(2014-06, 2030-05)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 109;        // monthsBetween(2005-05, 2014-06)
        pol.gbAmt             = 286770.327673;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.0035;
        pol.smooth            = 0.0;
        pol.fundValues = {
            51650.236298, 48791.903828, 30589.294590, 26522.010304, 0.0,
            0.0, 0.0, 38110.369406, 0.0, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB<double> product(pol, femaleMort);
        auto r = pricePolicy("DBSU", 130001, -8623.298003,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // ================================================================
    //  2. GMMB — Guaranteed Minimum Maturity Benefit
    // ================================================================

    std::cout << "\n--- GMMB (Maturity Benefit) ---\n" << std::endl;

    // MBRP: policy 20001, Male, age=41.53, 160 months, RP
    //   Reference FMV: 46157.68
    {
        VAPolicy pol;
        pol.currentAge        = 41.526;     // (41791-26634)/365.0
        pol.numMonths         = 160;        // monthsBetween(2014-06, 2027-10)
        pol.female            = false;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 92;         // monthsBetween(2006-10, 2014-06)
        pol.gbAmt             = 328650.980955;
        pol.updateRule        = BenefitBaseUpdate::ReturnOfPremium;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.005;
        pol.smooth            = 0.0;
        pol.fundValues = {
            59405.756117, 0.0, 0.0, 0.0, 54686.128952,
            0.0, 43858.053864, 0.0, 37803.172336, 47976.139451
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMMB<double> product(pol, maleMort);
        auto r = pricePolicy("MBRP", 20001, 46157.676237,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // MBSU: policy 80001, Female, age=57.37, 186 months, StepUp
    //   Reference FMV: 66428.69
    {
        VAPolicy pol;
        pol.currentAge        = 57.367;     // (41791-20852)/365.0
        pol.numMonths         = 186;        // monthsBetween(2014-06, 2029-12)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 102;        // monthsBetween(2005-12, 2014-06)
        pol.gbAmt             = 415679.636829;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.006;
        pol.smooth            = 0.0;
        pol.fundValues = {
            0.0, 42705.983387, 25531.013320, 25775.520025,
            41164.864661, 0.0, 34718.885774, 35611.319208,
            30175.807335, 36693.662752
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMMB<double> product(pol, femaleMort);
        auto r = pricePolicy("MBSU", 80001, 66428.690834,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // ================================================================
    //  3. GMAB — Guaranteed Minimum Accumulation Benefit
    // ================================================================

    std::cout << "\n--- GMAB (Accumulation Benefit) ---\n" << std::endl;

    // ABRP: policy 1, Female, age=47.36, months-to-mat=231, RP
    //   Reference FMV: 16763.29
    //   Only 1 fund with value (Fund 4 = FixedIncome)
    //   Original term = 336 months. numMonths=360 for full Gan projection.
    {
        VAPolicy pol;
        pol.currentAge        = 47.362;     // (41791-24504)/365.0
        pol.numMonths         = 360;        // full scenario length (like Gan)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 105;        // monthsBetween(2005-09, 2014-06)
        pol.gbAmt             = 87657.368596;
        pol.updateRule        = BenefitBaseUpdate::ReturnOfPremium;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.005;
        pol.smooth            = 0.0;
        pol.firstMaturityMonth = 231; // remaining months to matDate
        pol.renewalPeriod     = 336;  // original term (issue to mat)
        pol.fundValues = {
            0.0, 0.0, 0.0, 45008.862226, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMAB<double> product(pol, femaleMort);
        auto r = pricePolicy("ABRP", 1, 16763.294834,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // ABSU: policy 140001, Male, age=56.21, months-to-mat=103, StepUp
    //   Reference FMV: 137103.60
    //   Original term = 264 months. numMonths=360 for full Gan projection.
    {
        VAPolicy pol;
        pol.currentAge        = 56.205;     // (41791-21276)/365.0
        pol.numMonths         = 360;        // full scenario length (like Gan)
        pol.female            = false;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 161;        // monthsBetween(2001-01, 2014-06)
        pol.gbAmt             = 429585.502351;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.006;
        pol.smooth            = 0.0;
        pol.firstMaturityMonth = 103; // remaining months to matDate
        pol.renewalPeriod     = 264;  // original term (issue to mat)
        pol.fundValues = {
            26982.966733, 31937.701266, 15307.117950, 15136.486472, 32790.974625,
            29269.445542, 20568.723601, 21177.801705, 19463.302580, 24614.492970
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMAB<double> product(pol, maleMort);
        auto r = pricePolicy("ABSU", 140001, 137103.602292,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // ================================================================
    //  4. GMWB — Guaranteed Minimum Withdrawal Benefit
    // ================================================================

    std::cout << "\n--- GMWB (Withdrawal Benefit) ---\n" << std::endl;

    // WBRP: policy 40001, Female, age=61.96, 131 months, RP
    //   Reference FMV: 73047.77
    {
        VAPolicy pol;
        pol.currentAge        = 61.959;     // (41791-19176)/365.0
        pol.numMonths         = 131;        // monthsBetween(2014-06, 2025-05)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 121;        // monthsBetween(2004-05, 2014-06)
        pol.gbAmt             = 335538.471211;
        pol.updateRule        = BenefitBaseUpdate::ReturnOfPremium;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.0065;
        pol.smooth            = 0.0;
        pol.gmwbBalance       = 167769.235605;
        pol.wbWithdrawalRate  = 0.05;
        pol.fundValues = {
            16446.336564, 15480.532224, 0.0, 7778.903213, 14168.190087,
            16186.334368, 12871.545778, 0.0, 11480.089257, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMWB<double> product(pol, femaleMort);
        auto r = pricePolicy("WBRP", 40001, 73047.765426,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // WBSU: policy 50001, Male, age=61.29, 282 months, StepUp
    //   Reference FMV: -11516.24
    {
        VAPolicy pol;
        pol.currentAge        = 61.293;     // (41791-19419)/365.0
        pol.numMonths         = 282;        // monthsBetween(2014-06, 2037-12)
        pol.female            = false;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 66;         // monthsBetween(2008-12, 2014-06)
        pol.gbAmt             = 407767.312414;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.0075;
        pol.smooth            = 0.0;
        pol.gmwbBalance       = 257827.909641;
        pol.wbWithdrawalRate  = 0.05;
        pol.fundValues = {
            105074.603610, 101292.069312, 0.0, 0.0, 0.0,
            0.0, 81942.635171, 68404.432823, 0.0, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMWB<double> product(pol, maleMort);
        auto r = pricePolicy("WBSU", 50001, -11516.238182,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // ================================================================
    //  5. GMIB — Guaranteed Minimum Income Benefit
    // ================================================================

    std::cout << "\n--- GMIB (Income Benefit) ---\n" << std::endl;

    // IBRP: policy 90001, Female, age=62.04, 129 months, RP
    //   Reference FMV: 96784.23
    {
        VAPolicy pol;
        pol.currentAge        = 62.041;     // (41791-19146)/365.0
        pol.numMonths         = 129;        // monthsBetween(2014-06, 2025-03)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 87;         // monthsBetween(2007-03, 2014-06)
        pol.gbAmt             = 397075.099154;
        pol.updateRule        = BenefitBaseUpdate::ReturnOfPremium;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.006;
        pol.smooth            = 0.0;
        pol.guaranteedAnnuityRate = 0.05;
        pol.fundValues = {
            38974.213386, 35342.844218, 19959.792544, 25129.437540, 35835.401169,
            37702.396352, 28102.642539, 0.0, 23980.324423, 31369.433898
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMIB<double> product(pol, femaleMort);
        auto r = pricePolicy("IBRP", 90001, 96784.229152,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // IBSU: policy 180001, Male, age=37.11, 87 months, StepUp
    //   Reference FMV: 109445.76
    {
        VAPolicy pol;
        pol.currentAge        = 37.110;     // (41791-28246)/365.0
        pol.numMonths         = 87;         // monthsBetween(2014-06, 2021-09)
        pol.female            = false;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 129;        // monthsBetween(2003-09, 2014-06)
        pol.gbAmt             = 225547.403770;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.007;
        pol.smooth            = 0.0;
        pol.guaranteedAnnuityRate = 0.05;
        pol.fundValues = {
            34209.053726, 0.0, 0.0, 0.0, 26792.153797,
            33944.083157, 27094.641189, 22362.416296, 0.0, 25620.744271
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMIB<double> product(pol, maleMort);
        auto r = pricePolicy("IBSU", 180001, 109445.762842,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // ================================================================
    //  6. Combo: GMDB + GMMB
    // ================================================================

    std::cout << "\n--- Combos ---\n" << std::endl;

    // DBMB: policy 160001, Female, age=56.04, 207 months, StepUp
    //   Reference FMV: 9038.84  (from datasets/Greek.csv)
    {
        VAPolicy pol;
        pol.currentAge        = 56.038;     // (41791-21337)/365.0
        pol.numMonths         = 207;        // monthsBetween(2014-06, 2031-09)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 57;         // monthsBetween(2009-09, 2014-06)
        pol.gbAmt             = 98621.694458;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.0075;
        pol.smooth            = 0.0;
        pol.fundValues = {
            16304.544076, 15480.633309, 9404.054981, 0.0, 9915.298871,
            16025.890977, 12438.861227, 11453.523058, 0.0, 11525.557704
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB_MB<double> product(pol, femaleMort);
        auto r = pricePolicy("DBMB", 160001, 9038.842486,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // DBAB: policy 30001, Female, age=51.78, months-to-mat=160, StepUp
    //   Reference FMV: 37740.32
    //   Original term = 268 months. numMonths=360 for full Gan projection.
    {
        VAPolicy pol;
        pol.currentAge        = 51.784;     // (41791-22890)/365.0
        pol.numMonths         = 360;        // full scenario length (like Gan)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 116;        // monthsBetween(2004-10, 2014-06)
        pol.gbAmt             = 136060.127151;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.0075;
        pol.smooth            = 0.0;
        pol.firstMaturityMonth = 160; // remaining months to matDate
        pol.renewalPeriod     = 276;  // original term (issue to mat)
        pol.fundValues = {
            0.0, 19081.655780, 12030.538356, 9707.109842, 0.0,
            20045.283315, 15826.592488, 14516.373828, 0.0, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB_AB<double> product(pol, femaleMort);
        auto r = pricePolicy("DBAB", 30001, 37740.319357,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // DBWB: policy 100001, Female, age=54.45, 224 months, StepUp
    //   Reference FMV: 74358.08
    {
        VAPolicy pol;
        pol.currentAge        = 54.452;     // (41791-21916)/365.0
        pol.numMonths         = 224;        // monthsBetween(2014-06, 2033-02)
        pol.female            = true;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 88;         // monthsBetween(2007-02, 2014-06)
        pol.gbAmt             = 262353.597640;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.009;
        pol.smooth            = 0.0;
        pol.gmwbBalance       = 170529.838466;
        pol.wbWithdrawalRate  = 0.05;
        pol.fundValues = {
            29309.614905, 0.0, 15136.199176, 18690.420239, 26853.574128,
            0.0, 0.0, 0.0, 0.0, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB_WB<double> product(pol, femaleMort);
        auto r = pricePolicy("DBWB", 100001, 74358.080663,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // DBIB: policy 110001, Male, age=63.37, 74 months, StepUp
    //   Reference FMV: 70861.11
    {
        VAPolicy pol;
        pol.currentAge        = 63.373;     // (41791-18660)/365.0
        pol.numMonths         = 74;         // monthsBetween(2014-06, 2020-08)
        pol.female            = false;
        pol.survivorship      = 1.0;
        pol.monthsSinceIssue  = 154;        // monthsBetween(2001-08, 2014-06)
        pol.gbAmt             = 177820.028766;
        pol.updateRule        = BenefitBaseUpdate::StepUp;
        pol.rollUpRate        = 0.0;
        pol.baseFee           = 0.02;
        pol.riderFee          = 0.0085;
        pol.smooth            = 0.0;
        pol.guaranteedAnnuityRate = 0.05;
        pol.fundValues = {
            0.0, 49293.333368, 0.0, 0.0, 0.0,
            0.0, 35598.012168, 32219.504007, 0.0, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB_IB<double> product(pol, maleMort);
        auto r = pricePolicy("DBIB", 110001, 70861.113848,
                             product, pol.fundValues, nPaths);
        printResult(r);
        allResults.push_back(r);
    }

    // ================================================================
    //  Summary table
    // ================================================================

    std::cout << "\n==========================================================="
              << std::endl;
    std::cout << "  Summary" << std::endl;
    std::cout << "==========================================================="
              << std::endl;
    std::cout << std::fixed;
    std::cout << std::setw(8) << "Type"
              << std::setw(10) << "Policy"
              << std::setw(14) << "Ours"
              << std::setw(14) << "Gan"
              << std::setw(12) << "Diff"
              << std::setw(8) << "Diff%"
              << std::endl;
    std::cout << std::string(66, '-') << std::endl;

    for (const auto& r : allResults) {
        double diff = r.ourFMV - r.ganFMV;
        double pctDiff = (std::abs(r.ganFMV) > 1.0)
                       ? diff / r.ganFMV * 100.0 : 0.0;
        std::cout << std::setw(8) << r.label
                  << std::setw(10) << r.recordID
                  << std::setw(14) << std::setprecision(2) << r.ourFMV
                  << std::setw(14) << std::setprecision(2) << r.ganFMV
                  << std::setw(12) << std::setprecision(2) << diff
                  << std::setw(7) << std::setprecision(1) << pctDiff << "%"
                  << std::endl;
    }

    std::cout << "==========================================================="
              << std::endl;

    ThreadPool::getInstance()->stop();
    return 0;
}
