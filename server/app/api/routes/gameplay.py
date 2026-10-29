from fastapi import APIRouter, Header

from app.db import now_ms
from app.dependencies import DatabaseDep, SettingsDep
from app.schemas import ChallengeRequest, SubmitScoreRequest
from app.services import gameplay

router = APIRouter(tags=["gameplay"])


@router.post("/challenge")
def issue_challenge(
    req: ChallengeRequest, settings: SettingsDep, db: DatabaseDep, x_api_token: str = Header(default="")
) -> dict:
    with db.session() as conn:
        return gameplay.issue_challenge(conn, settings, req, x_api_token)


@router.post("/submit")
def submit_score(
    req: SubmitScoreRequest, settings: SettingsDep, db: DatabaseDep, x_api_token: str = Header(default="")
) -> dict:
    # Timestamp before waiting on the DB lock so queueing never counts toward elapsed time.
    received_at_ms = now_ms()
    with db.session() as conn:
        return gameplay.submit_score(conn, settings, req, x_api_token, received_at_ms)
