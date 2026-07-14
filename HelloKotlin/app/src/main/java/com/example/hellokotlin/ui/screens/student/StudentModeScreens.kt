@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.example.hellokotlin.ui.screens.student

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
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material.icons.filled.School
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.hellokotlin.data.model.Classmate
import com.example.hellokotlin.data.model.CommunityUser
import com.example.hellokotlin.data.model.JoinedClassSummary
import com.example.hellokotlin.data.model.StudentClassDetail
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.components.CommunityUserAvatar
import com.example.hellokotlin.ui.components.SectionTitle
import com.example.hellokotlin.ui.components.StatCard
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.ui.theme.ShuttleOrange
import com.example.hellokotlin.ui.theme.ShuttleOrangeLight
import kotlinx.coroutines.launch

@Composable
fun StudentClassesScreen(
    onBack: () -> Unit,
    onOpenClass: (String) -> Unit,
    refreshKey: Int = 0,
    modifier: Modifier = Modifier
) {
    var classes by remember { mutableStateOf<List<JoinedClassSummary>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }
    var showJoinDialog by remember { mutableStateOf(false) }

    LaunchedEffect(refreshKey) {
        loading = true
        XingyuRepository.getJoinedClasses().fold(
            onSuccess = { classes = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    if (showJoinDialog) {
        JoinClassDialog(
            onDismiss = { showJoinDialog = false },
            onJoined = { joined ->
                showJoinDialog = false
                classes = listOf(joined) + classes.filter { it.id != joined.id }
            }
        )
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("我的班级") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showJoinDialog = true }, containerColor = CourtGreen) {
                Icon(Icons.Default.Add, contentDescription = "加入班级", tint = Color.White)
            }
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(innerPadding),
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
                ) {
                    Row(modifier = Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.School, contentDescription = null, tint = CourtGreen)
                        Column(modifier = Modifier.padding(start = 12.dp)) {
                            Text("学员模式", fontWeight = FontWeight.SemiBold, color = CourtGreen)
                            Text(
                                "输入教练分享的邀请码加入班级，训练数据自动同步给教练",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            if (loading) {
                item { CircularProgressIndicator(modifier = Modifier.padding(24.dp)) }
            }
            error?.let { msg ->
                item { Text(msg, color = MaterialTheme.colorScheme.error) }
            }
            if (!loading && classes.isEmpty() && error == null) {
                item {
                    Text(
                        "还没有加入班级，点击右下角 + 输入邀请码",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(vertical = 24.dp)
                    )
                }
            }

            items(classes, key = { it.id }) { joined ->
                JoinedClassCard(joined = joined, onClick = { onOpenClass(joined.id) })
            }

            item { Spacer(modifier = Modifier.height(72.dp)) }
        }
    }
}

@Composable
private fun JoinedClassCard(joined: JoinedClassSummary, onClick: () -> Unit) {
    Card(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(joined.name, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            if (joined.description.isNotBlank()) {
                Text(
                    joined.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 6.dp)
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("教练 ${joined.coachName}", style = MaterialTheme.typography.labelMedium, color = ShuttleOrange)
                Text("${joined.memberCount} 人 · ${joined.joinedLabel} 加入", style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}

@Composable
private fun JoinClassDialog(onDismiss: () -> Unit, onJoined: (JoinedClassSummary) -> Unit) {
    var code by remember { mutableStateOf("") }
    var loading by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()

    AlertDialog(
        onDismissRequest = { if (!loading) onDismiss() },
        title = { Text("加入班级") },
        text = {
            Column {
                Text(
                    "向教练索取邀请码，例如演示班 XY2026",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                OutlinedTextField(
                    value = code,
                    onValueChange = { code = it.uppercase() },
                    label = { Text("邀请码") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth().padding(top = 12.dp)
                )
                error?.let { Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 8.dp)) }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    scope.launch {
                        loading = true
                        error = null
                        XingyuRepository.joinClass(code).fold(
                            onSuccess = { onJoined(it) },
                            onFailure = { error = it.message; loading = false }
                        )
                    }
                },
                enabled = !loading && code.length >= 4
            ) { Text(if (loading) "加入中…" else "加入") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss, enabled = !loading) { Text("取消") }
        }
    )
}

@Composable
fun StudentClassDetailScreen(
    classId: String,
    onBack: () -> Unit,
    onLeft: () -> Unit,
    modifier: Modifier = Modifier
) {
    var detail by remember { mutableStateOf<StudentClassDetail?>(null) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }
    var showLeaveDialog by remember { mutableStateOf(false) }
    var leaving by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    LaunchedEffect(classId) {
        loading = true
        XingyuRepository.getStudentClassView(classId).fold(
            onSuccess = { detail = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    if (showLeaveDialog) {
        AlertDialog(
            onDismissRequest = { if (!leaving) showLeaveDialog = false },
            title = { Text("退出班级") },
            text = { Text("确定要退出该班级吗？教练将无法再查看你的训练数据。") },
            confirmButton = {
                TextButton(
                    onClick = {
                        scope.launch {
                            leaving = true
                            XingyuRepository.leaveClass(classId).fold(
                                onSuccess = {
                                    showLeaveDialog = false
                                    onLeft()
                                },
                                onFailure = {
                                    error = it.message
                                    leaving = false
                                    showLeaveDialog = false
                                }
                            )
                        }
                    },
                    enabled = !leaving
                ) { Text("退出", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { showLeaveDialog = false }, enabled = !leaving) { Text("取消") }
            }
        )
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text(detail?.name ?: "班级详情") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { innerPadding ->
        when {
            loading && detail == null -> {
                Column(
                    modifier = Modifier.fillMaxSize().padding(innerPadding),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    CircularProgressIndicator(modifier = Modifier.padding(top = 48.dp))
                }
            }
            detail != null -> {
                val item = detail!!
                LazyColumn(
                    modifier = Modifier.fillMaxSize().padding(innerPadding),
                    contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    item {
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(14.dp),
                            colors = CardDefaults.cardColors(containerColor = ShuttleOrangeLight)
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text(item.description.ifBlank { "暂无简介" }, style = MaterialTheme.typography.bodyMedium)
                                Text(
                                    "教练 ${item.coachName} · ${item.memberCount} 名学员",
                                    style = MaterialTheme.typography.labelMedium,
                                    color = ShuttleOrange,
                                    modifier = Modifier.padding(top = 10.dp)
                                )
                            }
                        }
                    }

                    item {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            StatCard(label = "我的排名", value = "第 ${item.myRank} 名", modifier = Modifier.weight(1f), accent = ShuttleOrange)
                            StatCard(label = "我的均分", value = "${item.myStats.avgScore}", modifier = Modifier.weight(1f))
                            StatCard(label = "最高球速", value = "${item.myStats.maxBallSpeed}", modifier = Modifier.weight(1f))
                        }
                    }

                    item {
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(14.dp),
                            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
                        ) {
                            Row(modifier = Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
                                CommunityUserAvatar(
                                    user = CommunityUser(
                                        id = item.myStats.userId,
                                        nickname = item.myStats.displayName,
                                        avatarColorHex = item.myStats.avatarColorHex,
                                        avatarUrl = item.myStats.avatarUrl
                                    ),
                                    size = 48
                                )
                                Column(modifier = Modifier.padding(start = 12.dp)) {
                                    Text("我的训练概况", fontWeight = FontWeight.SemiBold)
                                    Text(
                                        "${item.myStats.matchCount} 场对打 · ${item.myStats.drillCount} 次练习 · ${item.myStats.lastActiveLabel}",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.padding(top = 4.dp)
                                    )
                                }
                            }
                        }
                    }

                    item {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Default.EmojiEvents, contentDescription = null, tint = Color(0xFFFFB300))
                            SectionTitle("班内排行", modifier = Modifier.padding(start = 8.dp))
                        }
                    }

                    itemsIndexed(item.classmates, key = { _, c -> c.userId }) { index, classmate ->
                        ClassmateRow(classmate = classmate, rank = index + 1)
                    }

                    item {
                        OutlinedButton(
                            onClick = { showLeaveDialog = true },
                            enabled = !leaving,
                            modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
                            colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error)
                        ) {
                            Text(if (leaving) "退出中…" else "退出班级")
                        }
                    }

                    error?.let { msg ->
                        item { Text(msg, color = MaterialTheme.colorScheme.error) }
                    }
                }
            }
            else -> {
                Column(modifier = Modifier.padding(innerPadding).padding(20.dp)) {
                    Text(error.orEmpty(), color = MaterialTheme.colorScheme.error)
                    Button(onClick = onBack, modifier = Modifier.padding(top = 16.dp)) { Text("返回") }
                }
            }
        }
    }
}

@Composable
private fun ClassmateRow(classmate: Classmate, rank: Int) {
    val rankColor = when (rank) {
        1 -> Color(0xFFFFB300)
        2 -> Color(0xFF90A4AE)
        3 -> Color(0xFFBF8970)
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }
    val bg = if (classmate.isMe) CourtGreenLight else MaterialTheme.colorScheme.surface

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = bg)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "$rank",
                fontWeight = FontWeight.Bold,
                fontSize = if (rank <= 3) 18.sp else 14.sp,
                color = rankColor,
                modifier = Modifier.padding(end = 8.dp)
            )
            CommunityUserAvatar(
                user = CommunityUser(
                    id = classmate.userId,
                    nickname = classmate.displayName,
                    avatarColorHex = classmate.avatarColorHex,
                    avatarUrl = classmate.avatarUrl
                ),
                size = 40
            )
            Column(modifier = Modifier.weight(1f).padding(horizontal = 10.dp)) {
                Text(
                    if (classmate.isMe) "${classmate.displayName}（我）" else classmate.displayName,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    "均分 ${classmate.avgScore} · 球速 ${classmate.maxBallSpeed} km/h",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}
