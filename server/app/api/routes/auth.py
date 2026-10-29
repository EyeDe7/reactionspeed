from fastapi import APIRouter

from app.dependencies import DatabaseDep, SettingsDep
from app.schemas import RegisterRequest
from app.services import players

router = APIRouter(prefix="/auth", tags=["auth"])


@router.post("/register")
def register_player(req: RegisterRequest, settings: SettingsDep, db: DatabaseDep) -> dict:
    with db.session() as conn:
        return players.register_player(conn, settings, req)
