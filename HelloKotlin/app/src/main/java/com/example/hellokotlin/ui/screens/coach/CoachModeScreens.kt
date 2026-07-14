@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.example.hellokotlin.ui.screens.coach

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
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Groups
import androidx.compose.material.icons.filled.PersonAdd
import androidx.compose.material.icons.filled.School
import androidx.compose.material.icons.filled.Speed
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
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.model.DrillSessionRecord
import com.example.hellokotlin.data.model.MatchRecord
import com.example.hellokotlin.data.model.StudentSummary
import com.example.hellokotlin.data.model.TrainingClassDetail
import com.example.hellokotlin.data.model.TrainingClassSummary
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.components.CommunityUserAvatar
import com.example.hellokotlin.ui.components.DrillRecordCard
import com.example.hellokotlin.ui.components.SectionTitle
import com.example.hellokotlin.ui.components.StatCard
import com.example.hellokotlin.ui.components.StrokeRecordCard
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.ui.theme.ShuttleOrange
import com.example.hellokotlin.ui.theme.ShuttleOrangeLight
import kotlinx.coroutines.launch

@Composable
fun CoachHomeScreen(
    onOpenClass: (String) -> Unit,
    refreshKey: Int = 0,
    modifier: Modifier = Modifier
) {
    var classes by remember { mutableStateOf<List<TrainingClassSummary>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }
    var showCreateDialog by remember { mutableStateOf(false) }
    val coachName = AppSession.currentUser?.displayName.orEmpty()

    LaunchedEffect(refreshKey) {
        loading = true
        XingyuRepository.getClasses().fold(
            onSuccess = { classes = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    if (showCreateDialog) {
        CreateClassDialog(
            onDismiss = { showCreateDialog = false },
            onCreated = { created ->
                showCreateDialog = false
                classes = listOf(created) + classes
            }
        )
    }

    Scaffold(
        modifier = modifier,
        floatingActionButton = {
            FloatingActionButton(onClick = { showCreateDialog = true }, containerColor = ShuttleOrange) {
                Icon(Icons.Default.Add, contentDescription = "新建班级", tint = androidx.compose.ui.graphics.Color.White)
            }
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(innerPadding),
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                Text("教练工作台", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
                Text(
                    "你好，$coachName · 管理班级与学员训练数据",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 4.dp, bottom = 8.dp)
                )
            }

            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(16.dp),
                    colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
                ) {
                    Row(modifier = Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.School, contentDescription = null, tint = CourtGreen)
                        Column(modifier = Modifier.padding(start = 12.dp)) {
                            Text("班级课模式", fontWeight = FontWeight.SemiBold, color = CourtGreen)
                            Text(
                                "学员板端数据自动上云 · 教练统一查看",
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
                        "还没有班级，点击右下角 + 创建第一个班级",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(vertical = 24.dp)
                    )
                }
            }

            items(classes, key = { it.id }) { trainingClass ->
                ClassSummaryCard(trainingClass = trainingClass, onClick = { onOpenClass(trainingClass.id) })
            }

            item { Spacer(modifier = Modifier.height(72.dp)) }
        }
    }
}

@Composable
private fun ClassSummaryCard(trainingClass: TrainingClassSummary, onClick: () -> Unit) {
    Card(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Groups, contentDescription = null, tint = ShuttleOrange)
                Text(
                    trainingClass.name,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(start = 10.dp)
                )
            }
            if (trainingClass.description.isNotBlank()) {
                Text(
                    trainingClass.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 8.dp)
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("${trainingClass.memberCount} 名学员", style = MaterialTheme.typography.labelMedium, color = CourtGreen)
                Text("邀请码 ${trainingClass.inviteCode}", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

@Composable
private fun CreateClassDialog(onDismiss: () -> Unit, onCreated: (TrainingClassSummary) -> Unit) {
    var name by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }
    var loading by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()

    AlertDialog(
        onDismissRequest = { if (!loading) onDismiss() },
        title = { Text("新建班级") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("班级名称") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                OutlinedTextField(
                    value = description,
                    onValueChange = { description = it },
                    label = { Text("简介（可选）") },
                    modifier = Modifier.fillMaxWidth()
                )
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    scope.launch {
                        loading = true
                        error = null
                        XingyuRepository.createClass(name, description).fold(
                            onSuccess = { onCreated(it) },
                            onFailure = { error = it.message; loading = false }
                        )
                    }
                },
                enabled = !loading && name.isNotBlank()
            ) { Text(if (loading) "创建中…" else "创建") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss, enabled = !loading) { Text("取消") }
        }
    )
}

@Composable
fun CoachClassDetailScreen(
    classId: String,
    onBack: () -> Unit,
    onOpenStudent: (String, String) -> Unit,
    refreshKey: Int = 0,
    modifier: Modifier = Modifier
) {
    var detail by remember { mutableStateOf<TrainingClassDetail?>(null) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }
    var showAddDialog by remember { mutableStateOf(false) }
    val clipboard = LocalClipboardManager.current
    val scope = rememberCoroutineScope()

    fun reload() {
        scope.launch {
            loading = true
            XingyuRepository.getClass(classId).fold(
                onSuccess = { detail = it; loading = false },
                onFailure = { error = it.message; loading = false }
            )
        }
    }

    LaunchedEffect(classId, refreshKey) { reload() }

    if (showAddDialog) {
        AddStudentDialog(
            onDismiss = { showAddDialog = false },
            onAdded = { showAddDialog = false; reload() },
            classId = classId
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
                },
                actions = {
                    IconButton(onClick = { showAddDialog = true }) {
                        Icon(Icons.Default.PersonAdd, contentDescription = "添加学员")
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
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Text("邀请码：${item.inviteCode}", fontWeight = FontWeight.SemiBold, color = ShuttleOrange)
                                    IconButton(onClick = {
                                        clipboard.setText(AnnotatedString(item.inviteCode))
                                    }) {
                                        Icon(Icons.Default.ContentCopy, contentDescription = "复制邀请码", tint = ShuttleOrange)
                                    }
                                }
                                Text(
                                    "${item.memberCount} 名学员 · 创建于 ${item.createdLabel}",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }

                    item {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            StatCard(
                                label = "学员",
                                value = "${item.memberCount}",
                                modifier = Modifier.weight(1f)
                            )
                            val avgAll = if (item.members.isEmpty()) 0 else item.members.map { it.avgScore }.average().toInt()
                            StatCard(label = "班均分", value = "$avgAll", modifier = Modifier.weight(1f))
                            val maxSpeed = item.members.maxOfOrNull { it.maxBallSpeed } ?: 0
                            StatCard(label = "最高球速", value = "$maxSpeed", modifier = Modifier.weight(1f))
                        }
                    }

                    item { SectionTitle("学员列表") }

                    if (item.members.isEmpty()) {
                        item {
                            Text(
                                "暂无学员，点击右上角添加，或让学员用邀请码加入",
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }

                    items(item.members, key = { it.userId }) { student ->
                        StudentRow(
                            student = student,
                            onClick = { onOpenStudent(classId, student.userId) }
                        )
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
private fun StudentRow(student: StudentSummary, onClick: () -> Unit) {
    Card(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            CommunityUserAvatar(
                user = com.example.hellokotlin.data.model.CommunityUser(
                    id = student.userId,
                    nickname = student.displayName,
                    avatarColorHex = student.avatarColorHex,
                    avatarUrl = student.avatarUrl
                ),
                size = 44
            )
            Column(modifier = Modifier.weight(1f).padding(horizontal = 12.dp)) {
                Text(student.displayName, fontWeight = FontWeight.SemiBold)
                Text(
                    "${student.phone} · ${student.lastActiveLabel}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Column(horizontalAlignment = Alignment.End) {
                Text("${student.avgScore}分", fontWeight = FontWeight.Bold, color = CourtGreen)
                Text("${student.matchCount}场/${student.drillCount}练", style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}

@Composable
private fun AddStudentDialog(classId: String, onDismiss: () -> Unit, onAdded: () -> Unit) {
    var phone by remember { mutableStateOf("") }
    var loading by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()

    AlertDialog(
        onDismissRequest = { if (!loading) onDismiss() },
        title = { Text("添加学员") },
        text = {
            Column {
                Text(
                    "输入学员注册手机号，对方需已安装 App 并完成注册",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                OutlinedTextField(
                    value = phone,
                    onValueChange = { phone = it },
                    label = { Text("学员手机号") },
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
                        XingyuRepository.addClassMember(classId, phone).fold(
                            onSuccess = { onAdded() },
                            onFailure = { error = it.message; loading = false }
                        )
                    }
                },
                enabled = !loading && phone.length >= 6
            ) { Text(if (loading) "添加中…" else "添加") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss, enabled = !loading) { Text("取消") }
        }
    )
}

private enum class StudentTab(val label: String) {
    MATCHES("对打记录"),
    DRILLS("练习记录")
}

@Composable
fun CoachStudentScreen(
    classId: String,
    studentId: String,
    onBack: () -> Unit,
    onOpenMatch: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    var student by remember { mutableStateOf<StudentSummary?>(null) }
    var matches by remember { mutableStateOf<List<MatchRecord>>(emptyList()) }
    var drills by remember { mutableStateOf<List<DrillSessionRecord>>(emptyList()) }
    var tabIndex by remember { mutableIntStateOf(0) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(classId, studentId) {
        loading = true
        error = null
        val classResult = XingyuRepository.getClass(classId)
        classResult.onSuccess { detail ->
            student = detail.members.find { it.userId == studentId }
        }.onFailure { error = it.message }

        XingyuRepository.getStudentMatches(classId, studentId).onSuccess { matches = it }
        XingyuRepository.getStudentDrills(classId, studentId).onSuccess { drills = it }
        loading = false
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text(student?.displayName ?: "学员详情") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { innerPadding ->
        Column(modifier = Modifier.fillMaxSize().padding(innerPadding)) {
            student?.let { s ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    StatCard(label = "均分", value = "${s.avgScore}", modifier = Modifier.weight(1f))
                    StatCard(label = "最高球速", value = "${s.maxBallSpeed}", modifier = Modifier.weight(1f))
                    StatCard(label = "对打/练习", value = "${s.matchCount}/${s.drillCount}", modifier = Modifier.weight(1f))
                }
            }

            PrimaryTabRow(selectedTabIndex = tabIndex) {
                StudentTab.entries.forEachIndexed { index, tab ->
                    Tab(
                        selected = tabIndex == index,
                        onClick = { tabIndex = index },
                        text = { Text(tab.label) }
                    )
                }
            }

            when {
                loading -> CircularProgressIndicator(modifier = Modifier.padding(24.dp))
                tabIndex == 0 -> {
                    if (matches.isEmpty()) {
                        Text("暂无对打记录", modifier = Modifier.padding(20.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    } else {
                        LazyColumn(
                            contentPadding = PaddingValues(20.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            items(matches, key = { it.id }) { match ->
                                Card(
                                    onClick = { onOpenMatch(match.id) },
                                    modifier = Modifier.fillMaxWidth(),
                                    shape = RoundedCornerShape(12.dp)
                                ) {
                                    Column(modifier = Modifier.padding(14.dp)) {
                                        Text(match.title, fontWeight = FontWeight.SemiBold)
                                        Text(
                                            "${match.dateLabel} · ${match.durationMin}分钟 · 均分${match.avgScore}",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            modifier = Modifier.padding(top = 4.dp)
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
                else -> {
                    if (drills.isEmpty()) {
                        Text("暂无练习记录", modifier = Modifier.padding(20.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    } else {
                        LazyColumn(
                            contentPadding = PaddingValues(20.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            items(drills, key = { it.id }) { drill ->
                                DrillRecordCard(record = drill)
                            }
                        }
                    }
                }
            }

            error?.let {
                Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(20.dp))
            }
        }
    }
}

@Composable
fun CoachStudentMatchScreen(
    classId: String,
    studentId: String,
    matchId: String,
    onBack: () -> Unit,
    modifier: Modifier = Modifier
) {
    var strokes by remember { mutableStateOf<List<com.example.hellokotlin.data.model.StrokeRecord>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(classId, studentId, matchId) {
        loading = true
        XingyuRepository.getStudentStrokes(classId, studentId, matchId).fold(
            onSuccess = { strokes = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("逐拍记录") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { innerPadding ->
        when {
            loading -> CircularProgressIndicator(modifier = Modifier.padding(innerPadding).padding(24.dp))
            strokes.isEmpty() -> Text("暂无击球记录", modifier = Modifier.padding(innerPadding).padding(20.dp))
            else -> LazyColumn(
                modifier = Modifier.fillMaxSize().padding(innerPadding),
                contentPadding = PaddingValues(20.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                items(strokes, key = { it.id }) { stroke ->
                    StrokeRecordCard(stroke = stroke)
                }
            }
        }
        error?.let {
            Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(20.dp))
        }
    }
}
