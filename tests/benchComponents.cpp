/*
    Benchmark: scenario generation vs cash-flow projection.

    Runs a serial MC loop decomposing time into:
      1. RNG — Sobol quasi-random number generation
      2. ESG — MultivariateBlackScholes::generatePath  (5 correlated indices)
      3. CF  — Product::payoffs  (VA cash-flow projection)

    Uses DBRP (70001) as a representative short policy (119 months)
    and DBIB (110001) as a representative long combo (360 months).
*/

#include <esg/multivariate_blackscholes.hpp>
#include <montecarlo/scenario.hpp>
#include <products/va_gmdb.hpp>
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

using namespace ActuaLib;
using Clock = std::chrono::high_resolution_clock;
using ns    = std::chrono::nanoseconds;

// ================================================================
//  Mortality tables — IAM 1996  (ages 5–115)
// ================================================================

static MortalityTable makeFemaleTable() {
    std::vector<int> ages(111);
    std::iota(ages.begin(), ages.end(), 5);
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
    return MortalityTable(ages, qx);
}

static MortalityTable makeMaleTable() {
    std::vector<int> ages(111);
    std::iota(ages.begin(), ages.end(), 5);
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
    return MortalityTable(ages, qx);
}

// ================================================================
//  Market data
// ================================================================

static const size_t d = 5;

static const std::vector<std::vector<double>> standardFundMap = {
    { 1.0, 0.0, 0.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0, 0.0, 0.0 },
    { 0.0, 0.0, 1.0, 0.0, 0.0 },
    { 0.0, 0.0, 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 0.0, 0.0, 1.0 },
    { 0.6, 0.4, 0.0, 0.0, 0.0 },
    { 0.5, 0.0, 0.5, 0.0, 0.0 },
    { 0.5, 0.0, 0.0, 0.5, 0.0 },
    { 0.0, 0.3, 0.7, 0.0, 0.0 },
    { 0.2, 0.2, 0.2, 0.2, 0.2 }
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

static std::vector<double> computePA(const std::vector<double>& fundValues) {
    std::vector<double> PA(d, 0.0);
    for (size_t f = 0; f < fundValues.size(); ++f)
        for (size_t k = 0; k < d; ++k)
            PA[k] += standardFundMap[f][k] * fundValues[f];
    for (auto& pa : PA)
        if (pa < 1e-8) pa = 1.0;
    return PA;
}

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
//  Component-level timing benchmark
// ================================================================

struct ComponentTiming {
    std::string label;
    size_t      nSteps;
    size_t      nPaths;
    double      rng_ns;
    double      esg_ns;
    double      cf_ns;
    double      total_ns;
};

static ComponentTiming benchmarkPolicy(
    const std::string& label,
    Product<double>& product,
    const std::vector<double>& fundValues,
    size_t nPaths)
{
    auto mdl = makeModel(fundValues);
    Sobol rng;

    auto cMdl = mdl.clone();
    auto cRng = rng.clone();

    const size_t nPayoffs = product.payoffLabels().size();

    cMdl->allocate(product.timeline(), product.defline());
    cMdl->init(product.timeline(), product.defline());
    cRng->init(cMdl->simDim());

    std::vector<double> gaussVec(cMdl->simDim());
    Scenario<double> path;
    allocatePath(product.defline(), path);
    initializePath(path);
    std::vector<double> payoffs(nPayoffs);

    long long rngTotal = 0, esgTotal = 0, cfTotal = 0;

    auto loopStart = Clock::now();

    for (size_t i = 0; i < nPaths; ++i) {
        auto t0 = Clock::now();
        cRng->nextG(gaussVec);
        auto t1 = Clock::now();
        cMdl->generatePath(gaussVec, path);
        auto t2 = Clock::now();
        product.payoffs(path, payoffs);
        auto t3 = Clock::now();

        rngTotal += std::chrono::duration_cast<ns>(t1 - t0).count();
        esgTotal += std::chrono::duration_cast<ns>(t2 - t1).count();
        cfTotal  += std::chrono::duration_cast<ns>(t3 - t2).count();
    }

    auto loopEnd = Clock::now();
    double totalNs = static_cast<double>(
        std::chrono::duration_cast<ns>(loopEnd - loopStart).count());

    const size_t nSteps = product.timeline().size();

    return ComponentTiming{
        label, nSteps, nPaths,
        static_cast<double>(rngTotal),
        static_cast<double>(esgTotal),
        static_cast<double>(cfTotal),
        totalNs
    };
}

static void printTiming(const ComponentTiming& t) {
    double sum = t.rng_ns + t.esg_ns + t.cf_ns;
    double pctRng = t.rng_ns / sum * 100.0;
    double pctEsg = t.esg_ns / sum * 100.0;
    double pctCf  = t.cf_ns  / sum * 100.0;
    double overhead = (t.total_ns - sum) / t.total_ns * 100.0;

    double perPathUs = t.total_ns / t.nPaths / 1000.0;
    double rngPerPath = t.rng_ns / t.nPaths / 1000.0;
    double esgPerPath = t.esg_ns / t.nPaths / 1000.0;
    double cfPerPath  = t.cf_ns  / t.nPaths / 1000.0;

    std::cout << std::fixed;
    std::cout << "\n  " << t.label << "  (" << t.nSteps
              << " monthly steps, " << t.nPaths << " paths)\n";
    std::cout << "  --------------------------------------------------\n";
    std::cout << "  Component       Total (ms)   Per-path (us)   Share\n";
    std::cout << "  --------------------------------------------------\n";
    std::cout << "  RNG (Sobol)   " << std::setw(10) << std::setprecision(1)
              << t.rng_ns / 1e6 << "   " << std::setw(13) << std::setprecision(2)
              << rngPerPath << "   " << std::setprecision(1) << pctRng << "%\n";
    std::cout << "  ESG (5 GBMs)  " << std::setw(10) << std::setprecision(1)
              << t.esg_ns / 1e6 << "   " << std::setw(13) << std::setprecision(2)
              << esgPerPath << "   " << std::setprecision(1) << pctEsg << "%\n";
    std::cout << "  CF  (payoffs) " << std::setw(10) << std::setprecision(1)
              << t.cf_ns / 1e6  << "   " << std::setw(13) << std::setprecision(2)
              << cfPerPath  << "   " << std::setprecision(1) << pctCf << "%\n";
    std::cout << "  --------------------------------------------------\n";
    std::cout << "  Total (wall)  " << std::setw(10) << std::setprecision(1)
              << t.total_ns / 1e6 << "   " << std::setw(13) << std::setprecision(2)
              << perPathUs << "   (overhead "
              << std::setprecision(1) << overhead << "%)\n";
}

// ================================================================
//  Main
// ================================================================

int main() {

    auto femaleMort = makeFemaleTable();
    auto maleMort   = makeMaleTable();

    const size_t nPaths = 100000;

    std::cout << "=========================================================\n"
              << "  Component Benchmark: RNG vs ESG vs Cash-Flow Projection\n"
              << "  Paths: " << nPaths << " (serial, Sobol)\n"
              << "=========================================================\n";

    // ----- 1. DBRP (119 months) — short, standalone GMDB -----
    {
        VAPolicy pol;
        pol.currentAge       = 34.773;
        pol.numMonths        = 119;
        pol.female           = true;
        pol.survivorship     = 1.0;
        pol.monthsSinceIssue = 109;
        pol.gbAmt            = 143299.596476;
        pol.updateRule       = BenefitBaseUpdate::ReturnOfPremium;
        pol.rollUpRate       = 0.0;
        pol.baseFee          = 0.02;
        pol.riderFee         = 0.0025;
        pol.smooth           = 0.0;
        pol.fundValues = {
            17305.777556, 0.0, 10249.159843, 0.0, 15163.152030,
            17039.858265, 13435.714669, 12769.149239, 11952.472558, 13720.639418
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB<double> product(pol, femaleMort);
        auto t = benchmarkPolicy("DBRP (GMDB, 119 months)", product,
                                 pol.fundValues, nPaths);
        printTiming(t);
    }

    // ----- 2. DBIB (360 months) — long, combo GMDB+GMIB -----
    {
        VAPolicy pol;
        pol.currentAge       = 54.907;
        pol.numMonths        = 360;
        pol.female           = false;
        pol.survivorship     = 1.0;
        pol.monthsSinceIssue = 109;
        pol.gbAmt            = 163456.483994;
        pol.updateRule       = BenefitBaseUpdate::ReturnOfPremium;
        pol.rollUpRate       = 0.0;
        pol.baseFee          = 0.02;
        pol.riderFee         = 0.0025;
        pol.smooth           = 0.0;
        pol.firstMaturityMonth = 1;
        pol.renewalPeriod      = 0;
        pol.fundValues = {
            30979.555782, 24165.671009, 0.0, 0.0, 0.0,
            0.0, 25145.814879, 45145.927620, 0.0, 0.0
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        GMDB_IB<double> product(pol, maleMort);
        auto t = benchmarkPolicy("DBIB (GMDB+GMIB combo, 360 months)", product,
                                 pol.fundValues, nPaths);
        printTiming(t);
    }

    // ----- 3. WBSU (299 months) — GMWB, StepUp -----
    {
        VAPolicy pol;
        pol.currentAge       = 43.110;
        pol.numMonths        = 299;
        pol.female           = true;
        pol.survivorship     = 1.0;
        pol.monthsSinceIssue = 109;
        pol.gbAmt            = 101791.508174;
        pol.updateRule       = BenefitBaseUpdate::StepUp;
        pol.rollUpRate       = 0.0;
        pol.baseFee          = 0.02;
        pol.riderFee         = 0.008;
        pol.smooth           = 0.0;
        pol.wbWithdrawalRate = 0.07;
        pol.fundValues = {
            0.0, 14447.879459, 0.0, 0.0, 10413.760802,
            0.0, 15652.424367, 0.0, 18457.020709, 14654.655449
        };
        pol.fundFees = standardFundFees;
        pol.fundMap  = standardFundMap;

        auto product = GMWB<double>(pol, femaleMort);
        auto t = benchmarkPolicy("WBSU (GMWB, 299 months)", product,
                                 pol.fundValues, nPaths);
        printTiming(t);
    }

    std::cout << "\n=========================================================\n";
    return 0;
}
