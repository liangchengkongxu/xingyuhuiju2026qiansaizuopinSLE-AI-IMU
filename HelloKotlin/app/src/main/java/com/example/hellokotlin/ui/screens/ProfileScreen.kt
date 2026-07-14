@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.example.hellokotlin.ui.screens

import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Help
import androidx.compose.material.icons.automirrored.filled.Logout
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.model.UserRole
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.components.SectionTitle
import com.example.hellokotlin.ui.components.StatCard
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.ui.theme.ShuttleOrange
import com.example.hellokotlin.util.AppReleaseInfo
import com.example.hellokotlin.util.OtaUpdateManager
import kotlinx.coroutines.launch

@Composable
fun ProfileScreen(
    onLogout: () -> Unit,
    onOpenHelp: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val user = AppSession.currentUser
    var matchCount by remember { mutableIntStateOf(0) }
    var strokeCount by remember { mutableIntStateOf(0) }
    var checkingUpdate by remember { mutableStateOf(false) }
    var updateMessage by remember { mutableStateOf<String?>(null) }
    var pendingRelease by remember { mutableStateOf<AppReleaseInfo?>(null) }
    var downloading by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        XingyuRepository.getMatches().onSuccess { matches ->
            matchCount = matches.size
            var total = 0
            matches.forEach { match ->
                XingyuRepository.getStrokes(match.id).onSuccess { total += it.size }
            }
            strokeCount = total
        }
    }

    fun checkUpdate(manual: Boolean) {
        scope.launch {
            checkingUpdate = true
            updateMessage = null
            XingyuRepository.checkAppUpdate().fold(
                onSuccess = { release ->
                    checkingUpdate = false
                    if (release != null) {
                        pendingRelease = release
                    } else if (manual) {
                        updateMessage = "当前已是最新版本 v${OtaUpdateManager.installedVersionName()}"
                    }
                },
                onFailure = {
                    checkingUpdate = false
                    updateMessage = it.message ?: "检查更新失败"
                }
            )
        }
    }

    pendingRelease?.let { release ->
        AlertDialog(
            onDismissRequest = { if (!downloading) pendingRelease = null },
            title = { Text("发现新版本 v${release.versionName}") },
            text = {
                Column {
                    Text(release.changelog, style = MaterialTheme.typography.bodyMedium)
                    if (downloading) {
                        CircularProgressIndicator(modifier = Modifier.padding(top = 16.dp))
                        Text("正在下载安装包…", modifier = Modifier.padding(top = 8.dp))
                    }
                }
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        if (!OtaUpdateManager.canInstallPackages(context)) {
                            OtaUpdateManager.openInstallPermissionSettings(context)
                            Toast.makeText(context, "请允许安装未知应用后重试", Toast.LENGTH_LONG).show()
                            return@TextButton
                        }
                        scope.launch {
                            downloading = true
                            OtaUpdateManager.downloadApk(context, release.apkUrl).fold(
                                onSuccess = { file ->
                                    downloading = false
                                    pendingRelease = null
                                    OtaUpdateManager.installApk(context, file).fold(
                                        onSuccess = { },
                                        onFailure = { updateMessage = it.message }
                                    )
                                },
                                onFailure = {
                                    downloading = false
                                    updateMessage = it.message
                                }
                            )
                        }
                    },
                    enabled = !downloading
                ) { Text("立即更新") }
            },
            dismissButton = {
                if (!release.forceUpdate) {
                    TextButton(onClick = { pendingRelease = null }, enabled = !downloading) {
                        Text("稍后")
                    }
                }
            }
        )
    }

    LazyColumn(
        modifier = modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Card(
                    modifier = Modifier.size(72.dp),
                    shape = CircleShape,
                    colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
                ) {
                    Icon(
                        Icons.Default.Person,
                        contentDescription = null,
                        tint = CourtGreen,
                        modifier = Modifier.fillMaxSize().padding(16.dp)
                    )
                }
                Column(modifier = Modifier.padding(start = 16.dp)) {
                    Text(user?.displayName ?: "未登录", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                    Text(user?.phone ?: "", color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(
                        AppSession.userRole?.label ?: "",
                        style = MaterialTheme.typography.bodySmall,
                        color = CourtGreen,
                        modifier = Modifier.padding(top = 2.dp)
                    )
                }
            }
        }

        item {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                StatCard("对打场次", "$matchCount", Modifier.weight(1f))
                StatCard("云端击球", "$strokeCount", Modifier.weight(1f), ShuttleOrange)
                StatCard("身份", if (AppSession.userRole == UserRole.PERSONAL) "个人" else "教练", Modifier.weight(1f))
            }
        }

        item { SectionTitle("应用") }

        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(14.dp),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("当前版本", fontWeight = FontWeight.Medium)
                    Text(
                        "v${OtaUpdateManager.installedVersionName()}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                    OutlinedButton(
                        onClick = { checkUpdate(manual = true) },
                        enabled = !checkingUpdate,
                        modifier = Modifier.fillMaxWidth().padding(top = 12.dp)
                    ) {
                        if (checkingUpdate) {
                            CircularProgressIndicator(modifier = Modifier.size(18.dp).padding(end = 8.dp))
                        } else {
                            Icon(Icons.Default.SystemUpdate, contentDescription = null, modifier = Modifier.padding(end = 8.dp))
                        }
                        Text(if (checkingUpdate) "检查中…" else "检查更新")
                    }
                    updateMessage?.let {
                        Text(it, color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(top = 8.dp), style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }

        item {
            ProfileMenuItem(
                icon = Icons.AutoMirrored.Filled.Help,
                title = "使用帮助",
                subtitle = "个人训练 · 班级 · 球友圈 · OTA 更新",
                onClick = onOpenHelp
            )
        }

        item {
            Button(
                onClick = onLogout,
                modifier = Modifier.fillMaxWidth().height(48.dp),
                shape = RoundedCornerShape(14.dp),
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.errorContainer, contentColor = MaterialTheme.colorScheme.onErrorContainer)
            ) {
                Icon(Icons.AutoMirrored.Filled.Logout, contentDescription = null)
                Text("退出登录", modifier = Modifier.padding(start = 8.dp))
            }
        }

        item { Spacer(modifier = Modifier.height(8.dp)) }
    }
}

@Composable
private fun ProfileMenuItem(
    icon: ImageVector,
    title: String,
    subtitle: String,
    onClick: () -> Unit
) {
    Card(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(icon, contentDescription = null, tint = CourtGreen)
            Column(modifier = Modifier.weight(1f).padding(horizontal = 12.dp)) {
                Text(title, fontWeight = FontWeight.Medium)
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Icon(Icons.Default.ChevronRight, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
