<!-- markdownlint-disable MD033 MD010 MD024 -->

# ActuaLib User Guide

<style>
:root {
	--manual-bg: #f7fafc;
	--manual-panel: #ffffff;
	--manual-ink: #173042;
	--manual-muted: #547082;
	--manual-accent: #0f766e;
	--manual-accent-2: #164e63;
	--manual-border: #d8e3ea;
	--manual-code-bg: #0f1720;
	--manual-code-ink: #e6edf3;
	--manual-shadow: 0 10px 28px rgba(15, 23, 42, 0.08);
}

body {
	line-height: 1.72;
}

.toc-shell {
	max-height: calc(100vh - 48px);
	display: flex;
	flex-direction: column;
}

.toc-shell .toc {
	min-height: 0;
	overflow-y: auto;
	overflow-x: hidden;
	padding-right: 0.35rem;
	scrollbar-gutter: stable;
}

.toc-shell .toc::-webkit-scrollbar {
	width: 0.55rem;
}

.toc-shell .toc::-webkit-scrollbar-thumb {
	background: rgba(84, 112, 130, 0.35);
	border-radius: 999px;
}

.toc-shell .toc::-webkit-scrollbar-track {
	background: transparent;
}

@media (max-width: 980px) {
	.toc-shell {
		max-height: none;
	}

	.toc-shell .toc {
		overflow: visible;
		padding-right: 0;
	}
	}

.manual-hero {
	margin: 1.2rem 0 1.6rem;
	padding: 1.6rem 1.5rem;
	background: linear-gradient(135deg, #f0fdfa 0%, #eff6ff 55%, #f8fafc 100%);
	border: 1px solid var(--manual-border);
	border-radius: 22px;
	box-shadow: var(--manual-shadow);
}

.manual-kicker {
	margin: 0 0 0.35rem;
	font-size: 0.82rem;
	font-weight: 800;
	letter-spacing: 0.08em;
	text-transform: uppercase;
	color: var(--manual-accent);
}

.manual-lead {
	margin: 0.8rem 0 0;
	color: var(--manual-muted);
	font-size: 1.02rem;
}

.manual-note {
	margin: 1.1rem 0 1.8rem;
	padding: 0.95rem 1rem;
	border-left: 4px solid var(--manual-accent);
	background: #f8fffe;
	border-radius: 0 14px 14px 0;
	color: var(--manual-ink);
}

.manual-tabs {
	display: flex;
	flex-wrap: wrap;
	gap: 0.8rem;
	margin: 1.3rem 0 1.9rem;
}

.manual-tabs a {
	display: inline-flex;
	align-items: center;
	justify-content: center;
	padding: 0.72rem 1.02rem;
	border-radius: 999px;
	border: 1px solid #bcd0da;
	background: linear-gradient(180deg, #ffffff 0%, #f3f8fb 100%);
	color: var(--manual-accent-2);
	text-decoration: none;
	font-weight: 700;
	font-size: 0.95rem;
	box-shadow: 0 4px 12px rgba(15, 23, 42, 0.05);
}

.manual-tabs a:hover {
	background: linear-gradient(180deg, #ecfeff 0%, #eff6ff 100%);
	border-color: #8bb7c8;
	color: #0f172a;
}

.manual-grid {
	display: grid;
	grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
	gap: 1rem;
	margin: 1.2rem 0 2rem;
}

.manual-card {
	padding: 1rem 1rem 1.05rem;
	background: var(--manual-panel);
	border: 1px solid var(--manual-border);
	border-radius: 18px;
	box-shadow: var(--manual-shadow);
}

.manual-card strong {
	display: block;
	margin-bottom: 0.4rem;
	color: var(--manual-accent-2);
}

.manual-card p {
	margin: 0;
	color: var(--manual-muted);
	font-size: 0.95rem;
}

h2 {
	margin-top: 2.6rem;
	padding-bottom: 0.45rem;
	border-bottom: 2px solid #e2ebf0;
}

h3 {
	margin-top: 1.8rem;
	color: var(--manual-accent-2);
}

h1 .anchor,
h2 .anchor,
h3 .anchor,
h4 .anchor,
h5 .anchor,
h6 .anchor,
.anchorjs-link,
.header-anchor,
.markdown-header-anchor,
.hash-link,
a.anchor {
	display: none !important;
	visibility: hidden !important;
	width: 0 !important;
	overflow: hidden !important;
	text-decoration: none !important;
}

pre {
	padding: 1rem 1.1rem;
	border-radius: 16px;
	background: var(--manual-code-bg) !important;
	color: var(--manual-code-ink) !important;
	border: 1px solid #1f2d3a;
	box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.03);
}

code {
	border-radius: 6px;
}

blockquote {
	margin: 1.1rem 0;
	padding: 0.85rem 1rem;
	background: #f8fafc;
	border-left: 4px solid #94a3b8;
	border-radius: 0 14px 14px 0;
}

hr {
	border: 0;
	height: 1px;
	margin: 2rem 0;
	background: linear-gradient(90deg, transparent, #cbd5e1, transparent);
}

.back-to-tabs {
	margin-top: 1rem;
	font-size: 0.92rem;
}

.back-to-tabs a {
	color: var(--manual-accent);
	text-decoration: none;
	font-weight: 700;
}

@media (max-width: 900px) {
	.manual-hero {
		padding: 1.2rem 1rem;
	}

	.manual-tabs a {
		width: 100%;
		justify-content: flex-start;
	}
}
</style>

<div class="manual-hero">
	<p class="manual-kicker">User Guide</p>
	<p><strong>ActuaLib</strong> is a C++17 actuarial valuation framework whose core aim is to compute fair market values and market risk sensitivities for life-contingent liabilities using one consistent Monte Carlo plus AAD workflow.</p>
	<p class="manual-lead">In practical terms, the library combines scenario generation, liability projection, simulation, and reverse-mode differentiation so users can produce FMV and Greeks from the same valuation code path. The framework is life-insurance oriented by design, and the current concrete product set in this repository is variable annuities, including nine implemented guaranteed-benefit riders.</p>
</div>

<div class="manual-note">
	The navigation below is intentionally styled like a tab bar. Each tab jumps to one part of the library, with the emphasis on practical use: what the module is for, when you would use it, and which implementation details matter for results.
</div>

<div id="module-tabs" class="manual-tabs">
	<a href="#overview">Overview</a>
	<a href="#aad">AAD</a>
	<a href="#concurrency">Concurrency</a>
	<a href="#math">Math</a>
	<a href="#demographics">Demographics</a>
	<a href="#esg">ESG</a>
	<a href="#monte-carlo">Monte Carlo</a>
	<a href="#products">Products</a>
	<a href="#examples-and-tests">Examples & Tests</a>
</div>

<div class="manual-grid">
	<div class="manual-card">
		<strong>AAD</strong>
		<p>How ActuaLib computes Greeks from the same valuation code used for FMV.</p>
	</div>
	<div class="manual-card">
		<strong>Concurrency</strong>
		<p>How large simulations are distributed across workers while keeping ownership deterministic.</p>
	</div>
	<div class="manual-card">
		<strong>Math</strong>
		<p>Numerical utilities used by the market models and simulation engines.</p>
	</div>
	<div class="manual-card">
		<strong>Monte Carlo</strong>
		<p>Serial, parallel, and AAD-aware simulation flows built on one path contract.</p>
	</div>
	<div class="manual-card">
		<strong>Products</strong>
		<p>Life and VA guarantee implementations built on a shared monthly projection skeleton.</p>
	</div>
	<div class="manual-card">
		<strong>ESG</strong>
		<p>The implemented market models: curves, inner pricing dynamics, and real-world outer scenarios.</p>
	</div>
</div>

---

## Overview

ActuaLib's overall purpose is straightforward: represent an insurance liability contract, generate market and actuarial states, project cash flows, discount and aggregate to fair market value, then compute risk sensitivities from that same valuation logic. The framework is therefore not just a collection of pricing utilities. It is an end-to-end valuation and risk engine.

The codebase is organized as a layered system. `math` and `aad` provide numerical and differentiation primitives. `concurrency` and `montecarlo` provide execution and simulation orchestration. `esg`, `demographics`, and `products` hold the market, mortality, and contract-projection logic users most often modify. `examples` and `tests` document the intended composition pattern in executable form.

The key architectural principle is scalar-generic implementation: models and products are templated on `T`, usually `double` or `Number`. With `T = double`, the run is pure pricing. With `T = Number`, the same operations become differentiable.

Risk derivatives with respect to model parameters are computed with Adjoint Algorithmic Differentiation (AAD) rather than bump-and-revalue finite differences. This follows the approach introduced to the financial community by Giles and Glasserman, *Smoking Adjoints: fast evaluation of Greeks in Monte Carlo calculations* (2005), https://api.semanticscholar.org/CorpusID:18679334.

Current product scope is best summarized as: life-insurance valuation framework, currently populated with variable-annuity guarantee products. In this repository, the concrete shipped riders are the nine VA rider types exposed by `makeRider()` (`GMDB`, `GMMB`, `GMAB`, `GMWB`, `GMIB`, `GMDB_AB`, `GMDB_MB`, `GMDB_WB`, `GMDB_IB`).

At runtime, data flow is: product defines timeline and `SampleDef`, model fills `Scenario<T>`, Monte Carlo runs pathwise liability projection and aggregation, then AAD back-propagates derivatives to taped inputs. Parallel execution clones model and RNG state per worker and assigns deterministic tape slots, while preserving the same user-facing valuation workflow.

```text
products -> projection timeline + SampleDef
esg/model -> generatePath(...) into Scenario<T>
montecarlo -> run paths, aggregate outputs
aad -> back-propagate adjoints if T = Number
concurrency -> schedule parallel work when enabled
```

### Top-Level Map

```text
ActuaLib/
|- aad/             reverse-mode scalar/tape implementation
|- concurrency/     task queue, executor pool, ThreadPool facade
|- demographics/    mortality abstractions and concrete tables
|- esg/             market models, yield curves, scenario generators
|- math/            matrices, interpolation, Cholesky, RNG engines
|- montecarlo/      path layout and serial/parallel simulation
|- products/        life and VA liability definitions
|- examples/        runnable reference compositions
`- tests/           low-level and regression coverage
```

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## AAD

Adjoint Algorithmic Differentiation (AAD) is a programmatic way to compute exact first-order sensitivities of a numerical valuation, up to machine precision, by applying the chain rule to the sequence of operations executed by code. For actuarial and derivatives workloads this is important because, when one valuation output depends on many risk factors, reverse mode (adjoint mode) computes all corresponding input sensitivities at a cost that is typically a small multiple of one valuation run, rather than one extra full run per bump.

For actuarial users, the practical intuition is: run the valuation once to produce the liability value and store the local calculation trail, then run a backward pass that propagates sensitivity contributions from output back to market and contract inputs. In Monte Carlo settings, this fits naturally with pathwise differentiation: each path contributes both value and gradient information, and gradients are aggregated with the same simulation logic.

In implementation terms, reverse mode needs recorded forward information because backward sensitivities depend on intermediate states computed earlier. That recorded forward information is the tape. It is not a market data time series; it is a computational record of the valuation path used to propagate adjoints correctly in reverse order.

In ActuaLib, the `aad` module implements this reverse-mode engine with explicit tape records rather than a large expression-template graph. The public scalar type is `ActuaLib::Number`, defined in `aad/number.hpp`. `Number` is the type upper layers use whenever they want a value to participate in differentiation. From the perspective of product or model code, it behaves like a scalar with arithmetic operators and selected math functions. From the perspective of the tape, each active operation appends a node with parent references and local partial derivatives.

The core storage object is `gradient_tape` in `aad/gradient_tape.hpp`. The tape stores `op_record` nodes in a `tape_arena` and maintains a parallel adjoint vector indexed by slot. That layout matters. It means reverse propagation is a simple backward walk over contiguous storage rather than a pointer chase across heap-allocated nodes. When performance problems appear in differentiated runs, this data layout is the first reason the implementation stays tractable.

One of the most important details in this module is mark/rewind semantics. AAD Monte Carlo is not implemented by taping all paths on one endlessly growing graph. Instead, model parameters are taped once, then the tape is marked after the static setup phase. Each path rewinds to that mark, evaluates one payoff, and propagates only to the mark. After all paths in a batch are processed, the marked region is propagated once more back to the original taped parameters. That keeps tape growth bounded and gives the simulation code a stable lifecycle to work with.

### Key Types

```cpp
using Tape = gradient_tape;

struct Node {
	static size_t numAdj;
};

class Number {
public:
	static thread_local Tape* tape;

	void putOnTape();
	double& adjoint();
	void propagateToStart();
	void propagateToMark();
	static void propagateMarkToStart();
};
```

### Typical AAD Setup

```cpp
#include "aad/AAD.hpp"

using namespace ActuaLib;

Tape tape;
Number::tape = &tape;

Number x(100.0);
Number y(0.03);
x.putOnTape();
y.putOnTape();

tape.mark();

Number z = x * exp(y);
z.propagateToMark();
Number::propagateMarkToStart();

double dz_dx = x.adjoint();
double dz_dy = y.adjoint();
```

### Implementation Notes

- `Number::putOnTape()` upgrades a passive scalar into an active taped leaf. If a variable is never put on tape, it will not contribute to adjoints.
- `choose_tape(...)` in the scalar implementation ensures binary operations use the correct active tape when one or both operands are active.
- `AAD.hpp` intentionally stays small. It is a convenience layer that exposes the operational helpers the rest of the library needs without forcing upper layers to know tape internals.
- `Node::numAdj` exists to preserve the result-count contract used by the rest of the codebase, even though the current implementation is centered on scalar reverse-mode accumulation.

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## Concurrency

The `concurrency` module provides a single execution abstraction for the whole library: `ThreadPool` is the public facade, and `executor_pool` is the real scheduler. This split is intentional. Upper layers depend on a stable singleton API, while the scheduling implementation can stay focused on worker lifecycle, queue semantics, and task submission. The result is a small surface area for callers and a testable, explicit execution engine underneath.

`executor_pool` uses `std::packaged_task<bool(void)>` for units of work and `std::future<bool>` as the handle type. That design allows the simulation layer to submit arbitrary closures while still retaining future-based synchronization semantics. It also means the pool does not need a custom task abstraction beyond aliases. If you are tracing a parallel pricing issue, the first types to keep in mind are `Task`, `task_handle`, and `TaskHandle`.

The queue in `task_queue.hpp` is deliberately simple: a mutex, a condition variable, and a FIFO queue. This is not a lock-free executor. That is a conscious tradeoff. The library wants deterministic worker-slot ownership and predictable shutdown semantics more than it wants the lowest possible queue-latency benchmark. For AAD parallelism, stable ownership is more valuable than clever queue behavior because model clones, RNG clones, and tape slots are indexed by worker number.

### Public Scheduling Pattern

```cpp
#include "concurrency/threadpool.hpp"

using namespace ActuaLib;

ThreadPool* pool = ThreadPool::getInstance();
pool->start(4);

TaskHandle job = pool->spawnTask([]() {
	// Do pricing or scenario work here
	return true;
});

pool->activeWait(job);
pool->stop();
```

### Internal Shape

```cpp
class executor_pool {
public:
	using task = std::packaged_task<bool(void)>;

	static executor_pool& instance();
	static std::size_t worker_index();

	template <class Callable>
	task_handle submit(Callable&& func);

	bool wait_assist(const task_handle& handle);
};
```

### Implementation Notes

- `ThreadPool::threadNum()` is not cosmetic. It is used by parallel Monte Carlo AAD to pick the correct model clone, path buffer, RNG clone, and tape slot.
- `activeWait()` can opportunistically execute queued work while waiting on a future. This keeps the caller productive rather than blocked.
- Setting `ACTUALIB_DISABLE_ACTIVE_STEAL` disables that work-stealing behavior, which is useful when isolating scheduling-sensitive bugs.
- `start()` and `stop()` are designed to be explicit lifecycle boundaries. Examples and tests should normally call them deliberately instead of relying on static destruction timing.

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## Math

The `math` module is the library's implementation floor. It contains the small, explicit kernels the financial and Monte Carlo code depends on: matrix storage, matrix multiplication, transpose, Cholesky factorization, interpolation, curve-filling utilities, random-number interfaces, RNG engines, and Gaussian helper functions. There is very little abstraction for its own sake here. The emphasis is on code that a developer can read quickly and reason about with confidence.

`math/matrix.hpp` provides a compact row-major `Matrix<T>` wrapper backed by a flat `std::vector<T>`. The class exposes direct row access through `operator[]`, which keeps model code easy to read and allows low ceremony when assembling covariance and correlation structures. The specialized matrix multiply for `Matrix<double>` uses a straightforward triple loop with an OpenMP pragma when available. That is an explicit performance/numerical-audit tradeoff: the code is simple enough to trust, and still fast enough for the problem sizes in this library.

`math/cholesky.hpp` is central to the equity models. It computes a lower-triangular factor using only operations that also exist on `Number`, which is why it can participate in differentiated workflows without a separate algorithm. This genericity matters even though not every market input is currently on tape. It keeps the numeric layer uniform and avoids hidden incompatibilities between double and AAD runs.

The random-number story is split deliberately. `math/randomnumbers/genericrng.hpp` defines the public `RNG` interface used by models and simulation code. `math/rng` contains concrete engine implementations such as `mrg32k3a_engine` and `sobol_engine`. `math/randomnumbers/mrg32k3a.hpp` and `math/randomnumbers/sobol.hpp` are then thin adapters that expose the library's public RNG types in the style the rest of the code expects. That separation makes it easy to clone RNG state in simulation code while keeping engine internals isolated.

### Matrix And Cholesky Example

```cpp
#include "math/matrix.hpp"
#include "math/cholesky.hpp"

using namespace ActuaLib;

Matrix<double> corr(2, 2);
corr[0][0] = 1.0; corr[0][1] = 0.25;
corr[1][0] = 0.25; corr[1][1] = 1.0;

Matrix<double> L = cholesky(corr);
Matrix<double> LT = transpose(L);
Matrix<double> reconstructed = L * LT;
```

### RNG Example

```cpp
#include "math/randomnumbers/mrg32k3a.hpp"

using namespace ActuaLib;

mrg32k3a rng(12345, 12346);
rng.init(5);

std::vector<double> z;
rng.nextG(z);

// Jump directly to the state used for path 100
rng.skipTo(100);
```

### Implementation Notes

- `math/stats/normal.hpp` is the canonical home for density, CDF, and inverse-CDF helpers used throughout the library.
- `math/utils.hpp` is not just generic filler code. `fillData()` is used to regularize curves and term grids in higher layers.
- The public RNG abstraction exposes both legacy-style methods (`nextU`, `nextG`, `skipTo`) and engine-style aliases (`next_uniform`, `next_gaussian`, `skip_to`).

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## Demographics

The `demographics` module isolates mortality logic from the financial and numerical layers. The root interface is `MortalityModel`, which exposes survival probability, death probability, age bounds, cloning, and a few actuarial convenience methods. The important point is not that this interface is small; it is that it is intentionally plain-double and independent of the AAD tape.

That design keeps a sharp boundary between actuarial assumptions and differentiated market state. Products consume mortality heavily, but the library does not currently force mortality through the tape. This keeps the taped graph smaller and reflects the way the current risk outputs are structured. If you need mortality sensitivities as first-class differentiated outputs in the future, this module is where the architectural change would start.

Concrete mortality implementations can remain table-driven and actuarially focused because the rest of the library sees only the probability interface. That means product code can ask for `_tp_x`-style behavior without caring whether the underlying implementation uses a lookup table, interpolation, or something more sophisticated.

### Mortality Interface Example

```cpp
class MortalityModel {
public:
	virtual Probability p(double x, double t) const = 0;
	virtual Probability q(double x, double t) const = 0;
	virtual int minAge() const = 0;
	virtual int maxAge() const = 0;
	virtual std::unique_ptr<MortalityModel> clone() const = 0;
};
```

### Usage Pattern

```cpp
double age = 65.0;
double oneYearSurvival = mortality.p(age, 1.0);
double deferredDeath = mortality.deferredQ(age, 5.0, 1.0);
```

### Implementation Notes

- Keep mortality logic self-contained. Products should ask for probabilities, not reach into a mortality table representation.
- The module is intentionally free of threading and tape concerns, which is part of why it composes cleanly with the rest of the system.

### Current Mortality Inputs

The mortality engine defines how a table is interpreted, but it does not impose one globally hardcoded production curve across the library. In the current codebase, the actual mortality assumption set is supplied by the caller, typically through example-level helper functions that build explicit age and `q_x` vectors.

The mortality assumptions used for variable-annuity work are based on the 1996 IAM tables for both male and female lives (SOA source). The implementation pattern is:

- integer age grid from `5` to `115`;
- one-year death probabilities supplied explicitly as a `std::vector<double>`;
- terminal probability forced to `1.0` at the highest age so the table closes with certain death;
- Uniform Distribution of Deaths interpolation for fractional ages.

Authoritative source citation for these assumptions:

Robert J. Johansen, "Review of Adequacy of 1983 Individual Annuity Mortality Table", Transactions of the Society of Actuaries, Vol. XLVII (1995), Appendix E. Accessed: 04/2013 from http://www.soa.org/Library/Research/Transactions-Of-Society-Of-Actuaries/1990-95/1995/January/tsa95v479.aspx

That last point matters operationally. If the code asks for survival or death probabilities over non-integer durations, `MortalityTable` converts annual probabilities into intra-year values under the UDD assumption rather than by stepwise annual jumps.

So there are really two levels of mortality assumptions in ActuaLib:

1. the mortality engine in `demographics/mortality.hpp`, which defines the actuarial interpretation of the table;
2. the mortality input vectors created by examples or applications, which define the actual calibrated mortality view used in a run.

### Where To Change Mortality Assumptions

If you want to change mortality behavior, the correct edit point depends on the type of change.

- Change `demographics/mortality.hpp` if you want to change the mathematical interpretation of mortality tables.
- Change the mortality-table construction in your application layer if you want a different mortality curve in an actual run. On current `main`, the tracked executable surface is `examples/simulate_rider.cpp`, and batched policy generation is described in `docs/portfolio_training.md`.
- Apply the same pattern in any local exploratory examples if you keep additional non-tracked driver programs in your worktree.

For most practical actuarial work, the right change is at the example or application level, not inside `MortalityTable` itself.

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## ESG

The `esg` module turns market assumptions into valuation inputs. In ActuaLib, that means both discounting infrastructure and explicit market models. The module is deliberately split into separate building blocks rather than one all-purpose scenario engine:

- `YieldCurve<T>` for discount factors and forward construction.
- `MultivariateBlackScholes<T>` for inner risk-neutral pricing.
- `RegimeSwitchingGenerator` for real-world outer equity scenarios.
- `InterestRateScenarioGenerator` for real-world outer interest-rate scenarios.

For users, the important distinction is measure and purpose. The inner model is a Q-measure pricing model used to value liabilities conditional on a market state. The outer models are P-measure scenario generators used to evolve that market state through time. On current `main`, the committed executable that composes those pieces end-to-end is [examples/simulate_rider.cpp](../examples/simulate_rider.cpp), and the higher-level portfolio workflow is described in [docs/portfolio_training.md](portfolio_training.md). The interested reader can find the real-world and risk-neutral nested valuation framework in Gan and Valdez, *Nested Stochastic Valuation of Large Variable Annuity Portfolios: Monte Carlo Simulation and Synthetic Datasets*, Data 2018, 3, 31, https://doi.org/10.3390/data3030031.

### Implemented Models At A Glance

1. Log-linear discount-curve interpolation with optional bootstrap from par swap rates.
2. Correlated multivariate Black-Scholes dynamics for inner valuation.
3. Two-state Markov regime-switching GBM for real-world equity evolution.
4. A three-state-variable monthly real-world interest-rate generator with stochastic volatility, mapped into a Nelson-Siegel style curve.

### Yield-Curve Representation

`YieldCurve<T>` stores tenors $0 = T_0 < T_1 < \dots < T_n$, discount factors $D(T_i)$, and cached values of $\ln D(T_i)$. For an arbitrary maturity $t$, the implementation uses log-linear interpolation:

$$
\ln D(t) = (1-w)\ln D(T_i) + w\ln D(T_{i+1}),
\qquad
w = \frac{t - T_i}{T_{i+1} - T_i}.
$$

Hence,

$$
D(t) = \exp\bigl(\ln D(t)\bigr).
$$

Beyond the last tenor, the code extrapolates with the flat forward rate implied by the last two knots. The same class also supports bootstrap from par swap rates using the fixed-leg identity

$$
s_k \sum_{i=1}^{k} \Delta_i D(T_i) = 1 - D(T_k),
$$

which is solved recursively for each new discount factor. In AAD runs, the discount knots can live on tape, so sensitivities to the input curve flow naturally through discounting and forward generation.

In the nested pricing stack, this `YieldCurve<T>` behavior remains the base interpolation/extrapolation rule. In addition, `MultivariateBlackScholes<T>` supports an optional Nelson-Siegel-informed tail mode (`useNSTail`) for maturities beyond the last curve knot. When enabled, the tail is shaped using the outer-node short/long rate state while remaining anchored to the on-tape last discount knot, so curve-risk reporting still maps to the same knot basis.

One implementation detail matters in practice: $D(0)$ is treated as structurally fixed, and `refreshLogDiscountsFromDiscountFactors()` explicitly resets $\ln D(0)=0$. This prevents stale tape edges through the origin knot after rewind cycles.

### Hull-White One-Factor Inner Interest-Rate Model

`HullWhiteBlackScholes<T>` extends the inner pricing model with a stochastic risk-neutral short rate calibrated to the outer AIRG yield curve at each outer scenario node.  The short rate follows the Hull-White one-factor (HW1F) model under $Q$:

$$
dr_t = \bigl[\theta(t) - a\,r_t\bigr]\,dt + \sigma\,dW_t^Q,
$$

where $\theta(t)$ is uniquely determined by the requirement that the model reproduces the outer yield curve exactly at $t = 0$.

**Exact discrete transition** (no Euler error):

$$
r_{i+1} = c_1\,r_i + c_{2,i} + c_3\,Z_i^{\text{rate}}, \qquad Z_i^{\text{rate}} \sim N(0,1),
$$

with precomputed constants
$$
c_1 = e^{-a\,\Delta t}, \quad
c_3 = \sigma\sqrt{\frac{1 - e^{-2a\Delta t}}{2a}}, \quad
c_{2,i} = \alpha(t_{i+1}) - \alpha(t_i)\,c_1,
$$
$$
\alpha(t) = f^M(0,t) + \frac{\sigma^2}{2a^2}(1 - e^{-at})^2,
$$
where $f^M(0,t) \approx [\ln D^M(t) - \ln D^M(t+\Delta t)]/\Delta t$ is the instantaneous forward rate from the outer AIRG curve.  The shape constants $c_1$ and $c_3$ depend only on the HW parameters $(a,\sigma)$ and are plain off-tape doubles.  The curve-dependent parts — the initial short rate
$$
r_0 = -\frac{\ln D^M(\Delta t)}{\Delta t}
$$
and the per-step drift shifts $c_{2,i}$ — are computed **on the AAD tape** via `HullWhite1F::computeOnTapeParts<T>()`, so every short-rate realisation $r_i$ propagates adjoints back to the outer yield-curve knots.

**Analytic path-dependent numeraire** (on AAD tape):

The money-market account $B(t_m) = 1/D^{\text{path}}(0,t_m)$ is accumulated step by step using the analytic HW one-step discount:

$$
P^{\mathrm{HW}}(t_i,\,t_{i+1}) = A_i\,e^{-B(\Delta t)\,r_i}, \qquad
B(\Delta t) = \frac{1 - e^{-a\Delta t}}{a},
$$

$$
\ln A_i = \Bigl(1 - \tfrac{B(\Delta t)}{\Delta t}\Bigr)\bigl[\ln D^M(t_{i+1}) - \ln D^M(t_i)\bigr] - \frac{\sigma^2 B(\Delta t)^2(1-e^{-2at_i})}{4a}.
$$

$\ln A_i$ is precomputed in `init()` using the on-tape curve discount factors $D^M(t_i)$, $D^M(t_{i+1})$, contributing one channel of IR Greek through the numeraire.  The dominant channel is the full HW calibration: $r_0$ and $c_{2,i}$ are also placed on the tape (via `computeOnTapeParts<T>()`), so the adjoint propagates through the entire short-rate recurrence
$$
r_{i+1} = c_1\,r_i + c_{2,i} + c_3\,Z_i
$$
back to the outer curve knots at every step.

**Equity drift** at each step uses the realised short rate $r_i$ (on tape, type `T`) rather than the static-curve forward rate used by `MultivariateBlackScholes`, so equity paths also carry IR sensitivity through the drift.  The dividend yield $q_k$ remains on tape for its own sensitivity.

**AAD tape layout** (same parameters as `MultivariateBlackScholes`):

| Parameter group | On tape? |
|---|---|
| Spots $S_k(0)$ | Yes |
| Dividend yields $q_k$ | Yes |
| Outer curve knots $D^M(T_j)$ | Yes — via $\ln A_i$ **and** via on-tape $r_0$, $c_{2,i}$ |
| Vols $\sigma_k$, correlations $\rho_{ij}$ | No (plain doubles) |
| HW parameters $a$, $\sigma$ | No (plain doubles) |
| Short rate $r_t$ | Yes — $T$-typed; adjoints propagate through the full HW recurrence |

**Simulation dimension**: $(d+1)\times\text{nSteps}$, with $d$ equity draws and 1 rate draw per step.

**Enabling HW inner rates** in a nested simulation:

```cpp
NestedSimConfig config;
config.useHullWhiteInner = true;
config.hwParams.a     = 0.05;   // mean-reversion speed
config.hwParams.sigma = 0.01;   // short-rate vol
```

When `useHullWhiteInner = false` (default), `buildInnerModel()` creates `MultivariateBlackScholes` with the static outer curve, preserving the existing behaviour exactly.

**Note on negative rates**: HW1F admits negative short rates.  For the short inner projection horizons typical of VA pricing (≤ 1 year), the probability of reaching significantly negative values from a positive starting rate is negligible at standard parameter values ($a \approx 0.05$, $\sigma \approx 0.01$).

**References**: Hull and White (1990), *Review of Financial Studies* 3(4):573–592; Hull (2022), *Options, Futures and Other Derivatives*, 11th ed., ch. 32.

### Multivariate Black-Scholes Inner Model

`MultivariateBlackScholes<T>` is the inner risk-neutral model used to value liabilities conditional on the market state handed over by the outer simulation. For asset $k$ under the pricing measure $Q$, the model is

$$
\frac{dS_k(t)}{S_k(t)} = \bigl(r(t) - q_k\bigr)dt + \sigma_k\, dW_k^Q(t),
\qquad
dW_i^Q dW_j^Q = \rho_{ij}\,dt.
$$

In the implementation:

- spots $S_k(0)$ are on tape when `T = Number`;
- dividend yields $q_k$ are on tape;
- curve discount knots are on tape;
- optional NS tail inputs (`nsShortRate`, `nsLongRate`) are off tape and affect only long-end curve shape;
- volatilities $\sigma_k$ and correlations $\rho_{ij}$ remain off tape as plain doubles.

The covariance matrix is assembled as

$$
\Sigma_{ij} = \sigma_i\sigma_j\rho_{ij},
$$

and a Cholesky factor $L$ is computed so that $\Sigma = LL^\top$. If $Z \sim N(0, I)$, correlated Gaussian shocks are obtained as $LZ$. The model then fills numeraires, forward prices, discount factors, and Libor-like quantities into `Scenario<T>` according to the product's `SampleDef`. This is the model that produces market-consistent values and AAD Greeks at a given outer node.

### Real-World Regime-Switching Equity Model

`RegimeSwitchingGenerator` implements a two-state Markov-modulated GBM for outer-loop equity evolution under the real-world measure. The regime $Z_t \in \{1,2\}$ is interpreted as a bull or bear state. Monthly transitions are derived from annualized transition intensities `p12` and `p21` using $\Delta t = 1/12$:

$$
P(Z_{t+\Delta t}=2 \mid Z_t=1) \approx p_{12}\Delta t,
\qquad
P(Z_{t+\Delta t}=1 \mid Z_t=2) \approx p_{21}\Delta t.
$$

Within regime $r$, asset $k$ follows

$$
\frac{dS_k(t)}{S_k(t)} = \mu_{r,k}\,dt + \sigma_{r,k}\, dW_k^P(t),
$$

with covariance matrix

$$
\Sigma_r(i,j) = \sigma_{r,i}\sigma_{r,j}\rho_{ij}.
$$

The implementation samples independent standard normals $Z$, correlates them with the regime-dependent Cholesky factor $L_r$, and forms monthly growth factors

$$
G_{r,k} = \exp\!\left[\left(\mu_{r,k} - \tfrac12\sigma_{r,k}^2\right)\Delta t + \sqrt{\Delta t}(L_r Z)_k\right].
$$

The container [esg/rw_scenarios.hpp](../esg/rw_scenarios.hpp) stores those growth factors path by path, and spot levels are reconstructed by cumulative multiplication. This model is intended for long-horizon outer projections where regime persistence and stress behavior matter.

### Real-World Interest-Rate Scenario Generator

`InterestRateScenarioGenerator` is a monthly real-world rate model with three state variables:

1. log-volatility,
2. log long rate,
3. the long-minus-short spread.

The implemented dynamics are

$$
\ln \sigma_{t+1} = (1-\beta_3)\ln \sigma_t + \beta_3\ln \tau_3 + \sigma_3\varepsilon_{3,t},
$$

$$
\ln r^L_{t+1} = (1-\beta_1)\ln r^L_t + \beta_1\ln \tau_1 + \psi(\tau_2 - d_t) + e^{\ln \sigma_{t+1}}\varepsilon_{1,t},
$$

$$
d_{t+1} = (1-\beta_2)d_t + \beta_2\tau_2 + \phi(\ln r^L_t - \ln \tau_1) + \sigma_2\varepsilon_{2,t}(r^L_{t+1})^{\theta}.
$$

The short rate is recovered as

$$
r^S_{t+1} = r^L_{t+1} - d_{t+1},
$$

with explicit lower and upper bounds enforced on rates and volatility for robustness. The shocks $(\varepsilon_1,\varepsilon_2,\varepsilon_3)$ are correlated through a 3 by 3 Cholesky factor built from `rho12`, `rho13`, and `rho23`.

This is not implemented as a textbook affine term-structure model. It is a practical actuarial-style ESG generator whose role in ActuaLib is to produce plausible real-world outer curves for nested valuation.  The model is the AIRG (Academy Interest Rate Generator) developed by the American Academy of Actuaries' Economic Scenario Work Group (ESWG); it is the mandated scenario generator for C3 Phase I/II/III and VM-20 regulatory capital calculations in the United States.  Support contact: ESGhelp@SOA.org.  Workbooks and documentation: actuary.org.

### Nelson-Siegel Curve Construction

After updating the short and long rates, the generator converts them into a full curve using a one-factor Nelson-Siegel shape with fixed decay parameter `k = 0.4`. For maturity $T$, the continuously compounded yield is

$$
y(T) = b_0 + b_1\frac{1 - e^{-kT}}{kT},
$$

where $b_0$ and $b_1$ are chosen so that the constructed curve matches the simulated short and long rates. Discount factors are then formed as

$$
D(0,T) = e^{-y(T)T}.
$$

The implemented maturity grid is $0.25$, $0.5$, $1$, $2$, $3$, $5$, $7$, $10$, $20$, and $30$ years, with an explicit $t=0$ anchor. This remains the base curve consumed downstream by nested pricing; when `useNSTail` is enabled, the inner model keeps this knot basis for risk reporting but uses the outer short/long-rate state to shape long-end discounting beyond the last knot.

### Representative Nested Calibration

The library provides reusable ESG building blocks, but the detailed calibration below should now be read as a representative nested setup rather than as the canonical tracked entry point on `main`. The actively tracked executable surface in this branch is `examples/simulate_rider.cpp`, and the batched training workflow is documented in `docs/portfolio_training.md`. Use those for current end-to-end runs.

#### Real-World Equity Parameters

The outer equity scenario generator currently uses `RSParams` with five indices:

- `nIndices = 5`
- bull-regime drifts `mu1 = {0.09, 0.10, 0.08, 0.035, 0.020}`
- bull-regime volatilities `sigma1 = {0.16, 0.22, 0.18, 0.05, 0.01}`
- bear-regime drifts `mu2 = {-0.08, -0.12, -0.09, 0.010, 0.015}`
- bear-regime volatilities `sigma2 = {0.28, 0.35, 0.30, 0.09, 0.02}`
- annual transition rates `p12 = 0.25`, `p21 = 0.75`
- initial probability of starting in regime 2 equal to `initProb2 = 0.20`
- outer path count `nRWPaths = 48`
- real-world RNG seed `rwSeed = 12345`

The current 5 by 5 equity correlation matrix is:

$$
\rho =
\begin{bmatrix}
1.000000 & 0.761332 & 0.556299 & 0.238114 & -0.025552 \\\\
0.761332 & 1.000000 & 0.443120 & 0.131246 & -0.024576 \\\\
0.556299 & 0.443120 & 1.000000 & 0.153277 & -0.023841 \\\\
0.238114 & 0.131246 & 0.153277 & 1.000000 & 0.062975 \\\\
-0.025552 & -0.024576 & -0.023841 & 0.062975 & 1.000000
\end{bmatrix}.
$$

This gives the current outer model a clear risk-on versus risk-off interpretation: regime 1 carries higher expected equity growth with lower volatility, while regime 2 carries negative equity drift with materially higher volatility.

#### Risk-Neutral Inner Equity Parameters

The inner `MultivariateBlackScholes<T>` model in the same example currently uses:

- initial spots `initialSpots = {30000, 25000, 20000, 15000, 10000}`
- volatilities `vols = {0.18, 0.24, 0.20, 0.06, 0.015}`
- dividend yields `divs = {0, 0, 0, 0, 0}`
- the same 5 by 5 correlation matrix shown above
- inner path count `nRNPaths = 256`

In the current implementation, spots, dividend yields, and discount-curve knots may participate in AAD, while the Black-Scholes volatilities and correlations remain off tape as ordinary doubles.

#### Real-World Interest-Rate Parameters

The outer interest-rate generator uses `IRModelParams`. In the shipped nested all-rider example, some values are set explicitly in the example code and the remaining values are inherited from the struct defaults. The full currently active parameter set is therefore:

- long-run targets `tau1 = 0.04`, `tau2 = 0.03`, `tau3 = 0.01`
- mean-reversion speeds `beta1 = 0.10`, `beta2 = 0.15`, `beta3 = 0.05`
- shock scales `sigma1 = 0.05`, `sigma2 = 0.02`, `sigma3 = 0.20`
- structural coefficients inherited from current defaults: `phi = 0.10`, `psi = 0.50`, `theta = 0.50`
- lower and upper bounds inherited from current defaults:
	- `minLongRate = 0.001`
	- `maxLongRate = 0.15`
	- `minShortRate = 0.001`
	- `maxShortRate = 0.25`
	- `minVol = 1.0e-4`
	- `maxVol = 0.50`
- shock correlations `rho12 = 0.30`, `rho13 = 0.10`, `rho23 = -0.20`
- initial short rate `initialIRShort = 0.02`
- initial long rate `initialIRLong = 0.04`
- initial volatility state `initialIRVol = 0.01`
- interest-rate RNG seed `irSeed = 54321`

The Nelson-Siegel conversion inside `InterestRateScenarioGenerator::buildNelsonSiegelCurve(...)` currently uses:

- maturity grid `0.25, 0.5, 1, 2, 3, 5, 7, 10, 20, 30`
- fixed decay parameter `k = 0.4`

Those hardcoded choices matter because they determine the shape of the curve handed from the outer real-world generator into the inner pricing problem.

In the current nested implementation, the outer node also retains the corresponding short and long rates. The nested builder can pass these into the inner model so optional NS-tail discounting uses more of the outer curve state without increasing the number of reported curve risk factors.

### Where And How To Change ESG Parameters

The main developer rule is simple: change example files when you want a different calibration, and change `esg/*.hpp` only when you want to alter model defaults or the model definition itself.

#### Change The Current Driver Calibration

For the tracked single-contract driver, adjust the relevant defaults or wiring in `examples/simulate_rider.cpp`, or pass policy and simulation settings through its CLI. For batch experiments, prefer changing the portfolio-generation and runner inputs described in `docs/portfolio_training.md`. Those are the primary change points for:

- real-world equity drifts, volatilities, transition rates, path counts, and seeds;
- risk-neutral spots, vols, dividends, and inner-path counts;
- real-world interest-rate levels, reversion speeds, volatilities, initial states, and seeds;
- benefit-base rule selection: pass `--benefit-base-rule {ROP, ratchet, roll-up}` to `gen_portfolio.py` to pin every policy in a portfolio to one rule, and `--update-rule {rop, step-up, roll-up}` to `sobolev_va_training.py` to train a surrogate on a single rule's data.

The complete paper-dataset pipeline (9 riders × 3 rules × 100 policies, 100 000 × 1 000 paths) is in `scripts/paper_pipeline.txt`.

If you keep additional local exploratory example files in your worktree, make corresponding edits there so the calibrations stay aligned.

#### Change Default Real-World Equity Parameters

Edit `RSParams` in `esg/regime_switching.hpp` if you want to change the library-level defaults for:

- `nIndices`
- `p12`
- `p21`
- `initProb2`

The drift, volatility, and correlation arrays are expected to be provided by the caller and are therefore best changed in the application or example that builds them.

#### Change Default Real-World Interest-Rate Parameters

Edit `IRModelParams` in `esg/interest_rate_model.hpp` if you want to change the library-level defaults for:

- `tau1`, `tau2`, `tau3`
- `beta1`, `beta2`, `beta3`
- `sigma1`, `sigma2`, `sigma3`
- `phi`, `psi`, `theta`
- lower and upper bounds on rates and volatility
- `rho12`, `rho13`, `rho23`

Edit `InterestRateScenarioGenerator::buildNelsonSiegelCurve(...)` in the same file if you want to change the maturity grid or the currently fixed decay parameter `k = 0.4`.

#### Change Risk-Neutral Inner Pricing Inputs

Edit the caller that assembles the nested run, typically `examples/simulate_rider.cpp` on current `main`, if you only want to change the numerical values of spots, vols, dividends, or path counts.

Use `NestedSimConfig::useNSTail` to toggle the inner long-end discounting mode in nested runs (`false` = knot-only curve extrapolation, `true` = NS-informed tail anchored to curve knots).

Set `NestedSimConfig::useHullWhiteInner = true` (with `hwParams.a` and `hwParams.sigma`) to replace the static-curve inner discount model with a stochastic Hull-White one-factor short rate calibrated to the outer AIRG curve.  See the *Hull-White One-Factor Inner Interest-Rate Model* section above for details.

Edit `MultivariateBlackScholes<T>` in `esg/multivariate_blackscholes.hpp` if you want to change the static-curve inner model contract.  Edit `HullWhiteBlackScholes<T>` in `esg/hull_white_blackscholes.hpp` if you want to change the stochastic inner rate model.

#### Change Mortality Inputs Used By ESG-Driven Examples

Edit the mortality-table construction helper in the relevant example or application source if you want a different mortality calibration in a nested valuation run.

Edit `demographics/mortality.hpp` only if you want to change the mathematical interpretation of that table, such as interpolation or actuarial helper logic.

### How The ESG Pieces Work Together

The nested valuation workflow is:

1. generate outer real-world equity and rate paths (AIRG outer interest-rate generator + regime-switching equity);
2. convert each outer state into inner pricing inputs: fund levels, the AIRG yield curve as the initial term structure, and optionally short/long rate state for NS-tail handling;
3. run an inner risk-neutral valuation using either:
   - `MultivariateBlackScholes<T>` with a **static outer curve** (`useHullWhiteInner = false`, default), or
   - `HullWhiteBlackScholes<T>` with a **stochastic HW1F short rate** calibrated to the outer curve (`useHullWhiteInner = true`);
4. average across outer scenarios to obtain the nested liability estimate.

Conceptually, the outer layer answers "where could the world be at the future valuation date?" and the inner layer answers "what is the market-consistent value of the guarantee conditional on that state?" For a fuller discussion of that real-world versus risk-neutral split in a VA context, the interested reader should again see Gan and Valdez (2018).

### Model Interface Example

```cpp
template<class T>
class LifeModel {
public:
	virtual void allocateProjection(
		const std::vector<double>& timeline,
		const std::vector<SampleDef>& defline) = 0;

	virtual void initProjection(
		const std::vector<double>& timeline,
		const std::vector<SampleDef>& defline) = 0;

	virtual size_t simDim() const = 0;

	virtual void generatePath(
		const std::vector<double>& gaussVec,
		Scenario<T>& path) const = 0;
};
```

### Black-Scholes Construction Example

```cpp
YieldCurve<Number> curve;
curve.setDiscountCurve({0.0, 1.0, 2.0}, {Number(1.0), Number(0.97), Number(0.93)});

MultivariateBlackScholes<Number> model(
	std::vector<Number>{Number(100.0), Number(95.0)},
	std::vector<Number>{Number(0.02), Number(0.01)},
	std::vector<double>{0.20, 0.25},
	corrMatrix,
	curve);
```

### What Users Should Watch

- `SampleDef::numAssets` must stay consistent with the model dimension. A mismatch usually appears as malformed path writes rather than as a clean compiler error.
- Yield-curve discount knots are part of the differentiated state in AAD inner pricing, so tape lifecycle issues often surface first in this module.
- The outer models are for scenario generation, not direct market-consistent pricing. Market-consistent valuation happens in the inner Black-Scholes layer.

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## Monte Carlo

The `montecarlo` module is the orchestration layer between models and products. It defines the path container format in `montecarlo/scenario.hpp` and implements the AAD-aware and nested simulation flows that are currently aligned with the library's active variable-annuity use case. The key idea is that a model fills a `Scenario<T>` path according to a `SampleDef` contract and a product evaluates that path into one or more outputs.

The central currently active path is `mcsimulationAAD.hpp`. That flow clones the model and RNG, allocates the path once, marks the tape after model initialization, rewinds to that mark per path, and accumulates adjoints in a bounded tape region. That design is what makes large differentiated path batches feasible without letting the graph grow path after path.

Nested simulation builds one more layer on top. The outer loop is parallel, and each worker performs the inner pricing serially against its own state. That choice is deliberate. It avoids cross-thread tape ownership and avoids nested schedulers fighting over the same work queue. The result is less theoretical parallelism inside each node, but a much cleaner implementation and a far lower debugging burden.

### AAD Monte Carlo Example

```cpp
LifeAADResults results = lifeSimulationAAD(
	product,
	model,
	rng,
	1000);

for (const auto& pathOutputs : results.pathOutputs) {
	// pathOutputs contains the product's output vector for one path
}
```

### Simulation Skeleton

```cpp
Scenario<Number> path;
allocatePath(prd.projectionDefline(), path);
initializePath(path);

for (size_t i = 0; i < nPaths; ++i) {
	tape.rewindToMark();
	cRng->nextG(gaussVec);
	cMdl->generatePath(gaussVec, path);
	prd.evaluateLiability(path, nPayoffs);
	Number result = aggFun(nPayoffs);
	result.propagateToMark();
}
```

### Implementation Notes

- `Scenario<T>` is not an incidental container. It is the core data contract between models and products.
- `allocatePath()` and `initializePath()` are designed to let path memory be reused rather than rebuilt for every path.
- Deterministic compensated summation in parallel AAD is not a micro-optimization; it is part of the correctness strategy for stable reported sensitivities.

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## Products

The product layer implements a variable-annuity guarantee engine in the same spirit as Gan and Valdez's projection framework for large VA portfolios, including standalone riders and combined riders.

Primary actuarial references used for scope and behavior:

- Gan, G.; Pan, J. *Valuation of Large Variable Annuity Portfolios: Monte Carlo Simulation and Synthetic Datasets* (AFIR-ERM Panama, 2017).
- Gan, G.; Valdez, E.A. *Nested Stochastic Valuation of Large Variable Annuity Portfolios: Monte Carlo Simulation and Synthetic Datasets*. Data 2018, 3, 31. [https://doi.org/10.3390/data3030031](https://doi.org/10.3390/data3030031)

In the paper's notation, monthly projected cash flows decompose into death benefits (DA), living benefits (LA), and risk charges (RC). This is exactly the actuarial split represented in ActuaLib's rider step logic.

### Actuarial Product Scope Implemented Today

| Rider type | Actuarial meaning | Contract behavior in ActuaLib |
| --- | --- | --- |
| `GMDB` | Guaranteed Minimum Death Benefit | Death claim shortfall versus account value at each step. |
| `GMMB` | Guaranteed Minimum Maturity Benefit | Maturity-only living guarantee, no renewal. |
| `GMAB` | Guaranteed Minimum Accumulation Benefit | Maturity guarantee with renewal/top-up mechanics. |
| `GMWB` | Guaranteed Minimum Withdrawal Benefit | Anniversary guaranteed withdrawal with insurer shortfall coverage. |
| `GMIB` | Guaranteed Minimum Income Benefit | Maturity annuitization floor versus account value. |
| `GMDB_AB` | Death + accumulation combo | Joint death guarantee and GMAB-style maturity guarantee. |
| `GMDB_MB` | Death + maturity combo | Joint death guarantee and GMMB-style maturity guarantee. |
| `GMDB_WB` | Death + withdrawal combo | Joint death guarantee and GMWB withdrawal logic. |
| `GMDB_IB` | Death + income combo | Joint death guarantee and GMIB annuitization logic. |

### Contract Assumptions Used By The Projection Engine

The Panama 2017 formulation is explicit about event ordering and policyholder behavior. The current implementation follows the same high-level actuarial pattern.

- Monthly step ordering: fund-management fees deducted first, then M&E plus rider charges, then death benefit if death occurs in-step, then living benefit if alive.
- Benefit-base styles: return-of-premium, annual roll-up, and annual ratchet are supported conceptually through guarantee-base update rules.
- GMWB behavior assumption: policyholder takes the maximum allowable withdrawal at anniversaries (subject to remaining guaranteed withdrawal balance).
- GMAB behavior assumption: at maturity, policy renews only when account value exceeds guarantee base; otherwise insurer shortfall is paid.
- GMIB maturity option: living benefit is based on the annuitization-floor comparison at maturity.

### Rider Definitions (Actuarial View)

The library computes monthly death benefit (DA), living benefit (LA), and rider charge (RC), then aggregates mortality-weighted present values into FMV.

- `GMDB`: DA = max(0, gbAmt - AV), LA = 0.
- `GMMB`: LA = max(0, gbAmt - AV) at maturity horizon only.
- `GMAB`: Same maturity LA as GMMB plus maturity renewal mechanics (top-up/reset) and possible multiple maturity events.
- `GMWB`: Anniversary guaranteed withdrawal with guaranteed withdrawal balance drawdown; LA is insurer shortfall when AV cannot fund withdrawal.
- `GMIB`: LA at maturity based on annuitization-floor value versus AV.
- `GMDB_*` combos: Add GMDB-style death protection to the corresponding living rider.

### Paper-to-Code Mapping (What Matters For Actuaries)

| Paper concept (Gan 2017) | Practical meaning | ActuaLib location |
| --- | --- | --- |
| DA / LA / RC decomposition | Separate death claim, living claim, and rider-charge cashflows each step | `products/va_base.hpp` and rider-specific `project(...)` |
| Guarantee base evolution (ROP / roll-up / ratchet) | Contract basis update rule at anniversaries | `VAPolicy::updateRule` consumed by rider logic |
| Maturity-only living claims (GMMB/GMIB) | Living benefits occur only at maturity checkpoints | `va_gmmb.hpp`, `va_gmib.hpp` |
| Anniversary withdrawal dynamics (GMWB) | Scheduled annual withdrawals against guarantee balance | `va_gmwb.hpp` |
| Combined riders (DBAB/DBIB/DBMB/DBWB) | Overlay GMDB death logic on living rider | `products/va_combo.hpp` |

### Calibration Context From Gan Panama 2017

The paper's synthetic portfolio uses rider-charge levels that are helpful as a starting sanity range (not as production calibration):

- Standalone riders roughly span 25 bps to 75 bps depending on guarantee richness and base style.
- Combined riders are modeled as sum of component fees minus a diversification adjustment (20 bps in the paper setup).

In practice, keep these as benchmark magnitudes only; real pricing should calibrate to current product specs, hedge costs, lapse assumptions, and market regime.

### Instantiate Each Implemented Rider (Factory)

```cpp
#include "products/va_factory.hpp"

using namespace ActuaLib;

auto gmdb    = makeRider<double>(RiderType::GMDB,    policy, mortality);
auto gmmb    = makeRider<double>(RiderType::GMMB,    policy, mortality);
auto gmab    = makeRider<double>(RiderType::GMAB,    policy, mortality);
auto gmwb    = makeRider<double>(RiderType::GMWB,    policy, mortality);
auto gmib    = makeRider<double>(RiderType::GMIB,    policy, mortality);
auto gmdbAb  = makeRider<double>(RiderType::GMDB_AB, policy, mortality);
auto gmdbMb  = makeRider<double>(RiderType::GMDB_MB, policy, mortality);
auto gmdbWb  = makeRider<double>(RiderType::GMDB_WB, policy, mortality);
auto gmdbIb  = makeRider<double>(RiderType::GMDB_IB, policy, mortality);
```

### Instantiate Direct Concrete Classes (Alternative)

```cpp
#include "products/va_gmdb.hpp"
#include "products/va_gmmb.hpp"
#include "products/va_gmab.hpp"
#include "products/va_gmwb.hpp"
#include "products/va_gmib.hpp"
#include "products/va_combo.hpp"

using namespace ActuaLib;

GMDB<double>    gmdb(policy, mortality);
GMMB<double>    gmmb(policy, mortality);
GMAB<double>    gmab(policy, mortality);
GMWB<double>    gmwb(policy, mortality);
GMIB<double>    gmib(policy, mortality);
GMDB_AB<double> gmdbAb(policy, mortality);
GMDB_MB<double> gmdbMb(policy, mortality);
GMDB_WB<double> gmdbWb(policy, mortality);
GMDB_IB<double> gmdbIb(policy, mortality);
```

### Policy Data Needed By Actuarial Users

All riders share `VAPolicy` and mortality inputs, with rider-specific fields activated as needed.

- Common: `fundValues`, `fundFees`, `baseFee`, `riderFee`, `currentAge`, `numMonths`, `monthsSinceIssue`, `female`, `gbAmt`, `updateRule`.
  `numMonths` is **remaining months from the valuation date to maturity** (not total contract term).
  `monthsSinceIssue` is months elapsed since inception and controls annual fee/benefit-base anniversary timing.
- `GMAB` and `GMDB_AB`: set maturity schedule fields such as `maturityPeriod` or `firstMaturityMonth` and `renewalPeriod`.
- `GMWB` and `GMDB_WB`: set `gmwbBalance` and `wbWithdrawalRate`.
- `GMIB` and `GMDB_IB`: set `guaranteedAnnuityRate`.

### Projection Architecture Behind All Riders

`VABase<T>` provides the shared monthly projection skeleton, and each rider overrides one hook:

```cpp
template <class T>
class VABase : public Product<T> {
protected:
    virtual VAStepCashflows<T> project(VAStepState<T>& state) const = 0;
};
```

For actuarial interpretation, this means all riders use one consistent timeline, one consistent mortality weighting, and one consistent fee/tape lifecycle. Product differences are localized to the monthly benefit logic in `project()`.

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

## Examples And Tests

The `examples` directory is still the fastest way to understand how the library is intended to be composed in practice, but on current `main` the tracked executable surface is `simulate_rider.cpp`. That file shows the current application-level pattern: build policy data, construct mortality and market models, run a nested valuation, and write FMV plus sensitivities to HDF5. The broader portfolio orchestration and surrogate-training workflow now lives in the Python scripts documented in `docs/portfolio_training.md`.

The `tests` directory serves two different roles. First, it contains low-level checks such as matrix behavior, AAD smoke tests, gradient verification, and RNG sanity/parity tests. Second, it acts as infrastructure-regression coverage. In this library, many important failures show up as changed valuation numbers or unstable sensitivities rather than as hard crashes. That makes the tests as much about preserving behavior as about catching programming mistakes.

When onboarding to the codebase, a good sequence is: read this guide, open one example, open the corresponding product class, then follow the call chain back into `LifeModel<T>`, `Scenario<T>`, and the Monte Carlo entry point. That path exposes the actual design much faster than reading headers in directory order.

### Minimal Reading Order For New Contributors

```text
1. products/va_base.hpp
2. esg/model.hpp
3. montecarlo/scenario.hpp
4. montecarlo/mcsimulationAAD.hpp
5. aad/number.hpp and aad/gradient_tape.hpp
6. examples/simulate_rider.cpp
```

### What To Check Before Changing Core Infrastructure

- If you change AAD lifecycle code, re-check tape mark/rewind behavior and parallel sensitivity reduction.
- If you change concurrency code, verify worker-slot ownership assumptions in parallel Monte Carlo AAD.
- If you change path layout or `SampleDef`, audit both model writers and product readers.
- If you change yield-curve or RNG code, expect valuation and regression tests to move before compile errors appear.

<p class="back-to-tabs"><a href="#module-tabs">Back to tabs</a></p>

---

<!-- markdownlint-enable MD033 MD010 MD024 -->

## Closing Notes

ActuaLib is easiest to use when the workflow stays explicit: models generate paths, products evaluate those paths, Monte Carlo repeats the exercise, and AAD differentiates the same valuation logic rather than replacing it. If you are new to the library, start from an example, identify the product, the market model, and the simulation entry point, and then work downward only as far as you need.

If you are extending the library, prefer clear data flow and deterministic ownership over additional abstraction. In this codebase, the most reliable improvements have come from preserving those boundaries while making the model assumptions and user-facing workflow easier to understand.
