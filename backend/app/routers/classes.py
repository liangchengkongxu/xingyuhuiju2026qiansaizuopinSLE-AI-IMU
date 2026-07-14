"""教练班级管理：创建班级、添加学员、查看学员训练数据。"""

from __future__ import annotations

import secrets

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.auth import get_current_user
from app.database import get_db
from app.models import ClassMember, DrillSession, Match, Stroke, TrainingClass, User
from app.schemas import (
    AddMemberIn,
    ClassCreateIn,
    ClassDetailOut,
    ClassJoinedOut,
    ClassOut,
    ClassUpdateIn,
    ClassmateOut,
    DrillOut,
    JoinClassIn,
    MatchOut,
    StrokeOut,
    StudentClassDetailOut,
    StudentSummaryOut,
)
from app.utils import fmt_date_label, fmt_relative, fmt_time

router = APIRouter(prefix="/classes", tags=["classes"])


def _gen_invite_code() -> str:
    return secrets.token_hex(3).upper()


def _mask_phone(phone: str) -> str:
    if len(phone) >= 11:
        return f"{phone[:3]}****{phone[-4:]}"
    return phone


async def _require_teacher(user: User) -> None:
    if user.role != "teacher":
        raise HTTPException(status_code=403, detail="仅教练账号可使用班级功能")


async def _get_owned_class(class_id: int, coach: User, db: AsyncSession) -> TrainingClass:
    training_class = await db.scalar(
        select(TrainingClass).where(TrainingClass.id == class_id, TrainingClass.coach_id == coach.id)
    )
    if training_class is None:
        raise HTTPException(status_code=404, detail="班级不存在")
    return training_class


async def _student_summary(db: AsyncSession, member: User) -> StudentSummaryOut:
    match_count = await db.scalar(
        select(func.count()).select_from(Match).where(Match.user_id == member.id)
    ) or 0
    drill_count = await db.scalar(
        select(func.count()).select_from(DrillSession).where(DrillSession.user_id == member.id)
    ) or 0
    avg_score = await db.scalar(
        select(func.avg(Match.avg_score)).where(Match.user_id == member.id)
    )
    max_speed = await db.scalar(
        select(func.max(Stroke.ball_speed_kmh))
        .join(Match, Match.id == Stroke.match_id)
        .where(Match.user_id == member.id)
    )
    if max_speed is None:
        max_speed = await db.scalar(
            select(func.max(DrillSession.ball_speed_kmh)).where(DrillSession.user_id == member.id)
        )
    last_match = await db.scalar(
        select(Match.started_at).where(Match.user_id == member.id).order_by(Match.started_at.desc()).limit(1)
    )
    last_drill = await db.scalar(
        select(DrillSession.practiced_at)
        .where(DrillSession.user_id == member.id)
        .order_by(DrillSession.practiced_at.desc())
        .limit(1)
    )
    last_at = None
    if last_match and last_drill:
        last_at = max(last_match, last_drill)
    else:
        last_at = last_match or last_drill

    return StudentSummaryOut(
        userId=str(member.id),
        displayName=member.display_name,
        phone=_mask_phone(member.phone),
        avatarColorHex=member.avatar_color,
        avatarUrl=member.avatar_url,
        matchCount=match_count,
        drillCount=drill_count,
        avgScore=int(avg_score or 0),
        maxBallSpeed=int(max_speed or 0),
        lastActiveLabel=fmt_relative(last_at) if last_at else "暂无记录",
    )


def _class_out(training_class: TrainingClass, member_count: int) -> ClassOut:
    return ClassOut(
        id=str(training_class.id),
        name=training_class.name,
        description=training_class.description,
        inviteCode=training_class.invite_code,
        memberCount=member_count,
        createdLabel=fmt_date_label(training_class.created_at),
    )


def _match_out(m: Match) -> MatchOut:
    return MatchOut(
        id=str(m.id),
        title=m.title,
        dateLabel=fmt_date_label(m.started_at),
        durationMin=m.duration_min,
        strokeCount=m.stroke_count,
        avgScore=m.avg_score,
        opponentLabel=m.opponent_label,
    )


def _stroke_out(s: Stroke) -> StrokeOut:
    return StrokeOut(
        id=str(s.id),
        actionTypeLabel=s.action_type_label,
        score=s.score,
        aiSuggestion=s.ai_suggestion,
        ballSpeedKmh=s.ball_speed_kmh,
        powerN=s.power_n,
        hitTimeLabel=fmt_time(s.hit_at),
    )


def _drill_out(d: DrillSession) -> DrillOut:
    return DrillOut(
        id=str(d.id),
        actionType=d.action_type,
        score=d.score,
        aiSuggestion=d.ai_suggestion,
        ballSpeedKmh=d.ball_speed_kmh,
        powerN=d.power_n,
        dateTimeLabel=fmt_date_label(d.practiced_at),
    )


async def _verify_class_student(
    class_id: int, student_id: int, coach: User, db: AsyncSession
) -> tuple[TrainingClass, User]:
    training_class = await _get_owned_class(class_id, coach, db)
    member = await db.scalar(
        select(ClassMember).where(ClassMember.class_id == class_id, ClassMember.user_id == student_id)
    )
    if member is None:
        raise HTTPException(status_code=404, detail="该学员不在本班")
    student = await db.get(User, student_id)
    if student is None:
        raise HTTPException(status_code=404, detail="学员不存在")
    return training_class, student


async def _verify_class_member(
    class_id: int, user: User, db: AsyncSession
) -> tuple[TrainingClass, ClassMember]:
    membership = await db.scalar(
        select(ClassMember).where(ClassMember.class_id == class_id, ClassMember.user_id == user.id)
    )
    if membership is None:
        raise HTTPException(status_code=404, detail="你不在该班级中")
    training_class = await db.get(TrainingClass, class_id)
    if training_class is None:
        raise HTTPException(status_code=404, detail="班级不存在")
    return training_class, membership


@router.get("", response_model=list[ClassOut])
async def list_classes(user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    await _require_teacher(user)
    rows = await db.scalars(
        select(TrainingClass).where(TrainingClass.coach_id == user.id).order_by(TrainingClass.created_at.desc())
    )
    result: list[ClassOut] = []
    for training_class in rows.all():
        count = await db.scalar(
            select(func.count()).select_from(ClassMember).where(ClassMember.class_id == training_class.id)
        ) or 0
        result.append(_class_out(training_class, count))
    return result


@router.get("/joined", response_model=list[ClassJoinedOut])
async def list_joined_classes(user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    memberships = (
        await db.scalars(
            select(ClassMember)
            .options(
                selectinload(ClassMember.training_class).selectinload(TrainingClass.coach)
            )
            .where(ClassMember.user_id == user.id)
            .order_by(ClassMember.joined_at.desc())
        )
    ).all()
    result: list[ClassJoinedOut] = []
    for membership in memberships:
        training_class = membership.training_class
        coach = training_class.coach
        count = await db.scalar(
            select(func.count()).select_from(ClassMember).where(ClassMember.class_id == training_class.id)
        ) or 0
        result.append(
            ClassJoinedOut(
                id=str(training_class.id),
                name=training_class.name,
                description=training_class.description,
                coachName=coach.display_name if coach else "教练",
                memberCount=count,
                joinedLabel=fmt_date_label(membership.joined_at),
            )
        )
    return result


@router.post("/join", response_model=ClassJoinedOut)
async def join_class(body: JoinClassIn, user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    if user.role == "teacher":
        raise HTTPException(status_code=400, detail="教练请使用教练端管理班级")
    code = body.inviteCode.strip().upper()
    training_class = await db.scalar(
        select(TrainingClass).options(selectinload(TrainingClass.coach)).where(TrainingClass.invite_code == code)
    )
    if training_class is None:
        raise HTTPException(status_code=404, detail="邀请码无效")
    existing = await db.scalar(
        select(ClassMember).where(ClassMember.class_id == training_class.id, ClassMember.user_id == user.id)
    )
    if not existing:
        db.add(ClassMember(class_id=training_class.id, user_id=user.id))
        await db.commit()
        membership = await db.scalar(
            select(ClassMember).where(ClassMember.class_id == training_class.id, ClassMember.user_id == user.id)
        )
    else:
        membership = existing
    count = await db.scalar(
        select(func.count()).select_from(ClassMember).where(ClassMember.class_id == training_class.id)
    ) or 0
    coach = await db.get(User, training_class.coach_id)
    return ClassJoinedOut(
        id=str(training_class.id),
        name=training_class.name,
        description=training_class.description,
        coachName=coach.display_name if coach else "教练",
        memberCount=count,
        joinedLabel=fmt_date_label(membership.joined_at if membership else training_class.created_at),
    )


@router.post("", response_model=ClassOut)
async def create_class(
    body: ClassCreateIn,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    for _ in range(5):
        code = _gen_invite_code()
        exists = await db.scalar(select(TrainingClass.id).where(TrainingClass.invite_code == code))
        if not exists:
            break
    else:
        raise HTTPException(status_code=500, detail="邀请码生成失败")

    training_class = TrainingClass(
        coach_id=user.id,
        name=body.name.strip(),
        description=body.description.strip(),
        invite_code=code,
    )
    db.add(training_class)
    await db.commit()
    await db.refresh(training_class)
    return _class_out(training_class, 0)


@router.get("/{class_id}", response_model=ClassDetailOut)
async def get_class(class_id: int, user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    await _require_teacher(user)
    training_class = await db.scalar(
        select(TrainingClass)
        .options(selectinload(TrainingClass.members).selectinload(ClassMember.user))
        .where(TrainingClass.id == class_id, TrainingClass.coach_id == user.id)
    )
    if training_class is None:
        raise HTTPException(status_code=404, detail="班级不存在")

    members_out: list[StudentSummaryOut] = []
    for membership in training_class.members:
        if membership.user:
            members_out.append(await _student_summary(db, membership.user))

    return ClassDetailOut(
        id=str(training_class.id),
        name=training_class.name,
        description=training_class.description,
        inviteCode=training_class.invite_code,
        coachName=user.display_name,
        memberCount=len(members_out),
        createdLabel=fmt_date_label(training_class.created_at),
        members=members_out,
    )


@router.put("/{class_id}", response_model=ClassOut)
async def update_class(
    class_id: int,
    body: ClassUpdateIn,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    training_class = await _get_owned_class(class_id, user, db)
    if body.name is not None:
        training_class.name = body.name.strip()
    if body.description is not None:
        training_class.description = body.description.strip()
    await db.commit()
    await db.refresh(training_class)
    count = await db.scalar(
        select(func.count()).select_from(ClassMember).where(ClassMember.class_id == training_class.id)
    ) or 0
    return _class_out(training_class, count)


@router.delete("/{class_id}")
async def delete_class(class_id: int, user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    await _require_teacher(user)
    training_class = await _get_owned_class(class_id, user, db)
    await db.delete(training_class)
    await db.commit()
    return {"ok": True}


@router.post("/{class_id}/members", response_model=StudentSummaryOut)
async def add_member(
    class_id: int,
    body: AddMemberIn,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    await _get_owned_class(class_id, user, db)
    phone = body.phone.strip()
    student = await db.scalar(select(User).where(User.phone == phone))
    if student is None:
        raise HTTPException(status_code=404, detail="未找到该手机号用户，请学员先注册 App")
    if student.id == user.id:
        raise HTTPException(status_code=400, detail="不能将自己加入班级")
    existing = await db.scalar(
        select(ClassMember).where(ClassMember.class_id == class_id, ClassMember.user_id == student.id)
    )
    if existing:
        raise HTTPException(status_code=400, detail="该学员已在班级中")
    db.add(ClassMember(class_id=class_id, user_id=student.id))
    await db.commit()
    return await _student_summary(db, student)


@router.delete("/{class_id}/members/{student_id}")
async def remove_member(
    class_id: int,
    student_id: int,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    await _get_owned_class(class_id, user, db)
    membership = await db.scalar(
        select(ClassMember).where(ClassMember.class_id == class_id, ClassMember.user_id == student_id)
    )
    if membership is None:
        raise HTTPException(status_code=404, detail="该学员不在本班")
    await db.delete(membership)
    await db.commit()
    return {"ok": True}


@router.get("/{class_id}/student-view", response_model=StudentClassDetailOut)
async def student_class_view(
    class_id: int, user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)
):
    training_class, _ = await _verify_class_member(class_id, user, db)
    coach = await db.get(User, training_class.coach_id)
    memberships = (
        await db.scalars(
            select(ClassMember)
            .options(selectinload(ClassMember.user))
            .where(ClassMember.class_id == class_id)
        )
    ).all()
    classmates_raw: list[tuple[User, StudentSummaryOut]] = []
    for membership in memberships:
        if membership.user:
            classmates_raw.append((membership.user, await _student_summary(db, membership.user)))
    classmates_raw.sort(key=lambda item: (-item[1].avgScore, -item[1].maxBallSpeed))
    my_stats = next((summary for member, summary in classmates_raw if member.id == user.id), None)
    if my_stats is None:
        my_stats = await _student_summary(db, user)
    my_rank = 1
    for idx, (_, summary) in enumerate(classmates_raw, start=1):
        if summary.userId == str(user.id):
            my_rank = idx
            break
    classmates = [
        ClassmateOut(
            userId=summary.userId,
            displayName=summary.displayName,
            avatarColorHex=summary.avatarColorHex,
            avatarUrl=summary.avatarUrl,
            avgScore=summary.avgScore,
            maxBallSpeed=summary.maxBallSpeed,
            isMe=summary.userId == str(user.id),
        )
        for _, summary in classmates_raw
    ]
    return StudentClassDetailOut(
        id=str(training_class.id),
        name=training_class.name,
        description=training_class.description,
        coachName=coach.display_name if coach else "教练",
        memberCount=len(classmates),
        myRank=my_rank,
        myStats=my_stats,
        classmates=classmates,
    )


@router.delete("/{class_id}/leave")
async def leave_class(class_id: int, user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    _, membership = await _verify_class_member(class_id, user, db)
    await db.delete(membership)
    await db.commit()
    return {"ok": True}


@router.get("/{class_id}/students/{student_id}", response_model=StudentSummaryOut)
async def get_student_summary(
    class_id: int,
    student_id: int,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    _, student = await _verify_class_student(class_id, student_id, user, db)
    return await _student_summary(db, student)


@router.get("/{class_id}/students/{student_id}/matches", response_model=list[MatchOut])
async def list_student_matches(
    class_id: int,
    student_id: int,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    await _verify_class_student(class_id, student_id, user, db)
    rows = await db.scalars(
        select(Match).where(Match.user_id == student_id).order_by(Match.started_at.desc())
    )
    return [_match_out(m) for m in rows.all()]


@router.get("/{class_id}/students/{student_id}/matches/{match_id}/strokes", response_model=list[StrokeOut])
async def list_student_strokes(
    class_id: int,
    student_id: int,
    match_id: int,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    await _verify_class_student(class_id, student_id, user, db)
    match = await db.scalar(select(Match).where(Match.id == match_id, Match.user_id == student_id))
    if match is None:
        raise HTTPException(status_code=404, detail="比赛不存在")
    rows = await db.scalars(select(Stroke).where(Stroke.match_id == match.id).order_by(Stroke.seq))
    return [_stroke_out(s) for s in rows.all()]


@router.get("/{class_id}/students/{student_id}/drills", response_model=list[DrillOut])
async def list_student_drills(
    class_id: int,
    student_id: int,
    action: str | None = None,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_teacher(user)
    await _verify_class_student(class_id, student_id, user, db)
    stmt = select(DrillSession).where(DrillSession.user_id == student_id)
    if action:
        stmt = stmt.where(DrillSession.action_type == action)
    stmt = stmt.order_by(DrillSession.practiced_at.desc())
    rows = await db.scalars(stmt)
    return [_drill_out(d) for d in rows.all()]
