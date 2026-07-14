package com.example.hellokotlin.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.hellokotlin.data.model.DrillSessionRecord
import com.example.hellokotlin.data.model.StrokeRecord
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight

@Composable
fun StrokeRecordCard(stroke: StrokeRecord, modifier: Modifier = Modifier) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.Top
        ) {
            ScoreBadge(score = stroke.score)
            Column(modifier = Modifier.weight(1f).padding(start = 12.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = androidx.compose.foundation.layout.Arrangement.SpaceBetween
                ) {
                    Text(stroke.actionTypeLabel, fontWeight = FontWeight.SemiBold)
                    Text(stroke.hitTimeLabel, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                MetricRow("球速", "${stroke.ballSpeedKmh} km/h")
                MetricRow("力度", "${stroke.powerN} N")
                AiSuggestionText(stroke.aiSuggestion)
            }
        }
    }
}

@Composable
fun DrillRecordCard(record: DrillSessionRecord, modifier: Modifier = Modifier) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.Top
        ) {
            ScoreBadge(score = record.score)
            Column(modifier = Modifier.weight(1f).padding(start = 12.dp)) {
                Text(record.dateTimeLabel, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                MetricRow("球速", "${record.ballSpeedKmh} km/h")
                MetricRow("力度", "${record.powerN} N")
                AiSuggestionText(record.aiSuggestion)
            }
        }
    }
}

@Composable
fun AiSuggestionText(text: String, modifier: Modifier = Modifier) {
    Card(
        modifier = modifier.fillMaxWidth().padding(top = 8.dp),
        shape = RoundedCornerShape(10.dp),
        colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
    ) {
        Text(
            text = "AI 建议：$text",
            style = MaterialTheme.typography.bodySmall,
            color = CourtGreen,
            modifier = Modifier.padding(10.dp)
        )
    }
}

@Composable
private fun MetricRow(label: String, value: String) {
    Text(
        text = "$label $value",
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = 2.dp)
    )
}
