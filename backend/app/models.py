from datetime import datetime

from sqlalchemy import DateTime, ForeignKey, Integer, String, Text, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.database import Base


class User(Base):
    __tablename__ = "users"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    phone: Mapped[str] = mapped_column(String(20), unique=True, index=True)
    password_hash: Mapped[str] = mapped_column(String(255))
    display_name: Mapped[str] = mapped_column(String(64))
    role: Mapped[str] = mapped_column(String(20), default="personal")
    avatar_color: Mapped[int] = mapped_column(Integer, default=0xFF1565C0)
    avatar_url: Mapped[str | None] = mapped_column(String(512), nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    posts: Mapped[list["Post"]] = relationship(back_populates="author")
    coached_classes: Mapped[list["TrainingClass"]] = relationship(back_populates="coach")
    class_memberships: Mapped[list["ClassMember"]] = relationship(back_populates="user")


class Match(Base):
    __tablename__ = "matches"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), index=True)
    title: Mapped[str] = mapped_column(String(128))
    opponent_label: Mapped[str] = mapped_column(String(64), default="对打伙伴")
    duration_min: Mapped[int] = mapped_column(Integer, default=0)
    stroke_count: Mapped[int] = mapped_column(Integer, default=0)
    avg_score: Mapped[int] = mapped_column(Integer, default=0)
    started_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    strokes: Mapped[list["Stroke"]] = relationship(back_populates="match", cascade="all, delete-orphan")


class Stroke(Base):
    __tablename__ = "strokes"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    match_id: Mapped[int] = mapped_column(ForeignKey("matches.id"), index=True)
    seq: Mapped[int] = mapped_column(Integer, default=1)
    action_type_label: Mapped[str] = mapped_column(String(32))
    score: Mapped[int] = mapped_column(Integer)
    ai_suggestion: Mapped[str] = mapped_column(Text, default="")
    ball_speed_kmh: Mapped[int] = mapped_column(Integer, default=0)
    power_n: Mapped[int] = mapped_column(Integer, default=0)
    hit_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    match: Mapped["Match"] = relationship(back_populates="strokes")


class DrillSession(Base):
    __tablename__ = "drill_sessions"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), index=True)
    action_type: Mapped[str] = mapped_column(String(32), index=True)
    score: Mapped[int] = mapped_column(Integer)
    ai_suggestion: Mapped[str] = mapped_column(Text, default="")
    ball_speed_kmh: Mapped[int] = mapped_column(Integer, default=0)
    power_n: Mapped[int] = mapped_column(Integer, default=0)
    practiced_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())


class Post(Base):
    __tablename__ = "posts"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), index=True)
    content: Mapped[str] = mapped_column(Text)
    attachment_kind: Mapped[str] = mapped_column(String(32), default="none")
    image_url: Mapped[str | None] = mapped_column(String(512), nullable=True)
    image_caption: Mapped[str | None] = mapped_column(String(256), nullable=True)
    stats_title: Mapped[str | None] = mapped_column(String(128), nullable=True)
    stats_detail: Mapped[str | None] = mapped_column(Text, nullable=True)
    like_count: Mapped[int] = mapped_column(Integer, default=0)
    comment_count: Mapped[int] = mapped_column(Integer, default=0)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    author: Mapped["User"] = relationship(back_populates="posts")
    comments: Mapped[list["PostComment"]] = relationship(
        back_populates="post", cascade="all, delete-orphan"
    )


class PostComment(Base):
    __tablename__ = "post_comments"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    post_id: Mapped[int] = mapped_column(ForeignKey("posts.id"), index=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), index=True)
    content: Mapped[str] = mapped_column(Text)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    post: Mapped["Post"] = relationship(back_populates="comments")
    author: Mapped["User"] = relationship()


class TrainingClass(Base):
    __tablename__ = "training_classes"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    coach_id: Mapped[int] = mapped_column(ForeignKey("users.id"), index=True)
    name: Mapped[str] = mapped_column(String(64))
    description: Mapped[str] = mapped_column(Text, default="")
    invite_code: Mapped[str] = mapped_column(String(12), unique=True, index=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    coach: Mapped["User"] = relationship(back_populates="coached_classes")
    members: Mapped[list["ClassMember"]] = relationship(
        back_populates="training_class", cascade="all, delete-orphan"
    )


class ClassMember(Base):
    __tablename__ = "class_members"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    class_id: Mapped[int] = mapped_column(ForeignKey("training_classes.id"), index=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), index=True)
    joined_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

    training_class: Mapped["TrainingClass"] = relationship(back_populates="members")
    user: Mapped["User"] = relationship(back_populates="class_memberships")
