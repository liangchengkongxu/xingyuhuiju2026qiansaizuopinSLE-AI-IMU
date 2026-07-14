@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.example.hellokotlin.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.model.CommunityPost
import com.example.hellokotlin.data.model.PostComment
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.components.CommunityPostBody
import com.example.hellokotlin.ui.components.CommunityPostStatsRow
import com.example.hellokotlin.ui.components.CommunityUserAvatar
import com.example.hellokotlin.ui.theme.ShuttleOrange
import kotlinx.coroutines.launch

@Composable
fun PostDetailScreen(
    postId: String,
    onBack: () -> Unit,
    onDeleted: () -> Unit,
    modifier: Modifier = Modifier
) {
    var post by remember { mutableStateOf<CommunityPost?>(null) }
    var comments by remember { mutableStateOf<List<PostComment>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var commentsLoading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }
    var deleting by remember { mutableStateOf(false) }
    var showDeleteDialog by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val currentUserId = AppSession.currentUser?.id
    val canDelete = post?.author?.id == currentUserId

    LaunchedEffect(postId) {
        loading = true
        commentsLoading = true
        error = null
        XingyuRepository.getPost(postId).fold(
            onSuccess = { post = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
        XingyuRepository.getPostComments(postId).fold(
            onSuccess = { comments = it; commentsLoading = false },
            onFailure = {
                if (error == null) error = it.message
                commentsLoading = false
            }
        )
    }

    if (showDeleteDialog) {
        AlertDialog(
            onDismissRequest = { if (!deleting) showDeleteDialog = false },
            title = { Text("删除动态") },
            text = { Text("确定要删除这条动态吗？删除后无法恢复。") },
            confirmButton = {
                TextButton(
                    onClick = {
                        scope.launch {
                            deleting = true
                            XingyuRepository.deletePost(postId).fold(
                                onSuccess = {
                                    showDeleteDialog = false
                                    onDeleted()
                                },
                                onFailure = {
                                    error = it.message
                                    deleting = false
                                    showDeleteDialog = false
                                }
                            )
                        }
                    },
                    enabled = !deleting
                ) {
                    Text("删除", color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = {
                TextButton(onClick = { showDeleteDialog = false }, enabled = !deleting) {
                    Text("取消")
                }
            }
        )
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("动态详情") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                },
                actions = {
                    if (canDelete) {
                        IconButton(onClick = { showDeleteDialog = true }, enabled = !deleting) {
                            Icon(Icons.Default.Delete, contentDescription = "删除", tint = MaterialTheme.colorScheme.error)
                        }
                    }
                }
            )
        }
    ) { innerPadding ->
        when {
            loading -> {
                Column(
                    modifier = Modifier.fillMaxSize().padding(innerPadding),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    CircularProgressIndicator(modifier = Modifier.padding(top = 48.dp))
                }
            }
            error != null && post == null -> {
                Column(modifier = Modifier.fillMaxSize().padding(innerPadding).padding(20.dp)) {
                    Text(error.orEmpty(), color = MaterialTheme.colorScheme.error)
                    Button(onClick = onBack, modifier = Modifier.padding(top = 16.dp)) {
                        Text("返回")
                    }
                }
            }
            post != null -> {
                val item = post!!
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(innerPadding)
                        .verticalScroll(rememberScrollState())
                        .padding(20.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        CommunityUserAvatar(user = item.author, size = 48)
                        Column(modifier = Modifier.padding(start = 12.dp)) {
                            Text(item.author.nickname, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.titleMedium)
                            Text(
                                item.timeLabel,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }

                    CommunityPostBody(post = item, imageHeight = 280, modifier = Modifier.padding(top = 16.dp))
                    CommunityPostStatsRow(post = item)

                    val commentDisplayCount = when {
                        commentsLoading -> item.commentCount
                        else -> comments.size
                    }

                    HorizontalDivider(modifier = Modifier.padding(vertical = 20.dp))

                    Text(
                        text = "评论 $commentDisplayCount",
                        fontWeight = FontWeight.SemiBold,
                        style = MaterialTheme.typography.titleSmall
                    )

                    when {
                        commentsLoading -> {
                            CircularProgressIndicator(modifier = Modifier.padding(top = 16.dp))
                        }
                        comments.isEmpty() -> {
                            Text(
                                text = "暂无评论",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(top = 12.dp)
                            )
                        }
                        else -> {
                            Column(
                                modifier = Modifier.padding(top = 12.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                comments.forEach { comment ->
                                    CommentRow(comment = comment)
                                }
                            }
                        }
                    }

                    error?.let {
                        Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 12.dp))
                    }

                    if (canDelete) {
                        Button(
                            onClick = { showDeleteDialog = true },
                            enabled = !deleting,
                            modifier = Modifier.fillMaxWidth().padding(top = 24.dp),
                            colors = ButtonDefaults.buttonColors(containerColor = ShuttleOrange)
                        ) {
                            Text(if (deleting) "删除中…" else "删除我的动态")
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun CommentRow(comment: PostComment) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f))
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(12.dp),
            verticalAlignment = Alignment.Top
        ) {
            CommunityUserAvatar(user = comment.author, size = 36)
            Column(modifier = Modifier.padding(start = 10.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(comment.author.nickname, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.bodyMedium)
                    Text(
                        comment.timeLabel,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(start = 8.dp)
                    )
                }
                Text(
                    comment.content,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        }
    }
}
