package com.example.hellokotlin.data.model

enum class UserRole(val label: String) {
    PERSONAL("个人训练者"),
    TEACHER("班级训练（教师）")
}

data class AuthUser(
    val id: String,
    val displayName: String,
    val phone: String
)

/** 板端单人练习五类动作 */
enum class DrillActionType(val key: String, val label: String) {
    NET_DROP("net_drop", "放网"),
    SMASH("smash", "杀球"),
    CLEAR("clear", "高远"),
    LIFT("lift", "挑球"),
    DRIVE("drive", "平抽");

    companion object {
        fun fromKey(key: String): DrillActionType? = entries.find { it.key == key }
    }
}

/** 对打/比赛场次 */
data class MatchRecord(
    val id: String,
    val title: String,
    val dateLabel: String,
    val durationMin: Int,
    val strokeCount: Int,
    val avgScore: Int,
    val opponentLabel: String = "对打伙伴"
)

/** 单次击球（比赛模式） */
data class StrokeRecord(
    val id: String,
    val actionTypeLabel: String,
    val score: Int,
    val aiSuggestion: String,
    val ballSpeedKmh: Int,
    val powerN: Int,
    val hitTimeLabel: String
)

/** 单人动作练习单次记录 */
data class DrillSessionRecord(
    val id: String,
    val actionType: DrillActionType,
    val score: Int,
    val aiSuggestion: String,
    val ballSpeedKmh: Int,
    val powerN: Int,
    val dateTimeLabel: String
)
