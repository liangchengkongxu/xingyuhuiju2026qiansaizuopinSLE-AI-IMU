@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package com.example.hellokotlin.ui.screens

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
fun HelpScreen(onBack: () -> Unit, modifier: Modifier = Modifier) {
    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("使用帮助") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                }
            )
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(innerPadding),
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp)
        ) {
            item {
                HelpSection(
                    title = "星羽汇聚是什么",
                    body = "羽毛球训练 App，连接 Hi3403 板端与云端。个人可查看对打/练习记录；教练可管理班级学员；球友圈可分享训练与排行。"
                )
            }
            item {
                HelpSection(
                    title = "个人训练者",
                    body = "首页进入「对打/比赛」查看每场逐拍数据（得分、AI 建议、球速、力度）；「单人动作练习」查看放网/杀球/高远/挑球/平抽记录。板端训练后数据自动同步到云端。"
                )
            }
            item {
                HelpSection(
                    title = "我的班级（学员）",
                    body = "首页 → 我的班级 → 输入教练邀请码加入（如 XY2026）。可查看班内排名与自己的训练概况。训练数据由板端上报后自动出现在班级排行中。"
                )
            }
            item {
                HelpSection(
                    title = "教练端",
                    body = "登录时选择「班级训练（教练）」。可创建班级、按手机号添加学员、查看每位学员的对打与练习记录。底部 Tab：班级 / 球友圈 / 我的。"
                )
            }
            item {
                HelpSection(
                    title = "球友圈",
                    body = "排行榜：球速、练习次数、得分三类榜单。动态：浏览、评论、发布图文或训练数据卡片。点进动态可查看完整评论列表。"
                )
            }
            item {
                HelpSection(
                    title = "检查更新（OTA）",
                    body = "我的 → 检查更新，可手动拉取线上新版本 APK 并安装。发布新版本后无需重新分发安装包，在服务器更新版本信息即可。"
                )
            }
            item {
                HelpSection(
                    title = "演示账号",
                    body = "个人学员：13800138000 / 123456\n教练：13800138005 / 123456\n班级邀请码：XY2026（周末提高班）、XYJUN（青少年基础班）"
                )
            }
            item {
                HelpSection(
                    title = "常见问题",
                    body = "• 看不到训练数据：确认板端上报时 user_phone 与 App 登录手机号一致。\n• 评论数不准：已在 v0.11 修复，以实际评论列表为准。\n• 更新安装失败：在系统设置中允许「安装未知应用」后重试。"
                )
            }
        }
    }
}

@Composable
private fun HelpSection(title: String, body: String) {
    Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, modifier = Modifier.padding(bottom = 8.dp, top = 8.dp))
    Text(body, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(bottom = 12.dp))
}
