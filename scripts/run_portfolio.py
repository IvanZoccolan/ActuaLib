"""
run_portfolio.py — batch runner for SimulateRider over a portfolio JSON file.

Reads a portfolio.json (produced by gen_portfolio.py), invokes the SimulateRider
binary for every policy, and writes per-policy HDF5 files.

Designed to run for days: supports resume (skips completed policies), per-attempt
retries with back-off, graceful SIGINT/SIGTERM shutdown, and a structured JSONL
log of every event.

USAGE
-----
    python scripts/run_portfolio.py [OPTIONS]

REQUIRED
  --portfolio FILE     Input portfolio JSON file.
  --binary    PATH     Path to the SimulateRider binary.
  --output-dir DIR     Directory where HDF5 files are written.
  --suffix    STR      Common filename stem; output files are named
                       {suffix}_{policy_id}.h5.

SIMULATION OPTIONS (passed to SimulateRider)
  --seed      INT      Base RNG seed for all policies.               [12345]
  --outer     INT      Outer (real-world) paths per policy.          [48]
  --inner     INT      Inner (pricing) paths per policy.             [256]
  --threads   INT      Worker threads inside SimulateRider.          [7]
  --rng       TYPE     Inner RNG: sobol or mrg32k3a.                 [sobol]
  --no-hw              Disable HW1F; use static outer curve.
  --hw-a      FLOAT    HW1F mean-reversion speed a.                  [0.10]
  --hw-sigma  FLOAT    HW1F short-rate volatility sigma.             [0.01]

RUNNER OPTIONS
  --log       FILE     JSONL log file path.  Defaults to
                       {output-dir}/{suffix}_run.log
  --retries   INT      Max retry attempts per policy on failure.      [3]
  --retry-wait SEC     Initial back-off between retries (doubles).   [10]
  --timeout   SEC      Per-policy wall-clock timeout (0 = none).     [0]
  --policies  IDs      Comma-separated policy_ids to run (subset).
                       Omit to run all.
  --dry-run            Print commands without executing.

NOTES
  • Already-completed policies (entry in log + output file present) are
    skipped automatically on restart — safe to rerun after interruption.
  • Send SIGINT or SIGTERM once to request a clean stop after the current
    policy finishes.  Send twice to force-exit immediately.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

# ──────────────────────────────────────────────────────────────────────────────
#  Graceful shutdown flag
# ──────────────────────────────────────────────────────────────────────────────

_shutdown_requested = False
_force_exit_count   = 0


def _handle_signal(sig, _frame):
    global _shutdown_requested, _force_exit_count
    _force_exit_count += 1
    if _force_exit_count == 1:
        print(
            f"\n[runner] Signal {sig} received — finishing current policy then stopping.",
            flush=True,
        )
        _shutdown_requested = True
    else:
        print("\n[runner] Force-exit.", flush=True)
        sys.exit(1)


signal.signal(signal.SIGINT,  _handle_signal)
signal.signal(signal.SIGTERM, _handle_signal)


# ──────────────────────────────────────────────────────────────────────────────
#  Logging helpers
# ──────────────────────────────────────────────────────────────────────────────

def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class RunLog:
    """Append-only JSONL log.  Provides fast lookup of completed policy_ids."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self._completed: set[int] = set()
        self._load()

    def _load(self) -> None:
        """Replay existing log to find already-completed policies."""
        if not self.path.exists():
            return
        with self.path.open() as fh:
            for lineno, raw in enumerate(fh, 1):
                raw = raw.strip()
                if not raw:
                    continue
                try:
                    rec = json.loads(raw)
                except json.JSONDecodeError:
                    print(f"[log] WARNING: malformed log line {lineno}, skipping.")
                    continue
                if rec.get("status") == "completed":
                    self._completed.add(int(rec["policy_id"]))
                # A later "started" without a "completed" means interrupted — retry.

    def is_completed(self, policy_id: int) -> bool:
        return policy_id in self._completed

    def write(self, record: dict) -> None:
        with self.path.open("a") as fh:
            fh.write(json.dumps(record) + "\n")
            fh.flush()
            os.fsync(fh.fileno())  # guarantee durability before next operation

    # Convenience wrappers
    def started(self, policy_id: int, seed: int, attempt: int, cmd: list[str]) -> None:
        self.write({
            "ts": _now_iso(),
            "status": "started",
            "policy_id": policy_id,
            "seed": seed,
            "attempt": attempt,
            "cmd": " ".join(cmd),
        })

    def completed(self, policy_id: int, seed: int, elapsed_s: float, output: str) -> None:
        self._completed.add(policy_id)
        self.write({
            "ts": _now_iso(),
            "status": "completed",
            "policy_id": policy_id,
            "seed": seed,
            "elapsed_s": round(elapsed_s, 3),
            "output": output,
        })

    def failed(self, policy_id: int, attempt: int, returncode: int,
               elapsed_s: float, stderr_tail: str) -> None:
        self.write({
            "ts": _now_iso(),
            "status": "failed",
            "policy_id": policy_id,
            "attempt": attempt,
            "returncode": returncode,
            "elapsed_s": round(elapsed_s, 3),
            "stderr_tail": stderr_tail,
        })

    def skipped(self, policy_id: int, reason: str) -> None:
        self.write({
            "ts": _now_iso(),
            "status": "skipped",
            "policy_id": policy_id,
            "reason": reason,
        })


# ──────────────────────────────────────────────────────────────────────────────
#  Portfolio-field → CLI mapping
# ──────────────────────────────────────────────────────────────────────────────

_RULE_MAP = {
    "ROP":      "rop",
    "ratchet":  "step-up",
    "roll-up":  "roll-up",
}


def build_command(
    binary: str,
    policy: dict,
    output_path: str,
    args: argparse.Namespace,
) -> list[str]:
    """Build the SimulateRider command-line for one policy record.

    Semantics
    ---------
    --age     = eval_age  (current age of insured, as observed in the portfolio DB today)
    --maturity = maturity_months (remaining months from today, pre-computed in gen_portfolio.py
               as term_months - aging_months; passed directly to SimulateRider)
    --horizon  = evaluation horizon from today (months), supplied via args.horizon
    --months-since-issue = aging_months (months elapsed since inception, used only for
                           correct placement of annual fee/benefit-base anniversaries
                           during the outer real-world aging loop)

    With these semantics the outer loop ages the policy from TODAY forward by
    args.horizon months under the real-world measure, then the inner risk-neutral
    engine prices the guarantee for an insured of age (eval_age + horizon/12) with
    (maturity_months - horizon) months left to maturity.
    """
    alloc_str = ",".join(f"{w:.6f}" for w in policy["alloc"])
    rule      = _RULE_MAP.get(policy["benefit_base_rule"], "step-up")

    cmd = [
        binary,
        "--rider",              str(policy["rider"]),
        "--age",                str(policy["eval_age"]),
        "--gender",             str(policy["gender"]),
        "--maturity",           str(policy["maturity_months"]),
        "--horizon",            str(args.horizon),
        "--months-since-issue", str(policy["aging_months"]),
        "--av",          str(policy["av"]),
        "--alloc",       alloc_str,
        "--gb",          str(policy["gb"]),
        "--base-fee",    str(policy["base_fee"]),
        "--rider-fee",   str(policy["rider_fee"]),
        "--update-rule", rule,
    ]

    # roll-up rate (only meaningful when rule is roll-up, but harmless otherwise)
    roll_rate = policy.get("roll_up_rate")
    if roll_rate is not None:
        cmd += ["--roll-up-rate", str(roll_rate)]

    # rider-specific rates
    wb_rate = policy.get("wb_withdrawal_rate")
    if wb_rate is not None:
        cmd += ["--wb-withdrawal-rate", str(wb_rate)]

    gmib_rate = policy.get("gmib_annuity_rate")
    if gmib_rate is not None:
        cmd += ["--gmib-annuity-rate", str(gmib_rate)]

    # per-fund MER rates
    fund_fees = policy.get("fund_fees")
    if fund_fees is not None:
        cmd += ["--fund-fees", ",".join(f"{f:.6f}" for f in fund_fees)]

    # GMWB starting balance (only present for GMWB/GMDB_WB riders)
    gmwb_bal = policy.get("gmwb_balance")
    if gmwb_bal is not None:
        cmd += ["--gmwb-balance", str(gmwb_bal)]

    # simulation options from CLI args
    cmd += [
        "--outer",   str(args.outer),
        "--inner",   str(args.inner),
        "--seed",    str(args.seed),
        "--threads", str(args.threads),
        "--rng",     args.rng,
    ]

    if args.no_hw:
        cmd.append("--no-hw")
    else:
        cmd += ["--hw-a", str(args.hw_a), "--hw-sigma", str(args.hw_sigma)]

    cmd += ["--output", output_path]
    return cmd


# ──────────────────────────────────────────────────────────────────────────────
#  Single-policy runner with retries
# ──────────────────────────────────────────────────────────────────────────────

# Error message prefixes that indicate a deterministic application-level
# failure.  Retrying an identical command would always produce the same result,
# so we abort the retry loop immediately when any of these are detected.
_DETERMINISTIC_ERROR_PREFIXES = (
    "simulation error:",
    "error:",
    "unknown flag",
    "--rider is required",
)


def _is_deterministic_error(stderr_tail: str) -> bool:
    """Return True if stderr indicates a deterministic (non-transient) failure."""
    lower = stderr_tail.lower()
    return any(lower.startswith(p) or p in lower
               for p in _DETERMINISTIC_ERROR_PREFIXES)


def run_policy(
    policy: dict,
    output_path: Path,
    log: RunLog,
    args: argparse.Namespace,
) -> bool:
    """
    Run one policy through SimulateRider.
    Returns True on success, False if all retry attempts are exhausted.
    """
    pid     = policy["policy_id"]
    max_att = args.retries
    wait    = args.retry_wait
    timeout = args.timeout if args.timeout > 0 else None

    for attempt in range(1, max_att + 1):
        cmd = build_command(str(args.binary), policy, str(output_path), args)

        if args.dry_run:
            print(f"[dry-run] policy {pid}: {' '.join(cmd)}")
            log.completed(pid, args.seed, 0.0, str(output_path))
            return True

        log.started(pid, args.seed, attempt, cmd)
        print(
            f"[runner] policy {pid:>5}  attempt {attempt}/{max_att}  "
            f"rider={policy['rider']}  output={output_path.name}",
            flush=True,
        )

        t0 = time.monotonic()
        try:
            proc = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
            elapsed = time.monotonic() - t0
        except subprocess.TimeoutExpired:
            elapsed = time.monotonic() - t0
            log.failed(pid, attempt, -1, elapsed, "TIMEOUT")
            print(
                f"[runner] policy {pid} timed out after {elapsed:.1f}s "
                f"(attempt {attempt}/{max_att})",
                flush=True,
            )
        except Exception as exc:
            elapsed = time.monotonic() - t0
            log.failed(pid, attempt, -2, elapsed, str(exc))
            print(f"[runner] policy {pid} exception: {exc}", flush=True)
        else:
            if proc.returncode == 0 and output_path.exists():
                log.completed(pid, args.seed, elapsed, str(output_path))
                print(
                    f"[runner] policy {pid} completed in {elapsed:.1f}s",
                    flush=True,
                )
                return True
            else:
                stderr_tail = (proc.stderr or "")[-400:].strip()
                log.failed(pid, attempt, proc.returncode, elapsed, stderr_tail)
                print(
                    f"[runner] policy {pid} FAILED (rc={proc.returncode}, "
                    f"{elapsed:.1f}s, attempt {attempt}/{max_att})",
                    flush=True,
                )
                if stderr_tail:
                    print(f"  stderr: {stderr_tail}", flush=True)

                # Deterministic application error — retrying the same command
                # will always produce the same failure.  Abort immediately.
                if _is_deterministic_error(stderr_tail):
                    print(
                        f"[runner] policy {pid} deterministic error — skipping retries.",
                        flush=True,
                    )
                    break

        if attempt < max_att:
            print(f"[runner] waiting {wait}s before retry …", flush=True)
            time.sleep(wait)
            wait = min(wait * 2, 120)   # cap back-off at 2 min

    print(f"[runner] policy {pid} ABANDONED after {attempt} attempt(s).", flush=True)
    return False


# ──────────────────────────────────────────────────────────────────────────────
#  CLI
# ──────────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Batch SimulateRider runner over a portfolio JSON.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    p.add_argument("--portfolio",  required=True,  type=Path,
                   help="Input portfolio JSON file.")
    p.add_argument("--binary",     required=True,  type=Path,
                   help="Path to the SimulateRider binary.")
    p.add_argument("--output-dir", required=True,  type=Path,
                   help="Directory for HDF5 output files.")
    p.add_argument("--suffix",     required=True,
                   help="Filename stem; files are named {suffix}_{policy_id}.h5.")

    # Simulation parameters
    p.add_argument("--seed",       type=int,   default=12345)
    p.add_argument("--outer",      type=int,   default=48)
    p.add_argument("--inner",      type=int,   default=256)
    p.add_argument("--threads",    type=int,   default=7)
    p.add_argument("--rng",        default="sobol",
                   choices=["sobol", "mrg32k3a"])
    p.add_argument("--horizon",    type=int,   default=None,
                   help="Evaluation horizon in months from today (12 = 1yr, "
                        "60 = 5yr, 120 = 10yr, 240 = 20yr).  "
                        "Defaults to the horizon_months stored in the portfolio meta. "
                        "The outer real-world loop ages the portfolio forward by this many "
                        "months before risk-neutral inner pricing.")
    p.add_argument("--no-hw",      action="store_true",
                   help="Disable HW1F (static outer curve).")
    p.add_argument("--hw-a",       type=float, default=0.10, dest="hw_a")
    p.add_argument("--hw-sigma",   type=float, default=0.01, dest="hw_sigma")

    # Runner options
    p.add_argument("--log",         type=Path, default=None,
                   help="JSONL log path (default: {output-dir}/{suffix}_run.log).")
    p.add_argument("--retries",     type=int,  default=3,
                   help="Max retry attempts per policy on failure.")
    p.add_argument("--retry-wait",  type=float, default=10.0, dest="retry_wait",
                   help="Initial back-off between retries in seconds (doubles).")
    p.add_argument("--timeout",     type=float, default=0.0,
                   help="Per-policy wall-clock timeout in seconds (0 = none).")
    p.add_argument("--policies",    type=str,  default=None,
                   help="Comma-separated policy_ids to run (subset of portfolio).")
    p.add_argument("--dry-run",     action="store_true", dest="dry_run",
                   help="Print commands without executing.")

    args = p.parse_args()

    # Resolve/validate paths
    args.portfolio = args.portfolio.expanduser().resolve()
    args.binary    = args.binary.expanduser().resolve()
    args.output_dir = args.output_dir.expanduser().resolve()

    if not args.portfolio.exists():
        p.error(f"Portfolio file not found: {args.portfolio}")
    if not args.dry_run and not args.binary.exists():
        p.error(f"Binary not found: {args.binary}")

    if args.log is None:
        args.log = args.output_dir / f"{args.suffix}_run.log"
    else:
        args.log = args.log.expanduser().resolve()

    return args


# ──────────────────────────────────────────────────────────────────────────────
#  main
# ──────────────────────────────────────────────────────────────────────────────

def main() -> int:
    args = parse_args()

    # Load portfolio
    with args.portfolio.open() as fh:
        portfolio = json.load(fh)
    policies: list[dict] = portfolio["policies"]

    # Resolve horizon: CLI overrides portfolio meta; meta overrides hard default.
    meta_horizon: int | None = portfolio.get("meta", {}).get("horizon_months")
    if args.horizon is None:
        if meta_horizon is not None:
            args.horizon = meta_horizon
            print(f"[runner] horizon set from portfolio meta: {args.horizon} months")
        else:
            args.horizon = 12
            print(f"[runner] horizon defaulting to 12 months (not found in portfolio meta)")
    elif meta_horizon is not None and args.horizon != meta_horizon:
        print(
            f"[runner] WARNING: --horizon {args.horizon} overrides portfolio meta "
            f"horizon_months={meta_horizon}.  Ensure this is intentional."
        )

    # Filter to requested subset if --policies was given
    if args.policies is not None:
        requested = {int(x) for x in args.policies.split(",")}
        policies  = [p for p in policies if p["policy_id"] in requested]
        if not policies:
            print("[runner] No matching policies after --policies filter.")
            return 0

    total = len(policies)

    # Prepare output directory and log
    args.output_dir.mkdir(parents=True, exist_ok=True)
    log = RunLog(args.log)

    # Print run header
    print("=" * 72)
    print(f"  run_portfolio.py")
    print(f"  portfolio : {args.portfolio}")
    print(f"  binary    : {args.binary}")
    print(f"  output-dir: {args.output_dir}")
    print(f"  suffix    : {args.suffix}")
    print(f"  seed      : {args.seed}")
    print(f"  outer/inner paths: {args.outer} / {args.inner}")
    print(f"  horizon   : {args.horizon} months from today ({args.horizon/12:.1f} yrs)")
    print(f"  HW1F      : {'disabled' if args.no_hw else f'a={args.hw_a}, sigma={args.hw_sigma}'}")
    print(f"  policies  : {total} total")
    print(f"  log       : {args.log}")
    print("=" * 72)
    print(flush=True)

    # Write a session-start record
    log.write({
        "ts":        _now_iso(),
        "status":    "session_start",
        "portfolio": str(args.portfolio),
        "binary":    str(args.binary),
        "seed":      args.seed,
        "outer":     args.outer,
        "inner":     args.inner,
        "n_policies": total,
        "suffix":    args.suffix,
        "hw_a":      args.hw_a,
        "hw_sigma":  args.hw_sigma,
        "no_hw":     args.no_hw,
        "horizon":   args.horizon,
    })

    n_completed = 0
    n_skipped   = 0
    n_failed    = 0

    wall_t0 = time.monotonic()

    for policy in policies:
        if _shutdown_requested:
            print("[runner] Shutdown requested — stopping before next policy.")
            break

        pid         = policy["policy_id"]
        output_path = args.output_dir / f"{args.suffix}_{pid}.h5"

        # Validate horizon against this policy's remaining maturity
        # (maturity_months is already remaining from today)
        if args.horizon >= policy["maturity_months"]:
            print(f"[policy {pid}] SKIP — horizon {args.horizon}m >= remaining maturity {policy['maturity_months']}m")
            n_skipped += 1
            continue

        # Skip if already done (log + file both present)
        if log.is_completed(pid) and output_path.exists():
            print(f"[runner] policy {pid:>5} SKIP (already done)", flush=True)
            log.skipped(pid, "already completed")
            n_skipped += 1
            continue

        ok = run_policy(policy, output_path, log, args)
        if ok:
            n_completed += 1
        else:
            n_failed += 1

    wall_elapsed = time.monotonic() - wall_t0

    # Session-end summary
    summary = {
        "ts":          _now_iso(),
        "status":      "session_end",
        "n_completed": n_completed,
        "n_skipped":   n_skipped,
        "n_failed":    n_failed,
        "wall_s":      round(wall_elapsed, 1),
        "shutdown_requested": _shutdown_requested,
    }
    log.write(summary)

    print()
    print("=" * 72)
    print(f"  Session complete in {wall_elapsed:.1f}s")
    print(f"  Completed : {n_completed}")
    print(f"  Skipped   : {n_skipped}")
    print(f"  Failed    : {n_failed}")
    if _shutdown_requested:
        print("  (run interrupted by signal — rerun to continue)")
    print("=" * 72)

    return 1 if n_failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
