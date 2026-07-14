package com.example.hellokotlin.data

import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.model.CommunityPost
import com.example.hellokotlin.data.model.CommunityUser
import com.example.hellokotlin.data.model.PostAttachmentKind
import com.example.hellokotlin.data.model.RankingEntry
import com.example.hellokotlin.data.model.RankingType

object MockSocialData {

    private val users = listOf(
        CommunityUser("u1", "羽林高手", 0xFF1B7A4E),
        CommunityUser("u2", "后场杀神", 0xFF1565C0),
        CommunityUser("u3", "网前精灵", 0xFF6A1B9A),
        CommunityUser("u4", "高远小王子", 0xFFFF8F00),
        CommunityUser("u5", "平抽达人", 0xFF00838F),
        CommunityUser("u6", "挑球能手", 0xFFC62828),
        CommunityUser("u7", "训练狂人", 0xFF4527A0),
        CommunityUser("u8", "业余一号", 0xFF558B2F)
    )

    fun rankings(type: RankingType): List<RankingEntry> = when (type) {
        RankingType.BALL_SPEED -> listOf(
            RankingEntry(1, users[1], 312),
            RankingEntry(2, users[0], 298),
            RankingEntry(3, users[4], 285),
            RankingEntry(4, users[3], 271),
            RankingEntry(5, users[6], 260),
            RankingEntry(6, users[2], 248),
            RankingEntry(7, users[5], 235),
            RankingEntry(8, users[7], 220)
        )
        RankingType.DRILL_COUNT -> listOf(
            RankingEntry(1, users[6], 1280),
            RankingEntry(2, users[0], 1156),
            RankingEntry(3, users[2], 980),
            RankingEntry(4, users[4], 876),
            RankingEntry(5, users[1], 802),
            RankingEntry(6, users[3], 745),
            RankingEntry(7, users[5], 690),
            RankingEntry(8, users[7], 520)
        )
        RankingType.SCORE -> listOf(
            RankingEntry(1, users[0], 92),
            RankingEntry(2, users[2], 89),
            RankingEntry(3, users[1], 87),
            RankingEntry(4, users[4], 85),
            RankingEntry(5, users[3], 83),
            RankingEntry(6, users[5], 81),
            RankingEntry(7, users[6], 78),
            RankingEntry(8, users[7], 74)
        )
    }

    private val feedPostsInternal = mutableListOf(
        CommunityPost(
            id = "p1",
            author = users[1],
            timeLabel = "10 分钟前",
            content = "今天对打模式杀球手感不错，板端 AI 建议我注意转体发力，分享一波数据～",
            attachmentKind = PostAttachmentKind.TRAINING_STATS,
            statsTitle = "对打 · 周三晚场",
            statsDetail = "均分 87 · 最高球速 312 km/h · 共 86 拍",
            likeCount = 24,
            commentCount = 6
        ),
        CommunityPost(
            id = "p2",
            author = users[2],
            timeLabel = "1 小时前",
            content = "放网专项练了 200 次，网前手感终于稳了一点，继续加油！",
            attachmentKind = PostAttachmentKind.IMAGE,
            imageCaption = "训练截图占位",
            likeCount = 18,
            commentCount = 3
        ),
        CommunityPost(
            id = "p3",
            author = users[3],
            timeLabel = "昨天 20:15",
            content = "高远球练习均分突破 85 了，感谢 Hi3403 的实时反馈，下周挑战 90！",
            attachmentKind = PostAttachmentKind.TRAINING_STATS,
            statsTitle = "高远 · 单人练习",
            statsDetail = "均分 85 · 球速 168 km/h · 力度 42 N",
            likeCount = 31,
            commentCount = 8
        ),
        CommunityPost(
            id = "p4",
            author = users[4],
            timeLabel = "昨天 14:02",
            content = "平抽对墙 30 分钟，手腕有点酸，大家有什么放松建议吗？",
            likeCount = 12,
            commentCount = 15
        ),
        CommunityPost(
            id = "p5",
            author = users[0],
            timeLabel = "2 天前",
            content = "周末比赛复盘：第三局体力下降导致杀球质量下滑，接下来加强耐力训练。",
            attachmentKind = PostAttachmentKind.IMAGE,
            imageCaption = "比赛数据卡片占位",
            likeCount = 45,
            commentCount = 11
        )
    )

    val feedPosts: List<CommunityPost> get() = feedPostsInternal

    fun currentAuthor(): CommunityUser {
        val user = AppSession.currentUser
        return CommunityUser(
            id = user?.id ?: "me",
            nickname = user?.displayName ?: "我",
            avatarColorHex = 0xFF1565C0
        )
    }

    fun addPost(
        content: String,
        attachmentKind: PostAttachmentKind,
        imageCaption: String? = null,
        statsTitle: String? = null,
        statsDetail: String? = null
    ) {
        val post = CommunityPost(
            id = "p${System.currentTimeMillis()}",
            author = currentAuthor(),
            timeLabel = "刚刚",
            content = content,
            attachmentKind = attachmentKind,
            imageCaption = imageCaption,
            statsTitle = statsTitle,
            statsDetail = statsDetail
        )
        feedPostsInternal.add(0, post)
    }
}
