import os
import uuid
from pathlib import Path

from fastapi import APIRouter, Depends, File, HTTPException, UploadFile
from sqlalchemy import desc, func, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.auth import get_current_user
from app.config import settings
from app.database import get_db
from app.models import DrillSession, Match, Post, PostComment, Stroke, User
from app.schemas import CommentOut, CommunityUserOut, PostIn, PostOut, RankingEntryOut, UploadOut
from app.utils import fmt_relative

router = APIRouter(prefix="/community", tags=["community"])


def author_out(user: User) -> CommunityUserOut:
    return CommunityUserOut(
        id=str(user.id),
        nickname=user.display_name,
        avatarColorHex=user.avatar_color,
        avatarUrl=user.avatar_url,
    )


def post_out(post: Post, comment_count: int | None = None) -> PostOut:
    return PostOut(
        id=str(post.id),
        author=author_out(post.author),
        timeLabel=fmt_relative(post.created_at),
        content=post.content,
        attachmentKind=post.attachment_kind,
        imageUrl=post.image_url,
        imageCaption=post.image_caption,
        statsTitle=post.stats_title,
        statsDetail=post.stats_detail,
        likeCount=post.like_count,
        commentCount=comment_count if comment_count is not None else post.comment_count,
    )


async def _comment_count(db: AsyncSession, post_id: int) -> int:
    return await db.scalar(
        select(func.count()).select_from(PostComment).where(PostComment.post_id == post_id)
    ) or 0


async def _load_post(post_id: int, db: AsyncSession) -> Post | None:
    return await db.scalar(
        select(Post).options(selectinload(Post.author)).where(Post.id == post_id)
    )


@router.get("/posts", response_model=list[PostOut])
async def list_posts(db: AsyncSession = Depends(get_db)):
    rows = await db.scalars(
        select(Post).options(selectinload(Post.author)).order_by(desc(Post.created_at)).limit(50)
    )
    result: list[PostOut] = []
    for post in rows.all():
        count = await _comment_count(db, post.id)
        result.append(post_out(post, comment_count=count))
    return result


@router.get("/posts/{post_id}", response_model=PostOut)
async def get_post(post_id: int, db: AsyncSession = Depends(get_db)):
    post = await _load_post(post_id, db)
    if post is None:
        raise HTTPException(status_code=404, detail="动态不存在")
    count = await _comment_count(db, post.id)
    return post_out(post, comment_count=count)


@router.post("/posts", response_model=PostOut)
async def create_post(
    body: PostIn,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    if not body.content.strip():
        raise HTTPException(status_code=400, detail="内容不能为空")
    post = Post(
        user_id=user.id,
        content=body.content.strip(),
        attachment_kind=body.attachmentKind,
        image_url=body.imageUrl,
        image_caption=body.imageCaption,
        stats_title=body.statsTitle,
        stats_detail=body.statsDetail,
    )
    db.add(post)
    await db.commit()
    post = await _load_post(post.id, db)
    return post_out(post)


@router.delete("/posts/{post_id}")
async def delete_post(
    post_id: int,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    post = await db.get(Post, post_id)
    if post is None:
        raise HTTPException(status_code=404, detail="动态不存在")
    if post.user_id != user.id:
        raise HTTPException(status_code=403, detail="只能删除自己的动态")
    await db.delete(post)
    await db.commit()
    return {"ok": True}


@router.get("/posts/{post_id}/comments", response_model=list[CommentOut])
async def list_comments(post_id: int, db: AsyncSession = Depends(get_db)):
    post = await db.get(Post, post_id)
    if post is None:
        raise HTTPException(status_code=404, detail="动态不存在")
    rows = await db.scalars(
        select(PostComment)
        .options(selectinload(PostComment.author))
        .where(PostComment.post_id == post_id)
        .order_by(PostComment.created_at.asc())
    )
    return [
        CommentOut(
            id=str(c.id),
            author=author_out(c.author),
            content=c.content,
            timeLabel=fmt_relative(c.created_at),
        )
        for c in rows.all()
    ]


@router.get("/rankings", response_model=list[RankingEntryOut])
async def rankings(type: str = "ball_speed", db: AsyncSession = Depends(get_db)):
    if type == "drill_count":
        stmt = (
            select(User, func.count(DrillSession.id).label("value"))
            .join(DrillSession, DrillSession.user_id == User.id)
            .group_by(User.id)
            .order_by(desc("value"))
            .limit(20)
        )
    elif type == "score":
        stmt = (
            select(User, func.avg(DrillSession.score).label("value"))
            .join(DrillSession, DrillSession.user_id == User.id)
            .group_by(User.id)
            .order_by(desc("value"))
            .limit(20)
        )
    else:
        stmt = (
            select(User, func.max(Stroke.ball_speed_kmh).label("value"))
            .join(Match, Match.user_id == User.id)
            .join(Stroke, Stroke.match_id == Match.id)
            .group_by(User.id)
            .order_by(desc("value"))
            .limit(20)
        )
    rows = (await db.execute(stmt)).all()
    result = []
    for idx, (user, value) in enumerate(rows, start=1):
        val = int(value or 0)
        result.append(RankingEntryOut(rank=idx, user=author_out(user), value=val))
    return result


@router.post("/upload", response_model=UploadOut)
async def upload_image(
    file: UploadFile = File(...),
    user: User = Depends(get_current_user),
):
    upload_dir = Path(settings.upload_dir)
    upload_dir.mkdir(parents=True, exist_ok=True)
    suffix = Path(file.filename or "image.jpg").suffix or ".jpg"
    name = f"{uuid.uuid4().hex}{suffix}"
    path = upload_dir / name
    content = await file.read()
    path.write_bytes(content)
    return UploadOut(url=f"/uploads/{name}")