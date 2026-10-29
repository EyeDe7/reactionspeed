"""Official client binaries: discovery on disk and the SHA-256 allowlist."""

import hashlib
import json
import logging
import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path

from app.config import BASE_DIR, Settings
from app.constants import PLATFORM_WINDOWS
from app.db import Database

logger = logging.getLogger(__name__)

WINDOWS_FILENAME = "reaction_benchmark.exe"
LINUX_FILENAME = "reaction_benchmark_linux_x86_64"
WINDOWS_LABEL = "windows_x86_64_exe"
LINUX_LABEL = "linux_x86_64"


@dataclass(frozen=True, slots=True)
class ClientBinary:
    available: bool
    path: Path | None
    filename: str
    platform_label: str
    sha256: str | None
    size_kb: float


def _search_dirs(settings: Settings) -> list[Path]:
    return [
        settings.binaries_dir,
        BASE_DIR / "binaries",
        Path("/app/binaries"),
        Path("/data/binaries"),
        BASE_DIR,
        BASE_DIR.parent,
    ]


def locate_client_binary(settings: Settings, platform: str) -> Path | None:
    filenames = (
        [WINDOWS_FILENAME] if platform == PLATFORM_WINDOWS else [LINUX_FILENAME, "reaction_benchmark"]
    )
    for directory in _search_dirs(settings):
        for fname in filenames:
            candidate = directory / fname
            if candidate.is_file():
                return candidate
    return None


def get_client_binary(settings: Settings, platform: str) -> ClientBinary:
    is_windows = platform == PLATFORM_WINDOWS
    label = WINDOWS_LABEL if is_windows else LINUX_LABEL
    filename = WINDOWS_FILENAME if is_windows else LINUX_FILENAME

    path = locate_client_binary(settings, platform)
    if path is None:
        return ClientBinary(False, None, filename, label, None, 0.0)

    with path.open("rb") as fh:
        sha = hashlib.file_digest(fh, "sha256").hexdigest()
    size_kb = round(path.stat().st_size / 1024.0, 1)
    return ClientBinary(True, path, filename, label, sha, size_kb)


# ---------------------------------------------------------------------------
# Allowlist (allowed_binaries table)
# ---------------------------------------------------------------------------
def is_binary_allowed(conn: sqlite3.Connection, binary_sha256: str, strict: bool) -> bool:
    if not strict:
        return True
    total_allowed = conn.execute("SELECT COUNT(*) FROM allowed_binaries").fetchone()[0]
    if total_allowed == 0:
        return True
    row = conn.execute(
        "SELECT 1 FROM allowed_binaries WHERE sha256 = ?", (binary_sha256.lower(),)
    ).fetchone()
    return row is not None


def list_allowed(conn: sqlite3.Connection) -> list[sqlite3.Row]:
    return conn.execute("SELECT sha256, platform_label, added_at FROM allowed_binaries").fetchall()


def allowed_by_platform(conn: sqlite3.Connection) -> dict[str, str]:
    return {row["platform_label"]: row["sha256"] for row in list_allowed(conn)}


def _upsert(conn: sqlite3.Connection, sha256: str, label: str) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO allowed_binaries (sha256, platform_label, added_at) VALUES (?, ?, ?)",
        (sha256.lower(), label, int(time.time())),
    )


def replace_platform_binary(conn: sqlite3.Connection, sha256: str, platform: str) -> None:
    conn.execute("DELETE FROM allowed_binaries WHERE platform_label = ?", (platform,))
    _upsert(conn, sha256, platform)


def delete_allowed(conn: sqlite3.Connection, sha256: str | None) -> None:
    if sha256:
        conn.execute("DELETE FROM allowed_binaries WHERE sha256 = ?", (sha256.strip().lower(),))
    else:
        conn.execute("DELETE FROM allowed_binaries")


def _load_allowlist_file(path: Path) -> dict[str, str] | None:
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        logger.warning("Could not load %s: %s", path, exc)
        return None
    if not isinstance(data, dict):
        logger.warning("Could not load %s: expected a JSON object of {label: sha256}", path)
        return None
    return {label: sha for label, sha in data.items() if isinstance(sha, str) and len(sha) == 64}


def sync_allowlist(db: Database, settings: Settings) -> None:
    """Rebuild the allowlist at startup.

    Precedence: ``ALLOWED_*_SHA256`` env vars replace ``allowed_binaries.json``;
    any official binaries hosted for download are always whitelisted on top.
    """
    file_entries = _load_allowlist_file(settings.allowed_binaries_path)
    env_entries = settings.env_allowed_binaries
    base_entries = env_entries or file_entries

    hosted = [get_client_binary(settings, plat) for plat in ("windows", "linux")]

    with db.session() as conn:
        if base_entries is not None:
            conn.execute("DELETE FROM allowed_binaries")
            for label, sha in base_entries.items():
                _upsert(conn, sha, label)
        for binary in hosted:
            if binary.available and binary.sha256:
                _upsert(conn, binary.sha256, binary.platform_label)
