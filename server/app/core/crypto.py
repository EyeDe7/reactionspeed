"""Cryptographic primitives: Ed25519 signatures, hash-chain links, attestation HMACs."""

import hashlib
import hmac

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

ED25519_KEY_BYTES = 32


def verify_ed25519(public_key_hex: str, message: str, signature_hex: str) -> bool:
    """Return True when ``signature_hex`` is a valid Ed25519 signature of ``message``."""
    try:
        pub = Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_key_hex))
        pub.verify(bytes.fromhex(signature_hex), message.encode("utf-8"))
        return True
    except (ValueError, InvalidSignature):
        return False


def is_valid_ed25519_pubkey(public_key_hex: str) -> bool:
    try:
        raw = bytes.fromhex(public_key_hex)
        if len(raw) != ED25519_KEY_BYTES:
            return False
        Ed25519PublicKey.from_public_bytes(raw)
        return True
    except ValueError:
        return False


def derive_public_key_hex(private_key: Ed25519PrivateKey) -> str:
    return private_key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    ).hex()


def compute_entry_hash(prev_hash: str, canonical_payload: str, ed25519_sig: str, submitted_at_str: str) -> str:
    """SHA-256 link of the hash-chain ledger."""
    msg = f"{prev_hash}|{canonical_payload}|{ed25519_sig}|{submitted_at_str}"
    return hashlib.sha256(msg.encode("utf-8")).hexdigest()


def hash_api_token(api_token: str) -> str:
    """Only SHA-256(api_token) is ever stored server-side."""
    return hashlib.sha256(api_token.encode("utf-8")).hexdigest()


def tokens_match(expected_hash: str, presented_token: str) -> bool:
    return hmac.compare_digest(expected_hash, hash_api_token(presented_token))


def secrets_match(expected: str, presented: str) -> bool:
    return hmac.compare_digest(presented, expected)


def compute_attestation_hmac(
    protocol_secret: str,
    binary_sha256: str,
    challenge_nonce: str,
    api_token: str,
    canonical_payload: str,
) -> str:
    """Two-stage HMAC proving the payload was produced by an official client binary."""
    attest_key = hmac.new(
        protocol_secret.encode("utf-8"),
        f"{binary_sha256}:{challenge_nonce}:{api_token}".encode("utf-8"),
        hashlib.sha256,
    ).hexdigest()
    return hmac.new(
        attest_key.encode("utf-8"),
        canonical_payload.encode("utf-8"),
        hashlib.sha256,
    ).hexdigest()
