package com.example.hellokotlin.data

import com.example.hellokotlin.data.model.DrillActionType
import com.example.hellokotlin.data.model.DrillSessionRecord
import com.example.hellokotlin.data.model.MatchRecord
import com.example.hellokotlin.data.model.StrokeRecord

/** 云端数据占位（Hi3403 → L610 4G → 云服务器，后续 Retrofit 替换） */
object MockCloudData {

    val matchRecords = listOf(
        MatchRecord("m1", "周三晚场对打", "昨天 19:30", 48, 142, 81, "搭档 A"),
        MatchRecord("m2", "周末练习赛", "3月24日", 36, 98, 76, "搭档 B"),
        MatchRecord("m3", "馆内战", "3月22日", 52, 168, 85, "随机配对")
    )

    private val strokesByMatch = mapOf(
        "m1" to listOf(
            StrokeRecord("s1", "杀球", 88, "击球点偏后，建议提前引拍", 156, 92, "19:31:02"),
            StrokeRecord("s2", "平抽", 76, "拍面略开，注意控网前节奏", 98, 58, "19:31:18"),
            StrokeRecord("s3", "高远", 82, "蹬转发力良好，可再提高随挥", 112, 71, "19:32:05"),
            StrokeRecord("s4", "放网", 91, "手感细腻，建议落点再贴网", 45, 32, "19:33:40"),
            StrokeRecord("s5", "挑球", 79, "手腕略僵，放松击球", 68, 48, "19:35:12")
        ),
        "m2" to listOf(
            StrokeRecord("s1", "杀球", 72, "力量充足但角度偏直", 148, 88, "15:10:01"),
            StrokeRecord("s2", "高远", 80, "稳定性不错", 105, 65, "15:12:33")
        ),
        "m3" to listOf(
            StrokeRecord("s1", "平抽", 85, "反应快，保持拍头稳定", 102, 62, "20:05:44"),
            StrokeRecord("s2", "杀球", 90, "时机好，可加强落点变化", 162, 95, "20:08:11")
        )
    )

    fun strokesForMatch(matchId: String): List<StrokeRecord> =
        strokesByMatch[matchId].orEmpty()

    private val drillRecords = mapOf(
        DrillActionType.NET_DROP to listOf(
            DrillSessionRecord("d1", DrillActionType.NET_DROP, 86, "搓球过网高度略高，再贴网 5cm", 42, 28, "今天 14:20"),
            DrillSessionRecord("d2", DrillActionType.NET_DROP, 79, "节奏稳定，可加快二次启动", 38, 25, "昨天 16:05")
        ),
        DrillActionType.SMASH to listOf(
            DrillSessionRecord("d3", DrillActionType.SMASH, 91, "击球点优秀，注意杀直线后的回中", 158, 94, "今天 15:02"),
            DrillSessionRecord("d4", DrillActionType.SMASH, 84, "鞭打充分，建议增加角度变化", 152, 89, "3月24日")
        ),
        DrillActionType.CLEAR to listOf(
            DrillSessionRecord("d5", DrillActionType.CLEAR, 78, "击球偏晚，提早侧身", 118, 72, "今天 11:30"),
            DrillSessionRecord("d6", DrillActionType.CLEAR, 82, "弧线饱满，保持", 115, 70, "3月23日")
        ),
        DrillActionType.LIFT to listOf(
            DrillSessionRecord("d7", DrillActionType.LIFT, 80, "挑球到位，注意落点深度", 72, 46, "昨天 10:15"),
            DrillSessionRecord("d8", DrillActionType.LIFT, 77, "手腕发力可再柔和", 68, 42, "3月22日")
        ),
        DrillActionType.DRIVE to listOf(
            DrillSessionRecord("d9", DrillActionType.DRIVE, 88, "平抽速度快，保持拍面垂直", 105, 64, "今天 09:40"),
            DrillSessionRecord("d10", DrillActionType.DRIVE, 83, "连贯性佳，可练习变线", 98, 60, "3月21日")
        )
    )

    fun drillRecordsFor(action: DrillActionType): List<DrillSessionRecord> =
        drillRecords[action].orEmpty()

    fun drillSummary(action: DrillActionType): Pair<Int, Int> {
        val list = drillRecordsFor(action)
        if (list.isEmpty()) return 0 to 0
        return list.size to list.map { it.score }.average().toInt()
    }
}
