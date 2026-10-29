"""Hash-chain ledger: integrity audit, export, and per-player key verification."""

import json
import sqlite3
from dataclasses import dataclass, field

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from app.constants import FLOAT_TOLERANCE_MS, GENESIS_HASH
from app.core.canonical import CanonicalPayload, CanonicalPayloadError, format_rounds, format_waits
from app.core.crypto import ED25519_KEY_BYTES, compute_entry_hash, derive_public_key_hex, verify_ed25519
from app.exceptions import BadRequestError


@dataclass(frozen=True, slots=True)
class AuditResult:
    ledger_intact: bool
    total_entries: int
    verified_entries: int
    valid_ids: frozenset[int]
    errors: list[str]
    head_hash: str

    @property
    def cache_key(self) -> str:
        return f"{self.total_entries}:{self.verified_entries}:{self.head_hash}"


def _payload_matches_row(row: sqlite3.Row, payload: CanonicalPayload) -> bool:
    rounds = json.loads(row["rounds_json"])
    waits = json.loads(row["wait_delays_json"])
    return (
        payload.player_id == row["player_id"]
        and payload.nickname == row["nickname"]
        and payload.challenge_id == row["challenge_id"]
        and payload.binary_sha256.lower() == row["binary_sha256"].lower()
        and payload.rounds_csv == format_rounds(rounds)
        and payload.waits_csv == format_waits(waits)
        and abs(float(payload.avg_ms) - float(row["avg_ms"])) <= FLOAT_TOLERANCE_MS
        and abs(float(payload.best_ms) - float(row["best_ms"])) <= FLOAT_TOLERANCE_MS
    )


def _verify_block_contents(row: sqlite3.Row, public_key: str | None) -> list[str]:
    """Check that SQL columns match the signed payload and the signature is valid."""
    sid = row["id"]
    errors: list[str] = []
    try:
        payload = CanonicalPayload.parse(row["canonical_payload"])
        if not _payload_matches_row(row, payload):
            errors.append(f"Block #{sid}: SQL columns do not match signed canonical payload")
    except CanonicalPayloadError:
        errors.append(f"Block #{sid}: Invalid canonical payload format")
    except (ValueError, TypeError, AttributeError):
        errors.append(f"Block #{sid}: Malformed JSON or canonical columns")

    if not public_key or not verify_ed25519(public_key, row["canonical_payload"], row["ed25519_signature"]):
        errors.append(f"Block #{sid}: Invalid Ed25519 signature for player {row['nickname']}")
    return errors


@dataclass
class LedgerAuditor:
    """Walks the full hash chain, caching expensive per-block signature checks.

    Cache entries are keyed on ``(block id, entry hash, player public key)`` so any
    tampering with a row or a player's key forces re-verification.
    """

    _block_cache: dict[tuple[int, str, str], list[str]] = field(default_factory=dict)

    def audit(self, conn: sqlite3.Connection) -> AuditResult:
        public_keys = {
            row["player_id"]: row["public_key"]
            for row in conn.execute("SELECT player_id, public_key FROM players")
        }
        rows = conn.execute(
            """
            SELECT id, player_id, nickname, challenge_id, avg_ms, best_ms,
                   rounds_json, wait_delays_json, binary_sha256, canonical_payload,
                   ed25519_signature, submitted_at_str, prev_hash, entry_hash
            FROM scores
            ORDER BY id ASC
            """
        ).fetchall()

        expected_prev = GENESIS_HASH
        errors: list[str] = []
        valid_ids: set[int] = set()

        for row in rows:
            sid, entry_hash = row["id"], row["entry_hash"]
            link_ok = True

            if row["prev_hash"] != expected_prev:
                errors.append(f"Block #{sid}: Broken hash chain link (prev_hash mismatch)")
                link_ok = False

            recomputed = compute_entry_hash(
                row["prev_hash"], row["canonical_payload"], row["ed25519_signature"], row["submitted_at_str"]
            )
            if recomputed != entry_hash:
                errors.append(f"Block #{sid}: Entry hash mismatch (row modified after insertion)")
                link_ok = False

            public_key = public_keys.get(row["player_id"])
            cache_key = (sid, entry_hash, public_key or "")
            block_errors = self._block_cache.get(cache_key) if link_ok else None
            if block_errors is None:
                block_errors = _verify_block_contents(row, public_key)
                if link_ok:
                    self._block_cache[cache_key] = block_errors
            errors.extend(block_errors)

            if link_ok and not block_errors:
                valid_ids.add(sid)
            expected_prev = entry_hash

        return AuditResult(
            ledger_intact=not errors,
            total_entries=len(rows),
            verified_entries=len(valid_ids),
            valid_ids=frozenset(valid_ids),
            errors=errors,
            head_hash=expected_prev,
        )


def last_entry_hash(conn: sqlite3.Connection) -> str:
    row = conn.execute("SELECT entry_hash FROM scores ORDER BY id DESC LIMIT 1").fetchone()
    return row["entry_hash"] if row else GENESIS_HASH


def export_ledger(conn: sqlite3.Connection, audit: AuditResult) -> list[dict]:
    rows = conn.execute(
        """
        SELECT s.id, s.nickname, s.avg_ms, s.best_ms, s.rounds_json, s.binary_sha256,
               s.canonical_payload, s.ed25519_signature, s.prev_hash, s.entry_hash,
               s.submitted_at_str, p.public_key
        FROM scores s
        LEFT JOIN players p ON s.player_id = p.player_id
        ORDER BY s.id ASC
        """
    )
    return [
        {
            "id": r["id"],
            "nickname": r["nickname"],
            "avg_ms": round(r["avg_ms"], 3),
            "best_ms": round(r["best_ms"], 3),
            "rounds_ms": json.loads(r["rounds_json"]),
            "binary_sha256": r["binary_sha256"],
            "canonical_payload": r["canonical_payload"],
            "ed25519_signature": r["ed25519_signature"],
            "prev_hash": r["prev_hash"],
            "entry_hash": r["entry_hash"],
            "timestamp": r["submitted_at_str"],
            "public_key": r["public_key"],
            "cryptographically_valid": r["id"] in audit.valid_ids,
        }
        for r in rows
    ]


def _find_player_by_pubkey(conn: sqlite3.Connection, public_key: str) -> sqlite3.Row | None:
    return conn.execute(
        "SELECT player_id, nickname, public_key, registered_at FROM players WHERE public_key = ?",
        (public_key,),
    ).fetchone()


def verify_player_key(conn: sqlite3.Connection, audit: AuditResult, key_hex: str) -> dict:
    """Audit a player's scores given either their public key or private key seed.

    With a private seed, the public key is derived and every canonical payload is
    re-signed to prove the stored signatures match byte-for-byte (Ed25519 is
    deterministic).
    """
    raw_hex = key_hex.strip().lower()
    try:
        key_bytes = bytes.fromhex(raw_hex)
        if len(key_bytes) != ED25519_KEY_BYTES:
            raise ValueError
    except ValueError:
        raise BadRequestError("Key must be a 64-character hexadecimal string (32 bytes)") from None

    private_key: Ed25519PrivateKey | None = None
    player = _find_player_by_pubkey(conn, raw_hex)
    public_key = raw_hex
    if player is None:
        private_key = Ed25519PrivateKey.from_private_bytes(key_bytes)
        public_key = derive_public_key_hex(private_key)
        player = _find_player_by_pubkey(conn, public_key)
        if player is None:
            return {
                "found_player": False,
                "key_type": "private_seed (derived public key)",
                "derived_public_key": public_key,
                "ledger_intact": audit.ledger_intact,
                "head_hash": audit.head_hash,
                "message": "No registered player found matching this key yet. Play 5 rounds to register!",
                "verified_scores": [],
            }

    score_rows = conn.execute(
        """
        SELECT id, avg_ms, best_ms, rounds_json, canonical_payload,
               ed25519_signature, prev_hash, entry_hash, submitted_at_str
        FROM scores
        WHERE player_id = ?
        ORDER BY id DESC
        """,
        (player["player_id"],),
    ).fetchall()

    verified_scores = []
    for r in score_rows:
        canon, stored_sig = r["canonical_payload"], r["ed25519_signature"]
        resign_match = None
        if private_key is not None:
            resign_match = private_key.sign(canon.encode("utf-8")).hex() == stored_sig.lower()
        recalculated = compute_entry_hash(r["prev_hash"], canon, stored_sig, r["submitted_at_str"])
        verified_scores.append(
            {
                "block_id": r["id"],
                "avg_ms": round(r["avg_ms"], 2),
                "best_ms": round(r["best_ms"], 2),
                "rounds_ms": json.loads(r["rounds_json"]),
                "timestamp": r["submitted_at_str"],
                "prev_hash": r["prev_hash"],
                "entry_hash": r["entry_hash"],
                "entry_hash_valid": recalculated == r["entry_hash"],
                "ed25519_signature": stored_sig,
                "public_key_verified": verify_ed25519(public_key, canon, stored_sig),
                "private_key_resign_exact_match": resign_match,
                "in_verified_chain": r["id"] in audit.valid_ids,
                "canonical_payload": canon,
            }
        )

    return {
        "found_player": True,
        "player_id": player["player_id"],
        "nickname": player["nickname"],
        "key_type": (
            "Ed25519 Private Key Seed (Re-Signed & Verified)" if private_key is not None else "Ed25519 Public Key"
        ),
        "derived_public_key": player["public_key"],
        "ledger_intact": audit.ledger_intact,
        "head_hash": audit.head_hash,
        "total_player_scores": len(verified_scores),
        "verified_scores": verified_scores,
    }
