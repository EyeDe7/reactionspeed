"""Leaderboard queries shared by the JSON API and the HTML dashboard."""

import json
import sqlite3
from dataclasses import dataclass

from app.constants import DEFAULT_PER_PAGE, MAX_PER_PAGE
from app.services.ledger import AuditResult


@dataclass(frozen=True, slots=True)
class Pagination:
    per_page: int
    avg_page: int
    single_page: int
    total_pages: int

    @classmethod
    def build(cls, total_rows: int, avg_page: int = 1, single_page: int = 1,
              per_page: int = DEFAULT_PER_PAGE) -> "Pagination":
        per_page = max(1, min(MAX_PER_PAGE, per_page))
        total_pages = max(1, (total_rows + per_page - 1) // per_page)
        return cls(
            per_page=per_page,
            avg_page=min(max(1, avg_page), total_pages),
            single_page=min(max(1, single_page), total_pages),
            total_pages=total_pages,
        )

    @property
    def avg_offset(self) -> int:
        return (self.avg_page - 1) * self.per_page

    @property
    def single_offset(self) -> int:
        return (self.single_page - 1) * self.per_page


_RANKED_SELECT = """
    SELECT s.id, COALESCE(p.nickname, s.nickname) AS nickname, s.avg_ms, s.best_ms,
           s.submitted_at_str, s.entry_hash
    FROM scores s
    LEFT JOIN players p ON s.player_id = p.player_id
"""


def _ranked_rows(
    conn: sqlite3.Connection, audit: AuditResult, order_by: str, limit: int, offset: int, primary: str
) -> list[dict]:
    rows = conn.execute(f"{_RANKED_SELECT} ORDER BY {order_by} LIMIT ? OFFSET ?", (limit, offset)).fetchall()
    return [
        {
            "rank": offset + idx,
            "block_id": r["id"],
            "nickname": r["nickname"],
            "ms": round(r[primary], 2),
            "avg_ms": round(r["avg_ms"], 2),
            "best_ms": round(r["best_ms"], 2),
            "entry_hash": r["entry_hash"],
            "timestamp": r["submitted_at_str"],
            "signature_valid": r["id"] in audit.valid_ids,
        }
        for idx, r in enumerate(rows, start=1)
    ]


def top_averages(conn: sqlite3.Connection, audit: AuditResult, page: Pagination) -> list[dict]:
    return _ranked_rows(
        conn, audit, "s.avg_ms ASC, s.id ASC", page.per_page, page.avg_offset, primary="avg_ms"
    )


def top_singles(conn: sqlite3.Connection, audit: AuditResult, page: Pagination) -> list[dict]:
    return _ranked_rows(
        conn, audit, "s.best_ms ASC, s.avg_ms ASC, s.id ASC", page.per_page, page.single_offset, primary="best_ms"
    )


def friend_bests(distribution: dict) -> list[dict]:
    return [
        {
            "nickname": p["nickname"],
            "ms": p["best_avg_ms"],
            "avg_ms": p["best_avg_ms"],
            "best_ms": p["best_single_ms"],
            "signature_valid": True,
        }
        for p in distribution["players_by_avg"]
    ]


def recent_blocks(conn: sqlite3.Connection, audit: AuditResult, limit: int) -> list[dict]:
    """Latest ledger blocks with full cryptographic detail (newest first)."""
    rows = conn.execute(
        """
        SELECT s.id, s.nickname, s.avg_ms, s.best_ms, s.rounds_json,
               s.prev_hash, s.entry_hash, s.ed25519_signature, s.canonical_payload,
               s.submitted_at_str, p.public_key
        FROM scores s
        LEFT JOIN players p ON s.player_id = p.player_id
        ORDER BY s.id DESC LIMIT ?
        """,
        (limit,),
    ).fetchall()
    return [
        {
            "block_id": r["id"],
            "nickname": r["nickname"],
            "avg_ms": r["avg_ms"],
            "best_ms": r["best_ms"],
            "rounds_ms": [float(x) for x in json.loads(r["rounds_json"])],
            "prev_hash": r["prev_hash"],
            "entry_hash": r["entry_hash"],
            "ed25519_signature": r["ed25519_signature"],
            "canonical_payload": r["canonical_payload"],
            "submitted_at_str": r["submitted_at_str"],
            "public_key": r["public_key"],
            "verified": r["id"] in audit.valid_ids,
        }
        for r in rows
    ]


def latest_block(conn: sqlite3.Connection) -> sqlite3.Row | None:
    return conn.execute(
        """
        SELECT s.id, s.nickname, s.player_id, s.prev_hash, s.canonical_payload,
               s.ed25519_signature, s.attestation_hmac, s.submitted_at_str, s.entry_hash,
               p.public_key
        FROM scores s
        LEFT JOIN players p ON s.player_id = p.player_id
        ORDER BY s.id DESC LIMIT 1
        """
    ).fetchone()
