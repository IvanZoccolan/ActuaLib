# ActuaLib
[![DOI](https://zenodo.org/badge/1288996505.svg)](https://doi.org/10.5281/zenodo.21493987)

## What is ActuaLib?

ActuaLib is a C++17 library whose core aim is to compute **fair market values and market risk sensitivities** for life-contingent insurance liabilities using **one consistent Monte Carlo plus Adjoint Algorithmic Differentiation (AAD) workflow**.

In practical terms: you describe a life-insurance liability contract (e.g., a variable annuity with a guarantee), generate stochastic market paths via nested Monte Carlo (real-world outer loop, risk-neutral inner loop), project the liability through those paths, and obtain both the fair market value and Greeks (price sensitivities to market inputs) from a single valuation code path using reverse-mode automatic differentiation.

This is fundamentally different from traditional pricing systems that compute FMV separately and then use bump-and-revalue to estimate Greeks. Here, one code path delivers both, at a computational cost that scales with the number of outputs (i.e., Greeks for many inputs cost about the same as one full valuation).

**Current product scope:** The framework is life-insurance oriented by design. The concrete products implemented in this repository are variable annuities (VA) with nine guarantee riders (GMDB, GMMB, GMAB, GMWB, GMIB, and four combination riders).

Detailed implementation documentation is available in [docs/LIBRARY_GUIDE.md](docs/LIBRARY_GUIDE.md).

---

## Core Concepts

### Fair Market Value (FMV) + Greeks via Nested Stochastic Simulation

ActuaLib implements **double stochastic simulation** for accurate VA pricing:

- **Outer loop** (real-world): Scenario generation of equity regimes and interest-rate paths under P-measure dynamics  
- **Inner loop** (risk-neutral): Valuation of guarantees under Q-measure Black-Scholes at each outer node  
- **Aggregation**: Average inner values across outer scenarios; sum mortality-weighted cash flows to obtain FMV

This nested approach is necessary because the liability value depends on both the current market state (inner loop) and plausible future market paths (outer loop).

### Adjoint Algorithmic Differentiation (AAD) for Greeks

Risk sensitivities (Greeks) are computed via AAD rather than bumping. Following Giles and Glasserman (2005, *Smoking Adjoints*), AAD records the forward valuation operations and then applies reverse-mode differentiation to recover all Greeks at a cost independent of the number of risk inputs.

In practice: you price the guarantee once with automatic differentiation enabled, and the system records how each intermediate value depends on the market inputs. A backward sweep then computes sensitivities to equity spots, dividend yields, interest rates, etc.—all in one pass.

### Monte Carlo + AAD = Pathwise Greeks

In nested simulation, each Monte Carlo path contributes both value and gradient information. The AAD tape is scoped per path so that gradients remain deterministic even in parallel execution. This is how the library makes Greeks practical at scale.

---

## Key Features

- **Nested Stochastic Valuation** — Double Monte Carlo with real-world (P-measure) outer dynamics for equity regimes and interest rates, and risk-neutral (Q-measure) inner valuation via multivariate Black-Scholes. This structure is essential for accurate VA guarantee pricing under uncertain market evolution.

- **Variable Annuity Product Suite** — Nine rider types: GMDB (death), GMMB (maturity), GMAB (accumulation), GMWB (withdrawal), GMIB (income), plus four combination riders (GMDB+AB, GMDB+MB, GMDB+WB, GMDB+IB). Implementation follows Gan and Valdez (2018) methodology.

- **Adjoint Algorithmic Differentiation (AAD)** — Compact reverse-mode tape with mark/rewind mechanics per Monte Carlo path. Returns all Greeks in one backward pass, making portfolio-scale sensitivities tractable when traditional bumping would be prohibitive.

- **Actuarial Mortality Models** — Table-based mortality with Uniform Distribution of Deaths (UDD) for fractional-age interpolation. Supports standard actuarial tables (IAM 1996, SOA, etc.).

- **ESG Toolkit** — Regime-switching 2-state Markov equity (P-measure outer), stochastic-volatility interest-rate model (P-measure outer), and correlated multivariate Black-Scholes (Q-measure inner) with full term-structure yield-curve generation.

- **C¹ Payoff Smoothing** — All `max(0, x)` guarantees use C¹ quadratic splines, ensuring well-behaved higher-order AAD sensitivities and numerical stability.

- **Parallel Monte Carlo** — Deterministic worker-slot ownership ensures thread-safe AAD accumulation. Per-worker model clones, RNG clones, and tape slots allow efficient outer-loop distribution without lock contention.

- **Runnable Examples** — Single-period, multi-period, and all-rider nested valuation examples demonstrate the end-to-end workflow.

---

## Design Philosophy

ActuaLib is structured around a **single integrated valuation pipeline** rather than a collection of point-solution tools. The philosophy is:

1. **One code path, no rewrites.** Whether you are measuring Greeks or FMV, the *same* pricing code runs. No separate bumping engines, no reconciliation risk.
2. **Models are tape-aware.** Each economic model (ESG, mortality, payoffs) writes its operations directly to the AAD tape, so sensitivities are exact and propagate cleanly regardless of nesting depth.
3. **Concurrency is transparent.** Parallelism is built into the Monte Carlo layer via deterministic worker-slot ownership, allowing per-thread model clones and tape state without explicit locking.
4. **Products define behavior, not code.** A product interface specifies cash-flow rules; the Monte Carlo engine and AAD framework handle aggregation and differentiation.

This unified architecture makes it practical to value large variable-annuity portfolios and compute hedge Greeks at portfolio scale.

## Architecture

The current codebase is organized as a single, cohesive implementation stack. The library is built as a set of focused modules: `aad` owns differentiation, `math` owns numerical kernels and RNG engines, `concurrency` owns execution, `montecarlo` owns path orchestration, and the actuarial and market model layers sit above that in `demographics`, `esg`, and `products`.

```
ActuaLib/
├── aad/             # Reverse-mode tape, scalar wrapper, adjoint helpers
│   ├── gradient_tape.hpp              # Linear tape storage + reverse propagation
│   ├── number.hpp                     # User-facing AAD scalar type
│   └── AAD.hpp                        # Tape utilities used by MC/model code
├── concurrency/     # Task queue, executor pool, ThreadPool facade
│   ├── executor_pool.hpp              # Worker lifecycle and task submission
│   ├── task_queue.hpp                 # Blocking FIFO work queue
│   └── threadpool.hpp                 # Public singleton used by simulation code
├── demographics/    # Mortality models (table-based with UDD)
├── esg/             # Economic Scenario Generators
│   ├── multivariate_blackscholes.hpp    # Multi-asset correlated GBM (Q-measure, inner loop)
│   ├── hull_white_blackscholes.hpp      # HW1F + GBM inner model, AAD-compatible numeraire (Q-measure)
│   ├── hull_white_1f.hpp                # HW1F precomputed constants, exact discrete transition
│   ├── regime_switching.hpp             # 2-state Markov equity (P-measure, outer loop)
│   ├── interest_rate_model.hpp          # AIRG outer rate generator, AAA ESWG (P-measure, outer loop)
│   ├── rw_scenarios.hpp                 # Real-world scenario containers
│   ├── yieldcurve.hpp                   # Yield-curve representation and discounting
│   └── model.hpp                        # Common ESG model interface
├── examples/        # Active runnable examples
│   └── simulate_rider.cpp               # Single-contract VA pricer: HW1F inner model, HDF5 output, full CLI
├── math/            # Matrix algebra, Cholesky, interpolation, utilities
│   ├── rng/                            # Engine implementations (MRG32k3a, Sobol)
│   ├── stats/                          # Normal density/CDF/inverse CDF helpers
│   └── randomnumbers/                  # Public RNG adapters used by models and MC
├── montecarlo/      # Path containers and serial/parallel MC simulation
│   ├── mcsimulationAAD.hpp              # Serial with AAD
│   └── nested_simulation.hpp            # Nested stochastic double MC
├── products/        # Financial products
│   ├── product.hpp                      # Base product interface
│   ├── va_policy.hpp                    # Policy data and contract inputs
│   ├── va_base.hpp                      # VA product base class
│   ├── va_gmdb.hpp                      # GMDB rider
│   ├── va_gmab.hpp                      # GMAB rider
│   ├── va_gmmb.hpp                      # GMMB rider
│   ├── va_gmwb.hpp                      # GMWB rider
│   ├── va_gmib.hpp                      # GMIB rider
│   ├── va_combo.hpp                     # Combined VA products
│   └── va_factory.hpp                   # VA product factory
└── docs/            # Library and methodology documentation
```

At runtime, the usual flow is: a product defines the projection timeline and requested path shape, an ESG model fills a `Scenario<T>` over that shape, the Monte Carlo layer evaluates the product over many paths, and the AAD layer back-propagates sensitivities when the scalar type is `ActuaLib::Number`. The parallel AAD path uses one model clone, one RNG clone, one path buffer, and one tape slot per execution slot so that derivative accumulation remains deterministic.

---

## Variable Annuity Products

In ActuaLib, a variable annuity (VA) is a life-contingent insurance product that provides guarantees linked to fund performance. The library implements nine distinct VA guarantee riders, all following the methodology from Gan and Valdez (2018):

> G. Gan & E. Valdez, *"Valuation of Large Variable Annuity Portfolios: Monte Carlo Simulation and Synthetic Datasets"*, Data 2018, 3, 31.

Each product specifies a **projection timeline**, **cash-flow rules** (death benefits, living benefits, rider charges), and **guarantee definitions**. The Monte Carlo engine then projects each product through thousands of nested stochastic paths and computes FMV and Greeks.

### Supported guarantee types

| Product | Description |
|---------|-------------|
| **GMDB** | Guaranteed Minimum Death Benefit |
| **GMMB** | Guaranteed Minimum Maturity Benefit |
| **GMAB** | Guaranteed Minimum Accumulation Benefit (renewable) |
| **GMWB** | Guaranteed Minimum Withdrawal Benefit |
| **GMIB** | Guaranteed Minimum Income Benefit |
| **GMDB+MB** | Death + Maturity combination |
| **GMDB+AB** | Death + Accumulation combination |
| **GMDB+WB** | Death + Withdrawal combination |
| **GMDB+IB** | Death + Income combination |

### Pricing formula

For each Monte Carlo path the present value is:

$$\text{FMV} = \left(\sum_{j=1}^{N} {}_{(j-1)\Delta t}p_x \cdot {}_{\Delta t}q_{x+(j-1)\Delta t} \cdot \text{DA}_j \cdot D(0,t_j) \;+\; \sum_j {}_{j\Delta t}p_x \cdot \text{LA}_j \cdot D(0,t_j) \;-\; \sum_j {}_{j\Delta t}p_x \cdot \text{RC}_j \cdot D(0,t_j)\right) \times S$$

where DA, LA, RC are the death-benefit, living-benefit, and rider-charge cash flows, $D(0,t)$ is the discount factor, and $S$ is the survivorship scaling.

### AAD smoothing

All `max(0, x)` payoffs use a C¹ quadratic spline with configurable half-width $\varepsilon = \texttt{smooth} \times \texttt{gbAmt}$, producing well-behaved derivatives for AAD while converging to the sharp payoff as $\varepsilon \to 0$. See [docs/va_smoothing.md](docs/va_smoothing.md) for details.

## Nested Stochastic Valuation

ActuaLib implements **double stochastic simulation** for accurate VA liability pricing under real-world (P-measure) equity and interest-rate dynamics.

### Architecture

```
Nested Stochastic Simulation
├── Outer Loop (Real-world, P-measure)
│   ├── Regime-Switching Equity Model (2-state Markov)
│   │   ├── Bull regime: drift μ_B, volatility σ_B
│   │   ├── Bear regime: drift μ_b, volatility σ_b
│   │   └── Monthly transitions (Markov matrix)
│   └── Interest Rate Generator (AIRG — AAA Economic Scenario Work Group)
│       ├── 3-factor log-normal model (long rate, spread, stoch. vol)
│       ├── Mean-reverting dynamics for long rate and spread
│       └── Generates full yield curves at each time step
│
└── Inner Loop (Risk-neutral, Q-measure, configurable paths per outer node)
    ├── Multivariate Black-Scholes (d-asset correlated GBM)
    │   ├── Cholesky-correlated equity indices
    │   ├── Static outer yield curve (default) or stochastic HW1F short rate
    │   └── Computed via AAD for exact Greeks
    └── Optional: Hull-White one-factor inner rate model
        ├── Calibrated to outer AIRG curve at each node
        ├── Exact discrete Gaussian transition (no Euler error)
        └── Analytic path-dependent numeraire on tape; r₀ and c₂[i] also on tape for full IR Greeks
```

### How it works

1. **Outer simulation**:
   - Generate real-world P-measure scenarios for equity regime switches and interest rates
   - Each monthly step: regime transition, equity drift/vol update, yield curve generation
   - Duration: typically 1–10 years

2. **Valuation at each outer node**:
   - For each outer scenario's equity state and yield curve:
   - Rebuild the inner risk-neutral state with multivariate Q-measure Black-Scholes
   - Price the VA guarantee under risk-neutral dynamics
   - Extract option value at that node

3. **Aggregation**:
   - Average inner valuations across all outer paths
   - Aggregate FMV and reported sensitivities across outer paths
   - Obtain Greeks via AAD backward passes through the pathwise tape state

### Models: Outer Loop

#### **Regime-Switching Equity (P-measure)**

```
dS_t / S_t = μ(Z_t) dt + σ(Z_t) dW_t,    Z_t ∈ {Bull, Bear}
```

Two-state Markov process with monthly transitions:
- **Bull regime**: Higher drift, lower vol (typically 8% / 15%)
- **Bear regime**: Lower drift, higher vol (typically 2% / 25%)
- Transition probabilities capture regime persistence

#### **AIRG Interest-Rate Model (P-measure) — outer loop**

3-factor log-normal model (long rate, spread, stochastic volatility) from the **AIRG** (Academy Interest Rate Generator) developed by the American Academy of Actuaries' **Economic Scenario Work Group (ESWG)**.  The AIRG is the mandated regulatory scenario generator for C3 Phase I/II/III and VM-20 calculations in the United States.

- Support: ESGhelp@SOA.org
- Workbooks and documentation: actuary.org

```
ln r^L_{t+1} = (1−β₁)ln r^L_t + β₁ ln τ₁ + ψ(τ₂ − d_t) + e^(ln σ_{t+1}) ε₁ₜ
ln σ_{t+1} = (1−β₃)ln σ_t + β₃ ln τ₃ + σ₃ ε₃ₜ          (stoch. vol)
d_{t+1}    = (1−β₂)d_t + β₂τ₂ + φ(ln r^L_t − ln τ₁) + σ₂ ε₂ₜ (r^L_{t+1})^θ
```

Outputs full Nelson-Siegel yield curves at each scenario/time step.

### Models: Inner Loop

#### **Multivariate Black-Scholes (Q-measure) — default inner model**

```
dS_i^j / S_i^j = r(t) dt + σ_i dW_i^j,    i = 1, …, d (e.g., 5 assets)
dW_i · dW_j = ρ_{ij} dt  (Cholesky-decomposed covariance)
```

Pricing:
- Discount using the static outer AIRG yield curve (no inner rate movement)
- Fund allocations across d indices computed via Monte Carlo
- Guarantee payoffs smoothed via C¹ splines (see va_smoothing.md)
- All operations recorded on AAD tape for exact Greeks

#### **Hull-White One-Factor (Q-measure) — optional stochastic inner rate model**

Enabled by setting `NestedSimConfig::useHullWhiteInner = true`.  Replaces the static discount curve with a stochastic short rate calibrated exactly to the outer AIRG yield curve at each node:

```
dr_t = [θ(t) − a r_t] dt + σ dW_t^Q
```

Key properties:
- **Exact discrete transition** (no Euler error): `r_{i+1} = c₁ r_i + c₂[i] + c₃ Z_i`
- **Initial term structure fit**: $P^{\mathrm{HW}}(0,T) = P^{\mathrm{AIRG}}(0,T)$ exactly
- **Path-dependent numeraire** $B(t) = \prod_i 1/P^{HW}(t_i, t_{i+1})$ accumulated on tape
- **IR Greeks** flow through two on-tape channels: $\ln A_i$ (numeraire) **and** the full HW calibration — $r_0$ and $c_{2,i}$ are $T$-typed so the adjoint propagates through the entire short-rate recurrence $r_{i+1} = c_1 r_i + c_{2,i} + c_3 Z_i$ back to every outer curve knot
- **Equity drift** uses the realised path $r_i$ (on tape, type `T`) at each step, so equity paths also carry IR sensitivity through the drift
- Adds only 1 Gaussian draw + 1 `exp` per step per inner path (~10–15% overhead)

Default parameters: `a = 0.10` (10% mean reversion), `sigma = 0.01` (1% vol).

### Products Priced with Nested MC

| Product | Death Benefit | Living Benefit | Rider Charge |
|---------|---------------|----------------|--------------|
| **GMDB** | max(GB, AV) | — | Fixed % |
| **GMMB** | — | max(GB, AV) at maturity | Fixed % |
| **GMAB** | — | max(GB, AV) on anniversary (voidable) | Fixed % |
| **GMWB** | death AV | Withdrawals up to max(GB, AV) | Fixed % |
| **GMIB** | death AV | Annuity option at maturity | Fixed % |
| **GMDB+MB** | max(GB, AV) | max(GB, AV) at maturity | Fixed % |
| **GMDB+AB** | max(GB, AV) | max(GB, AV) on anniversary | Fixed % |
| **GMDB+WB** | max(GB, AV) | Withdrawal guarantee | Fixed % |
| **GMDB+IB** | max(GB, AV) | Annuity option at maturity | Fixed % |

### Implementation details

**File structure:**
- [esg/regime_switching.hpp](esg/regime_switching.hpp) — 2-state Markov equity generator
- [esg/interest_rate_model.hpp](esg/interest_rate_model.hpp) — AIRG outer interest-rate generator (AAA ESWG, P-measure)
- [esg/hull_white_1f.hpp](esg/hull_white_1f.hpp) — HW1F precomputed constants
- [esg/hull_white_blackscholes.hpp](esg/hull_white_blackscholes.hpp) — HW1F + GBM inner model (Q-measure)
- [esg/multivariate_blackscholes.hpp](esg/multivariate_blackscholes.hpp) — Q-measure inner pricing
- [montecarlo/nested_simulation.hpp](montecarlo/nested_simulation.hpp) — Double MC orchestration

**Example:**

- [examples/simulate_rider.cpp](examples/simulate_rider.cpp) — Single-contract VA pricer with HW1F inner model, compact outer-label HDF5 output (FMV + delta + rho), and a full CLI (`--rider`, `--age`, `--gender`, `--maturity`, `--horizon`, `--months-since-issue`, `--av`, `--alloc`, `--gb`, `--rider-fee`, `--base-fee`, `--update-rule`, `--roll-up-rate`, `--outer`, `--inner`, `--seed`, `--rng`, `--no-hw`, `--hw-a`, `--hw-sigma`, `--output`). For the batched portfolio-to-surrogate workflow, see [docs/portfolio_training.md](docs/portfolio_training.md).

### AAD Risk Sensitivities

The nested MC is fully integrated with AAD:

- Inner pricing state is recorded on tape at each valuation node
- Backward sweep computes sensitivities to:
   - Equity spot prices
   - Dividend yields
   - Yield-curve discount knots used by the inner model

A single nested run yields FMV plus the differentiated market sensitivities exported by the inner model.

## Quick Start

### Prerequisites

- C++17 compiler (Clang 14+, GCC 11+)
- CMake 3.22+
- HDF5 development headers and libraries
- OpenMP runtime

Install dependencies for your platform:

```bash
# Debian / Ubuntu
sudo apt install libhdf5-dev libomp-dev

# Fedora / RHEL
sudo dnf install hdf5-devel libomp-devel

# macOS (Homebrew)
brew install hdf5 libomp
```

### Build

```bash
cmake -S . -B build
cmake --build build --target SimulateRider -j8
```

If you want to run the Python portfolio and surrogate workflow as well, create a Python environment and install the packages used by the scripts:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install numpy h5py torch matplotlib
```

### Run

```bash
# Price a GMDB for a 60-year-old male, 160-month remaining contract,
# policy issued 24 months ago, 1-year evaluation horizon, 10k × 1k paths
./examples/SimulateRider \
  --rider GMDB --age 60 --gender M --maturity 160 --horizon 12 --months-since-issue 24 \
  --av 110000 --alloc 0.30,0.25,0.20,0.15,0.10 \
  --rider-fee 0.010 --base-fee 0.005 --update-rule step-up \
  --outer 10000 --inner 1000 --rng sobol \
  --output results/gmdb_60m.h5
```

See `--help` for all options.

For synthetic-portfolio generation, batch pricing, and surrogate training on stored HDF5 outputs, use the documented workflow in [docs/portfolio_training.md](docs/portfolio_training.md).

### Example output

```
simulate_rider
  Rider:       GMDB
  Age:         60 yrs
  Gender:      M
  Maturity:    160 months
  Horizon:     12 months
  AV:          110000
  Allocation:  [0.3000, 0.2500, 0.2000, 0.1500, 0.1000]
  Guarantee:   110000 (= AV)
  Rider fee:   0.01
  Base fee:    0.005
  Update rule: step-up
  Outer paths: 10000
  Inner paths: 1000
  Inner RNG:   sobol
  HW1F:        enabled
  HW1F a:      0.10
  HW1F sigma:  0.01

Results (10000 outer paths, 268.64 s)
  Mean FMV:    -8860.73
  Std FMV:     1052.63
  Total delta: -10796.21
  Deltas:      [-3193.35, -2400.10, -2082.53, -1868.89, -1251.34]
  Rho:         [T0.25:0.0094, T0.5:0.0175, T1:-2.1487, T2:-2.0890,
                T3:-3.0378, T5:-4.0232, T7:-4.8494, T10:-5.5692,
                T20:-0.2754, T30:0.0000]

Output written to: results/gmdb_60m.h5
```

The HDF5 file contains per-path arrays ready for ML training:

| Dataset | Shape | Description |
|---------|-------|-------------|
| `/results/fmv_per_path`    | `[nOuter]`      | FMV on each outer path |
| `/results/delta`           | `[nOuter × 5]`  | Dollar-delta per equity index |
| `/results/rho`             | `[nOuter × 10]` | IR rho per yield-curve knot |
| `/results/curve_tenors`    | `[10]`          | Tenor grid (0.25, 0.5, 1, 2, 3, 5, 7, 10, 20, 30 yr) |
| `/results/outer_spots`     | `[nOuter × 5]`  | Equity spot levels at the outer pricing date (X features for ML) |
| `/results/outer_disc_factors` | `[nOuter × 10]` | IR discount factors at the outer pricing date (X features for ML) |
| `/results/outer_fund_values`  | `[nOuter × 5]`  | Aged fund values at the outer pricing date (X features for ML) |
| `/params/` | attributes | `rider`, `age`, `gender`, `maturity_months`, `horizon_months`, `months_since_issue`, `av`, `alloc_0–4`, `gb`, `rider_fee`, `base_fee`, `update_rule`, `roll_up_rate`, `rng`, HW params, `seed` |

## Example

| Executable | Description |
|-----------|-------------|
| `SimulateRider` | Single-contract VA nested pricer with HW1F stochastic inner rates and AAD Greeks. CLI-configurable rider type, demographics, account value, fund allocation, guarantee amount, fees, benefit-base update rule (`step-up`, `rop`, `roll-up`), path counts, RNG type (`sobol` or `mrg32k3a` with antithetic variates), and HW parameters. Key flags: `--maturity` (remaining months from today), `--horizon` (evaluation horizon in months, e.g. 12 = 1 yr), `--months-since-issue` (policy age since inception, for correct anniversary timing). Writes compact outer-path FMV, delta `[nOuter×5]`, rho `[nOuter×10]`, plus outer market state (equity spots, discount factors, fund values) to HDF5. Use `scripts/gen_portfolio.py` (with optional `--benefit-base-rule {ROP, ratchet, roll-up}` to pin every policy to one rule) to generate a synthetic portfolio JSON, `scripts/run_portfolio.py` to batch-price the whole portfolio, and `scripts/sobolev_va_training.py` (with optional `--update-rule {rop, step-up, roll-up}` to train on a single rule's data) for surrogate learning. See [docs/portfolio_training.md](docs/portfolio_training.md) for the end-to-end surrogate workflow and [scripts/paper_pipeline.txt](scripts/paper_pipeline.txt) for the complete 2 700-policy paper dataset pipeline (9 riders × 3 rules × 100 policies, 100 000 × 1 000 inner paths). |

## References

- G. Gan & E. Valdez, *"Valuation of Large Variable Annuity Portfolios: Monte Carlo Simulation and Synthetic Datasets"*, Data, 2018, 3(3), 31. https://doi.org/10.3390/data3030031
- M. Giles & P. Glasserman, *"Smoking Adjoints: Fast Monte Carlo Greeks"*, Risk Magazine, 2006. Foundational reference on reverse-mode AAD for financial derivatives.
- A. Savine, *"Modern Computational Finance: AAD and Parallel Simulations"*, Wiley, 2018. Comprehensive reference on AAD implementation and parallel Monte Carlo strategies.

## Attribution

The `aad/` and `concurrency/` modules contain code adapted from:
A. Savine, *"Modern Computational Finance: AAD and Parallel Simulations"*, Wiley, 2018.
Used and modified under the book's license (freely granted to purchasers; attribution preserved at the top of each derived source file).
All actuarial extensions are original work.

## License

[![CC BY-NC 4.0](https://licensebuttons.net/l/by-nc/4.0/88x31.png)](https://creativecommons.org/licenses/by-nc/4.0/)

This work is licensed under the **Creative Commons Attribution-NonCommercial 4.0 International License** (CC BY-NC 4.0).

You are free to share and adapt the material for **non-commercial purposes**, provided you give appropriate credit. Commercial use requires explicit written permission from the author.

Full licence text: https://creativecommons.org/licenses/by-nc/4.0/legalcode
