"""Client-binary allowlist management. Mutations require the ``X-Admin-Secret`` header."""

from fastapi import APIRouter

from app.dependencies import AdminDep, DatabaseDep, SettingsDep
from app.schemas import BinaryAllowRequest
from app.services import binaries

router = APIRouter(prefix="/admin/binaries", tags=["admin"])


@router.get("")
def list_binaries(settings: SettingsDep, db: DatabaseDep) -> dict:
    with db.session() as conn:
        rows = binaries.list_allowed(conn)
    return {
        "strict_binary_check": settings.strict_binary_check,
        "allowed_binaries": [
            {"sha256": r["sha256"], "platform": r["platform_label"], "added_at": r["added_at"]} for r in rows
        ],
    }


@router.post("", dependencies=[AdminDep])
def add_binary(req: BinaryAllowRequest, db: DatabaseDep) -> dict:
    sha = req.sha256.strip().lower()
    platform = req.platform.strip()
    with db.session() as conn:
        binaries.replace_platform_binary(conn, sha, platform)
    return {"status": "replaced", "sha256": sha, "platform": platform}


@router.delete("", dependencies=[AdminDep])
@router.post("/clear", dependencies=[AdminDep])
def clear_binaries(db: DatabaseDep, sha256: str = "") -> dict:
    with db.session() as conn:
        binaries.delete_allowed(conn, sha256 or None)
    return {"status": "deleted", "sha256": sha256 or "all"}
