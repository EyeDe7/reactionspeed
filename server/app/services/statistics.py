"""Reaction-time distribution statistics for the dashboard bell curve."""

import json
import math
import sqlite3
from dataclasses import dataclass, field

from app.services.ledger import AuditResult

# Values outside this window are ignored when fitting the distribution.
_SAMPLE_MIN_MS = 20.0
_SAMPLE_MAX_MS = 2000.0
_BIN_WIDTH = 10
_MIN_BIN = 40
_MAX_BIN = 380


def percentile_sorted(sorted_vals: list[float], q: float) -> float:
    """Linear-interpolated percentile of an already-sorted list."""
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    pos = (len(sorted_vals) - 1) * q
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (pos - lo)


def _empty_summary(bin_width: int) -> dict:
    return {
        "count": 0,
        "mean_ms": 0.0,
        "median_ms": 0.0,
        "std_ms": 0.0,
        "curve_std_ms": 18.0,
        "log_mu": math.log(200.0),
        "log_sigma": 0.16,
        "min_ms": 0.0,
        "max_ms": 0.0,
        "p10_ms": 0.0,
        "p25_ms": 0.0,
        "p75_ms": 0.0,
        "p90_ms": 0.0,
        "bin_width": bin_width,
        "bins": [],
    }


def summarize_series(
    values: list[float],
    bin_width: int = _BIN_WIDTH,
    min_bin: int = _MIN_BIN,
    max_bin: int = _MAX_BIN,
) -> dict:
    """Descriptive stats, Gaussian + log-normal fit parameters, and a histogram."""
    if not values:
        return _empty_summary(bin_width)

    vals = sorted(float(x) for x in values if _SAMPLE_MIN_MS <= float(x) <= _SAMPLE_MAX_MS)
    if not vals:
        return _empty_summary(bin_width)
    n = len(vals)
    mean_ms = sum(vals) / n
    median_ms = percentile_sorted(vals, 0.50)
    std_ms = math.sqrt(sum((x - mean_ms) ** 2 for x in vals) / n)
    curve_std_ms = max(std_ms, 9.0)

    log_vals = [math.log(max(25.0, x)) for x in vals]
    log_mu = sum(log_vals) / n
    log_sigma = max(math.sqrt(sum((lv - log_mu) ** 2 for lv in log_vals) / n), 0.055)

    num_bins = (max_bin - min_bin) // bin_width
    counts = [0] * num_bins
    for v in vals:
        idx = int((v - min_bin) // bin_width)
        if 0 <= idx < num_bins:
            counts[idx] += 1

    bins = []
    for i, count in enumerate(counts):
        b_start = min_bin + i * bin_width
        bins.append(
            {
                "bin_start": b_start,
                "bin_end": b_start + bin_width,
                "bin_center": b_start + bin_width * 0.5,
                "count": count,
                "density": round(count / (n * bin_width), 6),
            }
        )

    return {
        "count": n,
        "mean_ms": round(mean_ms, 2),
        "median_ms": round(median_ms, 2),
        "std_ms": round(std_ms, 2),
        "curve_std_ms": round(curve_std_ms, 2),
        "log_mu": round(log_mu, 6),
        "log_sigma": round(log_sigma, 6),
        "min_ms": round(vals[0], 2),
        "max_ms": round(vals[-1], 2),
        "p10_ms": round(percentile_sorted(vals, 0.10), 2),
        "p25_ms": round(percentile_sorted(vals, 0.25), 2),
        "p75_ms": round(percentile_sorted(vals, 0.75), 2),
        "p90_ms": round(percentile_sorted(vals, 0.90), 2),
        "bin_width": bin_width,
        "bins": bins,
    }


@dataclass
class _PlayerAggregate:
    nickname: str
    best_avg_ms: float = math.inf
    best_single_ms: float = math.inf
    sum_avg_ms: float = 0.0
    sets_count: int = 0

    def add(self, nickname: str, avg_ms: float, best_ms: float) -> None:
        self.nickname = nickname
        self.best_avg_ms = round(min(self.best_avg_ms, avg_ms), 2)
        self.best_single_ms = round(min(self.best_single_ms, best_ms), 2)
        self.sum_avg_ms += avg_ms
        self.sets_count += 1

    def as_dict(self) -> dict:
        return {
            "nickname": self.nickname,
            "best_avg_ms": self.best_avg_ms,
            "best_single_ms": self.best_single_ms,
            "mean_avg_ms": round(self.sum_avg_ms / self.sets_count, 2),
            "sets_count": self.sets_count,
        }


@dataclass
class DistributionCalculator:
    """Computes the verified-score distribution, memoised on the ledger head."""

    _cache_key: str | None = None
    _cache_data: dict | None = field(default=None, repr=False)

    def compute(self, conn: sqlite3.Connection, audit: AuditResult) -> dict:
        if self._cache_key == audit.cache_key and self._cache_data is not None:
            return self._cache_data

        rows = conn.execute(
            """
            SELECT s.id, COALESCE(p.nickname, s.nickname) AS nickname, s.player_id,
                   s.avg_ms, s.best_ms, s.rounds_json
            FROM scores s
            LEFT JOIN players p ON s.player_id = p.player_id
            ORDER BY s.id ASC
            """
        ).fetchall()

        all_rounds: list[float] = []
        all_averages: list[float] = []
        players: dict[str, _PlayerAggregate] = {}

        for row in rows:
            if row["id"] not in audit.valid_ids:
                continue
            avg_f, best_f = float(row["avg_ms"]), float(row["best_ms"])
            all_averages.append(avg_f)
            try:
                all_rounds.extend([float(x) for x in json.loads(row["rounds_json"])])
            except (ValueError, TypeError):
                pass

            account = row["player_id"] or row["nickname"]
            players.setdefault(account, _PlayerAggregate(row["nickname"])).add(row["nickname"], avg_f, best_f)

        players_by_avg = sorted((p.as_dict() for p in players.values()), key=lambda x: x["best_avg_ms"])
        players_by_single = sorted(players_by_avg, key=lambda x: x["best_single_ms"])

        result = {
            "rounds": summarize_series(all_rounds),
            "averages": summarize_series(all_averages),
            "unique_players_count": len(players_by_avg),
            "players_by_avg": players_by_avg,
            "players_by_single": players_by_single,
        }
        self._cache_key = audit.cache_key
        self._cache_data = result
        return result
