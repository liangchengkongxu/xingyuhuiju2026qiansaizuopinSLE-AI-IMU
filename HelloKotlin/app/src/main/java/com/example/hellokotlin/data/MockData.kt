package com.example.hellokotlin.data

import com.example.hellokotlin.data.model.AbilityRadar
import com.example.hellokotlin.data.model.ActionType
import com.example.hellokotlin.data.model.SwingAction
import com.example.hellokotlin.data.model.TrainingSession
import com.example.hellokotlin.data.model.UserProfile
import com.example.hellokotlin.data.model.WeeklyStat

object MockData {
    val profile = UserProfile(
        name = "球友小明",
        level = "业余进阶",
        totalSessions = 24,
        totalSwings = 3860,
        streakDays = 5
    )

    val todaySummary = TrainingSession(
        id = "today",
        dateLabel = "今天",
        durationMin = 42,
        swingCount = 186,
        avgScore = 82
    )

    val recentSessions = listOf(
        TrainingSession("s1", "昨天", 38, 152, 79),
        TrainingSession("s2", "3月24日", 55, 210, 85),
        TrainingSession("s3", "3月22日", 30, 98, 76)
    )

    val todayActions = listOf(
        SwingAction("a1", ActionType.SMASH, 88, "14:32", 92, 78),
        SwingAction("a2", ActionType.CLEAR, 76, "14:28", 68, 65),
        SwingAction("a3", ActionType.DROP, 91, "14:25", 55, 72),
        SwingAction("a4", ActionType.SERVE, 84, "14:20", 60, 58),
        SwingAction("a5", ActionType.SMASH, 79, "14:15", 88, 81),
        SwingAction("a6", ActionType.NET, 86, "14:10", 42, 45)
    )

    val abilityRadar = AbilityRadar(
        power = 0.78f,
        speed = 0.85f,
        endurance = 0.72f,
        technique = 0.80f
    )

    val weeklyScores = listOf(
        WeeklyStat("一", 72),
        WeeklyStat("二", 0),
        WeeklyStat("三", 78),
        WeeklyStat("四", 81),
        WeeklyStat("五", 85),
        WeeklyStat("六", 82),
        WeeklyStat("日", 79)
    )

    val weakPoint = "反手吊球"
    val recommendPlan = "反手吊球专项 · 20分钟"
}
