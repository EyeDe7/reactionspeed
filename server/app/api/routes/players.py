from fastapi import APIRouter

from app.dependencies import DatabaseDep, LedgerDep
from app.services.players import get_player_profile

router = APIRouter(tags=["players"])


@router.get("/player/{player_id}")
def get_player(player_id: str, db: DatabaseDep, ledger: LedgerDep) -> dict:
    with db.session() as conn:
        audit = ledger.audit(conn)
        return get_player_profile(conn, audit, player_id)
