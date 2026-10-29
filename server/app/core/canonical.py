"""Parsing and formatting of the signed canonical score payload.

Format (pipe-separated, 10 fields)::

    v1|player_id|nickname|challenge_id|nonce|avg_ms|best_ms|r1,..,r5|w1,..,w5|binary_sha256
"""

from collections.abc import Iterable
from dataclasses import dataclass

from app.constants import CANONICAL_FIELD_COUNT, CANONICAL_VERSION


class CanonicalPayloadError(ValueError):
    """Raised when a canonical payload string is structurally invalid."""


@dataclass(frozen=True, slots=True)
class CanonicalPayload:
    player_id: str
    nickname: str
    challenge_id: str
    nonce: str
    avg_ms: str
    best_ms: str
    rounds_csv: str
    waits_csv: str
    binary_sha256: str

    @classmethod
    def parse(cls, raw: str) -> "CanonicalPayload":
        parts = raw.split("|")
        if len(parts) != CANONICAL_FIELD_COUNT or parts[0] != CANONICAL_VERSION:
            raise CanonicalPayloadError("Invalid canonical payload format")
        return cls(*parts[1:])


def format_rounds(rounds: Iterable[float]) -> str:
    return ",".join(f"{float(r):.3f}" for r in rounds)


def format_waits(waits: Iterable[int]) -> str:
    return ",".join(str(int(w)) for w in waits)
