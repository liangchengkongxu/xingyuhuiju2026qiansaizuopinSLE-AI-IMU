@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.example.hellokotlin.ui.screens.personal

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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Groups
import androidx.compose.material.icons.filled.School
import androidx.compose.material.icons.filled.Sports
import androidx.compose.material.icons.filled.SportsMartialArts
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.model.DrillActionType
import com.example.hellokotlin.data.model.DrillSessionRecord
import com.example.hellokotlin.data.model.MatchRecord
import com.example.hellokotlin.data.model.StrokeRecord
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.components.DrillRecordCard
import com.example.hellokotlin.ui.components.SectionTitle
import com.example.hellokotlin.ui.components.StatCard
import com.example.hellokotlin.ui.components.StrokeRecordCard
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.ShuttleOrange

@Composable
fun PersonalHomeScreen(
    onOpenMatchMode: () -> Unit,
    onOpenDrillMode: () -> Unit,
    onOpenClasses: () -> Unit,
    modifier: Modifier = Modifier
) {
    val user = AppSession.currentUser

    LazyColumn(
        modifier = modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Text(
                text = "你好，${user?.displayName ?: "训练者"}",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = "选择板端训练模式查看云端记录",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )
        }

        item {
            ModeEntryCard(
                title = "对打 / 比赛模式",
                subtitle = "查看每场对打的逐拍记录：得分、AI 建议、球速、力度、击球时间",
                icon = Icons.Default.Groups,
                accent = ShuttleOrange,
                onClick = onOpenMatchMode
            )
        }

        item {
            ModeEntryCard(
                title = "单人动作练习",
                subtitle = "放网 · 杀球 · 高远 · 挑球 · 平抽 五类动作专项记录",
                icon = Icons.Default.SportsMartialArts,
                accent = CourtGreen,
                onClick = onOpenDrillMode
            )
        }

        item {
            ModeEntryCard(
                title = "我的班级",
                subtitle = "输入邀请码加入教练班级 · 查看班内排名与训练同步状态",
                icon = Icons.Default.School,
                accent = Color(0xFF00838F),
                onClick = onOpenClasses
            )
        }
    }
}

@Composable
private fun ModeEntryCard(
    title: String,
    subtitle: String,
    icon: ImageVector,
    accent: androidx.compose.ui.graphics.Color,
    onClick: () -> Unit
) {
    Card(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = accent)
    ) {
        Row(
            modifier = Modifier.padding(20.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(icon, contentDescription = null, tint = androidx.compose.ui.graphics.Color.White, modifier = Modifier.padding(end = 16.dp))
            Column {
                Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, color = androidx.compose.ui.graphics.Color.White)
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = androidx.compose.ui.graphics.Color.White.copy(alpha = 0.88f), modifier = Modifier.padding(top = 6.dp))
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MatchRecordListScreen(onBack: () -> Unit, onOpenMatch: (String) -> Unit) {
    var matches by remember { mutableStateOf<List<MatchRecord>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        XingyuRepository.getMatches().fold(
            onSuccess = { matches = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("对打 / 比赛记录") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(padding),
            contentPadding = PaddingValues(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (loading) {
                item { Text("加载中…", color = MaterialTheme.colorScheme.onSurfaceVariant) }
            }
            error?.let { msg ->
                item { Text(msg, color = MaterialTheme.colorScheme.error) }
            }
            if (!loading && matches.isEmpty() && error == null) {
                item { Text("暂无对打记录", color = MaterialTheme.colorScheme.onSurfaceVariant) }
            }
            items(matches) { match ->
                Card(
                    onClick = { onOpenMatch(match.id) },
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(match.title, fontWeight = FontWeight.SemiBold)
                            Text("${match.avgScore} 分", fontWeight = FontWeight.Bold, color = CourtGreen)
                        }
                        Text(
                            "${match.dateLabel} · ${match.opponentLabel} · ${match.durationMin} 分钟 · ${match.strokeCount} 拍",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(top = 6.dp)
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MatchDetailScreen(matchId: String, onBack: () -> Unit) {
    var match by remember { mutableStateOf<MatchRecord?>(null) }
    var strokes by remember { mutableStateOf<List<StrokeRecord>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(matchId) {
        loading = true
        error = null
        val matchResult = XingyuRepository.getMatches()
        val strokeResult = XingyuRepository.getStrokes(matchId)
        matchResult.fold(
            onSuccess = { match = it.find { m -> m.id == matchId } },
            onFailure = { error = it.message }
        )
        strokeResult.fold(
            onSuccess = { strokes = it },
            onFailure = { if (error == null) error = it.message }
        )
        loading = false
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(match?.title ?: "比赛详情") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(padding),
            contentPadding = PaddingValues(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                if (loading) {
                    Text("加载中…", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                val currentMatch = match
                if (currentMatch != null) {
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        StatCard("均分", "${currentMatch.avgScore}", Modifier.weight(1f))
                        StatCard("击球", "${currentMatch.strokeCount} 拍", Modifier.weight(1f), ShuttleOrange)
                        StatCard("时长", "${currentMatch.durationMin} 分", Modifier.weight(1f))
                    }
                }
                SectionTitle("逐拍记录")
                Text(
                    "动作类型 · 得分 · AI 建议 · 球速 · 力度 · 击球时间",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            items(strokes) { stroke ->
                StrokeRecordCard(stroke)
            }
            item { Spacer(modifier = Modifier.height(8.dp)) }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DrillPracticeHomeScreen(onBack: () -> Unit, onOpenAction: (DrillActionType) -> Unit) {
    var drills by remember { mutableStateOf<List<DrillSessionRecord>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        XingyuRepository.getDrills().fold(
            onSuccess = { drills = it; loading = false },
            onFailure = { loading = false }
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("单人动作练习") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(padding),
            contentPadding = PaddingValues(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                Text(
                    "板端五类专项动作，点击查看每次练习的 AI 打分与建议",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            items(DrillActionType.entries) { action ->
                val records = drills.filter { it.actionType == action }
                val count = records.size
                val avg = if (records.isEmpty()) 0 else records.map { it.score }.average().toInt()
                Card(
                    onClick = { onOpenAction(action) },
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(16.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Default.Sports, contentDescription = null, tint = CourtGreen)
                            Column(modifier = Modifier.padding(start = 12.dp)) {
                                Text(action.label, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.titleMedium)
                                Text("$count 次记录 · 均分 $avg", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                        Text("查看", color = CourtGreen, fontWeight = FontWeight.Medium)
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DrillActionRecordsScreen(action: DrillActionType, onBack: () -> Unit) {
    var records by remember { mutableStateOf<List<DrillSessionRecord>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(action) {
        loading = true
        XingyuRepository.getDrills(action).fold(
            onSuccess = { records = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("${action.label} · 练习记录") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(padding),
            contentPadding = PaddingValues(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                if (loading) {
                    Text("加载中…", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                Text(
                    "AI 打分 · 建议 · 球速 · 力度（云端数据）",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            items(records) { record ->
                DrillRecordCard(record)
            }
        }
    }
}
