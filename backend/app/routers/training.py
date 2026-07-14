from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.database import get_db
from app.models import DrillSession, Match, Stroke, User
from app.schemas import DeviceDrillIn, DeviceMatchIn, DrillOut, MatchOut, StrokeOut
from app.utils import fmt_date_label, fmt_time

router = APIRouter(tags=["training"])


def match_out(m: Match) -> MatchOut:
    return MatchOut(
        id=str(m.id),
        title=m.title,
        dateLabel=fmt_date_label(m.started_at),
        durationMin=m.duration_min,
        strokeCount=m.stroke_count,
        avgScore=m.avg_score,
        opponentLabel=m.opponent_label,
    )


def stroke_out(s: Stroke) -> StrokeOut:
    return StrokeOut(
        id=str(s.id),
        actionTypeLabel=s.action_type_label,
        score=s.score,
        aiSuggestion=s.ai_suggestion,
        ballSpeedKmh=s.ball_speed_kmh,
        powerN=s.power_n,
        hitTimeLabel=fmt_time(s.hit_at),
    )


def drill_out(d: DrillSession) -> DrillOut:
    return DrillOut(
        id=str(d.id),
        actionType=d.action_type,
        score=d.score,
        aiSuggestion=d.ai_suggestion,
        ballSpeedKmh=d.ball_speed_kmh,
        powerN=d.power_n,
        dateTimeLabel=fmt_date_label(d.practiced_at),
    )


@router.get("/matches", response_model=list[MatchOut])
async def list_matches(user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    rows = await db.scalars(
        select(Match).where(Match.user_id == user.id).order_by(Match.started_at.desc())
    )
    return [match_out(m) for m in rows.all()]


@router.get("/matches/{match_id}/strokes", response_model=list[StrokeOut])
async def list_strokes(match_id: int, user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    match = await db.scalar(select(Match).where(Match.id == match_id, Match.user_id == user.id))
    if match is None:
        raise HTTPException(status_code=404, detail="比赛不存在")
    rows = await db.scalars(select(Stroke).where(Stroke.match_id == match.id).order_by(Stroke.seq))
    return [stroke_out(s) for s in rows.all()]


@router.get("/drills", response_model=list[DrillOut])
async def list_drills(
    action: str | None = None,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    stmt = select(DrillSession).where(DrillSession.user_id == user.id)
    if action:
        stmt = stmt.where(DrillSession.action_type == action)
    stmt = stmt.order_by(DrillSession.practiced_at.desc())
    rows = await db.scalars(stmt)
    return [drill_out(d) for d in rows.all()]


@router.post("/device/ingest/match")
async def ingest_match(body: DeviceMatchIn, db: AsyncSession = Depends(get_db)):
    user_id = None
    if body.user_phone:
        user = await db.scalar(select(User).where(User.phone == body.user_phone))
        user_id = user.id if user else None
    if user_id is None:
        user = await db.scalar(select(User).order_by(User.id))
        user_id = user.id if user else None
    if user_id is None:
        raise HTTPException(status_code=400, detail="请先注册至少一个用户")

    scores = [int(s.get("score", 0)) for s in body.strokes]
    avg = round(sum(scores) / len(scores)) if scores else 0
    match = Match(
        user_id=user_id,
        title=body.title,
        opponent_label=body.opponent_label,
        duration_min=body.duration_min,
        stroke_count=len(body.strokes),
        avg_score=avg,
    )
    db.add(match)
    await db.flush()
    for idx, s in enumerate(body.strokes, start=1):
        db.add(
            Stroke(
                match_id=match.id,
                seq=idx,
                action_type_label=str(s.get("action_type", s.get("action_type_label", "未知"))),
                score=int(s.get("score", 0)),
                ai_suggestion=str(s.get("ai_suggestion", "")),
                ball_speed_kmh=int(s.get("ball_speed_kmh", 0)),
                power_n=int(s.get("power_n", 0)),
            )
        )
    await db.commit()
    return {"ok": True, "matchId": str(match.id)}


@router.post("/device/ingest/drill")
async def ingest_drill(body: DeviceDrillIn, db: AsyncSession = Depends(get_db)):
    user_id = None
    if body.user_phone:
        user = await db.scalar(select(User).where(User.phone == body.user_phone))
        user_id = user.id if user else None
    if user_id is None:
        user = await db.scalar(select(User).order_by(User.id))
        user_id = user.id if user else None
    if user_id is None:
        raise HTTPException(status_code=400, detail="请先注册至少一个用户")

    row = DrillSession(
        user_id=user_id,
        action_type=body.action_type,
        score=body.score,
        ai_suggestion=body.ai_suggestion,
        ball_speed_kmh=body.ball_speed_kmh,
        power_n=body.power_n,
    )
    db.add(row)
    await db.commit()
    return {"ok": True, "drillId": str(row.id)}
