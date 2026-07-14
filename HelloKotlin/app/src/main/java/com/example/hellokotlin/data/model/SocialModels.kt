package com.example.hellokotlin.data.model

enum class RankingType(val label: String, val unit: String) {
    BALL_SPEED("球速排行", "km/h"),
    DRILL_COUNT("练习动作数", "次"),
    SCORE("得分排行", "分")
}

data class CommunityUser(
    val id: String,
    val nickname: String,
    val avatarColorHex: Long,
    val avatarUrl: String? = null
)

data class RankingEntry(
    val rank: Int,
    val user: CommunityUser,
    val value: Int
)

enum class PostAttachmentKind {
    NONE,
    IMAGE,
    TRAINING_STATS
}

data class CommunityPost(
    val id: String,
    val author: CommunityUser,
    val timeLabel: String,
    val content: String,
    val attachmentKind: PostAttachmentKind = PostAttachmentKind.NONE,
    val imageUrl: String? = null,
    val imageCaption: String? = null,
    val statsTitle: String? = null,
    val statsDetail: String? = null,
    val likeCount: Int = 0,
    val commentCount: Int = 0
)

data class PostComment(
    val id: String,
    val author: CommunityUser,
    val timeLabel: String,
    val content: String
)
