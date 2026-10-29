"""End-to-end tests for the refactored Reaction Speed Test scoreboard server."""

import json
import time
from pathlib import Path

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from fastapi.testclient import TestClient

from app.config import DEFAULT_PROTOCOL_ATTESTATION_SECRET, Settings
from app.core.canonical import format_rounds, format_waits
from app.core.crypto import compute_attestation_hmac, derive_public_key_hex
from app.main import create_app

TEST_BINARY_SHA = "d2b0d08fa1642c7bd0864664d02508b3ddd856a5deded5214c1965905b7952c4"
TEST_INVITE_CODE = "TEST-INVITE-2026"
TEST_ADMIN_SECRET = "TEST-ADMIN-SECRET"


@pytest.fixture()
def client(tmp_path: Path) -> TestClient:
    allowlist_file = tmp_path / "allowed_binaries.json"
    allowlist_file.write_text(json.dumps({"linux_x86_64": TEST_BINARY_SHA}), encoding="utf-8")

    binaries_dir = tmp_path / "binaries"
    binaries_dir.mkdir()
    (binaries_dir / "reaction_benchmark.exe").write_bytes(b"MZ-test-windows-binary")
    (binaries_dir / "reaction_benchmark_linux_x86_64").write_bytes(b"\x7fELF-test-linux-binary")

    settings = Settings(
        db_path=tmp_path / "scoreboard.db",
        allowed_binaries_path=allowlist_file,
        binaries_dir=binaries_dir,
        invite_code=TEST_INVITE_CODE,
        admin_secret=TEST_ADMIN_SECRET,
        strict_binary_check=True,
    )
    app = create_app(settings)
    with TestClient(app) as test_client:
        yield test_client


def _make_keypair() -> tuple[Ed25519PrivateKey, str, str]:
    seed = b"\x11" * 32
    priv = Ed25519PrivateKey.from_private_bytes(seed)
    return priv, seed.hex(), derive_public_key_hex(priv)


def _register_and_submit(
    client: TestClient,
    priv: Ed25519PrivateKey,
    pub_hex: str,
    nickname: str = "FastPlayer",
    rounds: list[float] | None = None,
) -> dict:
    rounds = rounds or [172.4, 168.1, 180.2, 175.9, 169.4]
    reg = client.post(
        "/api/v1/auth/register",
        json={
            "nickname": nickname,
            "invite_code": TEST_INVITE_CODE,
            "public_key": pub_hex,
            "binary_sha256": TEST_BINARY_SHA,
        },
    )
    assert reg.status_code == 200, reg.text
    reg_data = reg.json()
    player_id = reg_data["player_id"]
    api_token = reg_data["api_token"]

    ts = int(time.time())
    ch_msg = f"CHALLENGE|{player_id}|{ts}|{TEST_BINARY_SHA}"
    ch_sig = priv.sign(ch_msg.encode("utf-8")).hex()
    ch = client.post(
        "/api/v1/challenge",
        headers={"X-Api-Token": api_token},
        json={
            "player_id": player_id,
            "timestamp": ts,
            "binary_sha256": TEST_BINARY_SHA,
            "ed25519_signature": ch_sig,
        },
    )
    assert ch.status_code == 200, ch.text
    ch_data = ch.json()

    # Backdate issued_at_ms in the DB so the test doesn't have to sleep ~12 seconds.
    with client.app.state.db.session() as conn:
        conn.execute(
            "UPDATE challenges SET issued_at_ms = issued_at_ms - 30000 WHERE challenge_id = ?",
            (ch_data["challenge_id"],),
        )

    waits = ch_data["wait_delays_ms"]
    avg_ms = sum(rounds) / 5.0
    best_ms = min(rounds)
    canonical = (
        f"v1|{player_id}|{nickname}|{ch_data['challenge_id']}|{ch_data['challenge_nonce']}|"
        f"{avg_ms:.3f}|{best_ms:.3f}|{format_rounds(rounds)}|{format_waits(waits)}|{TEST_BINARY_SHA}"
    )
    ed_sig = priv.sign(canonical.encode("utf-8")).hex()
    attest_hmac = compute_attestation_hmac(
        DEFAULT_PROTOCOL_ATTESTATION_SECRET,
        TEST_BINARY_SHA,
        ch_data["challenge_nonce"],
        api_token,
        canonical,
    )

    sub = client.post(
        "/api/v1/submit",
        headers={"X-Api-Token": api_token},
        json={
            "player_id": player_id,
            "challenge_id": ch_data["challenge_id"],
            "avg_ms": avg_ms,
            "best_ms": best_ms,
            "rounds_ms": rounds,
            "wait_delays_ms": waits,
            "binary_sha256": TEST_BINARY_SHA,
            "canonical_payload": canonical,
            "ed25519_signature": ed_sig,
            "attestation_hmac": attest_hmac,
        },
    )
    return {
        "register": reg_data,
        "challenge": ch_data,
        "submit_response": sub,
    }


def test_health_and_empty_web_pages(client: TestClient) -> None:
    resp = client.get("/api/v1/health")
    assert resp.status_code == 200
    body = resp.json()
    assert body["status"] == "ok"
    assert body["ledger_intact"] is True
    assert body["total_verified_scores"] == 0

    for path in ("/", "/download", "/crypto", "/security", "/how-it-works"):
        page = client.get(path)
        assert page.status_code == 200
        assert "<!DOCTYPE html>" in page.text

    for asset in ("/static/css/dashboard.css", "/static/js/dashboard.js", "/static/js/crypto.js"):
        static_resp = client.get(asset)
        assert static_resp.status_code == 200

    assert client.get("/download/windows").status_code == 200
    assert client.get("/download/linux").status_code == 200


def test_full_signed_submission_and_key_audit(client: TestClient) -> None:
    priv, seed_hex, pub_hex = _make_keypair()
    out = _register_and_submit(client, priv, pub_hex, nickname="Ace<script>")
    sub = out["submit_response"]
    assert sub.status_code == 200, sub.text
    assert sub.json()["status"] == "verified"

    lb = client.get("/api/v1/leaderboard").json()
    assert lb["ledger_intact"] is True
    assert lb["total_entries"] == 1
    assert lb["top_averages"][0]["nickname"] == "Ace<script>"

    # Verify Jinja2 autoescaping prevents raw <script> injection on the dashboard.
    dash_html = client.get("/").text
    assert "Ace&lt;script&gt;" in dash_html
    assert "Ace<script>" not in dash_html

    # Audit with private seed re-signs and matches byte-for-byte.
    key_audit = client.post("/api/v1/audit/verify-key", json={"key_hex": seed_hex}).json()
    assert key_audit["found_player"] is True
    assert key_audit["verified_scores"][0]["private_key_resign_exact_match"] is True
    assert key_audit["verified_scores"][0]["in_verified_chain"] is True

    # Audit with public key also succeeds.
    pub_audit = client.post("/api/v1/audit/verify-key", json={"key_hex": pub_hex}).json()
    assert pub_audit["found_player"] is True
    assert pub_audit["verified_scores"][0]["public_key_verified"] is True

    # Player profile endpoint.
    pid = out["register"]["player_id"]
    prof = client.get(f"/api/v1/player/{pid}")
    assert prof.status_code == 200
    assert len(prof.json()["scores"]) == 1


def test_anti_cheat_and_admin_endpoints(client: TestClient) -> None:
    priv, _, pub_hex = _make_keypair()
    # Bot-floor reaction (< 25ms) must be rejected with 403.
    bad = _register_and_submit(client, priv, pub_hex, rounds=[10.0, 180.0, 185.0, 190.0, 195.0])
    assert bad["submit_response"].status_code == 403
    assert "25ms" in bad["submit_response"].json()["error"]

    # Admin binary management requires X-Admin-Secret.
    new_sha = "a" * 64
    unauth = client.post("/api/v1/admin/binaries", json={"sha256": new_sha, "platform": "custom"})
    assert unauth.status_code == 403

    auth_add = client.post(
        "/api/v1/admin/binaries",
        headers={"X-Admin-Secret": TEST_ADMIN_SECRET},
        json={"sha256": new_sha, "platform": "custom"},
    )
    assert auth_add.status_code == 200
    listed = client.get("/api/v1/admin/binaries").json()
    assert any(b["sha256"] == new_sha for b in listed["allowed_binaries"])
