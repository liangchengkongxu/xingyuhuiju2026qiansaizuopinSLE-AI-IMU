"""演示数据种子：真实配图、微信风头像、球友圈动态（幂等可升级）。"""

from __future__ import annotations

import shutil
from datetime import datetime, timedelta, timezone
from pathlib import Path

from sqlalchemy import delete, func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import hash_password
from app.config import settings
from app.models import ClassMember, DrillSession, Match, Post, PostComment, Stroke, TrainingClass, User

DEMO_PASSWORD = "123456"
TEMPLATE_PHONE = "13800138000"
SEED_POST_MARKER = "post_0d460ba0322436bd0ac86f2f03d98e28.jpg"

DEMO_USERS = [
    {"phone": "13800138000", "name": "羽坛新锐", "color": 0xFF1565C0, "avatar": "avatar_01.jpg"},
    {"phone": "13800138001", "name": "疾风杀球", "color": 0xFFE65100, "avatar": "avatar_02.jpg"},
    {"phone": "13800138002", "name": "网前精灵", "color": 0xFF2E7D32, "avatar": "avatar_03.jpg"},
    {"phone": "13800138003", "name": "云顶高远", "color": 0xFF6A1B9A, "avatar": "avatar_04.jpg"},
    {"phone": "13800138004", "name": "平抽悍将", "color": 0xFFC62828, "avatar": "avatar_05.jpg"},
    {"phone": "13800138005", "name": "星羽教练", "color": 0xFF00838F, "avatar": "avatar_06.jpg"},
    {"phone": "13800138006", "name": "重杀少年", "color": 0xFF4527A0, "avatar": "avatar_07.jpg"},
    {"phone": "13800138007", "name": "后场杀手", "color": 0xFF00695C, "avatar": "avatar_08.jpg"},
    {"phone": "13800138008", "name": "混双达人", "color": 0xFFAD1457, "avatar": "avatar_01.jpg"},
    {"phone": "13800138009", "name": "控球大师", "color": 0xFF283593, "avatar": "avatar_02.jpg"},
    {"phone": "13800138010", "name": "爆冲狂人", "color": 0xFFEF6C00, "avatar": "avatar_03.jpg"},
    {"phone": "13800138011", "name": "挑球高手", "color": 0xFF558B2F, "avatar": "avatar_04.jpg"},
]


def _utc(days_ago: float = 0, hours: float = 0) -> datetime:
    return datetime.now(timezone.utc) - timedelta(days=days_ago, hours=hours)


def _assets_dir() -> Path:
    return Path(__file__).resolve().parent.parent / "seed_assets"


def _deploy_media(upload_dir: Path) -> tuple[dict[str, str], dict[str, str]]:
    """复制种子资源到 uploads，返回 avatar 与 post 图片 URL 映射。"""
    upload_dir.mkdir(parents=True, exist_ok=True)
    assets = _assets_dir()
    avatar_urls: dict[str, str] = {}
    post_urls: dict[str, str] = {}

    avatars_dir = assets / "avatars"
    if avatars_dir.is_dir():
        for src in avatars_dir.glob("*.jpg"):
            dest_name = src.name
            dest = upload_dir / dest_name
            shutil.copy2(src, dest)
            avatar_urls[src.name] = f"/uploads/{dest_name}"

    posts_dir = assets / "posts"
    if posts_dir.is_dir():
        for src in posts_dir.glob("*.jpg"):
            dest_name = f"post_{src.name}"
            dest = upload_dir / dest_name
            shutil.copy2(src, dest)
            post_urls[src.name] = f"/uploads/{dest_name}"

    releases_dir = assets / "releases"
    if releases_dir.is_dir():
        releases_upload = upload_dir / "releases"
        releases_upload.mkdir(parents=True, exist_ok=True)
        for src in releases_dir.glob("*.apk"):
            shutil.copy2(src, releases_upload / src.name)

    return avatar_urls, post_urls


async def _get_or_create_user(
    db: AsyncSession, phone: str, name: str, color: int, avatar_url: str | None
) -> User:
    user = await db.scalar(select(User).where(User.phone == phone))
    if user:
        user.display_name = name
        user.avatar_color = color
        if avatar_url:
            user.avatar_url = avatar_url
        return user
    user = User(
        phone=phone,
        display_name=name,
        password_hash=hash_password(DEMO_PASSWORD),
        role="personal",
        avatar_color=color,
        avatar_url=avatar_url,
    )
    db.add(user)
    await db.flush()
    return user


async def _seed_matches(db: AsyncSession, user: User, pack: str) -> None:
    existing = await db.scalar(select(func.count()).select_from(Match).where(Match.user_id == user.id))
    if existing and existing > 0:
        return

    packs = {
        "flagship": [
            {
                "title": "周末俱乐部对抗赛",
                "opponent": "李师兄 · 校队主力",
                "duration": 52,
                "strokes": [
                    ("杀球", 94, "击球点再靠前，拍面稍压", 312, 78),
                    ("平抽", 88, "手腕更放松，减少多余动作", 186, 52),
                    ("高远", 91, "蹬转发力连贯，落点到位", 168, 45),
                    ("放网", 86, "搓球再贴网一些", 42, 18),
                    ("挑球", 89, "拍面稳定，弧度理想", 95, 28),
                    ("杀球", 96, "完美一拍！速度与落点俱佳", 328, 82),
                    ("吊球", 90, "假动作可以再逼真", 142, 35),
                    ("平抽", 87, "注意护住反手位", 178, 49),
                ],
            },
            {
                "title": "晚场双打模拟",
                "opponent": "混双搭档训练",
                "duration": 38,
                "strokes": [
                    ("封网", 92, "反应快，封网角度刁", 88, 22),
                    ("杀球", 93, "连贯压制，保持节奏", 298, 74),
                    ("抽挡", 85, "重心略高，需再压低", 165, 44),
                    ("高远", 90, "后场调动有效", 172, 46),
                    ("杀球", 95, "板端 AI 评分最高一拍", 305, 79),
                ],
            },
            {
                "title": "Hi3403 边缘节点实测",
                "opponent": "AI 对打模式",
                "duration": 45,
                "strokes": [
                    ("杀球", 91, "4G 上云延迟 <200ms，数据完整", 289, 71),
                    ("平抽", 89, "边缘推理稳定", 192, 55),
                    ("高远", 88, "轨迹识别准确", 158, 40),
                    ("杀球", 94, "球速突破 300km/h", 318, 80),
                ],
            },
        ],
        "medium": [
            {
                "title": "晨练专项对打",
                "opponent": "球友小王",
                "duration": 30,
                "strokes": [
                    ("杀球", 90, "发力顺畅", 276, 68),
                    ("高远", 87, "落点偏后，可再深", 155, 38),
                    ("放网", 84, "手感不错", 38, 15),
                ],
            },
        ],
        "light": [
            {
                "title": "基础热身局",
                "opponent": "俱乐部球友",
                "duration": 25,
                "strokes": [
                    ("高远", 85, "热身到位", 148, 36),
                    ("杀球", 88, "状态回升", 265, 62),
                ],
            },
        ],
    }

    for idx, m in enumerate(packs.get(pack, packs["light"])):
        scores = [s[1] for s in m["strokes"]]
        match = Match(
            user_id=user.id,
            title=m["title"],
            opponent_label=m["opponent"],
            duration_min=m["duration"],
            stroke_count=len(m["strokes"]),
            avg_score=sum(scores) // len(scores),
            started_at=_utc(days_ago=idx + 1, hours=2),
        )
        db.add(match)
        await db.flush()
        for seq, (label, score, tip, speed, power) in enumerate(m["strokes"], start=1):
            db.add(
                Stroke(
                    match_id=match.id,
                    seq=seq,
                    action_type_label=label,
                    score=score,
                    ai_suggestion=tip,
                    ball_speed_kmh=speed,
                    power_n=power,
                    hit_at=_utc(days_ago=idx + 1, hours=2) + timedelta(seconds=seq * 18),
                )
            )


async def _seed_drills(db: AsyncSession, user: User, rich: bool = False) -> None:
    existing = await db.scalar(select(func.count()).select_from(DrillSession).where(DrillSession.user_id == user.id))
    if existing and existing > 0:
        return

    drills = [
        ("smash", "杀球", 94, "鞭打发力完整，拍头速度快", 318, 80),
        ("clear", "高远", 91, "弧线饱满，底线到位", 172, 46),
        ("net_drop", "放网", 88, "搓球细腻，贴网而过", 35, 12),
        ("lift", "挑球", 86, "弧度可控，落点精准", 92, 26),
        ("drive", "平抽", 90, "节奏稳定，压制对手", 188, 54),
    ]
    if not rich:
        drills = drills[:3]

    for idx, (key, _label, score, tip, speed, power) in enumerate(drills):
        for rep in range(2 if rich else 1):
            db.add(
                DrillSession(
                    user_id=user.id,
                    action_type=key,
                    score=score - rep,
                    ai_suggestion=tip,
                    ball_speed_kmh=speed - rep * 3,
                    power_n=power - rep,
                    practiced_at=_utc(days_ago=idx + rep, hours=4),
                )
            )


async def _seed_posts(db: AsyncSession, users: dict[str, User], post_urls: dict[str, str]) -> None:
    demo_ids = [u.id for u in users.values()]
    marker = await db.scalar(
        select(func.count())
        .select_from(Post)
        .where(Post.image_url.like(f"%{SEED_POST_MARKER}%"))
    )
    if marker and marker > 0:
        return

    await db.execute(delete(Post).where(Post.user_id.in_(demo_ids)))
    await db.flush()

    def img(filename: str) -> str | None:
        return post_urls.get(filename)

    posts_data = [
        # 纯文字
        (users["13800138000"], "今天 Hi3403 板端实测杀球 328km/h！星羽汇聚云端同步丝滑 🏸", "none", None, None, None, None, 56, 12),
        (users["13800138004"], "平抽对抗不落下风，推荐大家都试试星羽的数据分析。", "none", None, None, None, None, 33, 7),
        (users["13800138002"], "和球友们组队刷榜，球友圈排行太燃了！", "none", None, None, None, None, 25, 4),
        # 训练数据卡片
        (users["13800138001"], "晚场训练打卡，连续 30 拍高强度对拉，AI 建议超实用。", "training_stats", None, None, "对打 · 周末俱乐部对抗赛", "均分 91 · 最高球速 328 km/h · 共 8 拍", 42, 8),
        (users["13800138003"], "高远球落点控制练习第 12 天，进步明显。", "training_stats", None, None, "高远 · 专项练习", "均分 91 · 球速 172 km/h · 力度 46 N", 29, 5),
        (users["13800138005"], "班级课试点中，学员数据一键上云，教学效率翻倍。", "training_stats", None, None, "班级课 · 示范课", "12 名学员 · 均分 85 · 云端同步率 100%", 72, 20),
        # 真实配图动态
        (
            users["13800138000"],
            "线终于打断了！Arc11 Pro 陪我打了好久，准备去穿 XB65 27 磅，大家有推荐线型吗？",
            "image",
            img("0d460ba0322436bd0ac86f2f03d98e28.jpg"),
            "穿线前的纪念照",
            None,
            None,
            48,
            11,
        ),
        (
            users["13800138001"],
            "今天在球馆练发球，墙上写着「发球居然手抖」——被内涵到了 😂 继续练！",
            "image",
            img("ba738f5ee694ca519cd79eb8862b421b.jpg"),
            "发球专项训练",
            None,
            None,
            37,
            9,
        ),
        (
            users["13800138002"],
            "新入 RSL 1 号球，飞行稳、耐打度高，今晚球馆见！",
            "image",
            img("889cb4ab93640b5e55c3a074ac3ddec4.jpg"),
            "RSL No.1",
            None,
            None,
            44,
            13,
        ),
        (
            users["13800138003"],
            "新拍新线新护腕，橙色线床颜值在线，下午继续练高远。",
            "image",
            img("dc65330864196463ca79a145b89bbcd2.jpg"),
            "装备开箱",
            None,
            None,
            31,
            6,
        ),
        (
            users["13800138004"],
            "和球友们在球馆集合，六把拍同框，谁的最帅？",
            "image",
            img("70840a524f807dddd6348200f3ef3c87.jpg"),
            "球友局",
            None,
            None,
            52,
            16,
        ),
        (
            users["13800138005"],
            "工欲善其事必先利其器！Duora 10 和新鞋到手，周末约一场。",
            "image",
            img("37c5942e9333ed8e27f747d6652b2acf.jpg"),
            "尤尼克斯 Duora 10",
            None,
            None,
            63,
            14,
        ),
        (
            users["13800138000"],
            "Astrox 88S Game 配发球机 solo，一个人也能练得很爽。",
            "image",
            img("ce1ec9588ac99d58aea47077247e0162.jpg"),
            "发球机训练",
            None,
            None,
            41,
            8,
        ),
        (
            users["13800138001"],
            "老拍老鞋老人老地方——这就是我的周末。",
            "image",
            img("3044968811e0d3129f7710058736954f.jpg"),
            "球场日常",
            None,
            None,
            58,
            19,
        ),
        (
            users["13800138002"],
            "上午刚买的 RSL 1 号，下午有没有约球的朋友？",
            "image",
            img("d3e7a3456c1323bb4e30bd8d5aef66f5.jpg"),
            "新球开箱",
            None,
            None,
            36,
            7,
        ),
        (
            users["13800138003"],
            "蓝色线床穿好了，今晚球馆见！",
            "image",
            img("c3293b0e7377b49d744e4360bb271af0.jpg"),
            "穿线完成",
            None,
            None,
            28,
            5,
        ),
        (
            users["13800138004"],
            "逛专业店看到 Duora 陈列，橙蓝配色太帅了，心动中…",
            "image",
            img("ef3b0db8f0290e9ad7d32866f6983f26.jpg"),
            "装备党日常",
            None,
            None,
            22,
            4,
        ),
        (
            users["13800138005"],
            "手表记录：3 小时羽毛球，1730 千卡，有氧拉满。数据已同步星羽汇聚。",
            "image",
            img("b42b65321deca91ab6c1f7b6fb5fbd79.jpg"),
            "训练记录截图",
            None,
            None,
            45,
            10,
        ),
    ]

    for author, content, kind, image, caption, stats_title, stats_detail, likes, comments in posts_data:
        if kind == "image" and not image:
            continue
        db.add(
            Post(
                user_id=author.id,
                content=content,
                attachment_kind=kind,
                image_url=image,
                image_caption=caption,
                stats_title=stats_title,
                stats_detail=stats_detail,
                like_count=likes,
                comment_count=comments,
                created_at=_utc(days_ago=likes % 6, hours=comments % 12),
            )
        )


async def _seed_ranking_profile(
    db: AsyncSession,
    user: User,
    *,
    max_speed: int,
    drills: list[tuple[str, int, int]],
) -> None:
    """为排行榜补充轻量训练数据（一场对打 + 若干练习）。"""
    existing = await db.scalar(select(func.count()).select_from(Match).where(Match.user_id == user.id))
    if existing and existing > 0:
        return

    match = Match(
        user_id=user.id,
        title="俱乐部友谊赛",
        opponent_label="球友",
        duration_min=35,
        stroke_count=3,
        avg_score=88,
        started_at=_utc(days_ago=2),
    )
    db.add(match)
    await db.flush()
    for seq, (label, score, speed) in enumerate(
        [("杀球", 90, max_speed), ("高远", 86, max_speed - 40), ("平抽", 84, max_speed - 70)],
        start=1,
    ):
        db.add(
            Stroke(
                match_id=match.id,
                seq=seq,
                action_type_label=label,
                score=score,
                ai_suggestion="保持节奏",
                ball_speed_kmh=speed,
                power_n=55,
                hit_at=_utc(days_ago=2, hours=seq),
            )
        )

    for action, score, count in drills:
        for rep in range(count):
            db.add(
                DrillSession(
                    user_id=user.id,
                    action_type=action,
                    score=score - rep,
                    ai_suggestion="继续加油",
                    ball_speed_kmh=max(120, max_speed - 80 - rep * 2),
                    power_n=40 + rep,
                    practiced_at=_utc(days_ago=rep + 1, hours=3),
                )
            )


async def _seed_comments(db: AsyncSession, users: dict[str, User]) -> None:
    existing = await db.scalar(select(func.count()).select_from(PostComment))
    if existing and existing >= 15:
        return

    demo_ids = [u.id for u in users.values()]
    posts = (await db.scalars(select(Post).where(Post.user_id.in_(demo_ids)).order_by(Post.id))).all()
    if not posts:
        return

    commenters = list(users.values())
    template_groups = [
        ["太强了！这球速真实吗？", "同款配置求分享", "今晚球馆约吗？"],
        ["向大佬学习 🙏", "动作太标准了", "已收藏"],
        ["星羽汇聚数据同步真快", "Hi3403 方案稳", "我们也想上云"],
        ["装备党狂喜", "配色绝了", "求链接！"],
        ["坚持就是胜利", "一起加油", "下周见"],
        ["这均分也太高了吧", "AI 建议很实用", "回去练起来"],
    ]

    for idx, post in enumerate(posts):
        lines = template_groups[idx % len(template_groups)]
        actual = 0
        for j, text in enumerate(lines):
            commenter = commenters[(idx + j + 1) % len(commenters)]
            if commenter.id == post.user_id:
                commenter = commenters[(idx + j + 2) % len(commenters)]
            db.add(
                PostComment(
                    post_id=post.id,
                    user_id=commenter.id,
                    content=text,
                    created_at=_utc(days_ago=idx % 3, hours=j + 1),
                )
            )
            actual += 1
        post.comment_count = max(post.comment_count, actual)


async def _seed_classes(db: AsyncSession, users: dict[str, User]) -> None:
    coach = users.get("13800138005")
    if coach is None or coach.role != "teacher":
        coach.role = "teacher"
    if coach is None:
        return

    existing = await db.scalar(
        select(TrainingClass.id).where(TrainingClass.coach_id == coach.id, TrainingClass.name == "星羽周末提高班")
    )
    if existing:
        return

    weekend = TrainingClass(
        coach_id=coach.id,
        name="星羽周末提高班",
        description="周六下午 · 杀球/高远专项 · 板端数据实时同步",
        invite_code="XY2026",
    )
    db.add(weekend)
    await db.flush()

    junior = TrainingClass(
        coach_id=coach.id,
        name="青少年基础班",
        description="周三晚 · 放网/挑球基础 · 适合入门学员",
        invite_code="XYJUN",
    )
    db.add(junior)
    await db.flush()

    member_phones = [
        ("13800138000", weekend),
        ("13800138001", weekend),
        ("13800138002", weekend),
        ("13800138003", weekend),
        ("13800138004", junior),
        ("13800138006", junior),
        ("13800138007", junior),
    ]
    for phone, training_class in member_phones:
        student = users.get(phone)
        if student is None or student.id == coach.id:
            continue
        db.add(ClassMember(class_id=training_class.id, user_id=student.id))


async def clone_training_from_template(db: AsyncSession, user_id: int) -> None:
    template = await db.scalar(select(User).where(User.phone == TEMPLATE_PHONE))
    if template is None or template.id == user_id:
        return
    has = await db.scalar(select(func.count()).select_from(Match).where(Match.user_id == user_id))
    if has and has > 0:
        return

    matches = (await db.scalars(select(Match).where(Match.user_id == template.id))).all()
    for m in matches:
        nm = Match(
            user_id=user_id,
            title=m.title,
            opponent_label=m.opponent_label,
            duration_min=m.duration_min,
            stroke_count=m.stroke_count,
            avg_score=m.avg_score,
            started_at=m.started_at,
        )
        db.add(nm)
        await db.flush()
        strokes = (await db.scalars(select(Stroke).where(Stroke.match_id == m.id))).all()
        for s in strokes:
            db.add(
                Stroke(
                    match_id=nm.id,
                    seq=s.seq,
                    action_type_label=s.action_type_label,
                    score=s.score,
                    ai_suggestion=s.ai_suggestion,
                    ball_speed_kmh=s.ball_speed_kmh,
                    power_n=s.power_n,
                    hit_at=s.hit_at,
                )
            )

    drills = (await db.scalars(select(DrillSession).where(DrillSession.user_id == template.id))).all()
    for d in drills:
        db.add(
            DrillSession(
                user_id=user_id,
                action_type=d.action_type,
                score=d.score,
                ai_suggestion=d.ai_suggestion,
                ball_speed_kmh=d.ball_speed_kmh,
                power_n=d.power_n,
                practiced_at=d.practiced_at,
            )
        )


async def _purge_bad_image_posts(db: AsyncSession) -> None:
    """全局清理空白/占位配图动态。"""
    rows = (await db.scalars(select(Post).where(Post.attachment_kind == "image"))).all()
    for post in rows:
        url = post.image_url or ""
        if url == "" or "seed_court_" in url:
            await db.delete(post)
    await db.flush()


async def seed_demo_data(db: AsyncSession) -> None:
    upload_dir = Path(settings.upload_dir)
    avatar_urls, post_urls = _deploy_media(upload_dir)

    users: dict[str, User] = {}
    for item in DEMO_USERS:
        av = avatar_urls.get(item["avatar"])
        users[item["phone"]] = await _get_or_create_user(db, item["phone"], item["name"], item["color"], av)

    await _purge_bad_image_posts(db)

    await _seed_matches(db, users[TEMPLATE_PHONE], "flagship")
    await _seed_matches(db, users["13800138001"], "medium")
    await _seed_matches(db, users["13800138002"], "light")
    await _seed_matches(db, users["13800138003"], "light")
    await _seed_matches(db, users["13800138004"], "medium")
    await _seed_drills(db, users[TEMPLATE_PHONE], rich=True)
    await _seed_drills(db, users["13800138001"], rich=True)
    await _seed_drills(db, users["13800138003"], rich=False)
    await _seed_drills(db, users["13800138004"], rich=True)
    await _seed_drills(db, users["13800138005"], rich=True)

    ranking_extra = [
        ("13800138006", 315, [("smash", 91, 9), ("drive", 86, 4)]),
        ("13800138007", 302, [("clear", 88, 7), ("lift", 84, 5)]),
        ("13800138008", 296, [("drive", 87, 6), ("net_drop", 85, 8)]),
        ("13800138009", 288, [("net_drop", 90, 11), ("lift", 83, 4)]),
        ("13800138010", 322, [("smash", 93, 6), ("clear", 89, 5)]),
        ("13800138011", 279, [("lift", 86, 8), ("net_drop", 84, 6)]),
    ]
    for phone, speed, drills in ranking_extra:
        await _seed_ranking_profile(db, users[phone], max_speed=speed, drills=drills)

    await _seed_posts(db, users, post_urls)
    await _seed_comments(db, users)
    coach = users.get("13800138005")
    if coach:
        coach.role = "teacher"
    await _seed_classes(db, users)
    await db.commit()
