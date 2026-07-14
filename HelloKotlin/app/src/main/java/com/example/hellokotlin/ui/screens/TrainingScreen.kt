package com.example.hellokotlin.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.hellokotlin.data.model.DrillActionType
import com.example.hellokotlin.data.model.DrillSessionRecord
import com.example.hellokotlin.data.model.MatchRecord
import com.example.hellokotlin.data.model.StrokeRecord
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.components.SectionTitle
import com.example.hellokotlin.ui.components.StatCard
import com.example.hellokotlin.ui.components.StrokeRecordCard
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.ui.theme.ShuttleOrange

@Composable
fun TrainingScreen(modifier: Modifier = Modifier) {
    var matches by remember { mutableStateOf<List<MatchRecord>>(emptyList()) }
    var drills by remember { mutableStateOf<List<DrillSessionRecord>>(emptyList()) }
    var latestStrokes by remember { mutableStateOf<List<StrokeRecord>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        val matchResult = XingyuRepository.getMatches()
        val drillResult = XingyuRepository.getDrills()
        matchResult.onSuccess { list ->
            matches = list
            list.firstOrNull()?.let { match ->
                XingyuRepository.getStrokes(match.id).onSuccess { latestStrokes = it.take(3) }
            }
        }
        drillResult.onSuccess { drills = it }
        loading = false
    }

    val latestMatch = matches.firstOrNull()

    LazyColumn(
        modifier = modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Text("训练概览", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
            Text(
                "汇总云端对打与专项练习 · 详情请在首页进入对应模式",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp, bottom = 8.dp)
            )
        }

        item {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                StatCard("对打场次", "${matches.size}", Modifier.weight(1f), ShuttleOrange)
                StatCard("练习记录", "${drills.size}", Modifier.weight(1f), CourtGreen)
            }
        }

        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("最近对打", fontWeight = FontWeight.SemiBold, color = CourtGreen)
                    Text(
                        latestMatch?.let { "${it.title} · 均分 ${it.avgScore} · ${it.strokeCount} 拍" }
                            ?: "暂无记录",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 6.dp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }

        item { SectionTitle("最近击球（对打）") }

        if (latestStrokes.isEmpty()) {
            item {
                Text("暂无击球记录", color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        } else {
            items(latestStrokes) { stroke ->
                StrokeRecordCard(stroke)
            }
        }

        item {
            SectionTitle("专项练习均分")
            DrillActionType.entries.forEach { action ->
                val records = drills.filter { it.actionType == action }
                val avg = if (records.isEmpty()) 0 else records.map { it.score }.average().toInt()
                Text(
                    "${action.label}：均分 $avg",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(vertical = 4.dp)
                )
            }
        }

        item { Spacer(modifier = Modifier.height(8.dp)) }
    }
}
