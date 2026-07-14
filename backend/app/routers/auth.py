from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import create_access_token, get_current_user, hash_password, verify_password
from app.database import get_db
from app.models import User
from app.schemas import LoginIn, RegisterIn, RoleIn, TokenOut, UserOut
from app.seed import clone_training_from_template

router = APIRouter(prefix="/auth", tags=["auth"])


def to_user_out(user: User) -> UserOut:
    return UserOut(id=str(user.id), displayName=user.display_name, phone=user.phone, role=user.role)


@router.post("/register", response_model=TokenOut)
async def register(body: RegisterIn, db: AsyncSession = Depends(get_db)):
    exists = await db.scalar(select(User).where(User.phone == body.phone))
    if exists:
        raise HTTPException(status_code=400, detail="手机号已注册")
    user = User(
        phone=body.phone,
        display_name=body.display_name,
        password_hash=hash_password(body.password),
    )
    db.add(user)
    await db.commit()
    await db.refresh(user)
    await clone_training_from_template(db, user.id)
    await db.commit()
    token = create_access_token(user.id, user.phone)
    return TokenOut(token=token, user=to_user_out(user))


@router.post("/login", response_model=TokenOut)
async def login(body: LoginIn, db: AsyncSession = Depends(get_db)):
    user = await db.scalar(select(User).where(User.phone == body.phone))
    if user is None or not verify_password(body.password, user.password_hash):
        raise HTTPException(status_code=400, detail="手机号或密码错误")
    token = create_access_token(user.id, user.phone)
    return TokenOut(token=token, user=to_user_out(user))


@router.get("/me", response_model=UserOut)
async def me(user: User = Depends(get_current_user)):
    return to_user_out(user)


@router.put("/role", response_model=UserOut)
async def set_role(body: RoleIn, user: User = Depends(get_current_user), db: AsyncSession = Depends(get_db)):
    if body.role not in {"personal", "teacher"}:
        raise HTTPException(status_code=400, detail="无效身份")
    user.role = body.role
    await db.commit()
    await db.refresh(user)
    return to_user_out(user)
