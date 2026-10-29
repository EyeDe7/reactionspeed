#!/usr/bin/env python3
"""Compatibility entrypoint for ``uvicorn server:app`` and direct CLI execution.

All application logic lives in the :mod:`app` package.
"""

import uvicorn

from app.config import get_settings
from app.main import app, create_app

__all__ = ["app", "create_app"]


if __name__ == "__main__":
    settings = get_settings()
    uvicorn.run("app.main:app", host="0.0.0.0", port=settings.port, proxy_headers=True, forwarded_allow_ips="*")
