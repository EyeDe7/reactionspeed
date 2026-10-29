"""Request bodies accepted by the public API (wire format used by the C++ client)."""

from pydantic import BaseModel, Field

from app.constants import NICKNAME_MAX_LEN, NICKNAME_MIN_LEN


class RegisterRequest(BaseModel):
    nickname: str = Field(..., min_length=NICKNAME_MIN_LEN, max_length=NICKNAME_MAX_LEN)
    invite_code: str
    public_key: str = Field(..., min_length=64, max_length=64)
    binary_sha256: str = Field(..., min_length=64, max_length=64)


class ChallengeRequest(BaseModel):
    player_id: str
    timestamp: int
    binary_sha256: str
    ed25519_signature: str


class SubmitScoreRequest(BaseModel):
    player_id: str
    challenge_id: str
    avg_ms: float
    best_ms: float
    rounds_ms: list[float]
    wait_delays_ms: list[int]
    binary_sha256: str
    canonical_payload: str
    ed25519_signature: str
    attestation_hmac: str


class BinaryAllowRequest(BaseModel):
    sha256: str = Field(..., min_length=64, max_length=64)
    platform: str = "custom"


class KeyAuditRequest(BaseModel):
    key_hex: str = Field(..., min_length=64, max_length=64)
