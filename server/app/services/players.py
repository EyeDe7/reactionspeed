"""Player registration, authentication, and profile lookup."""

import json
import secrets
import sqlite3
import time

from app.config import Settings
from app.core.crypto import hash_api_token, is_valid_ed25519_pubkey, secrets_match, tokens_match
from app.exceptions import BadRequestError, ConflictError, ForbiddenError, NotFoundError, UnauthorizedError
from app.schemas import RegisterRequest
from app.services.binaries import is_binary_allowed
from app.services.ledger import AuditResult


def _issue_token() -> tuple[str, str]:
    """Return ``(api_token, sha256(api_token))``; only the hash is persisted."""
    api_token = secrets.token_hex(24)
    return api_token, hash_api_token(api_token)


def register_player(conn: sqlite3.Connection, settings: Settings, req: RegisterRequest) -> dict:
    """Bind a nickname to an Ed25519 public key and issue a fresh API token.

    Re-registering with an already-known public key rotates the token (and may
    rename the player) instead of creating a new account.
    """
    if not secrets_match(settings.invite_code, req.invite_code.strip()):
        raise ForbiddenError("Invalid invite_code")

    nickname = req.nickname.strip()
    if "|" in nickname:
        raise BadRequestError("Nickname cannot contain '|'")

    public_key = req.public_key.strip().lower()
    binary_sha256 = req.binary_sha256.strip().lower()
    if not is_valid_ed25519_pubkey(public_key):
        raise BadRequestError("Invalid Ed25519 public_key")

    if not is_binary_allowed(conn, binary_sha256, settings.strict_binary_check):
        raise ForbiddenError("Unrecognized or modified client binary SHA-256")

    nick_row = conn.execute("SELECT public_key FROM players WHERE nickname = ?", (nickname,)).fetchone()
    if nick_row and nick_row["public_key"] != public_key:
        raise ConflictError("Nickname already bound to another Ed25519 keypair")

    api_token, token_hash = _issue_token()
    existing = conn.execute("SELECT player_id FROM players WHERE public_key = ?", (public_key,)).fetchone()
    if existing:
        player_id = existing["player_id"]
        conn.execute(
            "UPDATE players SET nickname = ?, api_token_hash = ?, last_binary_sha256 = ? WHERE player_id = ?",
            (nickname, token_hash, binary_sha256, player_id),
        )
    else:
        player_id = "plr_" + secrets.token_hex(8)
        conn.execute(
            """INSERT INTO players (player_id, nickname, public_key, api_token_hash, registered_at, last_binary_sha256)
               VALUES (?, ?, ?, ?, ?, ?)""",
            (player_id, nickname, public_key, token_hash, int(time.time()), binary_sha256),
        )

    return {"player_id": player_id, "nickname": nickname, "api_token": api_token}


def authenticate_player(conn: sqlite3.Connection, player_id: str, api_token: str) -> sqlite3.Row:
    """Return the player's ``nickname`` / ``public_key`` row, or raise 401."""
    row = conn.execute(
        "SELECT nickname, public_key, api_token_hash FROM players WHERE player_id = ?", (player_id,)
    ).fetchone()
    if not row:
        raise UnauthorizedError("Unknown player_id")
    if not tokens_match(row["api_token_hash"], api_token.strip()):
        raise UnauthorizedError("Invalid X-Api-Token")
    return row


def nickname_taken_by_other(conn: sqlite3.Connection, nickname: str, player_id: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM players WHERE nickname = ? AND player_id != ?", (nickname, player_id)
    ).fetchone()
    return row is not None


def get_player_profile(conn: sqlite3.Connection, audit: AuditResult, player_id: str) -> dict:
    player = conn.execute(
        "SELECT nickname, public_key, registered_at FROM players WHERE player_id = ?", (player_id,)
    ).fetchone()
    if not player:
        raise NotFoundError("Player not found")

    scores = [
        {
            "id": r["id"],
            "avg_ms": round(r["avg_ms"], 3),
            "best_ms": round(r["best_ms"], 3),
            "rounds_ms": json.loads(r["rounds_json"]),
            "entry_hash": r["entry_hash"],
            "timestamp": r["submitted_at_str"],
            "signature_valid": r["id"] in audit.valid_ids,
        }
        for r in conn.execute(
            """SELECT id, avg_ms, best_ms, rounds_json, entry_hash, submitted_at_str
               FROM scores WHERE player_id = ? ORDER BY id DESC""",
            (player_id,),
        )
    ]
    return {
        "player_id": player_id,
        "nickname": player["nickname"],
        "public_key": player["public_key"],
        "registered_at": player["registered_at"],
        "scores": scores,
    }
