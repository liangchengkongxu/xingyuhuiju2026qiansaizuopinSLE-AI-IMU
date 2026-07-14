package com.example.hellokotlin

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.example.hellokotlin.data.SessionStore
import com.example.hellokotlin.navigation.AppNavHost
import com.example.hellokotlin.ui.theme.BadmintonTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        SessionStore.init(applicationContext)
        enableEdgeToEdge()
        setContent {
            BadmintonTheme {
                AppNavHost()
            }
        }
    }
}
