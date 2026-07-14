from collections.abc import AsyncGenerator

from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase

from app.config import settings

engine = create_async_engine(settings.database_url, echo=False)
SessionLocal = async_sessionmaker(engine, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


async def get_db() -> AsyncGenerator[AsyncSession, None]:
    async with SessionLocal() as session:
        yield session


async def init_db() -> None:
    from app import models  # noqa: F401

    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
        await _ensure_columns(conn)

    from app.database import SessionLocal
    from app.seed import seed_demo_data

    async with SessionLocal() as session:
        await seed_demo_data(session)


async def _ensure_columns(conn) -> None:
    """SQLite 补列（已有库升级）。"""
    from sqlalchemy import text

    columns = [
        ("users", "avatar_url", "VARCHAR(512)"),
        ("posts", "image_caption", "VARCHAR(256)"),
    ]
    for table, col, col_type in columns:
        try:
            await conn.execute(text(f"ALTER TABLE {table} ADD COLUMN {col} {col_type}"))
        except Exception:
            pass
