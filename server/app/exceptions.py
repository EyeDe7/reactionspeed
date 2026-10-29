"""Domain errors raised by the service layer.

Services stay HTTP-agnostic; :func:`app.main.create_app` maps these onto JSON
responses of the form ``{"error": "<detail>"}`` (the format the C++ client expects).
"""


class ScoreboardError(Exception):
    status_code: int = 400

    def __init__(self, detail: str) -> None:
        super().__init__(detail)
        self.detail = detail


class BadRequestError(ScoreboardError):
    status_code = 400


class UnauthorizedError(ScoreboardError):
    status_code = 401


class ForbiddenError(ScoreboardError):
    status_code = 403


class NotFoundError(ScoreboardError):
    status_code = 404


class ConflictError(ScoreboardError):
    status_code = 409
