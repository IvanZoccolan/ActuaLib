/*
    simulate_rider — single-contract VA nested pricer with Hull-White 1F inner model.

    Prices one Variable Annuity contract under the Hull-White one-factor
    stochastic interest-rate model for the inner (Q-measure) loop, combined
    with a 5-asset multivariate GBM outer (real-world) equity generator.

    Outputs results to an HDF5 file with per-outer-path FMV, aggregated
    dollar-deltas (equity sensitivities) and rho (IR sensitivities).

    ─────────────────────────────────────────────────────────────────────────
    USAGE
    ─────────────────────────────────────────────────────────────────────────
      ./simulate_rider [OPTIONS]

    REQUIRED OPTIONS
      --rider <name>      Rider type. One of:
                            GMDB     Guaranteed Minimum Death Benefit
                            GMMB     Guaranteed Minimum Maturity Benefit
                            GMAB     Guaranteed Minimum Accumulation Benefit
                            GMWB     Guaranteed Minimum Withdrawal Benefit
                            GMIB     Guaranteed Minimum Income Benefit
                            GMDB_AB  GMDB + GMAB (combined)
                            GMDB_MB  GMDB + GMMB (combined)
                            GMDB_WB  GMDB + GMWB (combined)
                            GMDB_IB  GMDB + GMIB (combined)

    POLICY OPTIONS (defaults match the Gan-Valdez reference contract)
      --age <years>       Policyholder's current age in years.        [65.0]
      --gender <M|F>      Policyholder gender (M = male, F = female). [F]
      --maturity <months> Months from valuation to contract maturity. [120]
      --horizon <months>  Outer real-world projection horizon from today
                          before the nested inner pricing step.        [12]
      --months-since-issue <n>  Months elapsed since policy inception.
                          Used to align annual fee anniversaries.      [0]
      --fund-fees <f,...,f>  Per-fund annual MER expense-ratio rates (5
                          values, comma-separated).
                          [0.0040,0.0050,0.0060,0.0025,0.0010]
      --gmwb-balance <amount>  Starting GMWB benefit account balance.
                          Defaults to --gb.
      --av <amount>       Total initial account value (dollars).       [110000]
      --alloc <w,...,w>   Comma-separated fund allocation weights (5),
                          must sum to 1.    [0.30,0.25,0.20,0.15,0.10]
      --gb <amount>       Guaranteed benefit amount (0 = use --av).   [0]
      --rider-fee <rate>  Annual rider charge (e.g. 0.010 = 1%).      [0.010]
      --base-fee <rate>   Annual base/MER fee.                         [0.005]
      --update-rule <r>   Benefit-base update rule on anniversaries:
                            step-up  GB = max(GB, AV)                [step-up]
                            rop      GB fixed (return of premium)
                            roll-up  GB *= (1 + roll-up-rate)
      --roll-up-rate <r>  Annual roll-up rate for roll-up rule.        [0.03]
      --wb-withdrawal-rate <r>  Annual withdrawal rate for GMWB/GMDB_WB
                          riders (e.g. 0.05 = 5 %/yr of guarantee base).[0.05]
      --gmib-annuity-rate <r>   Guaranteed annuity payout rate for
                          GMIB/GMDB_IB riders.                         [0.05]

    SIMULATION OPTIONS
      --outer <n>         Number of outer real-world paths.            [48]
      --inner <n>         Number of inner Q-measure (pricing) paths.   [256]
      --seed <uint64>     Base seed: outer equity (rwSeed), outer IR
                          (seed+11111), inner mrg32k3a (seed+99999/99998).
                          Sobol inner RNG is deterministic (ignores seed).[12345]
      --threads <n>       Worker threads for outer parallelism.        [7]
      --rng <type>        Inner RNG type: sobol or mrg32k3a.           [sobol]
                          mrg32k3a uses antithetic variates (always on).

    HULL-WHITE MODEL OPTIONS
      --hw-a <value>      HW1F mean-reversion speed a (years⁻¹).      [0.10]
      --hw-sigma <value>  HW1F short-rate volatility σ.               [0.01]

    OUTPUT OPTIONS
      --output <file>     Path to HDF5 output file.                    [simulate_rider_out.h5]

    ─────────────────────────────────────────────────────────────────────────
    OUTPUT HDF5 SCHEMA
    ─────────────────────────────────────────────────────────────────────────
    /results/
      fmv_per_path      [nOuter]          double  Fair market value on each outer path
      delta             [nOuter × 5]      double  Dollar-delta per outer path per equity index
      rho               [nOuter × nCurve] double  IR sensitivity dFMV/dD(T_i) per outer path
      curve_tenors      [nCurve]          double  Tenor grid for the rho columns (years)
      outer_spots       [nOuter × 5]      double  Equity spot prices at outer pricing date
      outer_disc_factors[nOuter × nCurve] double  IR discount factors at outer pricing date
      outer_fund_values [nOuter × 5]      double  Fund account values at outer pricing date
    /params/            (scalar dataset, f64 or string attributes)
      rider             string     Rider type name
      age               f64        Policyholder age (years)
      gender            string     "M" or "F"
      maturity_months   f64        Months to maturity
      horizon_months    f64        Outer projection horizon in months
      months_since_issue f64       Months elapsed since policy inception
      av                f64        Total initial account value
      gb                f64        Effective guarantee base amount
      alloc_0..4        f64        Fund allocation weights (5 sub-accounts)
      rider_fee         f64        Annual rider charge
      base_fee          f64        Annual base/MER fee
      update_rule       string     Benefit-base update rule (step-up/rop/roll-up)
      roll_up_rate      f64        Annual roll-up rate (meaningful when update_rule=roll-up)
      wb_withdrawal_rate  f64        Annual withdrawal rate (GMWB/GMDB_WB)
      gmib_annuity_rate   f64        Guaranteed annuity rate (GMIB/GMDB_IB)
      fund_fee_0..4     f64        Per-fund annual MER expense-ratio rates
      gmwb_balance_initial f64     Starting GMWB benefit account balance
      use_hw            f64        1.0 if HW1F inner model enabled
      hw_a              f64        HW mean-reversion speed
      hw_sigma          f64        HW short-rate vol
      n_outer           f64        Number of outer paths
      n_inner           f64        Number of inner paths
      seed              f64        RNG seed
      rng               string     Inner RNG type (sobol / mrg32k3a)

    ─────────────────────────────────────────────────────────────────────────
    MODELS
    ─────────────────────────────────────────────────────────────────────────
    Outer equity:  Regime-switching 2-state real-world GBM (5 indices).
    Outer rates:   AIRG 3-factor log-normal (AAA ESWG model).
    Inner equity:  Multivariate GBM with AAD.
    Inner rates:   Hull-White one-factor (HW1F), calibrated to the outer
                   yield curve at each outer path endpoint.  The path-
                   dependent numeraire exp(-∫r dt) and realised-rate equity
                   drift are simulated; the numeraire channel is on the AAD
                   tape so that dFMV/dD(T_i) (rho) is computed correctly.

    ─────────────────────────────────────────────────────────────────────────
    EXAMPLES
    ─────────────────────────────────────────────────────────────────────────
      # Price GMDB at default settings, write results to out.h5
      ./simulate_rider --rider GMDB --output out.h5

      # 65-year-old male, 10-year GMMB, 100 outer × 512 inner paths
      ./simulate_rider --rider GMMB --age 65 --gender M --maturity 120 \\
          --outer 100 --inner 512 --output gmmb_male.h5

      # GMWB with HW1F mean-reversion speed 0.10
      ./simulate_rider --rider GMWB --hw-a 0.10 --output gmwb_hw.h5

      # GMMB with roll-up guarantee (4 % / yr)
      ./simulate_rider --rider GMMB --update-rule roll-up --roll-up-rate 0.04 \\
          --output gmmb_rollup.h5

      # GMDB with return-of-premium (fixed guarantee, no step-up)
      ./simulate_rider --rider GMDB --update-rule rop --output gmdb_rop.h5
*/

#include <montecarlo/nested_simulation.hpp>
#include <demographics/mortality.hpp>
#include <products/va_factory.hpp>
#include <math/randomnumbers/sobol.hpp>
#include <math/randomnumbers/mrg32k3a.hpp>
#include <math/matrix.hpp>

// HDF5 C API
#include <hdf5.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ActuaLib;

// ─────────────────────────────────────────────────────────────────────────────
//  CLI argument struct
// ─────────────────────────────────────────────────────────────────────────────

struct Args {
    // Policy
    std::string riderName   = "";
    double      age         = 65.0;
    bool        female      = true;   // --gender F → true, M → false
    int         maturity    = 120;    // months
    int         horizon     = 12;     // months

    // Fund / guarantee
    double      av          = 110000.0;                                        // total initial account value
    std::vector<double> alloc = { 0.30, 0.25, 0.20, 0.15, 0.10 };                   // fund allocation weights
    double      gb          = 0.0;    // guaranteed benefit (0 = at-the-money, i.e. = av)
    double      riderFee    = 0.010;  // annual rider charge
    double      baseFee     = 0.005;  // annual base / MER fee
    std::string updateRule  = "step-up"; // "step-up", "rop", "roll-up"
    double      rollUpRate  = 0.03;   // annual roll-up rate (used when updateRule="roll-up")
    double      wbRate      = 0.05;   // annual withdrawal rate for GMWB/GMDB_WB
    double      gmibRate    = 0.05;   // guaranteed annuity rate for GMIB/GMDB_IB
    int         monthsSinceIssue = 0; // months elapsed since policy inception (for anniversary timing)
    std::vector<double> fundFees = { 0.0040, 0.0050, 0.0060, 0.0025, 0.0010 }; // per-fund annual MER
    double      gmwbBalance  = -1.0;  // starting GMWB balance (<0 = use gbAmt)

    // Simulation
    size_t      nOuter      = 48;
    size_t      nInner      = 256;
    uint64_t    seed        = 12345;
    size_t      threads     = 7;
    std::string rngType     = "sobol";  // "sobol" or "mrg32k3a" (antithetic always on)

    // HW1F
    bool        useHW       = true;
    double      hwA         = 0.10;
    double      hwSigma     = 0.01;

    // Output
    std::string outputFile  = "simulate_rider_out.h5";
};

static void printHelp()
{
    std::cout << R"(simulate_rider — single-contract VA nested pricer (HW1F inner model)

USAGE
  ./simulate_rider [OPTIONS]

REQUIRED OPTIONS
  --rider <name>      Rider type. One of:
                        GMDB  GMMB  GMAB  GMWB  GMIB
                        GMDB_AB  GMDB_MB  GMDB_WB  GMDB_IB

POLICY OPTIONS
  --age <years>       Policyholder's current age in years.        [65.0]
  --gender <M|F>      Policyholder gender.                        [F]
  --maturity <months> Months from valuation to contract maturity. [120]
  --horizon <months>  Outer real-world projection horizon from today in months. [12]
  --months-since-issue <n>  Months elapsed since policy inception.
                      Used to align annual fee/benefit-base anniversaries
                      correctly during outer aging.               [0]
  --av <amount>       Total initial account value (dollars).       [110000]
  --alloc <w,...,w>   Fund allocation weights (5 values, comma-
                      separated, must sum to 1).
                      [0.30,0.25,0.20,0.15,0.10]
  --gb <amount>       Guaranteed benefit amount (0 = use --av).   [0]
  --rider-fee <rate>  Annual rider charge (e.g. 0.010 = 1%).      [0.010]
  --base-fee <rate>   Annual base/MER fee.                         [0.005]
  --update-rule <r>   Benefit-base update rule on policy anniversaries:
                        step-up  GB = max(GB, AV)                [step-up]
                        rop      GB stays fixed (return of premium)
                        roll-up  GB *= (1 + roll-up-rate) annually
  --roll-up-rate <r>  Annual roll-up rate (used with roll-up rule). [0.03]
  --wb-withdrawal-rate <r>  Annual withdrawal rate for GMWB/GMDB_WB. [0.05]
  --gmib-annuity-rate <r>   Guaranteed annuity rate for GMIB/GMDB_IB.[0.05]
  --fund-fees <f,...,f>  Per-fund annual MER expense-ratio rates (5 values,
                      comma-separated).
                      [0.0040,0.0050,0.0060,0.0025,0.0010]
  --gmwb-balance <amount>  Starting GMWB benefit account balance.
                      Defaults to --gb (at-the-money).

SIMULATION OPTIONS
  --outer <n>         Outer real-world paths.                      [48]
  --inner <n>         Inner Q-measure (pricing) paths.             [256]
  --seed <uint64>     Base seed: outer equity + IR paths, and inner
                      mrg32k3a if used. Sobol is deterministic (no seed).[12345]
  --threads <n>       Worker threads.                              [7]
  --rng <type>        Inner RNG: sobol or mrg32k3a.                [sobol]
                      mrg32k3a enables antithetic variates (always on).

HULL-WHITE OPTIONS
  --no-hw             Disable HW1F; use static outer curve instead.
  --hw-a <value>      Mean-reversion speed a (years⁻¹).           [0.10]
  --hw-sigma <value>  Short-rate volatility sigma.                 [0.01]

OUTPUT OPTIONS
  --output <file>     HDF5 output file path.                       [simulate_rider_out.h5]

  --help              Print this message and exit.
)";
}

static Args parseArgs(int argc, char* argv[])
{
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("Flag '" + flag + "' requires a value");
            return argv[++i];
        };

        if (flag == "--help" || flag == "-h") {
            printHelp();
            std::exit(0);
        } else if (flag == "--rider") {
            args.riderName = next();
        } else if (flag == "--age") {
            args.age = std::stod(next());
        } else if (flag == "--gender") {
            const std::string g = next();
            if (g == "M" || g == "m")       args.female = false;
            else if (g == "F" || g == "f")  args.female = true;
            else throw std::runtime_error("--gender must be M or F");
        } else if (flag == "--maturity") {
            args.maturity = std::stoi(next());
        } else if (flag == "--horizon") {
            args.horizon = std::stoi(next());
        } else if (flag == "--months-since-issue") {
            args.monthsSinceIssue = std::stoi(next());
        } else if (flag == "--av") {
            args.av = std::stod(next());
        } else if (flag == "--alloc") {
            const std::string s = next();
            args.alloc.clear();
            size_t pos = 0, nxt;
            while ((nxt = s.find(',', pos)) != std::string::npos) {
                args.alloc.push_back(std::stod(s.substr(pos, nxt - pos)));
                pos = nxt + 1;
            }
            args.alloc.push_back(std::stod(s.substr(pos)));
            if (args.alloc.size() != 5)
                throw std::runtime_error("--alloc requires exactly 5 comma-separated weights");
        } else if (flag == "--gb") {
            args.gb = std::stod(next());
        } else if (flag == "--rider-fee") {
            args.riderFee = std::stod(next());
        } else if (flag == "--base-fee") {
            args.baseFee = std::stod(next());
        } else if (flag == "--update-rule") {
            args.updateRule = next();
            if (args.updateRule != "step-up" && args.updateRule != "rop" && args.updateRule != "roll-up")
                throw std::runtime_error("--update-rule must be 'step-up', 'rop', or 'roll-up'");
        } else if (flag == "--roll-up-rate") {
            args.rollUpRate = std::stod(next());
        } else if (flag == "--wb-withdrawal-rate") {
            args.wbRate = std::stod(next());
        } else if (flag == "--gmib-annuity-rate") {
            args.gmibRate = std::stod(next());
        } else if (flag == "--fund-fees") {
            const std::string s = next();
            args.fundFees.clear();
            size_t pos = 0, nxt;
            while ((nxt = s.find(',', pos)) != std::string::npos) {
                args.fundFees.push_back(std::stod(s.substr(pos, nxt - pos)));
                pos = nxt + 1;
            }
            args.fundFees.push_back(std::stod(s.substr(pos)));
            if (args.fundFees.size() != 5)
                throw std::runtime_error("--fund-fees requires exactly 5 comma-separated rates");
        } else if (flag == "--gmwb-balance") {
            args.gmwbBalance = std::stod(next());
        } else if (flag == "--outer") {
            args.nOuter = std::stoull(next());
        } else if (flag == "--inner") {
            args.nInner = std::stoull(next());
        } else if (flag == "--seed") {
            args.seed = std::stoull(next());
        } else if (flag == "--threads") {
            args.threads = std::stoull(next());
        } else if (flag == "--rng") {
            args.rngType = next();
            if (args.rngType != "sobol" && args.rngType != "mrg32k3a")
                throw std::runtime_error("--rng must be 'sobol' or 'mrg32k3a'");
        } else if (flag == "--no-hw") {
            args.useHW = false;
        } else if (flag == "--hw-a") {
            args.hwA = std::stod(next());
        } else if (flag == "--hw-sigma") {
            args.hwSigma = std::stod(next());
        } else if (flag == "--output") {
            args.outputFile = next();
        } else {
            throw std::runtime_error("Unknown flag: " + flag);
        }
    }

    if (args.riderName.empty())
        throw std::runtime_error("--rider is required (e.g. --rider GMDB)");
    if (args.age <= 0 || args.age >= 120)
        throw std::runtime_error("--age must be in (0, 120)");
    if (args.maturity <= 0)
        throw std::runtime_error("--maturity must be > 0");
    if (args.horizon < 0)
        throw std::runtime_error("--horizon must be >= 0");
    if (args.nOuter == 0)
        throw std::runtime_error("--outer must be >= 1");
    if (args.nInner == 0)
        throw std::runtime_error("--inner must be >= 1");
    if (args.av <= 0)
        throw std::runtime_error("--av must be > 0");
    if (args.alloc.size() != 5)
        throw std::runtime_error("--alloc must have exactly 5 weights");
    {
        const double wSum = std::accumulate(args.alloc.begin(), args.alloc.end(), 0.0);
        if (std::abs(wSum - 1.0) > 0.01)
            throw std::runtime_error("--alloc weights must sum to 1 (got " + std::to_string(wSum) + ")");
        for (double w : args.alloc)
            if (w < 0.0)
                throw std::runtime_error("--alloc weights must be non-negative");
    }
    if (args.gb < 0)
        throw std::runtime_error("--gb must be >= 0");
    if (args.riderFee < 0)
        throw std::runtime_error("--rider-fee must be >= 0");
    if (args.baseFee < 0)
        throw std::runtime_error("--base-fee must be >= 0");
    for (double f : args.fundFees)
        if (f < 0.0)
            throw std::runtime_error("--fund-fees rates must be non-negative");
    if (args.gmwbBalance != -1.0 && args.gmwbBalance < 0.0)
        throw std::runtime_error("--gmwb-balance must be >= 0 if specified");
    if (args.useHW && args.hwA <= 0)
        throw std::runtime_error("--hw-a must be > 0");
    if (args.useHW && args.hwSigma <= 0)
        throw std::runtime_error("--hw-sigma must be > 0");
    if (args.wbRate <= 0 || args.wbRate > 1.0)
        throw std::runtime_error("--wb-withdrawal-rate must be in (0, 1]");
    if (args.gmibRate <= 0 || args.gmibRate > 1.0)
        throw std::runtime_error("--gmib-annuity-rate must be in (0, 1]");

    return args;
}

static RiderType parseRiderType(const std::string& name)
{
    if (name == "GMDB")    return RiderType::GMDB;
    if (name == "GMMB")    return RiderType::GMMB;
    if (name == "GMAB")    return RiderType::GMAB;
    if (name == "GMWB")    return RiderType::GMWB;
    if (name == "GMIB")    return RiderType::GMIB;
    if (name == "GMDB_AB") return RiderType::GMDB_AB;
    if (name == "GMDB_MB") return RiderType::GMDB_MB;
    if (name == "GMDB_WB") return RiderType::GMDB_WB;
    if (name == "GMDB_IB") return RiderType::GMDB_IB;
    throw std::runtime_error("Unknown rider: '" + name +
        "'.  Valid names: GMDB GMMB GMAB GMWB GMIB GMDB_AB GMDB_MB GMDB_WB GMDB_IB");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Model configuration helpers  (mirroring nestedAllVARiders.cpp)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

    MortalityTable buildMortality()
    {
        std::vector<int> ages;
        for (int a = 5; a <= 115; ++a) ages.push_back(a);

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
        while (static_cast<int>(qx.size()) < static_cast<int>(ages.size()))
            qx.push_back(1.0);

        MortalityTable mortality;
        mortality.set(ages, qx);
        return mortality;
    }

    Matrix<double> buildCorrelationMatrix()
    {
        Matrix<double> corr(5, 5);
        const double c[5][5] = {
            {  1.000000,  0.761332,  0.556299,  0.238114, -0.025552 },
            {  0.761332,  1.000000,  0.443120,  0.131246, -0.024576 },
            {  0.556299,  0.443120,  1.000000,  0.153277, -0.023841 },
            {  0.238114,  0.131246,  0.153277,  1.000000,  0.062975 },
            { -0.025552, -0.024576, -0.023841,  0.062975,  1.000000 }
        };
        for (size_t i = 0; i < 5; ++i)
            for (size_t j = 0; j < 5; ++j)
                corr[i][j] = c[i][j];
        return corr;
    }

    std::vector<double> flattenRowMajor(const Matrix<double>& m)
    {
        std::vector<double> flat(m.rows() * m.cols());
        for (size_t i = 0; i < m.rows(); ++i)
            for (size_t j = 0; j < m.cols(); ++j)
                flat[i * m.cols() + j] = m[i][j];
        return flat;
    }

    NestedSimConfig makeConfig(const Args& args)
    {
        NestedSimConfig config;
        const Matrix<double> corr = buildCorrelationMatrix();

        RSParams rs;
        rs.nIndices  = 5;
        rs.mu1       = { 0.09, 0.10, 0.08, 0.035, 0.020 };
        rs.sigma1    = { 0.16, 0.22, 0.18, 0.05,  0.01  };
        rs.mu2       = { -0.08, -0.12, -0.09, 0.010, 0.015 };
        rs.sigma2    = { 0.28, 0.35, 0.30, 0.09, 0.02 };
        rs.correlation = flattenRowMajor(corr);
        rs.p12       = 0.25;
        rs.p21       = 0.75;
        rs.initProb2 = 0.20;

        IRModelParams irp;
        irp.tau1   = 0.04;  irp.tau2   = 0.03;  irp.tau3   = 0.01;
        irp.beta1  = 0.10;  irp.beta2  = 0.15;  irp.beta3  = 0.05;
        irp.sigma1 = 0.05;  irp.sigma2 = 0.02;  irp.sigma3 = 0.20;
        irp.rho12  = 0.30;  irp.rho13  = 0.10;  irp.rho23  = -0.20;

        config.nRWPaths       = args.nOuter;
        config.rsParams       = rs;
        config.rwSeed         = static_cast<uint64_t>(args.seed);
        config.irParams       = irp;
        config.initialIRShort = 0.02;
        config.initialIRLong  = 0.04;
        config.initialIRVol   = 0.01;
        config.irSeed         = args.seed + 11111ULL;
        config.initialSpots   = { 30000.0, 25000.0, 20000.0, 15000.0, 10000.0 };
        config.vols           = { 0.18, 0.24, 0.20, 0.06, 0.015 };
        config.divs           = { 0.00, 0.00, 0.00, 0.00, 0.00 };
        config.corrMatrix     = corr;
        config.nRNPaths       = args.nInner;

        config.useHullWhiteInner = args.useHW;
        config.hwParams.a        = args.hwA;
        config.hwParams.sigma    = args.hwSigma;

        return config;
    }

    VAPolicy makePolicy(const Args& args, RiderType rt)
    {
        VAPolicy policy;
        policy.currentAge            = args.age;
        policy.numMonths             = args.maturity;
        policy.female                = args.female;
        policy.survivorship          = 1.0;
        policy.monthsSinceIssue      = args.monthsSinceIssue;
        if (args.updateRule == "rop")
            policy.updateRule = BenefitBaseUpdate::ReturnOfPremium;
        else if (args.updateRule == "roll-up")
            policy.updateRule = BenefitBaseUpdate::RollUp;
        else
            policy.updateRule = BenefitBaseUpdate::StepUp;
        const double effectiveGb     = (args.gb > 0.0) ? args.gb : args.av;
        policy.gbAmt                 = effectiveGb;
        policy.rollUpRate            = (args.updateRule == "roll-up") ? args.rollUpRate : 0.0;
        policy.gmwbBalance           = 0.0;
        policy.wbWithdrawalRate      = 0.0;
        policy.maturityPeriod        = 0;
        policy.firstMaturityMonth    = 0;
        policy.renewalPeriod         = 0;
        policy.guaranteedAnnuityRate = args.gmibRate;
        policy.baseFee               = args.baseFee;
        policy.riderFee              = args.riderFee;
        policy.smooth                = 0.02;
        policy.fundValues.resize(5);
        for (size_t k = 0; k < 5; ++k)
            policy.fundValues[k] = args.av * args.alloc[k];
        policy.fundFees              = args.fundFees;

        switch (rt) {
        case RiderType::GMAB:
        case RiderType::GMDB_AB: {
            const int renewPeriod = 60;
            const int msi = args.monthsSinceIssue;
            const int rem = msi % renewPeriod;
            // Next 5-year anniversary expressed in months from TODAY
            policy.firstMaturityMonth = (rem == 0) ? renewPeriod : (renewPeriod - rem);
            policy.renewalPeriod      = renewPeriod;
            break;
        }
        case RiderType::GMWB:
        case RiderType::GMDB_WB: {
            policy.gmwbBalance      = (args.gmwbBalance >= 0.0) ? args.gmwbBalance : policy.gbAmt;
            policy.wbWithdrawalRate = args.wbRate;
            break;
        }
        case RiderType::GMIB:
        case RiderType::GMDB_IB:
            policy.guaranteedAnnuityRate = args.gmibRate;
            break;
        default:
            break;
        }

        return policy;
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  HDF5 output
// ─────────────────────────────────────────────────────────────────────────────

static void writeStringAttr(hid_t loc, const char* name, const std::string& val)
{
    hid_t dtype  = H5Tcopy(H5T_C_S1);
    H5Tset_size(dtype, val.size() + 1);
    H5Tset_strpad(dtype, H5T_STR_NULLTERM);
    hid_t space  = H5Screate(H5S_SCALAR);
    hid_t attr   = H5Acreate2(loc, name, dtype, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, dtype, val.c_str());
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(dtype);
}

static void writeDoubleAttr(hid_t loc, const char* name, double val)
{
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t attr  = H5Acreate2(loc, name, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, H5T_NATIVE_DOUBLE, &val);
    H5Aclose(attr);
    H5Sclose(space);
}

static void write1DDataset(hid_t grp, const char* name,
                            const std::vector<double>& data)
{
    hsize_t dims[1] = { data.size() };
    hid_t space = H5Screate_simple(1, dims, nullptr);
    hid_t dset  = H5Dcreate2(grp, name, H5T_NATIVE_DOUBLE, space,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());
    H5Dclose(dset);
    H5Sclose(space);
}

static void write2DDataset(hid_t grp, const char* name,
                            const std::vector<double>& data,
                            hsize_t nRows, hsize_t nCols)
{
    hsize_t dims[2] = { nRows, nCols };
    hid_t space = H5Screate_simple(2, dims, nullptr);
    hid_t dset  = H5Dcreate2(grp, name, H5T_NATIVE_DOUBLE, space,
                              H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());
    H5Dclose(dset);
    H5Sclose(space);
}

static void writeHDF5(
    const std::string&              outputFile,
    const Args&                     args,
    const std::vector<NestedResult>& results)
{
    const size_t nOuter  = results.size();
    const size_t d       = results.empty() ? 0 : results.front().dollarDeltas.size();
    const size_t nCurve  = results.empty() ? 0 : results.front().rho.size();

    // Build per-path arrays (row-major: row = outer path)
    std::vector<double> fmvPerPath(nOuter);
    std::vector<double> deltaPerPath(nOuter * d);    // [nOuter × d]
    std::vector<double> rhoPerPath(nOuter * nCurve); // [nOuter × nCurve]
    std::vector<double> outerSpotsFlat(nOuter * d);       // [nOuter × d]
    std::vector<double> outerDFFlat(nOuter * nCurve);     // [nOuter × nCurve]
    std::vector<double> outerFundValFlat(nOuter * d);     // [nOuter × d]
    std::vector<double> curveTenors;

    for (size_t p = 0; p < nOuter; ++p) {
        fmvPerPath[p] = results[p].fmv;
        for (size_t k = 0; k < d; ++k) {
            deltaPerPath[p * d + k]       = results[p].dollarDeltas[k];
            outerSpotsFlat[p * d + k]     = results[p].outerSpots.size() > k
                                            ? results[p].outerSpots[k] : 0.0;
            outerFundValFlat[p * d + k]   = results[p].outerFundValues.size() > k
                                            ? results[p].outerFundValues[k] : 0.0;
        }
        for (size_t i = 0; i < nCurve; ++i) {
            rhoPerPath[p * nCurve + i]  = results[p].rho[i];
            outerDFFlat[p * nCurve + i] = results[p].outerDiscFactors.size() > i
                                          ? results[p].outerDiscFactors[i] : 0.0;
        }
    }

    if (nOuter > 0) {
        // Skip the t=0 anchor (index 0) for the published tenor grid
        const auto& tenors = results.front().irCurve.tenors();
        curveTenors.assign(tenors.begin() + 1, tenors.end());
    }

    // Create parent directories if needed
    const auto parent = std::filesystem::path(outputFile).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    // Open/create file
    hid_t file = H5Fcreate(outputFile.c_str(), H5F_ACC_TRUNC,
                            H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0)
        throw std::runtime_error("Failed to create HDF5 file: " + outputFile);

    // /results group
    hid_t grpResults = H5Gcreate2(file, "/results", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    write1DDataset(grpResults, "fmv_per_path", fmvPerPath);
    write2DDataset(grpResults, "delta",        deltaPerPath,  static_cast<hsize_t>(nOuter), static_cast<hsize_t>(d));
    write2DDataset(grpResults, "rho",          rhoPerPath,    static_cast<hsize_t>(nOuter), static_cast<hsize_t>(nCurve));
    write1DDataset(grpResults, "curve_tenors", curveTenors);
    write2DDataset(grpResults, "outer_spots",        outerSpotsFlat,   static_cast<hsize_t>(nOuter), static_cast<hsize_t>(d));
    write2DDataset(grpResults, "outer_disc_factors", outerDFFlat,      static_cast<hsize_t>(nOuter), static_cast<hsize_t>(nCurve));
    write2DDataset(grpResults, "outer_fund_values",  outerFundValFlat, static_cast<hsize_t>(nOuter), static_cast<hsize_t>(d));
    H5Gclose(grpResults);

    // /params group — scalar attributes on the group itself
    hid_t grpParams = H5Gcreate2(file, "/params", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    writeStringAttr(grpParams, "rider",          args.riderName);
    writeDoubleAttr(grpParams, "age",            args.age);
    writeStringAttr(grpParams, "gender",         args.female ? "F" : "M");
    writeDoubleAttr(grpParams, "maturity_months", static_cast<double>(args.maturity));
    writeDoubleAttr(grpParams, "horizon_months",        static_cast<double>(args.horizon));
    writeDoubleAttr(grpParams, "months_since_issue",    static_cast<double>(args.monthsSinceIssue));
    writeDoubleAttr(grpParams, "av",             args.av);
    {
        const double effGb = (args.gb > 0.0) ? args.gb : args.av;
        writeDoubleAttr(grpParams, "gb",         effGb);
    }
    for (size_t k = 0; k < args.alloc.size(); ++k)
        writeDoubleAttr(grpParams, ("alloc_" + std::to_string(k)).c_str(), args.alloc[k]);
    writeDoubleAttr(grpParams, "rider_fee",      args.riderFee);
    writeDoubleAttr(grpParams, "base_fee",       args.baseFee);
    writeStringAttr(grpParams, "update_rule",    args.updateRule);
    writeDoubleAttr(grpParams, "roll_up_rate",   args.rollUpRate);
    writeDoubleAttr(grpParams, "wb_withdrawal_rate", args.wbRate);
    writeDoubleAttr(grpParams, "gmib_annuity_rate",  args.gmibRate);
    for (size_t k = 0; k < args.fundFees.size(); ++k)
        writeDoubleAttr(grpParams, ("fund_fee_" + std::to_string(k)).c_str(), args.fundFees[k]);
    {
        const double effGmwbBal = (args.gmwbBalance >= 0.0) ? args.gmwbBalance
                                  : ((args.gb > 0.0) ? args.gb : args.av);
        writeDoubleAttr(grpParams, "gmwb_balance_initial", effGmwbBal);
    }
    writeDoubleAttr(grpParams, "use_hw",         args.useHW ? 1.0 : 0.0);
    writeDoubleAttr(grpParams, "hw_a",           args.hwA);
    writeDoubleAttr(grpParams, "hw_sigma",       args.hwSigma);
    writeDoubleAttr(grpParams, "n_outer",        static_cast<double>(args.nOuter));
    writeDoubleAttr(grpParams, "n_inner",        static_cast<double>(args.nInner));
    writeDoubleAttr(grpParams, "seed",           static_cast<double>(args.seed));
    writeStringAttr(grpParams, "rng",            args.rngType);
    H5Gclose(grpParams);

    H5Fclose(file);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dispatch: run the right product template
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<NestedResult> runRider(
    RiderType              riderType,
    const VAPolicy&        policy,
    const MortalityTable&  mortality,
    const NestedSimConfig& config,
    const RNG&             equityRng,
    const RNG&             rateRng,
    size_t                 outerMonths)
{
    switch (riderType) {
    case RiderType::GMDB: {
        GMDB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMDB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMMB: {
        GMMB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMMB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMAB: {
        GMAB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMAB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMWB: {
        GMWB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMWB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMIB: {
        GMIB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMIB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMDB_AB: {
        GMDB_AB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMDB_AB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMDB_MB: {
        GMDB_MB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMDB_MB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMDB_WB: {
        GMDB_WB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMDB_WB>(config, p, equityRng, rateRng, outerMonths);
    }
    case RiderType::GMDB_IB: {
        GMDB_IB<double> p(policy, mortality);
        return NestedSimulation::runSinglePeriod<GMDB_IB>(config, p, equityRng, rateRng, outerMonths);
    }
    default:
        throw std::runtime_error("Unsupported rider type in dispatch");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    Args args;
    try {
        args = parseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "Run with --help for usage.\n";
        return 1;
    }

    RiderType riderType;
    try {
        riderType = parseRiderType(args.riderName);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "simulate_rider\n";
    std::cout << "  Rider:       " << args.riderName << "\n";
    std::cout << "  Age:         " << args.age << " yrs\n";
    std::cout << "  Gender:      " << (args.female ? "F" : "M") << "\n";
    std::cout << "  Maturity:    " << args.maturity << " months\n";
    std::cout << "  Horizon:     " << args.horizon << " months\n";
    std::cout << "  AV:          " << args.av << "\n";
    std::cout << "  Allocation:  [";
    for (size_t k = 0; k < args.alloc.size(); ++k) {
        if (k > 0) std::cout << ", ";
        std::cout << std::fixed << std::setprecision(4) << args.alloc[k];
    }
    std::cout << "]\n";
    {
        const double effGb = (args.gb > 0.0) ? args.gb : args.av;
        std::cout << "  Guarantee:   " << effGb
                  << (args.gb <= 0.0 ? " (= AV)" : "") << "\n";
    }
    std::cout << "  Rider fee:   " << args.riderFee << "\n";
    std::cout << "  Base fee:    " << args.baseFee << "\n";
    std::cout << "  Update rule: " << args.updateRule;
    if (args.updateRule == "roll-up")
        std::cout << " (rate=" << args.rollUpRate << ")";
    std::cout << "\n";
    std::cout << "  Outer paths: " << args.nOuter << "\n";
    std::cout << "  Inner paths: " << args.nInner << "\n";
    std::cout << "  Seed:        " << args.seed << "\n";
    std::cout << "  Inner RNG:   " << args.rngType
              << (args.rngType == "mrg32k3a" ? " (antithetic)" : "") << "\n";
    std::cout << "  HW1F:        " << (args.useHW ? "enabled" : "disabled (static curve)") << "\n";
    if (args.useHW) {
        std::cout << "  HW1F a:      " << args.hwA << "\n";
        std::cout << "  HW1F sigma:  " << args.hwSigma << "\n";
    }
    std::cout << "  Output:      " << args.outputFile << "\n";
    std::cout << std::endl;

    ThreadPool::getInstance()->start(args.threads);

    const MortalityTable  mortality = buildMortality();
    const NestedSimConfig config    = makeConfig(args);
    const VAPolicy        policy    = makePolicy(args, riderType);

    std::unique_ptr<RNG> innerRngPtr;
    if (args.rngType == "mrg32k3a") {
        innerRngPtr = std::make_unique<mrg32k3a>(
            static_cast<unsigned>(args.seed + 99999ULL),
            static_cast<unsigned>(args.seed + 99998ULL));
    } else {
        innerRngPtr = std::make_unique<Sobol>();
    }
    const RNG& equityInnerRng = *innerRngPtr;

    Sobol rateRng;

    const auto t0 = std::chrono::steady_clock::now();

    std::vector<NestedResult> results;
    try {
        results = runRider(riderType, policy, mortality, config,
                           equityInnerRng, rateRng,
                           static_cast<size_t>(args.horizon));
    } catch (const std::exception& e) {
        std::cerr << "Simulation error: " << e.what() << "\n";
        return 1;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsedSec =
        std::chrono::duration<double>(t1 - t0).count();

    // Aggregate for console output
    const size_t n      = results.size();
    const size_t d      = n > 0 ? results.front().dollarDeltas.size() : 0;
    const size_t nCurve = n > 0 ? results.front().rho.size() : 0;

    double meanFMV = 0.0, sq = 0.0;
    std::vector<double> meanDelta(d, 0.0), meanRho(nCurve, 0.0);
    for (const auto& r : results) {
        meanFMV += r.fmv;
        for (size_t k = 0; k < d; ++k)  meanDelta[k] += r.dollarDeltas[k];
        for (size_t i = 0; i < nCurve; ++i) meanRho[i] += r.rho[i];
    }
    if (n > 0) {
        const double inv = 1.0 / static_cast<double>(n);
        meanFMV  *= inv;
        for (double& v : meanDelta) v *= inv;
        for (double& v : meanRho)   v *= inv;
        for (const auto& r : results) {
            const double d_ = r.fmv - meanFMV;
            sq += d_ * d_;
        }
    }
    const double stdFMV = n > 1 ? std::sqrt(sq / static_cast<double>(n)) : 0.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Results (" << n << " outer paths, " << elapsedSec << " s)\n";
    std::cout << "  Mean FMV:    " << meanFMV  << "\n";
    std::cout << "  Std FMV:     " << stdFMV   << "\n";

    double totalDelta = 0.0;
    for (size_t k = 0; k < d; ++k) totalDelta += meanDelta[k];
    std::cout << "  Total delta: " << totalDelta << "\n";
    std::cout << "  Deltas:      [";
    for (size_t k = 0; k < d; ++k) {
        if (k) std::cout << ", ";
        std::cout << std::setprecision(2) << meanDelta[k];
    }
    std::cout << "]\n";

    if (n > 0 && nCurve > 0) {
        const auto& tenors = results.front().irCurve.tenors();
        std::cout << "  Rho:         [";
        for (size_t i = 0; i < nCurve; ++i) {
            if (i) std::cout << ", ";
            const double t = (i + 1 < tenors.size()) ? tenors[i + 1] : 0.0;
            std::cout << "T" << t << ":" << std::setprecision(4) << meanRho[i];
        }
        std::cout << "]\n";
    }

    try {
        writeHDF5(args.outputFile, args, results);
        std::cout << "\nOutput written to: " << args.outputFile << "\n";
    } catch (const std::exception& e) {
        std::cerr << "HDF5 write error: " << e.what() << "\n";
        ThreadPool::getInstance()->stop();
        return 1;
    }

    ThreadPool::getInstance()->stop();
    return 0;
}
