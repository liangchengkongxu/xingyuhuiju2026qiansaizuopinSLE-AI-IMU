package com.example.hellokotlin.data.model

data class TrainingClassSummary(
    val id: String,
    val name: String,
    val description: String,
    val inviteCode: String,
    val memberCount: Int,
    val createdLabel: String
)

data class StudentSummary(
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

data class TrainingClassDetail(
    val id: String,
    val name: String,
    val description: String,
    val inviteCode: String,
    val coachName: String,
    val memberCount: Int,
    val createdLabel: String,
    val members: List<StudentSummary>
)

data class JoinedClassSummary(
    val id: String,
    val name: String,
    val description: String,
    val coachName: String,
    val memberCount: Int,
    val joinedLabel: String
)

data class Classmate(
    val userId: String,
    val displayName: String,
    val avatarColorHex: Long,
    val avatarUrl: String? = null,
    val avgScore: Int = 0,
    val maxBallSpeed: Int = 0,
    val isMe: Boolean = false
)

data class StudentClassDetail(
    val id: String,
    val name: String,
    val description: String,
    val coachName: String,
    val memberCount: Int,
    val myRank: Int,
    val myStats: StudentSummary,
    val classmates: List<Classmate>
)
