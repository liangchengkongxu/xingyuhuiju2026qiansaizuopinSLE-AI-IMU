package com.example.hellokotlin.data.model

enum class ActionType(val label: String) {
    SMASH("杀球"),
    CLEAR("高远球"),
    DROP("吊球"),
    SERVE("发球"),
    NET("搓球")
}

data class AbilityRadar(
    val power: Float,
    val speed: Float,
    val endurance: Float,
    val technique: Float
)

data class TrainingSession(
    val id: String,
    val dateLabel: String,
    val durationMin: Int,
    val swingCount: Int,
    val avgScore: Int
)

data class SwingAction(
    val id: String,
    val type: ActionType,
    val score: Int,
    val timeLabel: String,
    val power: Int,
    val speed: Int
)

data class UserProfile(
    val name: String,
    val level: String,
    val totalSessions: Int,
    val totalSwings: Int,
    val streakDays: Int
)

data class WeeklyStat(
    val dayLabel: String,
    val score: Int
)
