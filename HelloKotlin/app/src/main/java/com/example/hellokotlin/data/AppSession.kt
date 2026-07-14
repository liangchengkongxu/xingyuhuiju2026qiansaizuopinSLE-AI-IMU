package com.example.hellokotlin.data

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.example.hellokotlin.data.model.AuthUser
import com.example.hellokotlin.data.model.UserRole

/** 本地会话（内存 + DataStore 持久化） */
object AppSession {
    var token: String? by mutableStateOf(null)
        private set
    var currentUser: AuthUser? by mutableStateOf(null)
        private set
    var userRole: UserRole? by mutableStateOf(null)
        private set

    val isLoggedIn: Boolean get() = currentUser != null && !token.isNullOrBlank()
    val isPersonalTrainer: Boolean get() = userRole == UserRole.PERSONAL

    fun restore(token: String, user: AuthUser, role: UserRole?) {
        this.token = token
        currentUser = user
        userRole = role
    }

    fun login(token: String, user: AuthUser) {
        this.token = token
        currentUser = user
        userRole = null
    }

    fun register(token: String, user: AuthUser) {
        this.token = token
        currentUser = user
        userRole = null
    }

    fun selectRole(role: UserRole) {
        userRole = role
    }

    fun clearRole() {
        userRole = null
    }

    fun logout() {
        token = null
        currentUser = null
        userRole = null
    }
}
