package com.example.hellokotlin.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import com.example.hellokotlin.data.model.AuthUser
import com.example.hellokotlin.data.model.UserRole
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map

private val Context.sessionDataStore: DataStore<Preferences> by preferencesDataStore(name = "xingyu_session")

data class SavedSession(
    val token: String,
    val userId: String,
    val displayName: String,
    val phone: String,
    val role: UserRole?
)

object SessionStore {
    private val KEY_TOKEN = stringPreferencesKey("token")
    private val KEY_USER_ID = stringPreferencesKey("user_id")
    private val KEY_DISPLAY_NAME = stringPreferencesKey("display_name")
    private val KEY_PHONE = stringPreferencesKey("phone")
    private val KEY_ROLE = stringPreferencesKey("role")

    private lateinit var appContext: Context

    fun init(context: Context) {
        appContext = context.applicationContext
    }

    suspend fun save(token: String, user: AuthUser, role: UserRole?) {
        appContext.sessionDataStore.edit { prefs ->
            prefs[KEY_TOKEN] = token
            prefs[KEY_USER_ID] = user.id
            prefs[KEY_DISPLAY_NAME] = user.displayName
            prefs[KEY_PHONE] = user.phone
            if (role != null) {
                prefs[KEY_ROLE] = role.name
            } else {
                prefs.remove(KEY_ROLE)
            }
        }
    }

    suspend fun load(): SavedSession? {
        val snapshot = appContext.sessionDataStore.data.map { prefs ->
            val token = prefs[KEY_TOKEN] ?: return@map null
            val userId = prefs[KEY_USER_ID] ?: return@map null
            val name = prefs[KEY_DISPLAY_NAME] ?: return@map null
            val phone = prefs[KEY_PHONE] ?: return@map null
            val role = prefs[KEY_ROLE]?.let { name ->
                runCatching { UserRole.valueOf(name) }.getOrNull()
            }
            SavedSession(token, userId, name, phone, role)
        }.first()
        return snapshot
    }

    suspend fun clear() {
        appContext.sessionDataStore.edit { it.clear() }
    }
}

fun SavedSession.applyToAppSession() {
    AppSession.restore(
        token = token,
        user = AuthUser(id = userId, displayName = displayName, phone = phone),
        role = role
    )
}
