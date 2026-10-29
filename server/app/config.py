"""Application settings, loaded from environment variables (Docker / TrueNAS)."""

from functools import lru_cache
from pathlib import Path

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

# Project root (the directory containing the ``app`` package); ``/app`` inside Docker.
BASE_DIR = Path(__file__).resolve().parent.parent

# Shared with the official C++ client binary; used to derive the per-submission
# attestation HMAC key. Changing it invalidates every deployed client.
DEFAULT_PROTOCOL_ATTESTATION_SECRET = "2220f17d38f1b75e33f68440f5bc7441d722a26b86c361bce2512c9ccc3383ad"
DEFAULT_ADMIN_SECRET = "CHANGE-ME-ADMIN-SECRET"


class Settings(BaseSettings):
    """Runtime configuration. Every field maps to an upper-case environment variable."""

    model_config = SettingsConfigDict(case_sensitive=False, extra="ignore")

    port: int = 8787
    db_path: Path = Path("/data/scoreboard.db")
    allowed_binaries_path: Path = Path("/app/allowed_binaries.json")
    binaries_dir: Path = Path("/app/binaries")

    invite_code: str = "FRIENDS-2026"
    admin_secret: str = DEFAULT_ADMIN_SECRET
    strict_binary_check: bool = True

    # Optional allowlist overrides; when either is set, they replace allowed_binaries.json.
    allowed_linux_sha256: str = ""
    allowed_windows_sha256: str = ""

    protocol_attestation_secret: str = Field(default=DEFAULT_PROTOCOL_ATTESTATION_SECRET, repr=False)

    @field_validator("strict_binary_check", mode="before")
    @classmethod
    def _parse_flag(cls, value: object) -> bool:
        # Mirror the legacy parser: only "1" / "true" / "yes" enable the flag.
        if isinstance(value, bool):
            return value
        return str(value).strip().lower() in ("1", "true", "yes")

    @field_validator("allowed_linux_sha256", "allowed_windows_sha256", mode="after")
    @classmethod
    def _normalize_sha(cls, value: str) -> str:
        return value.strip().lower()

    @property
    def env_allowed_binaries(self) -> dict[str, str]:
        """Allowlist entries supplied via env vars (only well-formed 64-char digests)."""
        candidates = {
            "linux_x86_64": self.allowed_linux_sha256,
            "windows_x86_64_exe": self.allowed_windows_sha256,
        }
        return {label: sha for label, sha in candidates.items() if len(sha) == 64}


@lru_cache
def get_settings() -> Settings:
    return Settings()
