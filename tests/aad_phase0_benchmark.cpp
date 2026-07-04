#include <aad/AAD.hpp>
#include <demographics/mortality.hpp>
#include <esg/multivariate_blackscholes.hpp>
#include <esg/yieldcurve.hpp>
#include <math/matrix.hpp>
#include <math/randomnumbers/sobol.hpp>
#include <montecarlo/mcsimulationAAD.hpp>
#include <montecarlo/parallelmcsimulationAAD.hpp>
#include <products/va_gmdb.hpp>
#include <products/va_policy.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

using namespace ActuaLib;

class SeededSobol final : public RNG {
public:
    explicit SeededSobol(unsigned seedOffset = 0) : seedOffset_(seedOffset) {}

    void init(const size_t simDim) override {
        dim_ = simDim;
        sobol_.init(simDim);
        sobol_.skipTo(seedOffset_);
    }

    void nextU(std::vector<double>& uVec) override {
        sobol_.nextU(uVec);
    }

    void nextG(std::vector<double>& gaussVec) override {
        sobol_.nextG(gaussVec);
    }

    std::unique_ptr<RNG> clone() const override {
        return std::make_unique<SeededSobol>(*this);
    }

    size_t simDim() const override {
        return dim_;
    }

    void skipTo(const unsigned b) override {
        sobol_.skipTo(seedOffset_ + b);
    }

private:
    unsigned seedOffset_ = 0;
    size_t dim_ = 0;
    Sobol sobol_;
};

template <class T>
std::vector<T> toTyped(const std::vector<double>& values) {
    std::vector<T> out(values.size());
    std::transform(values.begin(), values.end(), out.begin(),
                   [](double v) { return T(v); });
    return out;
}

MortalityTable makeMortalityTable() {
    std::vector<int> ages;
    std::vector<double> qx;
    ages.reserve(121);
    qx.reserve(121);
    for (int age = 0; age <= 120; ++age) {
        ages.push_back(age);
        double q = 0.0001 * std::exp(0.08 * static_cast<double>(age - 30));
        if (age < 20) {
            q = 0.0002;
        }
        q = std::min(1.0, std::max(0.00001, q));
        qx.push_back(q);
    }
    qx.back() = 1.0;
    return MortalityTable(ages, qx);
}

VAPolicy makePolicy() {
    VAPolicy pol;
    pol.currentAge = 45.0;
    pol.numMonths = 240;
    pol.female = true;
    pol.survivorship = 1.0;
    pol.monthsSinceIssue = 84;
    pol.gbAmt = 125000.0;
    pol.updateRule = BenefitBaseUpdate::StepUp;
    pol.baseFee = 0.02;
    pol.riderFee = 0.0025;
    pol.smooth = 0.01;

    pol.fundValues = {
        18000.0, 12000.0, 9000.0, 7000.0, 5000.0,
        15000.0, 10000.0, 14000.0, 8000.0, 6000.0
    };

    pol.fundFees = {
        0.003, 0.005, 0.006, 0.008, 0.001,
        0.0038, 0.0045, 0.0055, 0.0057, 0.0046
    };

    pol.fundMap = {
        {1.0, 0.0, 0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 0.0, 1.0},
        {0.6, 0.4, 0.0, 0.0, 0.0},
        {0.5, 0.0, 0.5, 0.0, 0.0},
        {0.5, 0.0, 0.0, 0.5, 0.0},
        {0.0, 0.3, 0.7, 0.0, 0.0},
        {0.2, 0.2, 0.2, 0.2, 0.2}
    };

    pol.validate();
    return pol;
}

template <class T>
MultivariateBlackScholes<T> makeModel(const VAPolicy& pol) {
    constexpr size_t d = 5;
    const double corr[5][5] = {
        { 1.000000,  0.761332,  0.556299,  0.238114, -0.025552 },
        { 0.761332,  1.000000,  0.443120,  0.131246, -0.024576 },
        { 0.556299,  0.443120,  1.000000,  0.153277, -0.023841 },
        { 0.238114,  0.131246,  0.153277,  1.000000,  0.062975 },
        {-0.025552, -0.024576, -0.023841,  0.062975,  1.000000 }
    };
    const std::vector<double> vols = {0.151255, 0.205336, 0.170563, 0.042894, 0.005663};
    const std::vector<double> tenors = {1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0, 20.0, 25.0, 30.0};
    const std::vector<double> swaps = {
        0.00280228, 0.00579118, 0.01012285, 0.01768415, 0.02283698,
        0.02751166, 0.03111376, 0.03286486, 0.03388387, 0.03453971
    };

    std::vector<double> pa(d, 0.0);
    for (size_t f = 0; f < pol.fundValues.size(); ++f) {
        for (size_t k = 0; k < d; ++k) {
            pa[k] += pol.fundMap[f][k] * pol.fundValues[f];
        }
    }
    for (auto& x : pa) {
        if (x < 1.0e-8) {
            x = 1.0;
        }
    }

    Matrix<double> corrM(d, d);
    for (size_t i = 0; i < d; ++i) {
        for (size_t j = 0; j < d; ++j) {
            corrM[i][j] = corr[i][j];
        }
    }

    YieldCurve<T> curve;
    curve.bootstrap(tenors, toTyped<T>(swaps));

    return MultivariateBlackScholes<T>(
        toTyped<T>(pa),
        std::vector<T>(d, T(0.0)),
        vols,
        corrM,
        curve);
}

double mean(const std::vector<double>& v) {
    if (v.empty()) {
        return 0.0;
    }
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double l2norm(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v) {
        s += x * x;
    }
    return std::sqrt(s);
}

} // namespace

int main(int argc, char** argv) {
    using namespace ActuaLib;

    std::string mode = "serial";
    unsigned seed = 0;
    size_t paths = 15000;

    if (argc > 1) mode = argv[1];
    if (argc > 2) seed = static_cast<unsigned>(std::stoul(argv[2]));
    if (argc > 3) paths = static_cast<size_t>(std::stoull(argv[3]));

    const auto policy = makePolicy();
    const auto mortality = makeMortalityTable();

    GMDB<Number> product(policy, mortality);
    auto model = makeModel<Number>(policy);
    SeededSobol rng(seed);

    const auto agg = [](const std::vector<Number>& p) { return p.front(); };

    if (mode == "parallel") {
        ThreadPool::getInstance()->start(std::max<size_t>(1, std::thread::hardware_concurrency() - 1));
    }

    const auto t0 = std::chrono::high_resolution_clock::now();

    AADSimulResults res(0, 0, 0);
    if (mode == "parallel") {
        res = mcParallelSimulationAAD(product, model, rng, paths, agg);
    } else {
        res = mcSimulationAAD(product, model, rng, paths, agg);
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (mode == "parallel") {
        ThreadPool::getInstance()->stop();
    }

    std::cout << std::fixed << std::setprecision(8)
              << mode << ","
              << seed << ","
              << paths << ","
              << ms << ","
              << mean(res.aggregated) << ","
              << l2norm(res.risks)
              << "\n";

    return 0;
}
