from fastapi import APIRouter

from app.dependencies import DatabaseDep, LedgerDep
from app.schemas import KeyAuditRequest
from app.services import ledger as ledger_service

router = APIRouter(prefix="/audit", tags=["audit"])


@router.get("/verify")
def verify_audit(db: DatabaseDep, ledger: LedgerDep) -> dict:
    with db.session() as conn:
        audit = ledger.audit(conn)
        entries = ledger_service.export_ledger(conn, audit)
    return {
        "ledger_intact": audit.ledger_intact,
        "total_entries": audit.total_entries,
        "verified_entries": audit.verified_entries,
        "head_hash": audit.head_hash,
        "errors": audit.errors,
        "entries": entries,
    }


@router.post("/verify-key")
def verify_with_key(req: KeyAuditRequest, db: DatabaseDep, ledger: LedgerDep) -> dict:
    """Audit a player's scores using either their Ed25519 private key seed or public key.

    Given a private seed (``private_seed_hex`` from ``reaction_config.ini``) the public
    key is derived, each canonical payload is re-signed, and stored signatures are
    compared byte-for-byte.
    """
    with db.session() as conn:
        audit = ledger.audit(conn)
        return ledger_service.verify_player_key(conn, audit, req.key_hex)
