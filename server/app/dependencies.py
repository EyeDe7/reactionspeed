"""FastAPI dependency providers (resolved from ``app.state``, so tests can inject their own)."""

from typing import Annotated

from fastapi import Depends, Header, Request

from app.config import Settings
from app.core.crypto import secrets_match
from app.db import Database
from app.exceptions import ForbiddenError
from app.services.ledger import LedgerAuditor
from app.services.statistics import DistributionCalculator


def get_settings(request: Request) -> Settings:
    return request.app.state.settings


def get_database(request: Request) -> Database:
    return request.app.state.db


def get_ledger(request: Request) -> LedgerAuditor:
    return request.app.state.ledger


def get_distribution(request: Request) -> DistributionCalculator:
    return request.app.state.distribution


SettingsDep = Annotated[Settings, Depends(get_settings)]
DatabaseDep = Annotated[Database, Depends(get_database)]
LedgerDep = Annotated[LedgerAuditor, Depends(get_ledger)]
DistributionDep = Annotated[DistributionCalculator, Depends(get_distribution)]


def require_admin(settings: SettingsDep, x_admin_secret: str = Header(default="")) -> None:
    if not secrets_match(settings.admin_secret, x_admin_secret):
        raise ForbiddenError("Invalid X-Admin-Secret")


AdminDep = Depends(require_admin)
