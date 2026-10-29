"""SQLite connection management and schema initialisation."""

import sqlite3
import threading
import time
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

from app.constants import CHALLENGE_PRUNE_STARTUP_GRACE_MS

_PRAGMAS = (
    "PRAGMA journal_mode = WAL",
    "PRAGMA synchronous = NORMAL",
    "PRAGMA temp_store = MEMORY",
    "PRAGMA cache_size = -32000",
    "PRAGMA mmap_size = 67108864",
    "PRAGMA busy_timeout = 5000",
    "PRAGMA foreign_keys = ON",
)

_SCHEMA = (
    """
    CREATE TABLE IF NOT EXISTS players (
        player_id TEXT PRIMARY KEY,
        nickname TEXT UNIQUE NOT NULL,
        public_key TEXT NOT NULL,
        api_token_hash TEXT NOT NULL,
        registered_at INTEGER NOT NULL,
        last_binary_sha256 TEXT
    )
    """,
    """
    CREATE TABLE IF NOT EXISTS challenges (
        challenge_id TEXT PRIMARY KEY,
        player_id TEXT NOT NULL,
        challenge_nonce TEXT NOT NULL,
        wait_delays_json TEXT NOT NULL,
        issued_at_ms INTEGER NOT NULL,
        expires_at_ms INTEGER NOT NULL,
        used INTEGER NOT NULL DEFAULT 0
    )
    """,
    """
    CREATE TABLE IF NOT EXISTS scores (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        player_id TEXT NOT NULL,
        nickname TEXT NOT NULL,
        challenge_id TEXT UNIQUE NOT NULL,
        avg_ms REAL NOT NULL,
        best_ms REAL NOT NULL,
        rounds_json TEXT NOT NULL,
        wait_delays_json TEXT NOT NULL,
        binary_sha256 TEXT NOT NULL,
        canonical_payload TEXT NOT NULL,
        ed25519_signature TEXT NOT NULL,
        attestation_hmac TEXT NOT NULL,
        submitted_at_str TEXT NOT NULL,
        prev_hash TEXT NOT NULL,
        entry_hash TEXT NOT NULL
    )
    """,
    """
    CREATE TABLE IF NOT EXISTS allowed_binaries (
        sha256 TEXT PRIMARY KEY,
        platform_label TEXT NOT NULL,
        added_at INTEGER NOT NULL
    )
    """,
    # Indexes for fast leaderboard sorting, pagination, player lookups, and challenge pruning
    "CREATE INDEX IF NOT EXISTS idx_players_pubkey ON players (public_key)",
    "CREATE INDEX IF NOT EXISTS idx_scores_avg_ms ON scores (avg_ms ASC, id ASC)",
    "CREATE INDEX IF NOT EXISTS idx_scores_best_ms ON scores (best_ms ASC, avg_ms ASC, id ASC)",
    "CREATE INDEX IF NOT EXISTS idx_scores_player_id ON scores (player_id, id DESC)",
    "CREATE INDEX IF NOT EXISTS idx_challenges_expires ON challenges (expires_at_ms, used)",
)


def now_ms() -> int:
    return int(time.time() * 1000)


class Database:
    """Owns the SQLite path and the process-wide write lock.

    All access goes through :meth:`session`, which serialises callers (SQLite has a
    single writer, and the hash-chain append must be atomic), commits on success,
    rolls back on error, and always closes the connection.
    """

    def __init__(self, path: Path) -> None:
        self.path = Path(path)
        self._lock = threading.Lock()

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.path, timeout=10.0)
        conn.row_factory = sqlite3.Row
        for pragma in _PRAGMAS:
            conn.execute(pragma)
        return conn

    @contextmanager
    def session(self) -> Iterator[sqlite3.Connection]:
        with self._lock:
            conn = self._connect()
            try:
                yield conn
                conn.commit()
            except BaseException:
                conn.rollback()
                raise
            finally:
                conn.close()

    def init_schema(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.session() as conn:
            for statement in _SCHEMA:
                conn.execute(statement)
            conn.execute(
                "DELETE FROM challenges WHERE expires_at_ms < ? OR used = 1",
                (now_ms() - CHALLENGE_PRUNE_STARTUP_GRACE_MS,),
            )
            conn.execute("PRAGMA optimize")
