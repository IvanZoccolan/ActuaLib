"""
gen_portfolio.py — generate a synthetic VA portfolio of policies for use with SimulateRider.

Each policy's contract parameters are drawn  following the following
synthetic-dataset methodology:

  Issue age Age0       ~ per-rider integer range (see RIDER_CONFIG):
                           accumulation riders:  U[40, 60]
                           death-benefit only:   U[40, 70]
                           income / withdrawal:  U[50, 72]
  Sex                  ~ Bernoulli(0.40 female)  — Gan (2017) Table 6 / US market
  Initial fund F(0)    ~ U[$50k, $300k]
  Guarantee base G     ~ F(0) × U[0.65, 1.50]  for GMDB-component riders
                         F(0) × U[0.90, 1.25]  for pure living benefits
  Term (yr):
      GMAB, GMDB_AB    ∈ {10, 15, 20}  (multiples of 5 for renewal alignment)
      GMWB, GMDB_WB, GMIB, GMDB_IB  ~ U{12, ..., 20}
      GMDB, GMMB, GMDB_MB            ~ U{10, ..., 20}
  Policy age k0 (mo.)  ~ U{12, ..., min(120, term_mo - horizon - 60)}
                         (≥5 yr remaining *after* the evaluation horizon)
  Base fee (M&E)       ~ U[80, 200] bp/yr  (Gan 2017: fixed 200 bp;
                         modern fee-based products: 80–150 bp)
  Rider fee            ~ per-rider range (bp/yr):
                           GMDB:    U[15,  40]   GMMB:    U[25,  50]
                           GMAB:    U[40,  75]   GMWB:    U[50, 100]
                           GMIB:    U[50, 100]
                           GMDB_AB: U[45,  95]   GMDB_MB: U[30,  70]
                           GMDB_WB: U[50, 115]   GMDB_IB: U[50, 115]
                         (calibrated to Bauer et al. 2008 + Gan Panama 2017)
  Benefit-base rule    ~ U{ROP, ratchet, roll-up}   (roll-up rate fixed at 3%)
  GMWB withdrawal rate ~ U[4%, 7%]
  GMIB annuity rate    ~ U[4%, 7%]
  Fund weights (5)     ~ U[0.20, 1.00] each, then normalised

Output: JSON file, one entry per (policy × rider) combination.

Usage:
    python gen_portfolio.py [OPTIONS]

Options:
    --policies   N       Number of policy holders to simulate.  [20]
    --riders     R [R …] Rider type(s) to include.              [all 9]
    --seed       S       Random seed.                            [20260327]
    --horizon    H       Real-world projection horizon in months shared by
                         all policies in the portfolio (e.g. 12 = 1 yr,
                         60 = 5 yr, 120 = 10 yr).  [12]
                         Every policy is sampled so that at least 5 years
                         of contract life remain *after* the horizon ends,
                         i.e.  maturity_months − horizon ≥ 60.
    --shards     N       Split the portfolio into N roughly equal files
                         for distributed execution on separate nodes. [1]
                         With --shards N the output paths become
                         <stem>_shard_00.json … <stem>_shard_<N-1>.json
                         (e.g. portfolio_shard_00.json).  All N shards
                         share the same meta-data; policy_id values are
                         globally unique across shards.
    --output     FILE    Output JSON path.                       [portfolio.json]
    --help               Print this message and exit.
"""

from __future__ import annotations

import argparse
import json
import math
import random
import statistics
import sys
from collections import Counter
from datetime import date
from pathlib import Path

# ─────────────────────────────────────────────────────────────────────────────
#  Constants
# ─────────────────────────────────────────────────────────────────────────────

ALL_RIDERS = [
    "GMDB", "GMMB", "GMAB", "GMWB", "GMIB",
    "GMDB_AB", "GMDB_MB", "GMDB_WB", "GMDB_IB",
]

# Term ranges (in years) by rider group (from paper Table 4)
TERM_FIXED_MULTIPLES = {"GMAB", "GMDB_AB"}            # {10, 15, 20}
TERM_RANGE_12_20     = {"GMWB", "GMDB_WB", "GMIB", "GMDB_IB"}  # U{12..20}
TERM_RANGE_10_20     = {"GMDB", "GMMB", "GMDB_MB"}    # U{10..20}

BENEFIT_BASE_RULES = ["ROP", "ratchet", "roll-up"]

ROLL_UP_RATE = 0.03   # fixed 3 % / yr (paper Table 4)

# Per-fund annual MER rates (expense ratios) for the 5 sub-accounts.
# Hardcoded to match the C++ defaults in simulate_rider.cpp; stored in
# the portfolio JSON to keep the path open for per-policy variation.
FUND_FEES: list[float] = [0.0040, 0.0050, 0.0060, 0.0025, 0.0010]

# Riders that include a death-benefit guarantee component.
# Their GB/AV ratio spans a wider range because ROP policies are commonly
# deep OTM after a sustained bull market (GB/AV ≈ 0.55–0.75).
GMDB_COMPONENT_RIDERS = {"GMDB", "GMDB_AB", "GMDB_MB", "GMDB_WB", "GMDB_IB"}

# Gender split: 40 % female — Gan (2017) Table 6; consistent with US VA market.
PROB_FEMALE: float = 0.40

# M&E / base fee range (basis points / year).
# Gan (2017): fixed 200 bp.  Pre-2015 commission-based products: 120–200 bp;
# modern fee-based products: 80–150 bp.  U[80, 200] covers the US market range.
BASE_FEE_BP: tuple[int, int] = (80, 200)

# Per-rider configuration: issue-age range (years, integer) and rider-fee
# range (basis points / year).
#
# Issue ages reflect typical buyer demographics by guarantee type:
#   accumulation (GMAB)   → broader working-age buyers: [40, 60]
#   pure death benefit    → widest range, any pre-retirement age: [40, 70]
#   income / withdrawal   → pre-retirement / early-retirement buyers: [50, 72]
#
# Rider fees calibrated to Bauer et al. (2008) Table 2, Gan Panama (2017)
# Table 1, and LIMRA / Morningstar US market surveys (2020–2024):
#   Individual riders: 15–100 bp depending on benefit type.
#   Combined products: ~20 bp bundling discount vs. sum of constituent riders.
RIDER_CONFIG: dict[str, dict] = {
    #             issue-age range   rider-fee range (bp/yr)
    "GMDB":    dict(age=(40, 70), fee_bp=(15,  40)),
    "GMMB":    dict(age=(40, 65), fee_bp=(25,  50)),
    "GMAB":    dict(age=(40, 60), fee_bp=(40,  75)),
    "GMWB":    dict(age=(50, 72), fee_bp=(50, 100)),
    "GMIB":    dict(age=(50, 72), fee_bp=(50, 100)),
    "GMDB_AB": dict(age=(40, 62), fee_bp=(45,  95)),
    "GMDB_MB": dict(age=(40, 65), fee_bp=(30,  70)),
    "GMDB_WB": dict(age=(50, 72), fee_bp=(50, 115)),
    "GMDB_IB": dict(age=(50, 72), fee_bp=(50, 115)),
}


# ─────────────────────────────────────────────────────────────────────────────
#  Helper
# ─────────────────────────────────────────────────────────────────────────────

def _alloc_weights(rng: random.Random) -> list[float]:
    """Draw 5 fund-allocation weights from U[0.20, 1.00] and normalise."""
    raw = [rng.uniform(0.20, 1.00) for _ in range(5)]
    total = sum(raw)
    return [round(w / total, 6) for w in raw]


def _term_years(rng: random.Random, rider: str) -> int:
    """Sample contract term in years following the paper's per-rider rules."""
    if rider in TERM_FIXED_MULTIPLES:
        return rng.choice([10, 15, 20])
    elif rider in TERM_RANGE_12_20:
        return rng.randint(12, 20)
    else:
        return rng.randint(10, 20)


def _policy_age_months(rng: random.Random, term_months: int, horizon: int = 12) -> int:
    """
    Sample policy age k0 in months.

    The portfolio is designed to be evaluated at a single shared horizon
    (real-world projection length).  To guarantee meaningful inner-pricing
    results at that future evaluation date, every policy must still have at
    least 5 years (60 months) of contract life remaining *after* the horizon
    expires, i.e.

        maturity_months − horizon  ≥  60
        ⟺  term_months − k0 − horizon  ≥  60
        ⟺  k0  ≤  term_months − horizon − 60

    Combined with the 10-year seasoning cap:
        k0 ∈ U{12, ..., min(120, term_months − horizon − 60)}
    """
    k0_max = min(120, term_months - horizon - 60)
    if k0_max < 12:
        # Edge case: contract too short to satisfy constraint — use 12 months
        return 12
    return rng.randint(12, k0_max)


# ─────────────────────────────────────────────────────────────────────────────
#  Core sampler
# ─────────────────────────────────────────────────────────────────────────────

def sample_policy(rng: random.Random, rider: str, policy_id: int, horizon: int = 12,
                  forced_rule: str | None = None) -> dict:
    """
    Draw one policy×rider record using per-rider calibrated distributions.

    Age and rider-fee ranges follow RIDER_CONFIG (calibrated to Bauer et al.
    2008, Gan Panama 2017, and LIMRA/Morningstar US VA market surveys).
    GB/AV range, gender split, and M&E fee follow Gan-Valdez (2020).

    Returns a dict that maps directly to SimulateRider CLI flags.
    """
    # Demographics: per-rider age range; gender split per Gan (2017)
    cfg = RIDER_CONFIG[rider]
    issue_age: int = rng.randint(*cfg["age"])
    gender: str = "F" if rng.random() < PROB_FEMALE else "M"

    # Contract term
    term_yr = _term_years(rng, rider)
    term_mo = term_yr * 12

    # Policy age at evaluation
    aging_mo = _policy_age_months(rng, term_mo, horizon)

    # Remaining horizon seen by the inner pricer
    maturity_mo = term_mo - aging_mo

    # Evaluation age
    eval_age = round(issue_age + aging_mo / 12.0, 4)

    # Financial parameters
    fund_total: float = round(rng.uniform(50_000.0, 300_000.0), 2)
    gb_multiplier: float = (
        rng.uniform(0.65, 1.50) if rider in GMDB_COMPONENT_RIDERS
        else rng.uniform(0.90, 1.25)
    )
    gb: float = round(fund_total * gb_multiplier, 2)

    # Fees: M&E base charge and per-rider guarantee charge (bp → annual fraction)
    base_fee: float  = round(rng.uniform(*BASE_FEE_BP)      / 10_000.0, 6)
    rider_fee: float = round(rng.uniform(*cfg["fee_bp"])    / 10_000.0, 6)

    # Fund allocation weights (5 sub-accounts)
    alloc = _alloc_weights(rng)

    # Benefit-base update rule
    bb_rule = forced_rule if forced_rule is not None else rng.choice(BENEFIT_BASE_RULES)

    # Rider-specific extras
    wb_withdrawal_rate: float | None = None
    gmib_annuity_rate: float | None = None
    if rider in {"GMWB", "GMDB_WB"}:
        wb_withdrawal_rate = round(rng.uniform(0.04, 0.07), 4)
    if rider in {"GMIB", "GMDB_IB"}:
        gmib_annuity_rate = round(rng.uniform(0.04, 0.07), 4)

    record: dict = {
        # Identification
        "policy_id":         policy_id,
        "rider":             rider,

        # Demographics
        "issue_age":         issue_age,
        "eval_age":          eval_age,
        "gender":            gender,

        # Contract horizon (as seen by SimulateRider)
        "maturity_months":   maturity_mo,   # --maturity
        "aging_months":      aging_mo,      # --months-since-issue

        # Financial
        "av":                fund_total,    # --av  (total initial AV)
        "gb":                gb,            # --gb
        "alloc":             alloc,         # --alloc  (5 weights, sum=1)
        "base_fee":          base_fee,      # --base-fee
        "rider_fee":         rider_fee,     # --rider-fee
        "fund_fees":         list(FUND_FEES),  # --fund-fees

        # Benefit-base rule (informational; SimulateRider uses step-up by default)
        "benefit_base_rule": bb_rule,
        "roll_up_rate":      ROLL_UP_RATE if bb_rule == "roll-up" else None,

        # Shared evaluation horizon — must match the --horizon passed to run_portfolio
        "horizon_months":    horizon,
    }

    if wb_withdrawal_rate is not None:
        record["wb_withdrawal_rate"] = wb_withdrawal_rate
        # GMWB balance today: gbAmt reduced by withdrawals taken since inception.
        record["gmwb_balance"] = round(
            gb * max(0.0, 1.0 - math.floor(aging_mo / 12) * wb_withdrawal_rate), 2
        )
    if gmib_annuity_rate is not None:
        record["gmib_annuity_rate"] = gmib_annuity_rate

    return record


# ─────────────────────────────────────────────────────────────────────────────
#  Summary statistics
# ─────────────────────────────────────────────────────────────────────────────

def _pct(values: list[float]) -> tuple[float, float, float, float, float, float, float]:
    """Return (mean, std, p25, p50, p75, min, max) for a non-empty list."""
    s   = sorted(values)
    n   = len(s)
    mn  = statistics.mean(s)
    sd  = statistics.stdev(s) if n > 1 else 0.0
    lo  = s[0]
    hi  = s[-1]
    qs  = statistics.quantiles(s, n=4)   # [p25, p50, p75]
    return mn, sd, qs[0], qs[1], qs[2], lo, hi


def _fmt_row(label: str, values: list[float], fmt: str = ".1f") -> str:
    mn, sd, p25, p50, p75, lo, hi = _pct(values)
    w = 26
    f = fmt  # e.g. ".1f"
    return (
        f"  {label:<{w}}"
        f"{mn:>8{f}}  {sd:>7{f}}  "
        f"{p25:>7{f}}  {p50:>7{f}}  {p75:>7{f}}  "
        f"{lo:>9{f}}  {hi:>9{f}}"
    )


def _section(title: str) -> str:
    return f"\n{'─'*72}\n  {title}\n{'─'*72}"


def _portfolio_summary(records: list[dict], riders: list[str],
                       seed: int, n_per_rider: int) -> str:
    """Build a human-readable summary string suitable for a scientific paper."""
    lines: list[str] = []
    today = date.today().isoformat()
    N = len(records)

    lines.append("=" * 72)
    lines.append("  Synthetic VA Portfolio — Summary Statistics")
    lines.append(f"  Generated : {today}   Seed : {seed}   N = {N} policies")
    lines.append("=" * 72)

    # ── 1. Composition ────────────────────────────────────────────────────────
    lines.append(_section("1. COMPOSITION"))
    lines.append(f"  Total records : {N}  ({n_per_rider} policies × {len(riders)} rider types)")
    lines.append("")
    lines.append(f"  {'Rider':<12}  {'N':>5}  {'%':>6}")
    lines.append(f"  {'─'*12}  {'─'*5}  {'─'*6}")
    counts = Counter(r["rider"] for r in records)
    for rider in riders:
        c = counts[rider]
        lines.append(f"  {rider:<12}  {c:>5}  {100*c/N:>5.1f}%")

    # ── 2. Demographics ───────────────────────────────────────────────────────
    lines.append(_section("2. DEMOGRAPHICS"))
    hdr = f"  {'':26}{'mean':>8}  {'std':>7}  {'p25':>7}  {'p50':>7}  {'p75':>7}  {'min':>9}  {'max':>9}"
    lines.append(hdr)
    lines.append("  " + "─" * 69)

    eval_ages  = [r["eval_age"]   for r in records]
    issue_ages = [r["issue_age"]  for r in records]
    lines.append(_fmt_row("Issue age (yrs)",  issue_ages, ".1f"))
    lines.append(_fmt_row("Evaluation age (yrs)", eval_ages,  ".1f"))
    lines.append("")

    # Gender by rider
    lines.append(f"  {'Rider':<12}  {'N female':>8}  {'% female':>9}")
    lines.append(f"  {'─'*12}  {'─'*8}  {'─'*9}")
    overall_f = sum(1 for r in records if r["gender"] == "F")
    lines.append(f"  {'All':<12}  {overall_f:>8}  {100*overall_f/N:>8.1f}%")
    for rider in riders:
        sub   = [r for r in records if r["rider"] == rider]
        nf    = sum(1 for r in sub if r["gender"] == "F")
        lines.append(f"  {rider:<12}  {nf:>8}  {100*nf/len(sub):>8.1f}%")

    # ── 3. Contract Horizon ───────────────────────────────────────────────────
    lines.append(_section("3. CONTRACT HORIZON  (months)"))
    lines.append(hdr)
    lines.append("  " + "─" * 69)
    aging   = [float(r["aging_months"])    for r in records]
    mat     = [float(r["maturity_months"]) for r in records]
    term    = [a + m for a, m in zip(aging, mat)]
    lines.append(_fmt_row("Policy age (aging)",   aging,  ".1f"))
    lines.append(_fmt_row("Remaining maturity",   mat,    ".1f"))
    lines.append(_fmt_row("Total term",           term,   ".1f"))

    # ── 4. Moneyness (GB / AV) ────────────────────────────────────────────────
    lines.append(_section("4. MONEYNESS  (GB / AV)"))
    lines.append(hdr)
    lines.append("  " + "─" * 69)
    ratios_all = [r["gb"] / r["av"] for r in records]
    lines.append(_fmt_row("GB/AV  (all)",         ratios_all, ".3f"))
    for rider in riders:
        sub = [r for r in records if r["rider"] == rider]
        lines.append(_fmt_row(f"GB/AV  {rider}", [r["gb"]/r["av"] for r in sub], ".3f"))
    lines.append("")
    lines.append(_fmt_row("AV ($k)", [r["av"]/1000 for r in records], ".1f"))

    # ── 5. Fees ───────────────────────────────────────────────────────────────
    lines.append(_section("5. FEES  (annual %)"))
    lines.append(hdr)
    lines.append("  " + "─" * 69)
    base_fees  = [r["base_fee"]  * 100 for r in records]
    rider_fees = [r["rider_fee"] * 100 for r in records]
    total_fees = [b + ri for b, ri in zip(base_fees, rider_fees)]
    lines.append(_fmt_row("Base fee (M&E)",        base_fees,  ".3f"))
    lines.append(_fmt_row("Rider fee",             rider_fees, ".3f"))
    lines.append(_fmt_row("Total fee",             total_fees, ".3f"))
    lines.append("")
    lines.append(f"  {'Rider':<12}  {'mean rider fee %':>17}  {'std':>7}")
    lines.append(f"  {'─'*12}  {'─'*17}  {'─'*7}")
    for rider in riders:
        sub = [r["rider_fee"] * 100 for r in records if r["rider"] == rider]
        lines.append(f"  {rider:<12}  {statistics.mean(sub):>17.3f}  {statistics.stdev(sub):>7.3f}")

    # ── 6. Benefit-base rules ─────────────────────────────────────────────────
    lines.append(_section("6. BENEFIT-BASE RULES"))
    lines.append(f"  {'Rule':<12}  {'N':>5}  {'%':>6}")
    lines.append(f"  {'─'*12}  {'─'*5}  {'─'*6}")
    rule_counts = Counter(r["benefit_base_rule"] for r in records)
    for rule in BENEFIT_BASE_RULES:
        c = rule_counts.get(rule, 0)
        lines.append(f"  {rule:<12}  {c:>5}  {100*c/N:>5.1f}%")

    # ── 7. Fund allocation ────────────────────────────────────────────────────
    lines.append(_section("7. FUND ALLOCATION  (average weight per sub-account)"))
    lines.append(f"  {'Sub-account':<14}  {'mean w':>8}  {'std':>7}  {'min':>7}  {'max':>7}")
    lines.append(f"  {'─'*14}  {'─'*8}  {'─'*7}  {'─'*7}  {'─'*7}")
    for k in range(5):
        w = [r["alloc"][k] for r in records]
        lines.append(
            f"  {f'Fund {k+1}':<14}  "
            f"{statistics.mean(w):>8.4f}  {statistics.stdev(w):>7.4f}  "
            f"{min(w):>7.4f}  {max(w):>7.4f}"
        )

    # ── 8. Rider-specific extras ───────────────────────────────────────────────
    wb_rates  = [r["wb_withdrawal_rate"] * 100
                 for r in records if r.get("wb_withdrawal_rate") is not None]
    ib_rates  = [r["gmib_annuity_rate"]  * 100
                 for r in records if r.get("gmib_annuity_rate")  is not None]
    if wb_rates or ib_rates:
        lines.append(_section("8. RIDER-SPECIFIC RATES  (%)"))
        lines.append(hdr)
        lines.append("  " + "─" * 69)
        if wb_rates:
            lines.append(_fmt_row("GMWB/GMDB_WB withdr.", wb_rates, ".2f"))
        if ib_rates:
            lines.append(_fmt_row("GMIB/GMDB_IB annuity", ib_rates, ".2f"))

    lines.append("\n" + "=" * 72)
    return "\n".join(lines)


# ─────────────────────────────────────────────────────────────────────────────
#  CLI
# ─────────────────────────────────────────────────────────────────────────────

def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=(
            "Generate a synthetic VA portfolio (policy × rider) JSON file.\n"
            "Policy characteristics are sampled from the distributions in\n"
            "paper/differential_ml_va.tex Table 4 (Gan-Valdez methodology)."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--policies", type=int, default=20, metavar="N",
        help="Number of policies to generate per rider type.  [20]",
    )
    p.add_argument(
        "--riders", nargs="+", default=None, metavar="RIDER",
        help=(
            "Rider type(s) to include.  One or more of: "
            + " ".join(ALL_RIDERS)
            + ".  Default: all 9."
        ),
    )
    p.add_argument(
        "--seed", type=int, default=20260327, metavar="S",
        help="Random seed for reproducibility.  [20260327]",
    )
    p.add_argument(
        "--horizon", type=int, default=12, metavar="H",
        help=(
            "Evaluation horizon in months used when constraining policy-age "
            "sampling (ensures at least 60 months remain after the horizon).  [12]"
        ),
    )
    p.add_argument(
        "--shards", type=int, default=1, metavar="N",
        help=(
            "Split the portfolio into N roughly equal JSON files "
            "(named <stem>_shard_00.json … <stem>_shard_N-1.json). "
            "Use 1 (default) to write a single file under --output."
        ),
    )
    p.add_argument(
        "--output", type=str, default="portfolio.json", metavar="FILE",
        help="Output JSON file path.  [portfolio.json]",
    )
    p.add_argument(
        "--benefit-base-rule", choices=BENEFIT_BASE_RULES, default=None,
        metavar="RULE",
        help=(
            "Force every policy to use this benefit-base update rule instead of "
            "sampling randomly.  One of: " + ", ".join(BENEFIT_BASE_RULES) + ".  "
            "[random]"
        ),
    )
    return p.parse_args(argv)


def _write_shard(
    path: Path,
    records: list[dict],
    meta: dict,
    shard_idx: int | None,
    n_shards: int,
) -> None:
    """Write one JSON shard file.  shard_idx=None means a single unsplit file."""
    payload: dict = {
        "meta": {
            **meta,
            "n_records": len(records),
        },
        "policies": records,
    }
    if shard_idx is not None:
        payload["meta"]["shard"]   = shard_idx
        payload["meta"]["n_shards"] = n_shards
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2)


def main(argv: list[str] | None = None) -> None:
    args = parse_args(sys.argv[1:] if argv is None else argv)

    # Validate rider list
    riders: list[str] = args.riders if args.riders else ALL_RIDERS
    unknown = [r for r in riders if r not in ALL_RIDERS]
    if unknown:
        print(f"Error: unknown rider(s): {', '.join(unknown)}", file=sys.stderr)
        print(f"Valid riders: {', '.join(ALL_RIDERS)}", file=sys.stderr)
        sys.exit(1)

    if args.policies < 1:
        print("Error: --policies must be >= 1", file=sys.stderr)
        sys.exit(1)

    if args.shards < 1:
        print("Error: --shards must be >= 1", file=sys.stderr)
        sys.exit(1)

    rng = random.Random(args.seed)

    forced_rule: str | None = getattr(args, "benefit_base_rule", None)

    records: list[dict] = []
    policy_id = 0
    for rider in riders:
        for _ in range(args.policies):
            rec = sample_policy(rng, rider, policy_id, horizon=args.horizon,
                                forced_rule=forced_rule)
            records.append(rec)
            policy_id += 1

    base_meta = {
        "generator":    "gen_portfolio.py",
        "seed":         args.seed,
        "n_policies":   args.policies,
        "riders":       riders,
        "horizon_months": args.horizon,
        "benefit_base_rule": forced_rule if forced_rule else "random",
        "methodology":  "Gan-Valdez synthetic-portfolio sampling (paper/differential_ml_va.tex, Table 4)",
    }

    out_path = Path(args.output)
    n_shards = args.shards
    written: list[Path] = []

    if n_shards == 1:
        # Single file — original behaviour, no shard suffix
        _write_shard(out_path, records, base_meta, shard_idx=None, n_shards=1)
        written.append(out_path)
    else:
        # Split records into n_shards roughly equal chunks.
        # math.ceil ensures the last shard may be slightly smaller.
        chunk = math.ceil(len(records) / n_shards)
        stem   = out_path.stem
        suffix = out_path.suffix or ".json"
        width  = len(str(n_shards - 1))  # zero-pad width
        for i in range(n_shards):
            shard_records = records[i * chunk : (i + 1) * chunk]
            if not shard_records:
                break  # fewer records than requested shards
            shard_path = out_path.with_name(
                f"{stem}_shard_{i:0{width}}{suffix}"
            )
            _write_shard(shard_path, shard_records, base_meta,
                         shard_idx=i, n_shards=n_shards)
            written.append(shard_path)

    # Build and print summary (over the full portfolio)
    summary = _portfolio_summary(records, riders, args.seed, args.policies)
    print(summary)

    # Save summary log alongside the first output file
    log_path = written[0].with_name(out_path.stem + "_summary.txt")
    log_path.write_text(summary + "\n", encoding="utf-8")
    print(f"\nSummary log written  : {log_path}")
    if len(written) == 1:
        print(f"Portfolio written to : {written[0]}")
    else:
        print(f"Portfolio split into {len(written)} shards:")
        for p in written:
            print(f"  {p}")


if __name__ == "__main__":
    main()
