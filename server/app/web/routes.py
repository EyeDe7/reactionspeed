"""HTML dashboard, crypto architecture deep-dive, and client binary downloads."""

from pathlib import Path

from fastapi import APIRouter, Request
from fastapi.responses import FileResponse, HTMLResponse
from fastapi.templating import Jinja2Templates

from app import constants as c
from app.core.crypto import compute_entry_hash
from app.dependencies import DatabaseDep, DistributionDep, LedgerDep, SettingsDep
from app.exceptions import NotFoundError
from app.services import binaries, leaderboard

TEMPLATES_DIR = Path(__file__).resolve().parent.parent / "templates"
templates = Jinja2Templates(directory=str(TEMPLATES_DIR))

router = APIRouter(tags=["web"])


def _demo_sample_block() -> dict:
    """Fallback block shown on ``/crypto`` before any live score has been recorded."""
    prev_hash = c.GENESIS_HASH
    canonical_payload = (
        "v1|plr_8f42b1c9d03e7a11|DemoPlayer|chk_9a3f1e70d4b288c1e50219ab|"
        "4f8c2a91e07b3d6512a98c4e701b3f55|178.420|164.115|"
        "181.240,175.890,164.115,188.005,182.850|2140,3412,1895,4020,2765|"
        "571c03037ab8308726408f6ed37f80fecab24b569ce11a0eaf4e3ada2c17e8b4"
    )
    signature = (
        "9c47e12a8b03f65d1e9a4c2870b5d3f16a82c94e01b7d53f8a20e6c419b8d752"
        "3f10a8c65e29b4d710c8e3f5a291d4b680e7c2a51f93d8b460a2e5c197b3d804"
    )
    submitted_at = "2026-10-29 00:00:00"
    return {
        "is_live": False,
        "block_id": 1,
        "nickname": "DemoPlayer",
        "player_id": "plr_8f42b1c9d03e7a11",
        "public_key": "e4a9120bc7853d4f9102e6c8a47b53d109f2a8c46e17b39d502c8e4a71b9d306",
        "prev_hash": prev_hash,
        "canonical_payload": canonical_payload,
        "ed25519_signature": signature,
        "attestation_hmac": "7b21e8c409a5d3f162e8b4c901a7d5f38c20e4a619b7d350f8a2c4e610b9d735",
        "submitted_at_str": submitted_at,
        "entry_hash": compute_entry_hash(prev_hash, canonical_payload, signature, submitted_at),
    }


@router.get("/", response_class=HTMLResponse)
def web_dashboard(
    request: Request,
    settings: SettingsDep,
    db: DatabaseDep,
    ledger: LedgerDep,
    distribution: DistributionDep,
    avg_page: int = 1,
    single_page: int = 1,
    per_page: int = c.DEFAULT_PER_PAGE,
) -> HTMLResponse:
    with db.session() as conn:
        audit = ledger.audit(conn)
        dist_data = distribution.compute(conn, audit)
        page = leaderboard.Pagination.build(audit.total_entries, avg_page, single_page, per_page)
        averages = leaderboard.top_averages(conn, audit, page)
        singles = leaderboard.top_singles(conn, audit, page)
        chain_blocks = leaderboard.recent_blocks(conn, audit, limit=c.DASHBOARD_CHAIN_BLOCKS)
        allowed = binaries.list_allowed(conn)

    context = {
        "audit": audit,
        "strict_binary_check": settings.strict_binary_check,
        "total_scores": audit.total_entries,
        "per_page": page.per_page,
        "avg_page": page.avg_page,
        "single_page": page.single_page,
        "total_pages": page.total_pages,
        "top_averages": averages,
        "top_singles": singles,
        "chain_blocks": chain_blocks,
        "allowed_binaries": allowed,
        "dashboard_state": {
            "avg_page": page.avg_page,
            "single_page": page.single_page,
            "per_page": page.per_page,
            "total_pages": page.total_pages,
            "distribution": dist_data,
        },
    }
    return templates.TemplateResponse(request, "dashboard.html", context)


@router.get("/download/windows")
@router.get("/download/reaction_benchmark.exe")
def download_windows_binary(settings: SettingsDep) -> FileResponse:
    path = binaries.locate_client_binary(settings, c.PLATFORM_WINDOWS)
    if path is None:
        raise NotFoundError("Windows binary (reaction_benchmark.exe) not found in /app/binaries on server")
    return FileResponse(
        path,
        media_type="application/vnd.microsoft.portable-executable",
        filename=binaries.WINDOWS_FILENAME,
    )


@router.get("/download/linux")
@router.get("/download/reaction_benchmark_linux_x86_64")
def download_linux_binary(settings: SettingsDep) -> FileResponse:
    path = binaries.locate_client_binary(settings, c.PLATFORM_LINUX)
    if path is None:
        raise NotFoundError(
            "Linux binary (reaction_benchmark_linux_x86_64) not found in /app/binaries on server"
        )
    return FileResponse(
        path,
        media_type="application/octet-stream",
        filename=binaries.LINUX_FILENAME,
    )


@router.get("/download", response_class=HTMLResponse)
def web_download_page(request: Request, settings: SettingsDep, db: DatabaseDep) -> HTMLResponse:
    win = binaries.get_client_binary(settings, c.PLATFORM_WINDOWS)
    lin = binaries.get_client_binary(settings, c.PLATFORM_LINUX)
    with db.session() as conn:
        allowed_map = binaries.allowed_by_platform(conn)

    context = {
        "windows": win,
        "linux": lin,
        "win_sha": win.sha256 or allowed_map.get(binaries.WINDOWS_LABEL, "Not available"),
        "lin_sha": lin.sha256 or allowed_map.get(binaries.LINUX_LABEL, "Not available"),
    }
    return templates.TemplateResponse(request, "download.html", context)


@router.get("/crypto", response_class=HTMLResponse)
@router.get("/security", response_class=HTMLResponse)
@router.get("/how-it-works", response_class=HTMLResponse)
def web_crypto_architecture_page(request: Request, db: DatabaseDep) -> HTMLResponse:
    with db.session() as conn:
        row = leaderboard.latest_block(conn)
        allowed = binaries.list_allowed(conn)

    if row is not None:
        sample_block = {
            "is_live": True,
            "block_id": row["id"],
            "nickname": row["nickname"],
            "player_id": row["player_id"],
            "public_key": row["public_key"] or "N/A",
            "prev_hash": row["prev_hash"],
            "canonical_payload": row["canonical_payload"],
            "ed25519_signature": row["ed25519_signature"],
            "attestation_hmac": row["attestation_hmac"],
            "submitted_at_str": row["submitted_at_str"],
            "entry_hash": row["entry_hash"],
        }
    else:
        sample_block = _demo_sample_block()

    context = {
        "sample_block": sample_block,
        "allowed_binaries": allowed,
    }
    return templates.TemplateResponse(request, "crypto.html", context)
