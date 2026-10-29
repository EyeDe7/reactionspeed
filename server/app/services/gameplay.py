"""Challenge issuance and verified score submission (anti-cheat pipeline)."""

import json
import secrets
import sqlite3
import statistics
import time

from app import constants as c
from app.config import Settings
from app.core.canonical import CanonicalPayload, CanonicalPayloadError, format_rounds, format_waits
from app.core.crypto import compute_attestation_hmac, compute_entry_hash, secrets_match, verify_ed25519
from app.db import now_ms
from app.exceptions import BadRequestError, ConflictError, ForbiddenError
from app.schemas import ChallengeRequest, SubmitScoreRequest
from app.services.binaries import is_binary_allowed
from app.services.ledger import last_entry_hash
from app.services.players import authenticate_player, nickname_taken_by_other


# ---------------------------------------------------------------------------
# Challenges
# ---------------------------------------------------------------------------
def issue_challenge(
    conn: sqlite3.Connection, settings: Settings, req: ChallengeRequest, api_token: str
) -> dict:
    """Issue a single-use ticket with server-chosen random wait delays."""
    if abs(int(time.time()) - req.timestamp) > c.MAX_CLOCK_SKEW_S:
        raise BadRequestError("System clock skew > 5 minutes")

    binary_sha256 = req.binary_sha256.strip().lower()
    if not is_binary_allowed(conn, binary_sha256, settings.strict_binary_check):
        raise ForbiddenError("Client binary SHA-256 failed integrity allowlist")

    player = authenticate_player(conn, req.player_id, api_token)

    msg = f"CHALLENGE|{req.player_id}|{req.timestamp}|{binary_sha256}"
    if not verify_ed25519(player["public_key"], msg, req.ed25519_signature.strip().lower()):
        raise ForbiddenError("Ed25519 challenge signature invalid")

    challenge_id = "chk_" + secrets.token_hex(12)
    challenge_nonce = secrets.token_hex(16)
    wait_delays = [
        c.CHALLENGE_WAIT_MIN_MS + secrets.randbelow(c.CHALLENGE_WAIT_SPREAD_MS + 1)
        for _ in range(c.ROUNDS_PER_SET)
    ]
    issued_ms = now_ms()
    expires_ms = issued_ms + c.CHALLENGE_TTL_MS

    # Opportunistically prune stale tickets (uses idx_challenges_expires).
    conn.execute(
        "DELETE FROM challenges WHERE expires_at_ms < ? OR used = 1",
        (issued_ms - c.CHALLENGE_PRUNE_GRACE_MS,),
    )
    conn.execute(
        """INSERT INTO challenges
               (challenge_id, player_id, challenge_nonce, wait_delays_json, issued_at_ms, expires_at_ms, used)
           VALUES (?, ?, ?, ?, ?, ?, 0)""",
        (challenge_id, req.player_id, challenge_nonce, json.dumps(wait_delays), issued_ms, expires_ms),
    )

    return {
        "challenge_id": challenge_id,
        "challenge_nonce": challenge_nonce,
        "wait_delays_ms": wait_delays,
        "expires_at_ms": expires_ms,
    }


def _consume_challenge(conn: sqlite3.Connection, challenge_id: str, player_id: str, at_ms: int) -> sqlite3.Row:
    """Atomically burn a challenge ticket. The burn is committed even if later checks fail."""
    ticket = conn.execute(
        """SELECT challenge_nonce, wait_delays_json, issued_at_ms, expires_at_ms, used
           FROM challenges WHERE challenge_id = ? AND player_id = ?""",
        (challenge_id, player_id),
    ).fetchone()
    if not ticket:
        raise ForbiddenError("Invalid challenge_id")
    if ticket["used"] != 0:
        raise ForbiddenError("Challenge ticket already used (replay blocked)")

    conn.execute("UPDATE challenges SET used = 1 WHERE challenge_id = ?", (challenge_id,))
    conn.commit()

    if at_ms > ticket["expires_at_ms"]:
        raise ForbiddenError("Challenge ticket expired")
    return ticket


# ---------------------------------------------------------------------------
# Anti-cheat
# ---------------------------------------------------------------------------
def validate_human_rounds(rounds: list[float]) -> None:
    """Reject physically implausible or macro-like reaction sets."""
    fast_rounds = 0
    for r in rounds:
        if r < c.MIN_REACTION_MS:
            raise ForbiddenError(f"Impossible reaction ({r:.2f}ms < 25ms hardware/bot floor)")
        if r < c.FAST_ROUND_THRESHOLD_MS:
            fast_rounds += 1
        if r > c.MAX_REACTION_MS:
            raise ForbiddenError(f"Reaction time exceeded 2000ms limit ({r:.2f}ms)")

    if fast_rounds > c.MAX_FAST_ROUNDS:
        raise ForbiddenError("Too many sub-100ms rounds in one set (pixel-bot blocked)")

    average = sum(rounds) / len(rounds)
    if average < c.MIN_HUMAN_AVERAGE_MS:
        raise ForbiddenError(f"5-round average ({average:.1f}ms) below 110ms human limit")

    if statistics.stdev(rounds) < c.MIN_ROUND_STDEV_MS:
        raise ForbiddenError("Unnatural sub-millisecond uniformity (macro blocked)")


def validate_elapsed_time(waits: list[int], rounds: list[float], issued_at_ms: int, at_ms: int) -> None:
    """The set cannot finish faster than the sum of all waits and reactions."""
    min_required_ms = sum(waits) + sum(rounds) - c.ELAPSED_TOLERANCE_MS
    actual_ms = at_ms - issued_at_ms
    if actual_ms < min_required_ms:
        raise ForbiddenError(f"Wall-clock timing violation: {actual_ms:.0f}ms < {min_required_ms:.0f}ms")


def _verify_canonical_payload(
    raw: str,
    *,
    player_id: str,
    challenge_id: str,
    nonce: str,
    binary_sha256: str,
    rounds: list[float],
    waits: list[int],
) -> CanonicalPayload:
    try:
        payload = CanonicalPayload.parse(raw)
    except CanonicalPayloadError:
        raise ForbiddenError("Malformed canonical payload") from None

    try:
        signed_avg, signed_best = float(payload.avg_ms), float(payload.best_ms)
    except ValueError:
        raise ForbiddenError("Invalid numeric values in canonical payload") from None

    expected_avg = sum(rounds) / len(rounds)
    expected_best = min(rounds)
    if (
        payload.player_id != player_id
        or payload.challenge_id != challenge_id
        or payload.nonce != nonce
        or payload.binary_sha256.lower() != binary_sha256
        or payload.rounds_csv != format_rounds(rounds)
        or payload.waits_csv != format_waits(waits)
        or abs(signed_avg - expected_avg) > c.FLOAT_TOLERANCE_MS
        or abs(signed_best - expected_best) > c.FLOAT_TOLERANCE_MS
    ):
        raise ForbiddenError("Canonical payload mismatch")
    return payload


# ---------------------------------------------------------------------------
# Submission
# ---------------------------------------------------------------------------
def submit_score(
    conn: sqlite3.Connection,
    settings: Settings,
    req: SubmitScoreRequest,
    api_token: str,
    received_at_ms: int,
) -> dict:
    """Validate a signed 5-round set and append it to the hash-chain ledger."""
    if len(req.rounds_ms) != c.ROUNDS_PER_SET or len(req.wait_delays_ms) != c.ROUNDS_PER_SET:
        raise BadRequestError("Exactly 5 rounds and 5 wait delays required")

    binary_sha256 = req.binary_sha256.strip().lower()
    api_token = api_token.strip()
    signature = req.ed25519_signature.strip().lower()

    if not is_binary_allowed(conn, binary_sha256, settings.strict_binary_check):
        raise ForbiddenError("Modified or unauthorized client binary hash")

    player = authenticate_player(conn, req.player_id, api_token)
    ticket = _consume_challenge(conn, req.challenge_id, req.player_id, received_at_ms)

    expected_waits = [int(w) for w in json.loads(ticket["wait_delays_json"])]
    if [int(w) for w in req.wait_delays_ms] != expected_waits:
        raise ForbiddenError("Wait delays do not match server-issued challenge")

    rounds = [float(r) for r in req.rounds_ms]
    validate_human_rounds(rounds)
    validate_elapsed_time(expected_waits, rounds, ticket["issued_at_ms"], received_at_ms)

    payload = _verify_canonical_payload(
        req.canonical_payload,
        player_id=req.player_id,
        challenge_id=req.challenge_id,
        nonce=ticket["challenge_nonce"],
        binary_sha256=binary_sha256,
        rounds=rounds,
        waits=expected_waits,
    )

    expected_attestation = compute_attestation_hmac(
        settings.protocol_attestation_secret,
        binary_sha256,
        ticket["challenge_nonce"],
        api_token,
        req.canonical_payload,
    )
    if not secrets_match(expected_attestation, req.attestation_hmac.strip().lower()):
        raise ForbiddenError("Binary attestation HMAC verification failed")

    if not verify_ed25519(player["public_key"], req.canonical_payload, signature):
        raise ForbiddenError("Ed25519 player signature verification failed")

    # The signed payload may carry a new nickname (client-side rename).
    nickname = player["nickname"]
    if payload.nickname != nickname and c.NICKNAME_MIN_LEN <= len(payload.nickname) <= c.NICKNAME_MAX_LEN:
        if nickname_taken_by_other(conn, payload.nickname, req.player_id):
            raise ConflictError("Nickname already bound to another Ed25519 keypair")
        nickname = payload.nickname
        conn.execute("UPDATE players SET nickname = ? WHERE player_id = ?", (nickname, req.player_id))

    avg_ms, best_ms = float(payload.avg_ms), float(payload.best_ms)
    prev_hash = last_entry_hash(conn)
    submitted_at_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    entry_hash = compute_entry_hash(prev_hash, req.canonical_payload, signature, submitted_at_str)

    conn.execute(
        """INSERT INTO scores (
               player_id, nickname, challenge_id, avg_ms, best_ms,
               rounds_json, wait_delays_json, binary_sha256, canonical_payload,
               ed25519_signature, attestation_hmac, submitted_at_str, prev_hash, entry_hash
           ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (
            req.player_id, nickname, req.challenge_id, avg_ms, best_ms,
            json.dumps([round(r, 3) for r in rounds]), json.dumps(expected_waits),
            binary_sha256, req.canonical_payload, signature,
            req.attestation_hmac.lower(), submitted_at_str, prev_hash, entry_hash,
        ),
    )

    return {
        "status": "verified",
        "avg_ms": round(avg_ms, 3),
        "best_ms": round(best_ms, 3),
        "prev_hash": prev_hash,
        "entry_hash": entry_hash,
    }
