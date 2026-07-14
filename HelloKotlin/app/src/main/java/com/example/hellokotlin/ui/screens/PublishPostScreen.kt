@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.example.hellokotlin.ui.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.BarChart
import androidx.compose.material.icons.filled.Image
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import com.example.hellokotlin.data.model.PostAttachmentKind
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.ui.theme.ShuttleOrange
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private enum class PublishAttachmentOption(val label: String, val kind: PostAttachmentKind) {
    TEXT("纯文字", PostAttachmentKind.NONE),
    IMAGE("分享图片", PostAttachmentKind.IMAGE),
    STATS("分享训练数据", PostAttachmentKind.TRAINING_STATS)
}

@Composable
fun PublishPostScreen(
    onBack: () -> Unit,
    onPublished: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var content by rememberSaveable { mutableStateOf("") }
    var attachment by rememberSaveable { mutableStateOf(PublishAttachmentOption.TEXT) }
    var imageCaption by rememberSaveable { mutableStateOf("") }
    var statsTitle by rememberSaveable { mutableStateOf("") }
    var statsDetail by rememberSaveable { mutableStateOf("") }
    var pickedUriString by rememberSaveable { mutableStateOf<String?>(null) }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    var loading by rememberSaveable { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val pickedUri = pickedUriString?.let { Uri.parse(it) }

    val imagePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.PickVisualMedia()
    ) { uri ->
        pickedUriString = uri?.toString()
        error = null
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("发布动态") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text(
                "分享训练心得、图片或数据卡片",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            OutlinedTextField(
                value = content,
                onValueChange = { content = it; error = null },
                modifier = Modifier.fillMaxWidth().height(140.dp),
                label = { Text("想说点什么…") },
                placeholder = { Text("例如：今天杀球手感不错，分享一下～") },
                shape = RoundedCornerShape(14.dp)
            )

            Text("附件类型", fontWeight = FontWeight.SemiBold)
            RowChips(
                options = PublishAttachmentOption.entries,
                selected = attachment,
                onSelect = {
                    attachment = it
                    error = null
                },
                label = { it.label }
            )

            when (attachment) {
                PublishAttachmentOption.IMAGE -> {
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(14.dp),
                        colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
                    ) {
                        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                            OutlinedButton(
                                onClick = {
                                    imagePicker.launch(
                                        PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)
                                    )
                                },
                                modifier = Modifier.fillMaxWidth(),
                                shape = RoundedCornerShape(12.dp)
                            ) {
                                Icon(Icons.Default.Image, contentDescription = null)
                                Text(
                                    if (pickedUri != null) "重新选择图片" else "从相册选择图片",
                                    modifier = Modifier.padding(start = 8.dp)
                                )
                            }
                            if (pickedUri != null) {
                                AsyncImage(
                                    model = pickedUri,
                                    contentDescription = "已选图片",
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .height(180.dp),
                                    contentScale = ContentScale.Crop
                                )
                            }
                            OutlinedTextField(
                                value = imageCaption,
                                onValueChange = { imageCaption = it },
                                modifier = Modifier.fillMaxWidth(),
                                label = { Text("图片说明（可选）") },
                                singleLine = true,
                                shape = RoundedCornerShape(12.dp)
                            )
                        }
                    }
                }
                PublishAttachmentOption.STATS -> {
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(14.dp),
                        colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
                    ) {
                        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                            Text("训练数据卡片", fontWeight = FontWeight.SemiBold, color = CourtGreen)
                            RowChips(
                                options = listOf("对打 · 最近一场", "杀球 · 练习", "高远 · 练习"),
                                selected = statsTitle,
                                onSelect = {
                                    statsTitle = it
                                    statsDetail = when (it) {
                                        "对打 · 最近一场" -> "均分 91 · 最高球速 328 km/h · 共 8 拍"
                                        "杀球 · 练习" -> "均分 94 · 球速 318 km/h · 力度 80 N"
                                        else -> "均分 91 · 球速 172 km/h · 力度 46 N"
                                    }
                                },
                                label = { it }
                            )
                            OutlinedTextField(
                                value = statsTitle,
                                onValueChange = { statsTitle = it },
                                modifier = Modifier.fillMaxWidth(),
                                label = { Text("数据标题") },
                                singleLine = true,
                                shape = RoundedCornerShape(12.dp)
                            )
                            OutlinedTextField(
                                value = statsDetail,
                                onValueChange = { statsDetail = it },
                                modifier = Modifier.fillMaxWidth(),
                                label = { Text("数据详情") },
                                shape = RoundedCornerShape(12.dp)
                            )
                            OutlinedButton(onClick = { /* 后续：从云端拉最近记录 */ }, shape = RoundedCornerShape(12.dp)) {
                                Icon(Icons.Default.BarChart, contentDescription = null)
                                Text("从云端选择最近记录", modifier = Modifier.padding(start = 8.dp))
                            }
                        }
                    }
                }
                PublishAttachmentOption.TEXT -> Unit
            }

            error?.let {
                Text(it, color = MaterialTheme.colorScheme.error)
            }

            Button(
                onClick = {
                    if (loading) return@Button
                    val trimmed = content.trim()
                    if (trimmed.isEmpty()) {
                        error = "请填写动态内容"
                        return@Button
                    }
                    if (attachment == PublishAttachmentOption.IMAGE && pickedUri == null) {
                        error = "请选择要分享的图片"
                        return@Button
                    }
                    if (attachment == PublishAttachmentOption.STATS && statsTitle.isBlank()) {
                        error = "请填写训练数据标题"
                        return@Button
                    }
                    scope.launch {
                        loading = true
                        error = null
                        val uploadResult = if (attachment == PublishAttachmentOption.IMAGE && pickedUri != null) {
                            withContext(Dispatchers.IO) {
                                val bytes = context.contentResolver.openInputStream(pickedUri)?.use { it.readBytes() }
                                    ?: return@withContext Result.failure(Exception("无法读取图片"))
                                XingyuRepository.uploadImage(bytes, "post_${System.currentTimeMillis()}.jpg")
                            }
                        } else {
                            Result.success(null)
                        }
                        uploadResult.fold(
                            onSuccess = { imageUrl ->
                                XingyuRepository.createPost(
                                    content = trimmed,
                                    attachmentKind = attachment.kind,
                                    imageUrl = imageUrl,
                                    imageCaption = imageCaption.takeIf { it.isNotBlank() },
                                    statsTitle = statsTitle.takeIf { it.isNotBlank() },
                                    statsDetail = statsDetail.takeIf { it.isNotBlank() }
                                ).fold(
                                    onSuccess = { onPublished() },
                                    onFailure = { err -> error = err.message; loading = false }
                                )
                            },
                            onFailure = { err ->
                                error = err.message
                                loading = false
                            }
                        )
                    }
                },
                enabled = !loading,
                modifier = Modifier.fillMaxWidth().height(52.dp),
                shape = RoundedCornerShape(14.dp),
                colors = ButtonDefaults.buttonColors(containerColor = ShuttleOrange)
            ) {
                Text(if (loading) "发布中…" else "发布", fontWeight = FontWeight.SemiBold)
            }

            Spacer(modifier = Modifier.height(8.dp))
        }
    }
}

@Composable
private fun <T> RowChips(
    options: List<T>,
    selected: T,
    onSelect: (T) -> Unit,
    label: (T) -> String,
    modifier: Modifier = Modifier
) {
    androidx.compose.foundation.layout.Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        options.forEach { option ->
            FilterChip(
                selected = selected == option,
                onClick = { onSelect(option) },
                label = { Text(label(option)) }
            )
        }
    }
}
