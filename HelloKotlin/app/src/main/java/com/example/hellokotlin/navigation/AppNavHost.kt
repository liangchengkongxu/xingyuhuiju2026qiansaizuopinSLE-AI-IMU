package com.example.hellokotlin.navigation

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BarChart
import androidx.compose.material.icons.filled.FitnessCenter
import androidx.compose.material.icons.filled.Groups
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.outlined.BarChart
import androidx.compose.material.icons.outlined.FitnessCenter
import androidx.compose.material.icons.outlined.Groups
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material.icons.outlined.Person
import androidx.compose.material.icons.filled.School
import androidx.compose.material.icons.outlined.School
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
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
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.model.DrillActionType
import com.example.hellokotlin.data.model.UserRole
import com.example.hellokotlin.data.repository.XingyuRepository
import com.example.hellokotlin.ui.screens.AnalyticsScreen
import com.example.hellokotlin.ui.screens.CommunityScreen
import com.example.hellokotlin.ui.screens.PostDetailScreen
import com.example.hellokotlin.ui.screens.HelpScreen
import com.example.hellokotlin.ui.screens.ProfileScreen
import com.example.hellokotlin.ui.screens.PublishPostScreen
import com.example.hellokotlin.ui.screens.TrainingScreen
import com.example.hellokotlin.ui.screens.coach.CoachClassDetailScreen
import com.example.hellokotlin.ui.screens.coach.CoachHomeScreen
import com.example.hellokotlin.ui.screens.coach.CoachStudentMatchScreen
import com.example.hellokotlin.ui.screens.coach.CoachStudentScreen
import com.example.hellokotlin.ui.screens.auth.LoginScreen
import com.example.hellokotlin.ui.screens.auth.RegisterScreen
import com.example.hellokotlin.ui.screens.auth.RoleSelectionScreen
import com.example.hellokotlin.ui.screens.student.StudentClassDetailScreen
import com.example.hellokotlin.ui.screens.student.StudentClassesScreen
import com.example.hellokotlin.ui.screens.personal.DrillActionRecordsScreen
import com.example.hellokotlin.ui.screens.personal.DrillPracticeHomeScreen
import com.example.hellokotlin.ui.screens.personal.MatchDetailScreen
import com.example.hellokotlin.ui.screens.personal.MatchRecordListScreen
import com.example.hellokotlin.ui.screens.personal.PersonalHomeScreen
import kotlinx.coroutines.launch

private sealed class MainTab(
    val route: String,
    val label: String,
    val selectedIcon: ImageVector,
    val unselectedIcon: ImageVector
) {
    data object Home : MainTab(Routes.HOME, "首页", Icons.Filled.Home, Icons.Outlined.Home)
    data object Training : MainTab(Routes.TRAINING, "训练", Icons.Filled.FitnessCenter, Icons.Outlined.FitnessCenter)
    data object Analytics : MainTab(Routes.ANALYTICS, "分析", Icons.Filled.BarChart, Icons.Outlined.BarChart)
    data object Community : MainTab(Routes.COMMUNITY, "球友圈", Icons.Filled.Groups, Icons.Outlined.Groups)
    data object Profile : MainTab(Routes.PROFILE, "我的", Icons.Filled.Person, Icons.Outlined.Person)
}

private val mainTabs = listOf(MainTab.Home, MainTab.Training, MainTab.Analytics, MainTab.Community, MainTab.Profile)

private sealed class CoachTab(
    val route: String,
    val label: String,
    val selectedIcon: ImageVector,
    val unselectedIcon: ImageVector
) {
    data object Classes : CoachTab(Routes.COACH_HOME, "班级", Icons.Filled.School, Icons.Outlined.School)
    data object Community : CoachTab(Routes.COMMUNITY, "球友圈", Icons.Filled.Groups, Icons.Outlined.Groups)
    data object Profile : CoachTab(Routes.PROFILE, "我的", Icons.Filled.Person, Icons.Outlined.Person)
}

private val coachTabs = listOf(CoachTab.Classes, CoachTab.Community, CoachTab.Profile)

@Composable
fun AppNavHost() {
    var sessionReady by remember { mutableStateOf(false) }
    var feedRefreshKey by remember { mutableIntStateOf(0) }
    var coachRefreshKey by remember { mutableIntStateOf(0) }
    var studentClassRefreshKey by remember { mutableIntStateOf(0) }
    val logoutScope = rememberCoroutineScope()
    val isCoach = AppSession.userRole == UserRole.TEACHER

    LaunchedEffect(Unit) {
        XingyuRepository.restoreSession()
        sessionReady = true
    }

    if (!sessionReady) {
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            CircularProgressIndicator()
        }
        return
    }

    val navController = rememberNavController()
    val backStack by navController.currentBackStackEntryAsState()
    val currentRoute = backStack?.destination?.route
    val showBottomBar = when {
        isCoach -> currentRoute in Routes.coachMainTabRoutes
        else -> currentRoute in Routes.mainTabRoutes
    }

    val startDestination = when {
        !AppSession.isLoggedIn -> Routes.LOGIN
        AppSession.userRole == null -> Routes.ROLE_SELECT
        AppSession.userRole == UserRole.TEACHER -> Routes.COACH_HOME
        else -> Routes.HOME
    }

    Scaffold(
        bottomBar = {
            if (showBottomBar) {
                NavigationBar {
                    if (isCoach) {
                        coachTabs.forEach { tab ->
                            val selected = currentRoute == tab.route
                            NavigationBarItem(
                                selected = selected,
                                onClick = {
                                    navController.navigate(tab.route) {
                                        popUpTo(navController.graph.findStartDestination().id) {
                                            saveState = true
                                        }
                                        launchSingleTop = true
                                        restoreState = true
                                    }
                                },
                                icon = {
                                    Icon(
                                        if (selected) tab.selectedIcon else tab.unselectedIcon,
                                        contentDescription = tab.label
                                    )
                                },
                                label = { Text(tab.label) }
                            )
                        }
                    } else {
                        mainTabs.forEach { tab ->
                            val selected = currentRoute == tab.route
                            NavigationBarItem(
                                selected = selected,
                                onClick = {
                                    navController.navigate(tab.route) {
                                        popUpTo(navController.graph.findStartDestination().id) {
                                            saveState = true
                                        }
                                        launchSingleTop = true
                                        restoreState = true
                                    }
                                },
                                icon = {
                                    Icon(
                                        if (selected) tab.selectedIcon else tab.unselectedIcon,
                                        contentDescription = tab.label
                                    )
                                },
                                label = { Text(tab.label) }
                            )
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = startDestination,
            modifier = Modifier.padding(innerPadding)
        ) {
            composable(Routes.LOGIN) {
                LoginScreen(
                    onLoginSuccess = { navController.navigate(Routes.ROLE_SELECT) { popUpTo(Routes.LOGIN) { inclusive = true } } },
                    onNavigateRegister = { navController.navigate(Routes.REGISTER) }
                )
            }
            composable(Routes.REGISTER) {
                RegisterScreen(
                    onRegisterSuccess = { navController.navigate(Routes.ROLE_SELECT) { popUpTo(Routes.LOGIN) { inclusive = true } } },
                    onNavigateLogin = { navController.popBackStack() }
                )
            }
            composable(Routes.ROLE_SELECT) {
                RoleSelectionScreen(
                    userName = AppSession.currentUser?.displayName ?: "",
                    onPersonalSelected = {
                        navController.navigate(Routes.HOME) {
                            popUpTo(Routes.ROLE_SELECT) { inclusive = true }
                        }
                    },
                    onTeacherSelected = {
                        navController.navigate(Routes.COACH_HOME) {
                            popUpTo(Routes.ROLE_SELECT) { inclusive = true }
                        }
                    }
                )
            }

            composable(Routes.COACH_HOME) {
                CoachHomeScreen(
                    onOpenClass = { navController.navigate(Routes.coachClass(it)) },
                    refreshKey = coachRefreshKey
                )
            }
            composable(
                route = Routes.COACH_CLASS,
                arguments = listOf(navArgument("classId") { type = NavType.StringType })
            ) { entry ->
                val classId = entry.arguments?.getString("classId").orEmpty()
                CoachClassDetailScreen(
                    classId = classId,
                    onBack = { navController.popBackStack() },
                    onOpenStudent = { cId, userId ->
                        navController.navigate(Routes.coachStudent(cId, userId))
                    },
                    refreshKey = coachRefreshKey
                )
            }
            composable(
                route = Routes.COACH_STUDENT,
                arguments = listOf(
                    navArgument("classId") { type = NavType.StringType },
                    navArgument("userId") { type = NavType.StringType }
                )
            ) { entry ->
                val classId = entry.arguments?.getString("classId").orEmpty()
                val userId = entry.arguments?.getString("userId").orEmpty()
                CoachStudentScreen(
                    classId = classId,
                    studentId = userId,
                    onBack = { navController.popBackStack() },
                    onOpenMatch = { matchId ->
                        navController.navigate(Routes.coachStudentMatch(classId, userId, matchId))
                    }
                )
            }
            composable(
                route = Routes.COACH_STUDENT_MATCH,
                arguments = listOf(
                    navArgument("classId") { type = NavType.StringType },
                    navArgument("userId") { type = NavType.StringType },
                    navArgument("matchId") { type = NavType.StringType }
                )
            ) { entry ->
                CoachStudentMatchScreen(
                    classId = entry.arguments?.getString("classId").orEmpty(),
                    studentId = entry.arguments?.getString("userId").orEmpty(),
                    matchId = entry.arguments?.getString("matchId").orEmpty(),
                    onBack = { navController.popBackStack() }
                )
            }

            composable(Routes.HOME) {
                PersonalHomeScreen(
                    onOpenMatchMode = { navController.navigate(Routes.MATCH_LIST) },
                    onOpenDrillMode = { navController.navigate(Routes.DRILL_HOME) },
                    onOpenClasses = { navController.navigate(Routes.STUDENT_CLASSES) }
                )
            }
            composable(Routes.STUDENT_CLASSES) {
                StudentClassesScreen(
                    onBack = { navController.popBackStack() },
                    onOpenClass = { navController.navigate(Routes.studentClass(it)) },
                    refreshKey = studentClassRefreshKey
                )
            }
            composable(
                route = Routes.STUDENT_CLASS,
                arguments = listOf(navArgument("classId") { type = NavType.StringType })
            ) { entry ->
                val classId = entry.arguments?.getString("classId").orEmpty()
                StudentClassDetailScreen(
                    classId = classId,
                    onBack = { navController.popBackStack() },
                    onLeft = {
                        studentClassRefreshKey++
                        navController.popBackStack()
                    }
                )
            }
            composable(Routes.TRAINING) { TrainingScreen() }
            composable(Routes.ANALYTICS) { AnalyticsScreen() }
            composable(Routes.COMMUNITY) {
                CommunityScreen(
                    onOpenPublish = { navController.navigate(Routes.COMMUNITY_POST) },
                    onOpenPost = { navController.navigate(Routes.communityDetail(it)) },
                    feedRefreshKey = feedRefreshKey
                )
            }
            composable(Routes.COMMUNITY_POST) {
                PublishPostScreen(
                    onBack = { navController.popBackStack() },
                    onPublished = {
                        feedRefreshKey++
                        navController.popBackStack()
                    }
                )
            }
            composable(
                route = Routes.COMMUNITY_DETAIL,
                arguments = listOf(navArgument("postId") { type = NavType.StringType })
            ) { entry ->
                PostDetailScreen(
                    postId = entry.arguments?.getString("postId").orEmpty(),
                    onBack = { navController.popBackStack() },
                    onDeleted = {
                        feedRefreshKey++
                        navController.popBackStack()
                    }
                )
            }
            composable(Routes.PROFILE) {
                ProfileScreen(
                    onLogout = {
                        logoutScope.launch {
                            XingyuRepository.logout()
                            navController.navigate(Routes.LOGIN) {
                                popUpTo(0) { inclusive = true }
                            }
                        }
                    },
                    onOpenHelp = { navController.navigate(Routes.HELP) }
                )
            }
            composable(Routes.HELP) {
                HelpScreen(onBack = { navController.popBackStack() })
            }

            composable(Routes.MATCH_LIST) {
                MatchRecordListScreen(
                    onBack = { navController.popBackStack() },
                    onOpenMatch = { navController.navigate(Routes.matchDetail(it)) }
                )
            }
            composable(
                route = Routes.MATCH_DETAIL,
                arguments = listOf(navArgument("matchId") { type = NavType.StringType })
            ) { entry ->
                MatchDetailScreen(
                    matchId = entry.arguments?.getString("matchId").orEmpty(),
                    onBack = { navController.popBackStack() }
                )
            }
            composable(Routes.DRILL_HOME) {
                DrillPracticeHomeScreen(
                    onBack = { navController.popBackStack() },
                    onOpenAction = { navController.navigate(Routes.drillAction(it.key)) }
                )
            }
            composable(
                route = Routes.DRILL_ACTION,
                arguments = listOf(navArgument("actionKey") { type = NavType.StringType })
            ) { entry ->
                val key = entry.arguments?.getString("actionKey").orEmpty()
                val action = DrillActionType.fromKey(key) ?: DrillActionType.SMASH
                DrillActionRecordsScreen(action = action, onBack = { navController.popBackStack() })
            }
        }
    }
}
