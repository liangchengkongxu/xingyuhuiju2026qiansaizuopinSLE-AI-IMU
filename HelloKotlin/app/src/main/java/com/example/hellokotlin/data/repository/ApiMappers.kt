package com.example.hellokotlin.data.repository

import com.example.hellokotlin.data.api.dto.CommentDto
import com.example.hellokotlin.data.api.dto.ClassDetailDto
import com.example.hellokotlin.data.api.dto.ClassmateDto
import com.example.hellokotlin.data.api.dto.JoinedClassDto
import com.example.hellokotlin.data.api.dto.StudentClassDetailDto
import com.example.hellokotlin.data.api.dto.ClassSummaryDto
import com.example.hellokotlin.data.api.dto.CommunityUserDto
import com.example.hellokotlin.data.api.dto.DrillDto
import com.example.hellokotlin.data.api.dto.MatchDto
import com.example.hellokotlin.data.api.dto.PostDto
import com.example.hellokotlin.data.api.dto.RankingEntryDto
import com.example.hellokotlin.data.api.dto.StudentSummaryDto
import com.example.hellokotlin.data.api.dto.StrokeDto
import com.example.hellokotlin.data.api.dto.UserDto
import com.example.hellokotlin.data.model.AuthUser
import com.example.hellokotlin.data.model.CommunityPost
import com.example.hellokotlin.data.model.CommunityUser
import com.example.hellokotlin.data.model.DrillActionType
import com.example.hellokotlin.data.model.DrillSessionRecord
import com.example.hellokotlin.data.model.MatchRecord
import com.example.hellokotlin.data.model.PostAttachmentKind
import com.example.hellokotlin.data.model.PostComment
import com.example.hellokotlin.data.model.RankingEntry
import com.example.hellokotlin.data.model.RankingType
import com.example.hellokotlin.data.model.Classmate
import com.example.hellokotlin.data.model.JoinedClassSummary
import com.example.hellokotlin.data.model.StudentClassDetail
import com.example.hellokotlin.data.model.StudentSummary
import com.example.hellokotlin.data.model.TrainingClassDetail
import com.example.hellokotlin.data.model.TrainingClassSummary
import com.example.hellokotlin.data.model.UserRole

fun UserDto.toAuthUser(): AuthUser = AuthUser(
    id = id,
    displayName = displayName,
    phone = phone
)

fun String.toUserRole(): UserRole? = when (this) {
    "personal" -> UserRole.PERSONAL
    "teacher" -> UserRole.TEACHER
    else -> null
}

fun UserRole.toApiRole(): String = when (this) {
    UserRole.PERSONAL -> "personal"
    UserRole.TEACHER -> "teacher"
}

fun RankingType.toApiType(): String = when (this) {
    RankingType.BALL_SPEED -> "ball_speed"
    RankingType.DRILL_COUNT -> "drill_count"
    RankingType.SCORE -> "score"
}

fun PostAttachmentKind.toApiKind(): String = when (this) {
    PostAttachmentKind.NONE -> "none"
    PostAttachmentKind.IMAGE -> "image"
    PostAttachmentKind.TRAINING_STATS -> "training_stats"
}

fun String.toPostAttachmentKind(): PostAttachmentKind = when (this) {
    "image" -> PostAttachmentKind.IMAGE
    "training_stats" -> PostAttachmentKind.TRAINING_STATS
    else -> PostAttachmentKind.NONE
}

fun MatchDto.toMatchRecord(): MatchRecord = MatchRecord(
    id = id,
    title = title,
    dateLabel = dateLabel,
    durationMin = durationMin,
    strokeCount = strokeCount,
    avgScore = avgScore,
    opponentLabel = opponentLabel
)

fun StrokeDto.toStrokeRecord() = com.example.hellokotlin.data.model.StrokeRecord(
    id = id,
    actionTypeLabel = actionTypeLabel,
    score = score,
    aiSuggestion = aiSuggestion,
    ballSpeedKmh = ballSpeedKmh,
    powerN = powerN,
    hitTimeLabel = hitTimeLabel
)

fun DrillDto.toDrillSessionRecord(): DrillSessionRecord? {
    val action = DrillActionType.fromKey(actionType) ?: return null
    return DrillSessionRecord(
        id = id,
        actionType = action,
        score = score,
        aiSuggestion = aiSuggestion,
        ballSpeedKmh = ballSpeedKmh,
        powerN = powerN,
        dateTimeLabel = dateTimeLabel
    )
}

fun CommunityUserDto.toCommunityUser(): CommunityUser = CommunityUser(
    id = id,
    nickname = nickname,
    avatarColorHex = avatarColorHex,
    avatarUrl = avatarUrl
)

fun RankingEntryDto.toRankingEntry(): RankingEntry = RankingEntry(
    rank = rank,
    user = user.toCommunityUser(),
    value = value
)

fun PostDto.toCommunityPost(): CommunityPost = CommunityPost(
    id = id,
    author = author.toCommunityUser(),
    timeLabel = timeLabel,
    content = content,
    attachmentKind = attachmentKind.toPostAttachmentKind(),
    imageUrl = imageUrl,
    imageCaption = imageCaption,
    statsTitle = statsTitle,
    statsDetail = statsDetail,
    likeCount = likeCount,
    commentCount = commentCount
)

fun CommentDto.toPostComment(): PostComment = PostComment(
    id = id,
    author = author.toCommunityUser(),
    timeLabel = timeLabel,
    content = content
)

fun ClassSummaryDto.toTrainingClassSummary(): TrainingClassSummary = TrainingClassSummary(
    id = id,
    name = name,
    description = description,
    inviteCode = inviteCode,
    memberCount = memberCount,
    createdLabel = createdLabel
)

fun StudentSummaryDto.toStudentSummary(): StudentSummary = StudentSummary(
    userId = userId,
    displayName = displayName,
    phone = phone,
    avatarColorHex = avatarColorHex,
    avatarUrl = avatarUrl,
    matchCount = matchCount,
    drillCount = drillCount,
    avgScore = avgScore,
    maxBallSpeed = maxBallSpeed,
    lastActiveLabel = lastActiveLabel
)

fun ClassDetailDto.toTrainingClassDetail(): TrainingClassDetail = TrainingClassDetail(
    id = id,
    name = name,
    description = description,
    inviteCode = inviteCode,
    coachName = coachName,
    memberCount = memberCount,
    createdLabel = createdLabel,
    members = members.map { it.toStudentSummary() }
)

fun JoinedClassDto.toJoinedClassSummary(): JoinedClassSummary = JoinedClassSummary(
    id = id,
    name = name,
    description = description,
    coachName = coachName,
    memberCount = memberCount,
    joinedLabel = joinedLabel
)

fun ClassmateDto.toClassmate(): Classmate = Classmate(
    userId = userId,
    displayName = displayName,
    avatarColorHex = avatarColorHex,
    avatarUrl = avatarUrl,
    avgScore = avgScore,
    maxBallSpeed = maxBallSpeed,
    isMe = isMe
)

fun StudentClassDetailDto.toStudentClassDetail(): StudentClassDetail = StudentClassDetail(
    id = id,
    name = name,
    description = description,
    coachName = coachName,
    memberCount = memberCount,
    myRank = myRank,
    myStats = myStats.toStudentSummary(),
    classmates = classmates.map { it.toClassmate() }
)
