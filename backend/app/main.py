from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from app.config import settings
from app.database import init_db
from app.routers import app_release, auth, classes, community, training


@asynccontextmanager
async def lifespan(_: FastAPI):
    Path(settings.upload_dir).mkdir(parents=True, exist_ok=True)
    Path("data").mkdir(parents=True, exist_ok=True)
    await init_db()
    yield


app = FastAPI(title=settings.app_name, lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=[settings.cors_origins] if settings.cors_origins != "*" else ["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

api = settings.api_prefix
app.include_router(auth.router, prefix=api)
app.include_router(training.router, prefix=api)
app.include_router(classes.router, prefix=api)
app.include_router(community.router, prefix=api)
app.include_router(app_release.router, prefix=api)
app.mount("/uploads", StaticFiles(directory=settings.upload_dir), name="uploads")


@app.get("/")
async def root():
    return {"app": settings.app_name, "status": "ok"}


@app.get("/health")
async def health():
    return {"status": "healthy"}
