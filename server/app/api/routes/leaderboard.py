from fastapi import APIRouter

from app.constants import DEFAULT_PER_PAGE
from app.dependencies import DatabaseDep, DistributionDep, LedgerDep
from app.services import leaderboard

router = APIRouter(tags=["leaderboard"])


@router.get("/distribution")
def get_distribution(db: DatabaseDep, ledger: LedgerDep, distribution: DistributionDep) -> dict:
    with db.session() as conn:
        audit = ledger.audit(conn)
        dist = distribution.compute(conn, audit)
    return {
        "ledger_intact": audit.ledger_intact,
        "total_entries": audit.total_entries,
        "verified_entries": audit.verified_entries,
        "head_hash": audit.head_hash,
        "distribution": dist,
    }


@router.get("/leaderboard")
def get_leaderboard(
    db: DatabaseDep,
    ledger: LedgerDep,
    distribution: DistributionDep,
    avg_page: int = 1,
    single_page: int = 1,
    per_page: int = DEFAULT_PER_PAGE,
) -> dict:
    with db.session() as conn:
        audit = ledger.audit(conn)
        dist = distribution.compute(conn, audit)
        page = leaderboard.Pagination.build(audit.total_entries, avg_page, single_page, per_page)
        averages = leaderboard.top_averages(conn, audit, page)
        singles = leaderboard.top_singles(conn, audit, page)

    return {
        "ledger_intact": audit.ledger_intact,
        "total_entries": audit.total_entries,
        "verified_entries": audit.verified_entries,
        "head_hash": audit.head_hash,
        "per_page": page.per_page,
        "total_pages": page.total_pages,
        "avg_page": page.avg_page,
        "single_page": page.single_page,
        "top_averages": averages,
        "top_singles": singles,
        "friend_bests": leaderboard.friend_bests(dist),
        "distribution": dist,
        "audit_errors": audit.errors,
    }
