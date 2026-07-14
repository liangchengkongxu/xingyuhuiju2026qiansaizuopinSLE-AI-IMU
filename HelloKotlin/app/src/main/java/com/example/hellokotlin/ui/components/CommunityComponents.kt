package com.example.hellokotlin.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChatBubbleOutline
import androidx.compose.material.icons.filled.FavoriteBorder
import androidx.compose.material.icons.filled.Image
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import com.example.hellokotlin.data.model.CommunityPost
import com.example.hellokotlin.data.model.CommunityUser
import com.example.hellokotlin.data.model.PostAttachmentKind
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.util.resolveMediaUrl

@Composable
fun CommunityUserAvatar(user: CommunityUser, size: Int, modifier: Modifier = Modifier) {
    val avatarUrl = resolveMediaUrl(user.avatarUrl)
    Box(
        modifier = modifier
            .size(size.dp)
            .clip(CircleShape)
            .background(Color(user.avatarColorHex)),
        contentAlignment = Alignment.Center
    ) {
        if (avatarUrl != null) {
            AsyncImage(
                model = avatarUrl,
                contentDescription = user.nickname,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop
            )
        } else {
            Text(
                text = user.nickname.take(1),
                color = Color.White,
                fontWeight = FontWeight.Bold,
                fontSize = (size / 2.5).sp
            )
        }
    }
}

@Composable
fun CommunityPostBody(
    post: CommunityPost,
    imageHeight: Int = 200,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier) {
        Text(
            text = post.content,
            style = MaterialTheme.typography.bodyMedium
        )

        when (post.attachmentKind) {
            PostAttachmentKind.IMAGE -> {
                val imageUrl = resolveMediaUrl(post.imageUrl)
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 12.dp)
                        .height(imageHeight.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(CourtGreenLight),
                    contentAlignment = Alignment.Center
                ) {
                    if (imageUrl != null) {
                        AsyncImage(
                            model = imageUrl,
                            contentDescription = post.imageCaption,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Crop
                        )
                    } else {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(Icons.Default.Image, contentDescription = null, tint = CourtGreen, modifier = Modifier.size(40.dp))
                            Text(
                                post.imageCaption ?: "图片",
                                style = MaterialTheme.typography.bodySmall,
                                color = CourtGreen,
                                modifier = Modifier.padding(top = 8.dp)
                            )
                        }
                    }
                }
                post.imageCaption?.let { caption ->
                    Text(
                        caption,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 6.dp)
                    )
                }
            }
            PostAttachmentKind.TRAINING_STATS -> {
                Card(
                    modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
                    shape = RoundedCornerShape(12.dp),
                    colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
                ) {
                    Column(modifier = Modifier.padding(14.dp)) {
                        Text(post.statsTitle.orEmpty(), fontWeight = FontWeight.SemiBold, color = CourtGreen)
                        Text(
                            post.statsDetail.orEmpty(),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(top = 4.dp)
                        )
                    }
                }
            }
            PostAttachmentKind.NONE -> Unit
        }
    }
}

@Composable
fun CommunityPostStatsRow(post: CommunityPost, modifier: Modifier = Modifier) {
    Row(
        modifier = modifier.fillMaxWidth().padding(top = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(24.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.FavoriteBorder, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Text("${post.likeCount}", modifier = Modifier.padding(start = 4.dp), style = MaterialTheme.typography.bodySmall)
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.ChatBubbleOutline, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Text("${post.commentCount}", modifier = Modifier.padding(start = 4.dp), style = MaterialTheme.typography.bodySmall)
        }
    }
}
