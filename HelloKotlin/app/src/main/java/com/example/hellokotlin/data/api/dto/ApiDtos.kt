package com.example.hellokotlin.data.api.dto

import com.squareup.moshi.Json

data class RegisterRequest(
    @Json(name = "display_name") val displayName: String,
    val phone: String,
    val password: String
)

data class LoginRequest(
    val phone: String,
    val password: String
)

data class RoleRequest(
    val role: String
)

data class TokenResponse(
    val token: String,
    val user: UserDto
)

data class UserDto(
    val id: String,
    val displayName: String,
    val phone: String,
    val role: String = "personal"
)

data class MatchDto(
    val id: String,
    val title: String,
    val dateLabel: String,
    val durationMin: Int,
    val strokeCount: Int,
    val avgScore: Int,
    val opponentLabel: String
)

data class StrokeDto(
    val id: String,
    val actionTypeLabel: String,
    val score: Int,
    val aiSuggestion: String,
    val ballSpeedKmh: Int,
    val powerN: Int,
    val hitTimeLabel: String
)

data class DrillDto(
    val id: String,
    val actionType: String,
    val score: Int,
    val aiSuggestion: String,
    val ballSpeedKmh: Int,
    val powerN: Int,
    val dateTimeLabel: String
)

data class CommunityUserDto(
    val id: String,
    val nickname: String,
    val avatarColorHex: Long,
    val avatarUrl: String? = null
)

data class RankingEntryDto(
    val rank: Int,
    val user: CommunityUserDto,
    val value: Int
)

data class PostDto(
    val id: String,
    val author: CommunityUserDto,
    val timeLabel: String,
    val content: String,
    val attachmentKind: String,
    val imageUrl: String? = null,
    val imageCaption: String? = null,
    val statsTitle: String? = null,
    val statsDetail: String? = null,
    val likeCount: Int = 0,
    val commentCount: Int = 0
)

data class CommentDto(
    val id: String,
    val author: CommunityUserDto,
    val timeLabel: String,
    val content: String
)

data class CreatePostRequest(
    val content: String,
    val attachmentKind: String = "none",
    val imageUrl: String? = null,
    val imageCaption: String? = null,
    val statsTitle: String? = null,
    val statsDetail: String? = null
)

data class UploadResponse(
    val url: String
)

data class ErrorDetail(
    val detail: String? = null
)

data class AppReleaseDto(
    val versionCode: Int,
    val versionName: String,
    val apkUrl: String,
    val changelog: String,
    val forceUpdate: Boolean = false
)

data class ClassCreateRequest(
    val name: String,
    val description: String = ""
)

data class AddMemberRequest(
    val phone: String
)

data class ClassSummaryDto(
    val id: String,
    val name: String,
    val description: String,
    val inviteCode: String,
    val memberCount: Int,
    val createdLabel: String
)

data class StudentSummaryDto(
    val userId: String,
    val displayName: String,
    val phone: String,
    val avatarColorHex: Long,
    val avatarUrl: String? = null,
    val matchCount: Int = 0,
    val drillCount: Int = 0,
    val avgScore: Int = 0,
    val maxBallSpeed: Int = 0,
    val lastActiveLabel: String = "暂无记录"
)

data class ClassDetailDto(
    val id: String,
    val name: String,
    val description: String,
    val inviteCode: String,
    val coachName: String,
    val memberCount: Int,
    val createdLabel: String,
    val members: List<StudentSummaryDto>
)

data class JoinClassRequest(
    val inviteCode: String
)

data class JoinedClassDto(
    val id: String,
    val name: String,
    val description: String,
    val coachName: String,
    val memberCount: Int,
    val joinedLabel: String
)

data class ClassmateDto(
    val userId: String,
    val displayName: String,
    val avatarColorHex: Long,
    val avatarUrl: String? = null,
    val avgScore: Int = 0,
    val maxBallSpeed: Int = 0,
    val isMe: Boolean = false
)

data class StudentClassDetailDto(
    val id: String,
    val name: String,
    val description: String,
    val coachName: String,
    val memberCount: Int,
    val myRank: Int,
    val myStats: StudentSummaryDto,
    val classmates: List<ClassmateDto>
)
