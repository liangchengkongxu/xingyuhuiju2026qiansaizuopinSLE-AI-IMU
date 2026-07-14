package com.example.hellokotlin.ui.screens.auth

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import com.example.hellokotlin.R
import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.model.UserRole
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.theme.CourtGreen
import com.example.hellokotlin.ui.theme.ShuttleOrange
import kotlinx.coroutines.launch

@Composable
private fun AppLogo(size: Dp, modifier: Modifier = Modifier) {
    Image(
        painter = painterResource(R.drawable.ic_app_logo),
        contentDescription = "星羽汇聚",
        modifier = modifier
            .size(size)
            .clip(RoundedCornerShape(size * 0.22f)),
        contentScale = ContentScale.Fit
    )
}

@Composable
fun LoginScreen(
    onLoginSuccess: () -> Unit,
    onNavigateRegister: () -> Unit,
    modifier: Modifier = Modifier
) {
    var phone by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    var loading by rememberSaveable { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 28.dp, vertical = 48.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        AppLogo(size = 88.dp)
        Text(
            "星羽汇聚",
            style = MaterialTheme.typography.headlineLarge,
            fontWeight = FontWeight.Bold,
            color = CourtGreen,
            modifier = Modifier.padding(top = 20.dp)
        )
        Text(
            "登录后同步云端训练数据（Hi3403 → 云）",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 8.dp, bottom = 32.dp)
        )

        OutlinedTextField(
            value = phone,
            onValueChange = { phone = it; error = null },
            label = { Text("手机号") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
            modifier = Modifier.fillMaxWidth()
        )
        Spacer(modifier = Modifier.height(12.dp))
        OutlinedTextField(
            value = password,
            onValueChange = { password = it; error = null },
            label = { Text("密码") },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
            modifier = Modifier.fillMaxWidth()
        )

        error?.let {
            Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 8.dp))
        }

        Button(
            onClick = {
                if (loading) return@Button
                scope.launch {
                    loading = true
                    error = null
                    XingyuRepository.login(phone.trim(), password).fold(
                        onSuccess = { onLoginSuccess() },
                        onFailure = { error = it.message }
                    )
                    loading = false
                }
            },
            enabled = !loading,
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 24.dp)
                .height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(containerColor = ShuttleOrange)
        ) {
            Text(if (loading) "登录中…" else "登录", fontWeight = FontWeight.SemiBold)
        }

        TextButton(onClick = onNavigateRegister, modifier = Modifier.padding(top = 8.dp)) {
            Text("没有账号？注册")
        }
    }
}

@Composable
fun RegisterScreen(
    onRegisterSuccess: () -> Unit,
    onNavigateLogin: () -> Unit,
    modifier: Modifier = Modifier
) {
    var name by rememberSaveable { mutableStateOf("") }
    var phone by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }
    var confirm by rememberSaveable { mutableStateOf("") }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    var loading by rememberSaveable { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 28.dp, vertical = 48.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        AppLogo(size = 72.dp)
        Text(
            "注册账号",
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(top = 20.dp, bottom = 4.dp)
        )
        Text(
            "数据将绑定至云端账户",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 4.dp, bottom = 24.dp)
        )

        OutlinedTextField(value = name, onValueChange = { name = it; error = null }, label = { Text("昵称") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        Spacer(modifier = Modifier.height(12.dp))
        OutlinedTextField(value = phone, onValueChange = { phone = it; error = null }, label = { Text("手机号") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone), modifier = Modifier.fillMaxWidth())
        Spacer(modifier = Modifier.height(12.dp))
        OutlinedTextField(value = password, onValueChange = { password = it; error = null }, label = { Text("密码") }, singleLine = true, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())
        Spacer(modifier = Modifier.height(12.dp))
        OutlinedTextField(value = confirm, onValueChange = { confirm = it; error = null }, label = { Text("确认密码") }, singleLine = true, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())

        error?.let { Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 8.dp)) }

        Button(
            onClick = {
                if (loading) return@Button
                if (password != confirm) {
                    error = "两次密码不一致"
                    return@Button
                }
                scope.launch {
                    loading = true
                    error = null
                    XingyuRepository.register(name.trim(), phone.trim(), password).fold(
                        onSuccess = { onRegisterSuccess() },
                        onFailure = { error = it.message }
                    )
                    loading = false
                }
            },
            enabled = !loading,
            modifier = Modifier.fillMaxWidth().padding(top = 24.dp).height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(containerColor = CourtGreen)
        ) {
            Text(if (loading) "注册中…" else "注册")
        }

        TextButton(onClick = onNavigateLogin) {
            Text("已有账号？去登录")
        }
    }
}

@Composable
fun RoleSelectionScreen(
    userName: String,
    onPersonalSelected: () -> Unit,
    onTeacherSelected: () -> Unit,
    modifier: Modifier = Modifier
) {
    var loading by rememberSaveable { mutableStateOf(false) }
    var error by rememberSaveable { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center
    ) {
        Text("你好，$userName", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text(
            "请选择使用身份",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 4.dp, bottom = 28.dp)
        )

        error?.let {
            Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(bottom = 12.dp))
        }

        RoleCard(
            title = "个人训练者",
            subtitle = "对打/比赛 · 单人动作练习\n数据来自板端云端同步",
            enabled = !loading,
            onClick = {
                scope.launch {
                    loading = true
                    error = null
                    XingyuRepository.setRole(UserRole.PERSONAL).fold(
                        onSuccess = { onPersonalSelected() },
                        onFailure = { error = it.message }
                    )
                    loading = false
                }
            },
            accent = CourtGreen
        )
        Spacer(modifier = Modifier.height(16.dp))
        RoleCard(
            title = "班级训练（教练）",
            subtitle = "创建班级 · 管理学员\n查看全班训练数据",
            enabled = !loading,
            onClick = {
                scope.launch {
                    loading = true
                    error = null
                    XingyuRepository.setRole(UserRole.TEACHER).fold(
                        onSuccess = { onTeacherSelected() },
                        onFailure = { error = it.message }
                    )
                    loading = false
                }
            },
            accent = ShuttleOrange
        )
    }
}

@Composable
private fun RoleCard(
    title: String,
    subtitle: String,
    enabled: Boolean,
    onClick: () -> Unit,
    accent: androidx.compose.ui.graphics.Color
) {
    Button(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.fillMaxWidth().height(120.dp),
        shape = RoundedCornerShape(20.dp),
        colors = ButtonDefaults.buttonColors(containerColor = accent)
    ) {
        Column(modifier = Modifier.fillMaxWidth(), horizontalAlignment = Alignment.Start) {
            Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, color = androidx.compose.ui.graphics.Color.White)
            Text(subtitle, style = MaterialTheme.typography.bodySmall, color = androidx.compose.ui.graphics.Color.White.copy(alpha = 0.9f), modifier = Modifier.padding(top = 8.dp))
        }
    }
}

@Composable
fun ClassComingSoonScreen(onBack: () -> Unit, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text("班级模式", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
        Text(
            "教师端班级课功能正在开发中，敬请期待。",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 12.dp, bottom = 32.dp)
        )
        Button(onClick = onBack) {
            Text("返回选择身份")
        }
    }
}
