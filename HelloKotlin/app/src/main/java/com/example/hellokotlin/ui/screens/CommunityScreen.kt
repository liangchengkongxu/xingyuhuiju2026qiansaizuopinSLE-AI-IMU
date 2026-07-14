@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

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
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.outlined.Leaderboard
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.hellokotlin.data.model.CommunityPost
import com.example.hellokotlin.data.model.RankingEntry
import com.example.hellokotlin.data.model.RankingType
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.components.CommunityPostBody
import com.example.hellokotlin.ui.components.CommunityPostStatsRow
import com.example.hellokotlin.ui.components.CommunityUserAvatar
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.CourtGreenLight
import com.example.hellokotlin.ui.theme.ShuttleOrange
import com.example.hellokotlin.ui.theme.ShuttleOrangeLight

private enum class CommunitySection(val label: String) {
    RANKING("排行榜"),
    FEED("动态")
}

@Composable
fun CommunityScreen(
    onOpenPublish: () -> Unit,
    onOpenPost: (String) -> Unit,
    feedRefreshKey: Int = 0,
    modifier: Modifier = Modifier
) {
    var sectionIndex by rememberSaveable { mutableIntStateOf(CommunitySection.FEED.ordinal) }
    val sections = CommunitySection.entries

    Column(modifier = modifier.fillMaxSize()) {
        Column(modifier = Modifier.padding(horizontal = 20.dp, vertical = 16.dp)) {
            Text(
                text = "球友圈",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = "排行竞技 · 分享训练",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )
        }

        PrimaryTabRow(selectedTabIndex = sectionIndex) {
            sections.forEachIndexed { index, section ->
                Tab(
                    selected = sectionIndex == index,
                    onClick = { sectionIndex = index },
                    text = { Text(section.label) }
                )
            }
        }

        when (sections[sectionIndex]) {
            CommunitySection.RANKING -> RankingSection(modifier = Modifier.weight(1f))
            CommunitySection.FEED -> FeedSection(
                onOpenPublish = onOpenPublish,
                onOpenPost = onOpenPost,
                feedRefreshKey = feedRefreshKey,
                modifier = Modifier.weight(1f)
            )
        }
    }
}

@Composable
private fun RankingSection(modifier: Modifier = Modifier) {
    var rankingType by remember { mutableStateOf(RankingType.BALL_SPEED) }
    var entries by remember { mutableStateOf<List<RankingEntry>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(rankingType) {
        loading = true
        XingyuRepository.getRankings(rankingType).fold(
            onSuccess = { entries = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    LazyColumn(
        modifier = modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                RankingType.entries.forEach { type ->
                    FilterChip(
                        selected = rankingType == type,
                        onClick = { rankingType = type },
                        label = { Text(type.label, maxLines = 1, overflow = TextOverflow.Ellipsis) }
                    )
                }
            }
        }

        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = CourtGreenLight)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(Icons.Outlined.Leaderboard, contentDescription = null, tint = CourtGreen)
                    Column(modifier = Modifier.padding(start = 12.dp)) {
                        Text("本周 ${rankingType.label}", fontWeight = FontWeight.SemiBold, color = CourtGreen)
                        Text(
                            "云端汇总 · 实时排行",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }

        if (loading) {
            item { Text("加载中…", color = MaterialTheme.colorScheme.onSurfaceVariant) }
        }
        error?.let { msg ->
            item { Text(msg, color = MaterialTheme.colorScheme.error) }
        }
        if (!loading && entries.isEmpty() && error == null) {
            item { Text("暂无排行数据", color = MaterialTheme.colorScheme.onSurfaceVariant) }
        }

        items(entries) { entry ->
            RankingRow(entry = entry, unit = rankingType.unit)
        }

        item { Spacer(modifier = Modifier.height(8.dp)) }
    }
}

@Composable
private fun RankingRow(entry: RankingEntry, unit: String) {
    val rankColor = when (entry.rank) {
        1 -> Color(0xFFFFB300)
        2 -> Color(0xFF90A4AE)
        3 -> Color(0xFFBF8970)
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = if (entry.rank <= 3) 2.dp else 1.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "${entry.rank}",
                fontWeight = FontWeight.Bold,
                fontSize = if (entry.rank <= 3) 20.sp else 16.sp,
                color = rankColor,
                modifier = Modifier.padding(end = 4.dp)
            )
            CommunityUserAvatar(user = entry.user, size = 44)
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = 12.dp)
            ) {
                Text(entry.user.nickname, fontWeight = FontWeight.SemiBold)
                if (entry.rank <= 3) {
                    Text(
                        "TOP ${entry.rank}",
                        style = MaterialTheme.typography.labelSmall,
                        color = rankColor
                    )
                }
            }
            Text(
                text = "${entry.value} $unit",
                fontWeight = FontWeight.Bold,
                color = CourtGreen
            )
        }
    }
}

@Composable
private fun FeedSection(
    onOpenPublish: () -> Unit,
    onOpenPost: (String) -> Unit,
    feedRefreshKey: Int,
    modifier: Modifier = Modifier
) {
    var posts by remember { mutableStateOf<List<CommunityPost>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }
    val listState = rememberSaveable(saver = LazyListState.Saver) {
        LazyListState()
    }

    LaunchedEffect(feedRefreshKey) {
        loading = true
        XingyuRepository.getPosts().fold(
            onSuccess = { posts = it; loading = false },
            onFailure = { error = it.message; loading = false }
        )
    }

    LazyColumn(
        state = listState,
        modifier = modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                onClick = onOpenPublish,
                shape = RoundedCornerShape(14.dp),
                colors = CardDefaults.cardColors(containerColor = ShuttleOrangeLight)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(Icons.Default.Share, contentDescription = null, tint = ShuttleOrange)
                    Text(
                        "分享训练数据、图片或文字…",
                        modifier = Modifier.padding(start = 12.dp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }

        if (loading) {
            item { Text("加载中…", color = MaterialTheme.colorScheme.onSurfaceVariant) }
        }
        error?.let { msg ->
            item { Text(msg, color = MaterialTheme.colorScheme.error) }
        }

        items(posts, key = { it.id }) { post ->
            FeedPostCard(post = post, onClick = { onOpenPost(post.id) })
        }

        item { Spacer(modifier = Modifier.height(8.dp)) }
    }
}

@Composable
private fun FeedPostCard(post: CommunityPost, onClick: () -> Unit) {
    Card(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CommunityUserAvatar(user = post.author, size = 40)
                Column(modifier = Modifier.padding(start = 10.dp)) {
                    Text(post.author.nickname, fontWeight = FontWeight.SemiBold)
                    Text(
                        post.timeLabel,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            CommunityPostBody(post = post, modifier = Modifier.padding(top = 12.dp))
            CommunityPostStatsRow(post = post)
        }
    }
}
