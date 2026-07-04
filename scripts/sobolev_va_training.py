from __future__ import annotations

import argparse
import copy
import json
import sys
import time
from pathlib import Path
from typing import Dict, List, Tuple

import h5py
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

RIDER_TYPES: List[str] = [
    "GMDB", "GMMB", "GMAB", "GMWB", "GMIB",
    "GMDB_AB", "GMDB_MB", "GMDB_WB", "GMDB_IB",
]
NUM_RIDERS = len(RIDER_TYPES)
N_DPRICE_DIRDF = 10

IR_KNOT_TENORS: List[float] = [0.25, 0.5, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 20.0, 30.0]

ORIG_SPOT_START = 21
ORIG_SPOT_END = 26
ORIG_IR_START = 26
ORIG_IR_END = 36


def _decode_attr(v) -> str:
    if isinstance(v, bytes):
        return v.decode("utf-8")
    return str(v)


def _gender_to_float(gender_attr: str) -> float:
    g = gender_attr.strip().upper()
    return 1.0 if g == "M" else 0.0


def get_device() -> torch.device:
    if torch.cuda.is_available():
        return torch.device("cuda")
    if hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
        return torch.device("mps")
    return torch.device("cpu")


class FeedForwardSobolevNet(nn.Module):
    def __init__(
        self,
        n_input: int,
        hidden_size: int,
        hidden_layers: int,
        use_skip: bool,
    ) -> None:
        super().__init__()
        self.use_skip = use_skip
        self.input_proj = nn.Linear(n_input, hidden_size)
        self.hidden = nn.ModuleList([nn.Linear(hidden_size, hidden_size) for _ in range(hidden_layers)])
        self.output_proj = nn.Linear(hidden_size, 1)
        self.act = nn.Softplus()

        for m in self.modules():
            if isinstance(m, nn.Linear):
                nn.init.xavier_uniform_(m.weight)
                nn.init.zeros_(m.bias)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        h = self.act(self.input_proj(x))
        for layer in self.hidden:
            z = self.act(layer(h))
            h = h + z if self.use_skip else z
        return self.output_proj(h)


def _lambda_weights(targets_norm: torch.Tensor, max_lambda: float = 10.0) -> Tuple[torch.Tensor, torch.Tensor]:
    """Compute per-feature lambda weights = 1/rms(target), capped two ways:

    1. 90th-percentile relative cap (original logic) – prevents outlier knots
       from dominating when *some* targets are tiny.
    2. Absolute cap ``max_lambda`` – prevents explosion when *all* rms values
       are << 1.  The initial network gradient w.r.t. normalised inputs is O(1),
       so beta * lam^2 * 1^2 must stay O(1); with beta >= 0.01 this requires
       lam <= ~10.  Without this cap, small-rho products (e.g. GMDB_WB under
       HW sigma=0.01) produce lam ~ 20000 and training diverges.
    """
    rms = torch.sqrt((targets_norm ** 2).mean(dim=0) + 1e-24)
    active = rms > 1e-5
    lam = torch.zeros_like(rms)
    if active.any():
        lam[active] = 1.0 / rms[active]
        relative_cap = float(torch.quantile(lam[active], 0.9))
        cap = min(relative_cap, max_lambda)
        lam = torch.where(active, torch.clamp(lam, max=cap), lam)
    return lam, active


def _group_mean(values: np.ndarray, group_ids: np.ndarray) -> np.ndarray:
    n_groups = int(group_ids.max()) + 1 if group_ids.size > 0 else 0
    out = np.zeros((n_groups,) + values.shape[1:], dtype=np.float64)
    cnt = np.zeros(n_groups, dtype=np.int64)
    for i in range(values.shape[0]):
        g = int(group_ids[i])
        out[g] += values[i]
        cnt[g] += 1
    cnt = np.maximum(cnt, 1)
    out /= cnt.reshape((-1,) + (1,) * (values.ndim - 1))
    return out


def _extract_contract_scalars(params_attrs: Dict) -> List[float]:
    return [
        float(params_attrs.get("age", 0.0)),
        float(params_attrs.get("maturity_months", 0.0)),
        float(params_attrs.get("horizon_months", 0.0)),
        float(params_attrs.get("months_since_issue", 0.0)),
        float(params_attrs.get("av", 0.0)),
        float(params_attrs.get("gb", 0.0)),
        float(params_attrs.get("base_fee", 0.0)),
        float(params_attrs.get("rider_fee", 0.0)),
        float(params_attrs.get("wb_withdrawal_rate", 0.0)),
        float(params_attrs.get("gmib_annuity_rate", 0.0)),
    ]


def _load_split_from_h5_dir(
    split_dir: Path,
    rider: str,
    update_rule_filter: "str | None" = None,
) -> Dict[str, "torch.Tensor | np.ndarray"]:
    files = sorted(split_dir.glob("*.h5"))
    if not files:
        raise RuntimeError(f"No .h5 files found in {split_dir}")

    feat_rows: List[List[float]] = []
    y_rows: List[float] = []
    gs_rows: List[List[float]] = []
    gi_rows: List[List[float]] = []
    dd_rows: List[List[float]] = []
    spot_rows: List[List[float]] = []
    group_ids: List[int] = []

    next_group_id = 0
    kept_files = 0

    for h5_path in files:
        with h5py.File(h5_path, "r") as f:
            if "/results" not in f or "/params" not in f:
                continue

            params = f["/params"].attrs
            rider_attr = _decode_attr(params.get("rider", ""))
            if rider_attr.strip().upper() != rider:
                continue

            if update_rule_filter is not None:
                rule_attr = _decode_attr(params.get("update_rule", "")).strip().lower()
                if rule_attr != update_rule_filter.lower():
                    continue

            required = {
                "fmv_per_path",
                "delta",
                "rho",
                "outer_spots",
                "outer_disc_factors",
                "outer_fund_values",
            }
            if not required.issubset(set(f["/results"].keys())):
                continue

            fmv = np.asarray(f["/results/fmv_per_path"], dtype=np.float64)
            path_dd = np.asarray(f["/results/delta"], dtype=np.float64)
            path_rh = np.asarray(f["/results/rho"], dtype=np.float64)
            outer_spots = np.asarray(f["/results/outer_spots"], dtype=np.float64)
            outer_dfs = np.asarray(f["/results/outer_disc_factors"], dtype=np.float64)
            outer_funds = np.asarray(f["/results/outer_fund_values"], dtype=np.float64)

            if fmv.ndim != 1 or path_dd.ndim != 2 or path_rh.ndim != 2:
                raise RuntimeError(f"Unexpected dataset ranks in {h5_path}")

            n_outer = fmv.shape[0]
            n_inner = int(params.get("n_inner", 0))

            if path_dd.shape[0] != n_outer or path_rh.shape[0] != n_outer:
                raise RuntimeError(f"Price/greek shape mismatch in {h5_path}")

            if outer_spots.shape[0] != n_outer or outer_dfs.shape[0] != n_outer or outer_funds.shape[0] != n_outer:
                raise RuntimeError(f"Outer-state shape mismatch in {h5_path}")

            gender_f = _gender_to_float(_decode_attr(params.get("gender", "F")))
            rider_oh = [0.0] * NUM_RIDERS
            rider_oh[RIDER_TYPES.index(rider)] = 1.0
            c_scalars = _extract_contract_scalars(params)

            kept_files += 1
            for p in range(n_outer):
                sample_scalars = [
                    1.0 / float(max(n_inner, 1)),
                ]
                feat_base = (
                    [gender_f]
                    + rider_oh
                    + c_scalars
                    + sample_scalars
                    + outer_spots[p].tolist()
                    + outer_dfs[p].tolist()
                    + outer_funds[p].tolist()
                )
                if len(feat_base) != 41:
                    raise RuntimeError(f"Expected 41 features, got {len(feat_base)} in {h5_path}")

                y_i = float(fmv[p])
                dd_i = path_dd[p, :].astype(np.float64)
                rh_i = path_rh[p, :].astype(np.float64)

                spot_safe = np.maximum(np.abs(outer_spots[p]), 1e-12)
                gs_i = dd_i / spot_safe

                feat_rows.append(feat_base)
                y_rows.append(y_i)
                gs_rows.append(gs_i.tolist())
                gi_rows.append(rh_i.tolist())
                dd_rows.append(dd_i.tolist())
                spot_rows.append(outer_spots[p].astype(np.float64).tolist())
                group_ids.append(next_group_id)

                next_group_id += 1

    if kept_files == 0:
        raise RuntimeError(f"No files for rider={rider} found in {split_dir}")

    x = torch.tensor(np.asarray(feat_rows, dtype=np.float32))
    y = torch.tensor(np.asarray(y_rows, dtype=np.float32)).unsqueeze(1)
    gs = torch.tensor(np.asarray(gs_rows, dtype=np.float32))
    gi = torch.tensor(np.asarray(gi_rows, dtype=np.float32))
    dd = torch.tensor(np.asarray(dd_rows, dtype=np.float32))
    spot_raw = torch.tensor(np.asarray(spot_rows, dtype=np.float32))
    gids = np.asarray(group_ids, dtype=np.int64)

    return {
        "x": x,
        "y": y,
        "gs": gs,
        "gi": gi,
        "dd": dd,
        "spot_raw": spot_raw,
        "group_ids": gids,
    }


def preprocess(
    train_raw: Dict[str, torch.Tensor | np.ndarray],
    test_raw: Dict[str, torch.Tensor | np.ndarray],
    *,
    ir_snr_threshold: float,
    ir_abs_snr: float = 0.0,
    max_lambda: float = 10.0,
    min_df_tenor: float = 0.0,
    max_df_tenor: float = float("inf"),
) -> Dict:
    x_train_raw: torch.Tensor = train_raw["x"]
    x_test_raw: torch.Tensor = test_raw["x"]

    y_train_raw: torch.Tensor = train_raw["y"]
    y_test_raw: torch.Tensor = test_raw["y"]

    gs_train_raw: torch.Tensor = train_raw["gs"]
    gs_test_raw: torch.Tensor = test_raw["gs"]

    gi_train_raw: torch.Tensor = train_raw["gi"]
    gi_test_raw: torch.Tensor = test_raw["gi"]

    dd_train_raw: torch.Tensor = train_raw["dd"]
    dd_test_raw: torch.Tensor = test_raw["dd"]

    spot_raw_train: torch.Tensor = train_raw["spot_raw"]
    spot_raw_test: torch.Tensor = test_raw["spot_raw"]

    train_group_ids: np.ndarray = train_raw["group_ids"]
    test_group_ids: np.ndarray = test_raw["group_ids"]

    feat_std = x_train_raw.std(dim=0)
    keep_mask = feat_std > 1e-10
    # Drop disc-factor features outside [min_df_tenor, max_df_tenor].  For typical
    # VA products (e.g. GMDB_WB, ~10y remaining life) the 0.25y/0.5y tenors are
    # near-constants (CV < 0.3%) and redundant with the 1y knot, while the 20y/30y
    # AIRG long-rate factors are decorrelated from the pricing-relevant 1y-10y
    # segment and carry essentially zero FMV information (|corr| < 0.006).
    for k, tenor in enumerate(IR_KNOT_TENORS):
        if tenor < min_df_tenor or tenor > max_df_tenor:
            keep_mask[ORIG_IR_START + k] = False
    keep_idx = torch.where(keep_mask)[0]

    spot_positions: List[int] = []
    for orig in range(ORIG_SPOT_START, ORIG_SPOT_END):
        m = (keep_idx == orig).nonzero(as_tuple=True)[0]
        if len(m) != 1:
            raise RuntimeError(f"Spot feature {orig} unexpectedly dropped as constant")
        spot_positions.append(int(m.item()))

    ir_positions: List[int] = []
    ir_knot_kept: List[int] = []
    for k, orig in enumerate(range(ORIG_IR_START, ORIG_IR_END)):
        m = (keep_idx == orig).nonzero(as_tuple=True)[0]
        if len(m) == 1:
            ir_positions.append(int(m.item()))
            ir_knot_kept.append(k)

    x_tr = x_train_raw[:, keep_mask]
    x_te = x_test_raw[:, keep_mask]

    x_mean = x_tr.mean(dim=0)
    x_std = x_tr.std(dim=0).clamp(min=1e-12)
    x_tr_n = (x_tr - x_mean) / x_std
    x_te_n = (x_te - x_mean) / x_std

    y_mean = y_train_raw.mean(dim=0)
    y_std = y_train_raw.std(dim=0).clamp(min=1e-12)
    y_tr_n = (y_train_raw - y_mean) / y_std
    y_te_n = (y_test_raw - y_mean) / y_std

    sp_feat_std = x_std[spot_positions]
    gs_tr_n = gs_train_raw * (sp_feat_std / y_std)
    gs_te_n = gs_test_raw * (sp_feat_std / y_std)
    lam_spot, _ = _lambda_weights(gs_tr_n, max_lambda=max_lambda)

    ir_feat_std_kept = x_std[ir_positions] if ir_positions else torch.empty(0)
    if ir_positions:
        gi_tr_n_kept = gi_train_raw[:, ir_knot_kept] * (ir_feat_std_kept / y_std)
        gi_te_n_kept = gi_test_raw[:, ir_knot_kept] * (ir_feat_std_kept / y_std)
        mean_abs = gi_tr_n_kept.abs().mean(dim=0)
        rms_vals = (gi_tr_n_kept ** 2).mean(dim=0).sqrt()
        max_mean_abs = float(mean_abs.max()) if len(mean_abs) > 0 else 0.0
        # Relative filter: drop knots weaker than ir_snr_threshold * strongest IR knot
        rel_mask = mean_abs > ir_snr_threshold * max(max_mean_abs, 1e-12)
        # Absolute filter: drop knots whose normalised rho RMS < ir_abs_snr.
        # In normalised space 1.0 = 1 price-std per 1-sigma disc-factor shock.
        # ir_abs_snr=0.01 means the knot must explain >=1% of price std to be
        # worth supervising; near-zero rho (e.g. HW sigma=0.01) is dropped.
        abs_mask = rms_vals > ir_abs_snr
        snr_mask = rel_mask & abs_mask
    else:
        gi_tr_n_kept = torch.empty(x_tr.shape[0], 0)
        gi_te_n_kept = torch.empty(x_te.shape[0], 0)
        snr_mask = torch.zeros(0, dtype=torch.bool)

    active_ir_global = [ir_knot_kept[i] for i, a in enumerate(snr_mask) if a]
    active_ir_feat_positions = [ir_positions[i] for i, a in enumerate(snr_mask) if a]

    gi_tr_active = gi_tr_n_kept[:, snr_mask]
    gi_te_active = gi_te_n_kept[:, snr_mask]
    lam_ir, _ = _lambda_weights(gi_tr_active, max_lambda=max_lambda)

    n_dropped = int((~keep_mask).sum().item())

    return {
        "x_train": x_tr_n,
        "x_test": x_te_n,
        "y_train": y_tr_n,
        "y_test": y_te_n,
        "gs_train": gs_tr_n,
        "gs_test": gs_te_n,
        "gi_train": gi_tr_active,
        "gi_test": gi_te_active,
        "lam_spot": lam_spot,
        "lam_ir": lam_ir,
        "spot_positions": spot_positions,
        "active_ir_feat_positions": active_ir_feat_positions,
        "active_ir_global": active_ir_global,
        "all_ir_feat_positions": ir_positions,
        "all_ir_global": ir_knot_kept,
        "x_mean": x_mean,
        "x_std": x_std,
        "y_mean": y_mean,
        "y_std": y_std,
        "sp_feat_std": sp_feat_std,
        "ir_feat_std": ir_feat_std_kept,
        "y_train_raw": y_train_raw,
        "y_test_raw": y_test_raw,
        "gs_train_raw": gs_train_raw,
        "gs_test_raw": gs_test_raw,
        "gi_train_raw": gi_train_raw,
        "gi_test_raw": gi_test_raw,
        "dd_train_raw": dd_train_raw,
        "dd_test_raw": dd_test_raw,
        "spot_raw_train": spot_raw_train,
        "spot_raw_test": spot_raw_test,
        "train_group_ids": train_group_ids,
        "test_group_ids": test_group_ids,
        "n_features": int(x_tr_n.shape[1]),
        "n_dropped": n_dropped,
        "n_active_ir": len(active_ir_global),
    }


def predict_with_grads(model: nn.Module, xb: torch.Tensor) -> Tuple[torch.Tensor, torch.Tensor]:
    y = model(xb)
    (grads,) = torch.autograd.grad(
        outputs=y,
        inputs=xb,
        grad_outputs=torch.ones_like(y),
        create_graph=True,
        retain_graph=True,
    )
    return y, grads


def train_model(
    model: nn.Module,
    pack: Dict,
    *,
    mode: str,
    beta_spot: float,
    beta_ir: float,
    price_weight: float = 1.0,
    lr: float,
    epochs: int,
    batch_size: int,
    device: torch.device,
    weight_decay: float = 0.0,
    patience: int = 0,
    verbose: bool = True,
) -> Dict[str, List[float]]:
    x_t = pack["x_train"].to(device)
    y_t = pack["y_train"].to(device)
    gs_t = pack["gs_train"].to(device)
    gi_t = pack["gi_train"].to(device)
    lam_s = pack["lam_spot"].to(device)
    lam_i = pack["lam_ir"].to(device)

    sp_pos = pack["spot_positions"]
    ir_pos = pack["active_ir_feat_positions"]

    use_spot = mode == "sobolev"
    use_ir = mode == "sobolev" and len(ir_pos) > 0

    optimizer = optim.Adam(model.parameters(), lr=lr, weight_decay=weight_decay)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs, eta_min=lr * 0.01)

    n = x_t.shape[0]
    history: Dict[str, List[float]] = {"total": [], "value": [], "spot_grad": [], "ir_grad": []}

    # ── Early-stopping setup ─────────────────────────────────────────────────
    # Validation uses the first 8192 rows of the test split (already held out
    # from training; no risk of leakage).  The Sobolev loss is computed with
    # create_graph=False — we only need the scalar to track improvement.
    if patience > 0:
        _val_n = min(8192, pack["x_test"].shape[0])
        x_v = pack["x_test"].to(device)[:_val_n]
        y_v = pack["y_test"].to(device)[:_val_n]
        gs_v = pack["gs_test"].to(device)[:_val_n]
        gi_v = pack["gi_test"].to(device)[:_val_n]
        best_val_loss = float("inf")
        best_state: Dict = {}
        no_improve = 0
        best_epoch = 0
        history["val_total"] = []

    for epoch in range(1, epochs + 1):
        model.train()
        perm = torch.randperm(n, device=device)
        sums = {"total": 0.0, "value": 0.0, "spot_grad": 0.0, "ir_grad": 0.0}
        nb = 0

        for i in range(0, n, batch_size):
            idx = perm[i:i + batch_size]
            xb = x_t[idx].requires_grad_(True)
            yb = y_t[idx]

            if use_spot or use_ir:
                y_pred, grads_full = predict_with_grads(model, xb)
            else:
                y_pred = model(xb)

            loss_val = ((y_pred - yb) ** 2).mean()
            loss_spot = torch.zeros((), device=device)
            loss_ir = torch.zeros((), device=device)

            if use_spot:
                g_spot_pred = grads_full[:, sp_pos]
                g_spot_true = gs_t[idx]
                loss_spot = ((lam_s ** 2) * (g_spot_pred - g_spot_true) ** 2).mean()

            if use_ir:
                g_ir_pred = grads_full[:, ir_pos]
                g_ir_true = gi_t[idx]
                loss_ir = ((lam_i ** 2) * (g_ir_pred - g_ir_true) ** 2).mean()

            pw = price_weight if use_spot else 1.0
            loss = pw * loss_val + beta_spot * loss_spot + beta_ir * loss_ir

            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            sums["total"] += float(loss.item())
            sums["value"] += float(loss_val.item())
            sums["spot_grad"] += float(loss_spot.item())
            sums["ir_grad"] += float(loss_ir.item())
            nb += 1

        scheduler.step()
        for k in sums:
            history[k].append(sums[k] / max(nb, 1))

        if verbose and (epoch <= 5 or epoch % 20 == 0 or epoch == epochs):
            lr_now = scheduler.get_last_lr()[0]
            print(
                f"    [{mode}] {epoch:4d}/{epochs}  "
                f"total={history['total'][-1]:.6f}  "
                f"val={history['value'][-1]:.6f}  "
                f"spot={history['spot_grad'][-1]:.6f}  "
                f"ir={history['ir_grad'][-1]:.6f}  "
                f"lr={lr_now:.2e}"
            )

        # ── Per-epoch validation + early stopping ───────────────────────────
        if patience > 0:
            model.eval()
            xv = x_v.detach().requires_grad_(use_spot or use_ir)
            if use_spot or use_ir:
                # create_graph=False: we only need first-order grads for the
                # validation scalar; no backprop through grads required.
                yv = model(xv)
                (gv,) = torch.autograd.grad(
                    outputs=yv,
                    inputs=xv,
                    grad_outputs=torch.ones_like(yv),
                    create_graph=False,
                    retain_graph=False,
                )
            else:
                with torch.no_grad():
                    yv = model(xv)
                gv = None

            pw = price_weight if use_spot else 1.0
            vl = float((pw * ((yv - y_v) ** 2).mean()).item())
            if use_spot:
                vl += float((beta_spot * (lam_s ** 2 * (gv[:, sp_pos] - gs_v) ** 2).mean()).item())
            if use_ir:
                vl += float((beta_ir * (lam_i ** 2 * (gv[:, ir_pos] - gi_v) ** 2).mean()).item())
            history["val_total"].append(vl)
            model.train()

            if vl < best_val_loss:
                best_val_loss = vl
                best_state = copy.deepcopy(model.state_dict())
                no_improve = 0
                best_epoch = epoch
            else:
                no_improve += 1
                if no_improve >= patience:
                    if verbose:
                        print(
                            f"    [{mode}] Early stop at epoch {epoch} "
                            f"(no improvement for {patience} epochs; best epoch {best_epoch})"
                        )
                    break

    # Restore weights from the best-validation-loss epoch
    if patience > 0 and best_state:
        model.load_state_dict(best_state)
        if verbose:
            print(
                f"    [{mode}] Restored weights from best epoch {best_epoch} "
                f"(val_loss={best_val_loss:.6f})"
            )

    return history


def _denorm_price(y_norm: np.ndarray, pack: Dict) -> np.ndarray:
    return y_norm * pack["y_std"].numpy() + pack["y_mean"].numpy()


def _denorm_spot_greeks(g_norm: np.ndarray, pack: Dict) -> np.ndarray:
    sp_std = pack["sp_feat_std"].numpy()
    y_std = pack["y_std"].numpy()
    return g_norm * (y_std / sp_std)


def _denorm_ir_greeks_kept(g_norm: np.ndarray, pack: Dict) -> np.ndarray:
    y_std = pack["y_std"].numpy()
    ir_feat_std_kept = pack["x_std"][pack["all_ir_feat_positions"]].numpy()
    return g_norm * (y_std / ir_feat_std_kept)


def predict_all(
    model: nn.Module,
    pack: Dict,
    split: str,
    device: torch.device,
    batch_size: int = 4096,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    model.eval()

    x_all = pack[f"x_{split}"].to(device)
    sp_pos = pack["spot_positions"]
    all_ir_pos = pack["all_ir_feat_positions"]
    all_ir_global = pack["all_ir_global"]

    n = x_all.shape[0]

    y_acc: List[np.ndarray] = []
    gs_acc: List[np.ndarray] = []
    gi_kept_acc: List[np.ndarray] = []

    for i in range(0, n, batch_size):
        xb = x_all[i:i + batch_size].requires_grad_(True)
        y_p, grads = predict_with_grads(model, xb)
        y_acc.append(y_p.detach().cpu().numpy())
        gs_acc.append(grads[:, sp_pos].detach().cpu().numpy())
        if len(all_ir_pos) > 0:
            gi_kept_acc.append(grads[:, all_ir_pos].detach().cpu().numpy())

    y_n = np.concatenate(y_acc, axis=0)
    gs_n = np.concatenate(gs_acc, axis=0)
    gi_kept_n = np.concatenate(gi_kept_acc, axis=0) if len(all_ir_pos) > 0 else np.zeros((n, 0), dtype=np.float64)

    price = _denorm_price(y_n, pack).ravel()
    dcds = _denorm_spot_greeks(gs_n, pack)

    ir_full = np.zeros((n, N_DPRICE_DIRDF), dtype=np.float64)
    if len(all_ir_pos) > 0:
        ir_kept = _denorm_ir_greeks_kept(gi_kept_n, pack)
        for j, knot in enumerate(all_ir_global):
            ir_full[:, knot] = ir_kept[:, j]

    spot_vals = pack[f"spot_raw_{split}"].numpy()
    dollar_delta = dcds * spot_vals

    return price, dollar_delta, ir_full


def evaluate(model: nn.Module, pack: Dict, split: str, device: torch.device) -> Dict[str, float]:
    price_p_s, dd_p_s, ir_p_s = predict_all(model, pack, split=split, device=device)

    gids = pack[f"{split}_group_ids"]

    price_p = _group_mean(price_p_s.reshape(-1, 1), gids).ravel()
    dd_p = _group_mean(dd_p_s, gids)
    ir_p = _group_mean(ir_p_s, gids)

    price_t = _group_mean(pack[f"y_{split}_raw"].numpy(), gids).ravel()
    dd_t = _group_mean(pack[f"dd_{split}_raw"].numpy(), gids)
    ir_t = _group_mean(pack[f"gi_{split}_raw"].numpy(), gids)

    def rmse(a: np.ndarray, b: np.ndarray) -> float:
        return float(np.sqrt(np.mean((a - b) ** 2)))

    def rel(err: float, ref: np.ndarray) -> float:
        return float(err / max(float(np.mean(np.abs(ref))), 1e-12))

    def r2(pred: np.ndarray, true: np.ndarray) -> float:
        ss_res = float(np.sum((pred - true) ** 2))
        ss_tot = float(np.sum((true - true.mean()) ** 2))
        return 1.0 - ss_res / max(ss_tot, 1e-24)

    pr = rmse(price_p, price_t)
    dr = rmse(dd_p.ravel(), dd_t.ravel())

    metrics: Dict[str, float] = {
        "price_rmse": pr,
        "price_rel_rmse": rel(pr, price_t),
        "price_r2": r2(price_p, price_t),
        "dollar_delta_rmse": dr,
        "dollar_delta_rel": rel(dr, dd_t.ravel()),
        "dollar_delta_r2": r2(dd_p.ravel(), dd_t.ravel()),
    }

    active = pack["active_ir_global"]
    if len(active) > 0:
        ir_t_active = ir_t[:, active]
        ir_p_active = ir_p[:, active]
        ir_r = rmse(ir_p_active.ravel(), ir_t_active.ravel())
        metrics["ir_greek_rmse"] = ir_r
        metrics["ir_greek_rel"] = rel(ir_r, ir_t_active.ravel())
        metrics["ir_greek_r2"] = r2(ir_p_active.ravel(), ir_t_active.ravel())
        for k in active:
            kr = rmse(ir_p[:, k], ir_t[:, k])
            metrics[f"ir_knot_{k}_{IR_KNOT_TENORS[k]}y_rmse"] = kr

    return metrics


def _scatter2(
    ax_s: plt.Axes,
    ax_h: plt.Axes,
    y_true: np.ndarray,
    y_van: np.ndarray,
    y_sob: np.ndarray,
    title: str,
    max_pts: int = 15000,
) -> None:
    n = len(y_true)
    if n > max_pts:
        idx = np.random.choice(n, max_pts, replace=False)
        y_true = y_true[idx]
        y_van = y_van[idx]
        y_sob = y_sob[idx]

    lo = min(float(y_true.min()), float(y_van.min()), float(y_sob.min()))
    hi = max(float(y_true.max()), float(y_van.max()), float(y_sob.max()))

    ax_s.scatter(y_true, y_van, s=4, alpha=0.2, color="tab:orange", label="Vanilla", rasterized=True)
    ax_s.scatter(y_true, y_sob, s=4, alpha=0.2, color="tab:blue", label="Sobolev", rasterized=True)
    ax_s.plot([lo, hi], [lo, hi], "k--", lw=0.8)
    ax_s.set(xlabel="Actual (MC mean)", ylabel="Predicted (NN mean)", title=title)
    ax_s.legend(fontsize=8, markerscale=3)
    ax_s.grid(alpha=0.2)

    ax_h.hist(y_van - y_true, bins=60, alpha=0.5, color="tab:orange", label="Vanilla")
    ax_h.hist(y_sob - y_true, bins=60, alpha=0.5, color="tab:blue", label="Sobolev")
    ax_h.axvline(0, color="k", lw=0.8, ls="--")
    ax_h.set(xlabel="Residual", title=f"{title} residuals")
    ax_h.legend(fontsize=8)
    ax_h.grid(alpha=0.2)


def _outer_means_for_model(model: nn.Module, pack: Dict, split: str, device: torch.device) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    y_s, dd_s, ir_s = predict_all(model, pack, split=split, device=device)
    gids = pack[f"{split}_group_ids"]
    y_o = _group_mean(y_s.reshape(-1, 1), gids).ravel()
    dd_o = _group_mean(dd_s, gids)
    ir_o = _group_mean(ir_s, gids)
    return y_o, dd_o, ir_o


def _outer_means_true(pack: Dict, split: str) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    gids = pack[f"{split}_group_ids"]
    y_t = _group_mean(pack[f"y_{split}_raw"].numpy(), gids).ravel()
    dd_t = _group_mean(pack[f"dd_{split}_raw"].numpy(), gids)
    ir_t = _group_mean(pack[f"gi_{split}_raw"].numpy(), gids)
    return y_t, dd_t, ir_t


def plot_learning_curves(hists: Dict[str, Dict[str, List[float]]], title_prefix: str) -> plt.Figure:
    keys = ["total", "value", "spot_grad", "ir_grad"]
    titles = ["Total Loss", "Price MSE", "Spot Greek MSE", "IR Greek MSE"]
    colors = {"vanilla": "tab:orange", "sobolev": "tab:blue"}

    fig, axs = plt.subplots(1, 4, figsize=(22, 4))
    for ax, key, title in zip(axs, keys, titles):
        for tag, hist in hists.items():
            ep = range(1, len(hist[key]) + 1)
            ax.plot(ep, hist[key], color=colors[tag], lw=1.2, label=tag)
            # overlay validation total-loss curve (dashed) when early stopping was used
            if key == "total" and "val_total" in hist:
                ep_v = range(1, len(hist["val_total"]) + 1)
                ax.plot(ep_v, hist["val_total"], color=colors[tag], lw=1.0,
                        linestyle="--", alpha=0.7, label=f"{tag} val")
        ax.set(title=title, xlabel="Epoch", ylabel="Loss")
        ax.set_yscale("log")
        ax.legend(fontsize=8)
        ax.grid(alpha=0.25)

    fig.suptitle(f"{title_prefix} - Learning Curves", fontsize=13, y=1.02)
    fig.tight_layout()
    return fig


def plot_price(pack: Dict, models: Dict[str, nn.Module], device: torch.device, split: str, title_prefix: str) -> plt.Figure:
    preds = {tag: _outer_means_for_model(m, pack, split=split, device=device)[0] for tag, m in models.items()}
    y_t = _outer_means_true(pack, split=split)[0]

    fig, axs = plt.subplots(1, 2, figsize=(14, 5))
    _scatter2(
        axs[0],
        axs[1],
        y_t,
        preds["vanilla"],
        preds["sobolev"],
        "Price",
    )
    fig.suptitle(f"{title_prefix} - Price", fontsize=13, y=1.02)
    fig.tight_layout()
    return fig


def plot_dollar_delta(pack: Dict, models: Dict[str, nn.Module], device: torch.device, split: str, title_prefix: str) -> plt.Figure:
    pred = {tag: _outer_means_for_model(m, pack, split=split, device=device)[1] for tag, m in models.items()}
    gt = _outer_means_true(pack, split=split)[1]

    n_g = gt.shape[1]
    fig, axs = plt.subplots(n_g, 2, figsize=(14, 4 * n_g))
    if n_g == 1:
        axs = axs[np.newaxis, :]

    for k in range(n_g):
        _scatter2(
            axs[k, 0],
            axs[k, 1],
            gt[:, k],
            pred["vanilla"][:, k],
            pred["sobolev"][:, k],
            f"Dollar Delta S_{k + 1}",
        )

    fig.suptitle(f"{title_prefix} - Dollar Delta", fontsize=13, y=1.005)
    fig.tight_layout()
    return fig


def plot_ir_greeks(pack: Dict, models: Dict[str, nn.Module], device: torch.device, split: str, title_prefix: str) -> plt.Figure | None:
    active = pack["active_ir_global"]
    if len(active) == 0:
        return None

    pred = {tag: _outer_means_for_model(m, pack, split=split, device=device)[2] for tag, m in models.items()}
    gt = _outer_means_true(pack, split=split)[2]

    n_a = len(active)
    fig, axs = plt.subplots(n_a, 2, figsize=(14, 4 * n_a))
    if n_a == 1:
        axs = axs[np.newaxis, :]

    for rank, k in enumerate(active):
        label = f"dP/dDF({IR_KNOT_TENORS[k]}y)"
        _scatter2(
            axs[rank, 0],
            axs[rank, 1],
            gt[:, k],
            pred["vanilla"][:, k],
            pred["sobolev"][:, k],
            label,
        )

    fig.suptitle(
        f"{title_prefix} - IR Greeks (active tenors {[IR_KNOT_TENORS[k] for k in active]}y)",
        fontsize=12,
        y=1.005,
    )
    fig.tight_layout()
    return fig


def plot_ir_knot_structure(pack: Dict, models: Dict[str, nn.Module], device: torch.device, split: str, title_prefix: str) -> plt.Figure:
    active = pack["active_ir_global"]
    gt = _outer_means_true(pack, split=split)[2]

    tenors = [f"{IR_KNOT_TENORS[k]}y" for k in range(N_DPRICE_DIRDF)]
    colors = {"vanilla": "tab:orange", "sobolev": "tab:blue"}

    fig, ax = plt.subplots(figsize=(13, 5))
    x = np.arange(N_DPRICE_DIRDF)
    width = 0.25

    n_m = len(models)
    offsets = np.linspace(-(n_m - 1) * width / 2, (n_m - 1) * width / 2, n_m)

    for (tag, model), offset in zip(models.items(), offsets):
        ir_pred = _outer_means_for_model(model, pack, split=split, device=device)[2]
        rm = np.sqrt(np.mean((ir_pred - gt) ** 2, axis=0))
        ax.bar(x + offset, rm, width, label=tag, color=colors[tag], alpha=0.85, edgecolor="none")

    for k in active:
        ax.axvspan(k - 0.5, k + 0.5, color="lightblue", alpha=0.15, zorder=0)

    ax.set_xticks(x)
    ax.set_xticklabels(tenors, rotation=45, ha="right")
    ax.set_ylabel("IR Greek RMSE")
    ax.set_xlabel("IR knot tenor")
    ax.set_title(f"{title_prefix} - IR Greek RMSE per knot (shaded=active)")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    return fig


def _save_figure_pdf(fig: plt.Figure, out_path: Path) -> None:
    fig.savefig(out_path, dpi=150, bbox_inches="tight")


def _build_summary_page(
    rider: str,
    args: argparse.Namespace,
    metrics: Dict[str, Dict[str, float]],
    train_count: int,
    test_count: int,
) -> plt.Figure:
    fig = plt.figure(figsize=(11, 8.5))
    ax = fig.add_subplot(111)
    ax.axis("off")

    lines: List[str] = []
    lines.append(f"sobolev_va_training summary - rider={rider}")
    lines.append("")
    lines.append(f"train samples: {train_count:,}")
    lines.append(f"test samples : {test_count:,}")
    lines.append("")
    lines.append("hyperparameters")
    lines.append(f"  epochs={args.epochs}, lr={args.lr}, batch_size={args.batch_size}")
    lines.append(
        f"  hidden_layers={args.hidden_layers}, hidden_size={args.hidden_size}, skip_connections={args.skip_connections}"
    )
    lines.append(f"  beta_spot={args.beta_spot}, beta_ir={args.beta_ir}, price_weight={args.price_weight}, ir_snr={args.ir_snr}")
    lines.append("")

    for mode in ["vanilla", "sobolev"]:
        lines.append(f"{mode} metrics")
        mm = metrics[mode]
        for k in sorted(mm.keys()):
            lines.append(f"  {k:<36s} {mm[k]:.6f}")
        lines.append("")

    text = "\n".join(lines)
    ax.text(0.02, 0.98, text, va="top", ha="left", family="monospace", fontsize=10)
    fig.tight_layout()
    return fig


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Train vanilla and Sobolev feed-forward models on outer-path prices and greeks from HDF5",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    p.add_argument("--train-dir", required=True, help="Directory with training HDF5 files")
    p.add_argument("--test-dir", required=True, help="Directory with test HDF5 files")
    p.add_argument("--max-train-samples", type=int, default=None,
                   help="Cap the number of training outer paths to this value (first N rows after "
                        "loading).  When --train-dir and --test-dir point to the same directory "
                        "the first N rows are used for training and the remaining rows are used "
                        "for evaluation, guaranteeing no leakage.")
    p.add_argument("--rider", required=True, help=f"Rider type. One of: {', '.join(RIDER_TYPES)}")
    p.add_argument(
        "--update-rule", choices=["rop", "step-up", "roll-up"], default=None,
        help=(
            "Load only policies whose HDF5 update_rule attribute matches this value. "
            "Values correspond to SimulateRider --update-rule: rop, step-up, roll-up. "
            "Omit to load all rules.  [all]"
        ),
    )

    p.add_argument("--epochs", type=int, default=200)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--batch-size", type=int, default=512)

    p.add_argument("--hidden-layers", type=int, default=6)
    p.add_argument("--hidden-size", type=int, default=256)

    p.add_argument("--skip-connections", action="store_true", default=True,
                   help="Use residual skip connections")
    p.add_argument("--no-skip-connections", dest="skip_connections", action="store_false",
                   help="Disable residual skip connections")

    p.add_argument("--beta-spot", type=float, default=0.03,
                   help="Sobolev weight for spot derivatives")
    p.add_argument("--beta-ir", type=float, default=0.03,
                   help="Sobolev weight for IR derivatives")
    p.add_argument("--price-weight", type=float, default=1.0,
                   help="Weight on the price MSE term in the Sobolev loss.  "
                        "Set <1 (e.g. 0.01) to let derivative targets dominate; "
                        "the price is still used as an anchor.  Default 1.0.")
    p.add_argument("--ir-snr", type=float, default=0.05,
                   help="Min fraction of max |IR derivative| for a knot to be active (relative filter)")
    p.add_argument("--ir-abs-snr", type=float, default=0.0,
                   help="Absolute threshold on normalised rho RMS below which a knot is dropped.  "
                        "In normalised space, 1.0 = 1 price-std per 1-sigma disc-factor shock.  "
                        "Default 0 (disabled); rely on --ir-snr relative filter only.  "
                        "Set >0 (e.g. 0.01) to additionally drop knots whose rho is economically "
                        "negligible regardless of the relative filter.")
    p.add_argument("--max-lambda", type=float, default=10.0,
                   help="Absolute upper bound on lambda weights in _lambda_weights.  "
                        "Prevents 1/rms explosion when normalised derivative targets are "
                        "tiny (e.g. IR greeks under HW sigma=0.01).  Default 10.")
    p.add_argument("--min-df-tenor", type=float, default=None,
                   help="Drop disc-factor input features with tenor < this value (years).  "
                        "Default None → auto: drop tenors whose CV < 0.5%% (near-constant "
                        "short-end knots from the AIRG outer model, typically 0.25y/0.5y).  "
                        "Pass 0.0 to disable.")
    p.add_argument("--max-df-tenor", type=float, default=None,
                   help="Drop disc-factor input features with tenor > this value (years).  "
                        "Default None → auto: largest tenor ≤ max contract maturity in "
                        "the training set (drops irrelevant long-end AIRG knots).  "
                        "Pass a large value (e.g. 9999) to disable.")

    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--weight-decay", type=float, default=1e-4,
                   help="L2 weight decay for Adam.  Compatible with Sobolev training.  Default 1e-4.")
    p.add_argument("--patience", type=int, default=0,
                   help="Early-stopping patience: stop training if the Sobolev validation loss "
                        "(computed on the first 8192 test rows each epoch) does not improve for "
                        "this many consecutive epochs, then restore the best-epoch weights.  "
                        "0 = disabled (train for the full --epochs).  Recommended: 20–40.")
    p.add_argument("--save-dir", required=True, help="Output directory for json/models/reports/pdf")

    return p.parse_args()


def main() -> None:
    args = parse_args()

    rider = args.rider.strip().upper()
    if rider not in RIDER_TYPES:
        raise ValueError(f"Unknown rider '{rider}'. Valid: {RIDER_TYPES}")

    train_dir = Path(args.train_dir)
    test_dir = Path(args.test_dir)
    if not train_dir.is_absolute():
        train_dir = PROJECT_ROOT / train_dir
    if not test_dir.is_absolute():
        test_dir = PROJECT_ROOT / test_dir

    save_dir = Path(args.save_dir)
    if not save_dir.is_absolute():
        save_dir = PROJECT_ROOT / save_dir
    save_dir.mkdir(parents=True, exist_ok=True)

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    device = get_device()
    print(f"Device : {device}")
    print(f"Rider  : {rider}")
    print(f"Train  : {train_dir}")
    print(f"Test   : {test_dir}")
    update_rule_filter = args.update_rule if args.update_rule else None
    if update_rule_filter:
        print(f"Rule   : {update_rule_filter}")
    print(
        "Arch   : "
        f"hidden_layers={args.hidden_layers}, hidden_size={args.hidden_size}, "
        f"skip_connections={args.skip_connections}"
    )

    print("\n=== Loading outer-path HDF5 datasets ===")
    train_raw = _load_split_from_h5_dir(train_dir, rider, update_rule_filter)
    test_raw = _load_split_from_h5_dir(test_dir, rider, update_rule_filter)

    # ── max-train-samples: cap training rows; when both dirs are the same path,
    #    exclude the training rows from the evaluation set to prevent leakage.
    #    A global shuffle (fixed seed=42) is applied BEFORE the split so that
    #    the N training rows are drawn proportionally from every contract, not
    #    just the first file in sorted order.
    if args.max_train_samples is not None:
        n_total = train_raw["x"].shape[0]
        n_keep = min(args.max_train_samples, n_total)
        rng = np.random.default_rng(42)
        perm = rng.permutation(n_total)
        # Apply the same permutation to both loaded copies so the split is consistent.
        train_raw = {k: v[perm] for k, v in train_raw.items()}
        if Path(train_dir).resolve() == Path(test_dir).resolve():
            test_raw = {k: v[perm] for k, v in test_raw.items()}
            train_raw = {k: v[:n_keep] for k, v in train_raw.items()}
            test_raw  = {k: v[n_keep:] for k, v in test_raw.items()}
            print(f"  max-train-samples={n_keep:,}: shuffled (seed=42), using first {n_keep:,} "
                  f"rows for train, remaining {test_raw['x'].shape[0]:,} rows for test (no overlap).")
        else:
            train_raw = {k: v[:n_keep] for k, v in train_raw.items()}
            print(f"  max-train-samples={n_keep:,}: shuffled (seed=42), training set capped at {n_keep:,} rows.")

    print(f"Train outer samples : {train_raw['x'].shape[0]:,}")
    print(f"Test  outer samples : {test_raw['x'].shape[0]:,}")

    # ── Auto-derive disc-factor tenor bounds from training data ──────────────
    # min_df_tenor: drop short-end tenors whose CV < 0.5% (near-constant in the
    #   AIRG outer-rate model, e.g. 0.25y/0.5y).  Independent of contract terms.
    # max_df_tenor: drop tenors beyond the longest contract maturity in the
    #   training set — beyond-maturity disc factors carry no pricing information.
    x_raw = train_raw["x"]
    if args.min_df_tenor is None:
        auto_min = 0.0
        for k, tenor in enumerate(IR_KNOT_TENORS):
            col = x_raw[:, ORIG_IR_START + k].float()
            cv = float(col.std() / col.mean().abs().clamp(min=1e-12))
            if cv >= 0.005:   # CV threshold 0.5 %%
                auto_min = tenor
                break
        args.min_df_tenor = auto_min
        if auto_min > 0.0:
            print(f"  Auto min-df-tenor : {auto_min}y  (CV-threshold 0.5%%)")
    if args.max_df_tenor is None:
        # maturity_months is at raw feature index 11:
        #   gender[0] + rider_oh[1-9] + age[10] + maturity_months[11]
        max_mat_mo = float(x_raw[:, 11].max())
        max_mat_yr = max_mat_mo / 12.0
        valid_tenors = [t for t in IR_KNOT_TENORS if t <= max_mat_yr + 0.5]
        args.max_df_tenor = max(valid_tenors) if valid_tenors else float("inf")
        print(f"  Auto max-df-tenor : {args.max_df_tenor}y  (max maturity {max_mat_yr:.1f}y in train set)")

    pack = preprocess(
        train_raw, test_raw,
        ir_snr_threshold=args.ir_snr,
        ir_abs_snr=args.ir_abs_snr,
        max_lambda=args.max_lambda,
        min_df_tenor=args.min_df_tenor,
        max_df_tenor=args.max_df_tenor,
    )
    print(f"Features kept       : {pack['n_features']} (dropped {pack['n_dropped']} constants/filtered)")
    dropped_df = [f"{t}y" for t in IR_KNOT_TENORS
                  if t < args.min_df_tenor or t > args.max_df_tenor]
    if dropped_df:
        print(f"  Disc-factor tenors excluded from X: {dropped_df}")
    active_knot_labels = [f'k{k}={IR_KNOT_TENORS[k]}y' for k in pack['active_ir_global']]
    print(f"Active IR knots     : {active_knot_labels if active_knot_labels else '(none — rho below ir_abs_snr threshold, IR Sobolev disabled)'}")

    models: Dict[str, FeedForwardSobolevNet] = {}
    histories: Dict[str, Dict[str, List[float]]] = {}

    for mode in ["vanilla", "sobolev"]:
        print(f"\n--- Training {mode} ---")
        model = FeedForwardSobolevNet(
            n_input=pack["n_features"],
            hidden_size=args.hidden_size,
            hidden_layers=args.hidden_layers,
            use_skip=args.skip_connections,
        ).to(device)

        t0 = time.perf_counter()
        hist = train_model(
            model,
            pack,
            mode=mode,
            beta_spot=args.beta_spot,
            beta_ir=args.beta_ir,
            price_weight=args.price_weight,
            lr=args.lr,
            epochs=args.epochs,
            batch_size=args.batch_size,
            device=device,
            weight_decay=args.weight_decay,
            patience=args.patience,
            verbose=True,
        )
        elapsed = time.perf_counter() - t0
        print(f"Time ({mode}): {elapsed:.1f}s")

        models[mode] = model
        histories[mode] = hist

    print("\n" + "=" * 70)
    print(f"EVALUATION ({rider})")
    print("=" * 70)

    all_metrics: Dict[str, Dict[str, float]] = {}
    for mode, model in models.items():
        metrics = evaluate(model, pack, split="test", device=device)
        all_metrics[mode] = metrics
        print(f"\n[{mode}]")
        for k, v in metrics.items():
            print(f"  {k:<40s} {v:14.6f}")

    torch.save(models["vanilla"].state_dict(), save_dir / "vanilla_model.pt")
    torch.save(models["sobolev"].state_dict(), save_dir / "sobolev_model.pt")

    # Plot figures
    figs: List[Tuple[str, plt.Figure]] = []

    figs.append(("learning_curves.pdf", plot_learning_curves(histories, title_prefix=rider)))
    figs.append(("price_comparison.pdf", plot_price(pack, models, device=device, split="test", title_prefix=rider)))
    figs.append(("dollar_delta_comparison.pdf", plot_dollar_delta(pack, models, device=device, split="test", title_prefix=rider)))

    fig_ir = plot_ir_greeks(pack, models, device=device, split="test", title_prefix=rider)
    if fig_ir is not None:
        figs.append(("ir_greeks_comparison.pdf", fig_ir))

    figs.append(("ir_knot_rmse.pdf", plot_ir_knot_structure(pack, models, device=device, split="test", title_prefix=rider)))

    for name, fig in figs:
        _save_figure_pdf(fig, save_dir / name)

    summary_page = _build_summary_page(
        rider,
        args,
        all_metrics,
        train_count=int(train_raw["x"].shape[0]),
        test_count=int(test_raw["x"].shape[0]),
    )

    with PdfPages(save_dir / "summary_report.pdf") as pdf:
        pdf.savefig(summary_page, bbox_inches="tight")
        for _, fig in figs:
            pdf.savefig(fig, bbox_inches="tight")

    plt.close(summary_page)
    for _, fig in figs:
        plt.close(fig)

    summary = {
        "rider": rider,
        "args": vars(args),
        "device": str(device),
        "active_ir_knots": pack["active_ir_global"],
        "active_ir_tenors": [IR_KNOT_TENORS[k] for k in pack["active_ir_global"]],
        "metrics": all_metrics,
        "output_files": [name for name, _ in figs] + ["summary_report.pdf", "summary.json", "vanilla_model.pt", "sobolev_model.pt"],
    }
    (save_dir / "summary.json").write_text(json.dumps(summary, indent=2))

    print("\nOutputs written to:", save_dir)
    print("  - vanilla_model.pt")
    print("  - sobolev_model.pt")
    print("  - summary.json")
    print("  - summary_report.pdf")
    for name, _ in figs:
        print(f"  - {name}")


if __name__ == "__main__":
    main()
