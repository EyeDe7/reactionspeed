"""Aggregates every ``/api/v1`` router."""

from fastapi import APIRouter

from app.api.routes import admin, audit, auth, gameplay, health, leaderboard, players

api_router = APIRouter(prefix="/api/v1")
api_router.include_router(health.router)
api_router.include_router(auth.router)
api_router.include_router(gameplay.router)
api_router.include_router(leaderboard.router)
api_router.include_router(audit.router)
api_router.include_router(players.router)
api_router.include_router(admin.router)
