from datetime import datetime

from pydantic import BaseModel, Field


class TokenOut(BaseModel):
    token: str
    user: "UserOut"


class RegisterIn(BaseModel):
    display_name: str = Field(min_length=1, max_length=32)
    phone: str = Field(min_length=6, max_length=20)
    password: str = Field(min_length=4, max_length=64)


class LoginIn(BaseModel):
    phone: str
    password: str


class UserOut(BaseModel):
    id: str
    displayName: str
    phone: str
    role: str = "personal"

    class Config:
        from_attributes = True


class RoleIn(BaseModel):
    role: str


class StrokeOut(BaseModel):
    id: str
    actionTypeLabel: str
    score: int
    aiSuggestion: str
    ballSpeedKmh: int
    powerN: int
    hitTimeLabel: str


class MatchOut(BaseModel):
    id: str
    title: str
    dateLabel: str
    durationMin: int
    strokeCount: int
    avgScore: int
    opponentLabel: str


class DrillOut(BaseModel):
    id: str
    actionType: str
    score: int
    aiSuggestion: str
    ballSpeedKmh: int
    powerN: int
    dateTimeLabel: str


class CommunityUserOut(BaseModel):
    id: str
    nickname: str
    avatarColorHex: int
    avatarUrl: str | None = None


class PostOut(BaseModel):
    id: str
    author: CommunityUserOut
    timeLabel: str
    content: str
    attachmentKind: str
    imageUrl: str | None = None
    imageCaption: str | None = None
    statsTitle: str | None = None
    statsDetail: str | None = None
    likeCount: int = 0
    commentCount: int = 0


class CommentOut(BaseModel):
    id: str
    author: CommunityUserOut
    content: str
    timeLabel: str


class PostIn(BaseModel):
    content: str
    attachmentKind: str = "none"
    imageUrl: str | None = None
    imageCaption: str | None = None
    statsTitle: str | None = None
    statsDetail: str | None = None


class RankingEntryOut(BaseModel):
    rank: int
    user: CommunityUserOut
    value: int


class DeviceMatchIn(BaseModel):
    device_id: str
    user_phone: str | None = None
    title: str
    opponent_label: str = "对打伙伴"
    duration_min: int = 0
    strokes: list[dict]


class DeviceDrillIn(BaseModel):
    device_id: str
    user_phone: str | None = None
    action_type: str
    score: int
    ai_suggestion: str = ""
    ball_speed_kmh: int = 0
    power_n: int = 0


class UploadOut(BaseModel):
    url: str


class AppReleaseOut(BaseModel):
    versionCode: int
    versionName: str
    apkUrl: str
    changelog: str
    forceUpdate: bool = False


class ClassCreateIn(BaseModel):
    name: str = Field(min_length=1, max_length=64)
    description: str = Field(default="", max_length=256)


class ClassUpdateIn(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=64)
    description: str | None = Field(default=None, max_length=256)


class AddMemberIn(BaseModel):
    phone: str = Field(min_length=6, max_length=20)


class JoinClassIn(BaseModel):
    inviteCode: str = Field(min_length=4, max_length=12)


class StudentSummaryOut(BaseModel):
    userId: str
    displayName: str
    phone: str
    avatarColorHex: int
    avatarUrl: str | None = None
    matchCount: int = 0
    drillCount: int = 0
    avgScore: int = 0
    maxBallSpeed: int = 0
    lastActiveLabel: str = "暂无记录"


class ClassOut(BaseModel):
    id: str
    name: str
    description: str
    inviteCode: str
    memberCount: int
    createdLabel: str


class ClassDetailOut(BaseModel):
    id: str
    name: str
    description: str
    inviteCode: str
    coachName: str
    memberCount: int
    createdLabel: str
    members: list[StudentSummaryOut]


class ClassJoinedOut(BaseModel):
    id: str
    name: str
    description: str
    coachName: str
    memberCount: int
    joinedLabel: str


class ClassmateOut(BaseModel):
    userId: str
    displayName: str
    avatarColorHex: int
    avatarUrl: str | None = None
    avgScore: int = 0
    maxBallSpeed: int = 0
    isMe: bool = False


class StudentClassDetailOut(BaseModel):
    id: str
    name: str
    description: str
    coachName: str
    memberCount: int
    myRank: int
    myStats: StudentSummaryOut
    classmates: list[ClassmateOut]


TokenOut.model_rebuild()
