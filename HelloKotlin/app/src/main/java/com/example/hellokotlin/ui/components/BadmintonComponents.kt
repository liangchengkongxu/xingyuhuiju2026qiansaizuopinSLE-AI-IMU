package com.example.hellokotlin.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.hellokotlin.data.model.AbilityRadar
import com.example.hellokotlin.data.model.WeeklyStat
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.ui.theme.ScoreHigh
import com.example.hellokotlin.ui.theme.ScoreLow
import com.example.hellokotlin.ui.theme.ScoreMid
import com.example.hellokotlin.ui.theme.ShuttleOrange
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

@Composable
fun SectionTitle(title: String, modifier: Modifier = Modifier) {
    Text(
        text = title,
        style = MaterialTheme.typography.titleMedium,
        fontWeight = FontWeight.SemiBold,
        modifier = modifier.padding(bottom = 8.dp)
    )
}

@Composable
fun StatCard(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    accent: Color = CourtGreen
) {
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(text = label, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(
                text = value,
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
                color = accent,
                modifier = Modifier.padding(top = 4.dp)
            )
        }
    }
}

@Composable
fun ScoreBadge(score: Int, modifier: Modifier = Modifier) {
    val color = when {
        score >= 85 -> ScoreHigh
        score >= 70 -> ScoreMid
        else -> ScoreLow
    }
    Box(
        modifier = modifier.size(48.dp),
        contentAlignment = Alignment.Center
    ) {
        Card(
            shape = RoundedCornerShape(12.dp),
            colors = CardDefaults.cardColors(containerColor = color.copy(alpha = 0.15f))
        ) {
            Text(
                text = score.toString(),
                modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                color = color,
                fontWeight = FontWeight.Bold,
                fontSize = 16.sp
            )
        }
    }
}

@Composable
fun RadarChart(
    data: AbilityRadar,
    modifier: Modifier = Modifier
) {
    val labels = listOf("力量", "速度", "耐力", "技巧")
    val values = listOf(data.power, data.speed, data.endurance, data.technique)

    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(220.dp),
            contentAlignment = Alignment.Center
        ) {
            Canvas(modifier = Modifier.size(180.dp)) {
                val center = Offset(size.width / 2, size.height / 2)
                val radius = min(size.width, size.height) / 2 * 0.85f
                val count = 4
                val angleStep = (2 * Math.PI / count).toFloat()
                val startAngle = (-Math.PI / 2).toFloat()

                for (level in 1..3) {
                    val r = radius * level / 3f
                    val gridPath = Path()
                    for (i in 0 until count) {
                        val angle = startAngle + i * angleStep
                        val x = center.x + r * cos(angle)
                        val y = center.y + r * sin(angle)
                        if (i == 0) gridPath.moveTo(x, y) else gridPath.lineTo(x, y)
                    }
                    gridPath.close()
                    drawPath(gridPath, color = Color(0xFFE0E0E0), style = Stroke(width = 1.5f))
                }

                val dataPath = Path()
                values.forEachIndexed { i, v ->
                    val angle = startAngle + i * angleStep
                    val r = radius * v
                    val x = center.x + r * cos(angle)
                    val y = center.y + r * sin(angle)
                    if (i == 0) dataPath.moveTo(x, y) else dataPath.lineTo(x, y)
                }
                dataPath.close()
                drawPath(dataPath, color = CourtGreen.copy(alpha = 0.25f))
                drawPath(dataPath, color = CourtGreen, style = Stroke(width = 2.5f))
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            labels.forEachIndexed { i, label ->
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(
                        "${(values[i] * 100).toInt()}",
                        fontWeight = FontWeight.Bold,
                        color = CourtGreen,
                        fontSize = 14.sp
                    )
                }
            }
        }
    }
}

@Composable
fun WeeklyBarChart(stats: List<WeeklyStat>, modifier: Modifier = Modifier) {
    val maxScore = stats.maxOf { it.score }.coerceAtLeast(1)

    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(140.dp)
            .padding(horizontal = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.Bottom
    ) {
        stats.forEach { stat ->
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Bottom,
                modifier = Modifier.weight(1f)
            ) {
                if (stat.score > 0) {
                    Text(
                        text = stat.score.toString(),
                        style = MaterialTheme.typography.labelSmall,
                        color = CourtGreen,
                        fontSize = 10.sp
                    )
                }
                Box(
                    modifier = Modifier
                        .padding(top = 4.dp, bottom = 6.dp)
                        .size(width = 28.dp, height = ((stat.score.toFloat() / maxScore) * 90).dp.coerceAtLeast(4.dp))
                ) {
                    Card(
                        modifier = Modifier.matchParentSize(),
                        shape = RoundedCornerShape(topStart = 6.dp, topEnd = 6.dp),
                        colors = CardDefaults.cardColors(
                            containerColor = if (stat.score > 0) CourtGreen else CourtGreenLight
                        )
                    ) {}
                }
                Text(
                    text = stat.dayLabel,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
fun HighlightBanner(
    title: String,
    subtitle: String,
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = CourtGreen)
    ) {
        Column(modifier = Modifier.padding(20.dp)) {
            Text(
                text = title,
                color = Color.White,
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = subtitle,
                color = Color.White.copy(alpha = 0.85f),
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(top = 6.dp)
            )
        }
    }
}

@Composable
fun RecommendCard(plan: String, weakPoint: String, modifier: Modifier = Modifier) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = ShuttleOrange.copy(alpha = 0.12f))
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text("个性化推荐", fontWeight = FontWeight.SemiBold, color = ShuttleOrange)
                Text(
                    text = "弱点：$weakPoint",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 4.dp)
                )
                Text(
                    text = plan,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                    modifier = Modifier.padding(top = 6.dp)
                )
            }
        }
    }
}
