"""FastAPI application factory and lifespan hooks."""

from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

from app import __version__
from app.api.router import api_router
from app.config import Settings, get_settings
from app.db import Database
from app.exceptions import ScoreboardError
from app.services.binaries import sync_allowlist
from app.services.ledger import LedgerAuditor
from app.services.statistics import DistributionCalculator
from app.web import routes as web_routes

STATIC_DIR = Path(__file__).resolve().parent / "static"


def _attach_state(application: FastAPI, settings: Settings) -> None:
    application.state.settings = settings
    application.state.db = Database(settings.db_path)
    application.state.ledger = LedgerAuditor()
    application.state.distribution = DistributionCalculator()


@asynccontextmanager
async def lifespan(application: FastAPI) -> AsyncIterator[None]:
    settings: Settings = application.state.settings
    db: Database = application.state.db
    db.init_schema()
    sync_allowlist(db, settings)
    yield


def create_app(settings: Settings | None = None) -> FastAPI:
    resolved = settings or get_settings()
    application = FastAPI(
        title="Reaction Speed Test — Verified Scoreboard API",
        version=__version__,
        description=(
            "Private Friends Scoreboard with Ed25519 Asymmetric Signatures, "
            "Binary Attestation, and Hash-Chain Ledger."
        ),
        lifespan=lifespan,
    )
    _attach_state(application, resolved)

    @application.exception_handler(ScoreboardError)
    async def handle_domain_error(_request: Request, exc: ScoreboardError) -> JSONResponse:
        return JSONResponse(status_code=exc.status_code, content={"error": exc.detail})

    @application.exception_handler(HTTPException)
    async def handle_http_exception(_request: Request, exc: HTTPException) -> JSONResponse:
        return JSONResponse(status_code=exc.status_code, content={"error": str(exc.detail)})

    application.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")
    application.include_router(api_router)
    application.include_router(web_routes.router)
    return application


app = create_app()
