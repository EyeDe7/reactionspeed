from fastapi import APIRouter

from app.dependencies import DatabaseDep, LedgerDep, SettingsDep

router = APIRouter(tags=["health"])


@router.get("/health")
def health(settings: SettingsDep, db: DatabaseDep, ledger: LedgerDep) -> dict:
    with db.session() as conn:
        audit = ledger.audit(conn)
    return {
        "status": "ok",
        "strict_binary_check": settings.strict_binary_check,
        "ledger_intact": audit.ledger_intact,
        "total_verified_scores": audit.verified_entries,
        "head_hash": audit.head_hash,
    }
