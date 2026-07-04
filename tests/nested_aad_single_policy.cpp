#include <gtest/gtest.h>

#include <concurrency/threadpool.hpp>
#include <demographics/mortality.hpp>
#include <math/matrix.hpp>
#include <math/randomnumbers/sobol.hpp>
#include <montecarlo/nested_simulation.hpp>
#include <products/va_gmdb.hpp>

#include <cmath>
#include <vector>

namespace {

using namespace ActuaLib;

MortalityTable buildMortality() {
    std::vector<int> ages;
    for (int age = 5; age <= 115; ++age) {
        ages.push_back(age);
    }

    std::vector<double> qx = {
        0.000159,0.000131,0.000110,0.000111,0.000114,
        0.000119,0.000127,0.000136,0.000147,0.000159,
        0.000172,0.000186,0.000200,0.000215,0.000230,
        0.000245,0.000260,0.000276,0.000291,0.000307,
        0.000322,0.000336,0.000350,0.000362,0.000373,
        0.000383,0.000392,0.000400,0.000407,0.000415,
        0.000426,0.000439,0.000457,0.000481,0.000512,
        0.000549,0.000593,0.000646,0.000706,0.000775,
        0.000853,0.000941,0.001039,0.001149,0.001270,
        0.001403,0.001548,0.001705,0.001876,0.002060,
        0.002260,0.002477,0.002713,0.002970,0.003252,
        0.003566,0.003916,0.004308,0.004746,0.005231,
        0.005762,0.006339,0.006963,0.007637,0.008390,
        0.009256,0.010268,0.011459,0.012859,0.014484,
        0.016339,0.018428,0.020760,0.023347,0.026211,
        0.029387,0.032930,0.036914,0.041430,0.046591,
        0.052542,0.059450,0.067498,0.076895,0.087862,
        0.100605,0.115206,0.131624,0.149656,0.169013,
        0.189296,0.210104,0.231046,0.251752,0.271880,
        0.291121,0.309202,0.325889,0.340989,0.354344,
        0.365835,0.375391,0.382983,0.388617,0.392325,
        1.000000
    };
    while (static_cast<int>(qx.size()) < static_cast<int>(ages.size())) {
        qx.push_back(1.0);
    }

    MortalityTable mortality;
    mortality.set(ages, qx);
    return mortality;
}

Matrix<double> buildCorrelationMatrix() {
    Matrix<double> corr(1, 1);
    corr[0][0] = 1.0;
    return corr;
}

VAPolicy makePolicy() {
    VAPolicy policy;
    policy.currentAge            = 65.0;
    policy.numMonths             = 120;
    policy.female                = true;
    policy.survivorship          = 1.0;
    policy.monthsSinceIssue      = 0;
    policy.updateRule            = BenefitBaseUpdate::StepUp;
    policy.gbAmt                 = 100000.0;
    policy.rollUpRate            = 0.0;
    policy.gmwbBalance           = 0.0;
    policy.wbWithdrawalRate      = 0.0;
    policy.maturityPeriod        = 0;
    policy.firstMaturityMonth    = 0;
    policy.renewalPeriod         = 0;
    policy.guaranteedAnnuityRate = 0.05;
    policy.baseFee               = 0.005;
    policy.riderFee              = 0.010;
    policy.smooth                = 0.02;
    policy.fundValues            = { 100000.0 };
    policy.fundFees              = { 0.0 };
    policy.validate();
    return policy;
}

NestedSimConfig makeConfig() {
    NestedSimConfig config;

    RSParams rsParams;
    rsParams.nIndices = 1;
    rsParams.mu1 = { 0.08 };
    rsParams.sigma1 = { 0.15 };
    rsParams.mu2 = { 0.00 };
    rsParams.sigma2 = { 0.25 };
    rsParams.correlation = { 1.0 };
    rsParams.p12 = 0.25;
    rsParams.p21 = 0.75;
    rsParams.initProb2 = 0.25;

    IRModelParams irParams;
    irParams.tau1   = 0.04;
    irParams.tau2   = 0.03;
    irParams.tau3   = 0.01;
    irParams.beta1  = 0.10;
    irParams.beta2  = 0.15;
    irParams.beta3  = 0.05;
    irParams.sigma1 = 0.05;
    irParams.sigma2 = 0.02;
    irParams.sigma3 = 0.20;
    irParams.rho12  = 0.30;
    irParams.rho13  = 0.10;
    irParams.rho23  = -0.20;

    config.nRWPaths       = 1;
    config.rsParams       = rsParams;
    config.rwSeed         = 12345;
    config.irParams       = irParams;
    config.initialIRShort = 0.02;
    config.initialIRLong  = 0.04;
    config.initialIRVol   = 0.01;
    config.irSeed         = 54321;
    config.initialSpots   = { 100000.0 };
    config.vols           = { 0.20 };
    config.divs           = { 0.00 };
    config.corrMatrix     = buildCorrelationMatrix();
    config.nRNPaths       = 64;
    return config;
}

RWScenarioSet makeOuterPath() {
    RWScenarioSet rw(1, 12, 1);
    const double growths[12] = {
        1.0120,
        0.9960,
        1.0080,
        1.0030,
        1.0070,
        0.9940,
        1.0090,
        1.0040,
        0.9980,
        1.0060,
        1.0020,
        1.0050
    };

    for (size_t t = 1; t <= 12; ++t) {
        rw(0, t, 0) = growths[t - 1];
    }
    return rw;
}

TEST(NestedAadSinglePolicy, GmdbSinglePathRegression) {
    ThreadPool::getInstance()->start(1);

    const auto mortality = buildMortality();
    const auto policy = makePolicy();
    const auto config = makeConfig();
    const auto rw = makeOuterPath();

    GMDB<double> product(policy, mortality);
    Sobol equityRng;
    Sobol rateRng;

    const auto results = NestedSimulation::runSinglePeriod<GMDB>(
        config,
        product,
        equityRng,
        rateRng,
        12,
        &rw);

    ThreadPool::getInstance()->stop();

    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results.front().dollarDeltas.size(), 1u);
    ASSERT_FALSE(results.front().rho.empty());

    constexpr double expectedFmv = -6664.7316545844;
    constexpr double expectedDelta = -8495.5555804921;
    constexpr double expectedFirstRho = 76.9420449168;
    const std::vector<double> expectedRho = {
        expectedFirstRho,
        121.8809604788,
        -860.7074225266,
        -624.1538073712,
        -97.0719866975,
        736.0024514404,
        2230.3346836049,
        1033.7821576372,
        0.0,
        0.0
    };

    EXPECT_TRUE(std::isfinite(results.front().fmv));
    EXPECT_NEAR(results.front().fmv, expectedFmv, 1e-9);

    EXPECT_TRUE(std::isfinite(results.front().dollarDeltas[0]));
    EXPECT_NEAR(results.front().dollarDeltas[0], expectedDelta, 1e-9);

    ASSERT_EQ(results.front().rho.size(), expectedRho.size());
    for (size_t i = 0; i < expectedRho.size(); ++i) {
        EXPECT_TRUE(std::isfinite(results.front().rho[i]));
        EXPECT_NEAR(results.front().rho[i], expectedRho[i], 1e-9);
    }
}

} // namespace