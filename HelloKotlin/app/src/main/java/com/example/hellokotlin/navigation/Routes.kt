package com.example.hellokotlin.navigation

object Routes {
    const val LOGIN = "login"
    const val REGISTER = "register"
    const val ROLE_SELECT = "role_select"
    const val CLASS_SOON = "class_soon"

    const val COACH_HOME = "coach_home"
    const val COACH_CLASS = "coach_class/{classId}"
    const val COACH_STUDENT = "coach_class/{classId}/student/{userId}"
    const val COACH_STUDENT_MATCH = "coach_class/{classId}/student/{userId}/match/{matchId}"

    const val STUDENT_CLASSES = "student_classes"
    const val STUDENT_CLASS = "student_class/{classId}"

    const val HOME = "home"
    const val TRAINING = "training"
    const val ANALYTICS = "analytics"
    const val COMMUNITY = "community"
    const val COMMUNITY_POST = "community_post"
    const val COMMUNITY_DETAIL = "community_detail/{postId}"
    const val PROFILE = "profile"
    const val HELP = "help"

    const val MATCH_LIST = "match_list"
    const val MATCH_DETAIL = "match_detail/{matchId}"
    const val DRILL_HOME = "drill_home"
    const val DRILL_ACTION = "drill_action/{actionKey}"

    fun matchDetail(matchId: String) = "match_detail/$matchId"
    fun drillAction(actionKey: String) = "drill_action/$actionKey"
    fun communityDetail(postId: String) = "community_detail/$postId"
    fun coachClass(classId: String) = "coach_class/$classId"
    fun coachStudent(classId: String, userId: String) = "coach_class/$classId/student/$userId"
    fun coachStudentMatch(classId: String, userId: String, matchId: String) =
        "coach_class/$classId/student/$userId/match/$matchId"
    fun studentClass(classId: String) = "student_class/$classId"

    val mainTabRoutes = setOf(HOME, TRAINING, ANALYTICS, COMMUNITY, PROFILE)
    val coachMainTabRoutes = setOf(COACH_HOME, COMMUNITY, PROFILE)
}
