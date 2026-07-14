package com.example.hellokotlin.data.repository

import com.example.hellokotlin.data.AppSession
import com.example.hellokotlin.data.SessionStore
import com.example.hellokotlin.data.api.dto.AddMemberRequest
import com.example.hellokotlin.data.api.dto.AppReleaseDto
import com.example.hellokotlin.data.api.dto.ClassCreateRequest
import com.example.hellokotlin.data.api.dto.CreatePostRequest
import com.example.hellokotlin.data.api.dto.ErrorDetail
import com.example.hellokotlin.data.api.dto.JoinClassRequest
import com.example.hellokotlin.data.api.dto.LoginRequest
import com.example.hellokotlin.data.api.dto.RegisterRequest
import com.example.hellokotlin.data.api.dto.RoleRequest
import com.example.hellokotlin.data.applyToAppSession
import com.example.hellokotlin.data.model.AuthUser
import com.example.hellokotlin.data.model.CommunityPost
import com.example.hellokotlin.data.model.DrillActionType
import com.example.hellokotlin.data.model.DrillSessionRecord
import com.example.hellokotlin.data.model.MatchRecord
import com.example.hellokotlin.data.model.PostAttachmentKind
import com.example.hellokotlin.data.model.PostComment
import com.example.hellokotlin.data.model.RankingEntry
import com.example.hellokotlin.data.model.RankingType
import com.example.hellokotlin.data.model.JoinedClassSummary
import com.example.hellokotlin.data.model.StudentClassDetail
import com.example.hellokotlin.data.model.StudentSummary
import com.example.hellokotlin.data.model.StrokeRecord
import com.example.hellokotlin.data.model.TrainingClassDetail
import com.example.hellokotlin.data.model.TrainingClassSummary
import com.example.hellokotlin.data.model.UserRole
import com.example.hellokotlin.data.network.NetworkModule
import com.example.hellokotlin.util.AppReleaseInfo
import com.squareup.moshi.Moshi
import com.squareup.moshi.kotlin.reflect.KotlinJsonAdapterFactory
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.MultipartBody
import okhttp3.RequestBody.Companion.toRequestBody
import retrofit2.HttpException
import java.io.IOException

object XingyuRepository {
    private val api = NetworkModule.api
    private val errorMoshi = Moshi.Builder().add(KotlinJsonAdapterFactory()).build()

    suspend fun restoreSession(): Boolean {
        val saved = SessionStore.load() ?: return false
        saved.applyToAppSession()
        return apiCall {
            val user = api.me()
            AppSession.restore(
                token = AppSession.token.orEmpty(),
                user = user.toAuthUser(),
                role = saved.role
            )
            SessionStore.save(AppSession.token.orEmpty(), user.toAuthUser(), saved.role)
            true
        }.getOrElse {
            AppSession.logout()
            SessionStore.clear()
            false
        }
    }

    private suspend fun persistSession() {
        val token = AppSession.token ?: return
        val user = AppSession.currentUser ?: return
        SessionStore.save(token, user, AppSession.userRole)
    }

    suspend fun login(phone: String, password: String): Result<AuthUser> = apiCall {
        if (phone.isBlank() || password.length < 4) {
            throw IllegalArgumentException("请输入手机号与至少 4 位密码")
        }
        val response = api.login(LoginRequest(phone.trim(), password))
        AppSession.login(response.token, response.user.toAuthUser())
        persistSession()
        response.user.toAuthUser()
    }

    suspend fun register(name: String, phone: String, password: String): Result<AuthUser> = apiCall {
        if (name.isBlank() || phone.isBlank() || password.length < 4) {
            throw IllegalArgumentException("请填写完整注册信息")
        }
        val response = api.register(RegisterRequest(name.trim(), phone.trim(), password))
        AppSession.register(response.token, response.user.toAuthUser())
        persistSession()
        response.user.toAuthUser()
    }

    suspend fun setRole(role: UserRole): Result<UserRole> = apiCall {
        val user = api.setRole(RoleRequest(role.toApiRole()))
        val confirmed = user.role.toUserRole() ?: role
        AppSession.selectRole(confirmed)
        persistSession()
        confirmed
    }

    suspend fun logout() {
        AppSession.logout()
        SessionStore.clear()
    }

    suspend fun uploadImage(bytes: ByteArray, fileName: String = "image.jpg"): Result<String> = apiCall {
        val body = bytes.toRequestBody("image/*".toMediaTypeOrNull())
        val part = MultipartBody.Part.createFormData("file", fileName, body)
        api.uploadImage(part).url
    }

    suspend fun getMatches(): Result<List<MatchRecord>> = apiCall {
        api.getMatches().map { it.toMatchRecord() }
    }

    suspend fun getStrokes(matchId: String): Result<List<StrokeRecord>> = apiCall {
        api.getStrokes(matchId).map { it.toStrokeRecord() }
    }

    suspend fun getDrills(action: DrillActionType? = null): Result<List<DrillSessionRecord>> = apiCall {
        api.getDrills(action?.key).mapNotNull { it.toDrillSessionRecord() }
    }

    suspend fun getPosts(): Result<List<CommunityPost>> = apiCall {
        api.getPosts().map { it.toCommunityPost() }
    }

    suspend fun getPost(postId: String): Result<CommunityPost> = apiCall {
        api.getPost(postId).toCommunityPost()
    }

    suspend fun getPostComments(postId: String): Result<List<PostComment>> = apiCall {
        api.getPostComments(postId).map { it.toPostComment() }
    }

    suspend fun deletePost(postId: String): Result<Unit> = apiCall {
        api.deletePost(postId)
    }

    suspend fun createPost(
        content: String,
        attachmentKind: PostAttachmentKind,
        imageUrl: String? = null,
        imageCaption: String? = null,
        statsTitle: String? = null,
        statsDetail: String? = null
    ): Result<CommunityPost> = apiCall {
        api.createPost(
            CreatePostRequest(
                content = content,
                attachmentKind = attachmentKind.toApiKind(),
                imageUrl = imageUrl,
                imageCaption = imageCaption,
                statsTitle = statsTitle,
                statsDetail = statsDetail
            )
        ).toCommunityPost()
    }

    suspend fun getRankings(type: RankingType): Result<List<RankingEntry>> = apiCall {
        api.getRankings(type.toApiType()).map { it.toRankingEntry() }
    }

    suspend fun getClasses(): Result<List<TrainingClassSummary>> = apiCall {
        api.getClasses().map { it.toTrainingClassSummary() }
    }

    suspend fun createClass(name: String, description: String = ""): Result<TrainingClassSummary> = apiCall {
        if (name.isBlank()) throw IllegalArgumentException("请输入班级名称")
        api.createClass(ClassCreateRequest(name.trim(), description.trim())).toTrainingClassSummary()
    }

    suspend fun getClass(classId: String): Result<TrainingClassDetail> = apiCall {
        api.getClass(classId).toTrainingClassDetail()
    }

    suspend fun deleteClass(classId: String): Result<Unit> = apiCall {
        api.deleteClass(classId)
    }

    suspend fun addClassMember(classId: String, phone: String): Result<StudentSummary> = apiCall {
        if (phone.isBlank()) throw IllegalArgumentException("请输入学员手机号")
        api.addClassMember(classId, AddMemberRequest(phone.trim())).toStudentSummary()
    }

    suspend fun removeClassMember(classId: String, studentId: String): Result<Unit> = apiCall {
        api.removeClassMember(classId, studentId)
    }

    suspend fun getStudentMatches(classId: String, studentId: String): Result<List<MatchRecord>> = apiCall {
        api.getStudentMatches(classId, studentId).map { it.toMatchRecord() }
    }

    suspend fun getStudentStrokes(classId: String, studentId: String, matchId: String): Result<List<StrokeRecord>> = apiCall {
        api.getStudentStrokes(classId, studentId, matchId).map { it.toStrokeRecord() }
    }

    suspend fun getStudentDrills(classId: String, studentId: String, action: DrillActionType? = null): Result<List<DrillSessionRecord>> = apiCall {
        api.getStudentDrills(classId, studentId, action?.key).mapNotNull { it.toDrillSessionRecord() }
    }

    suspend fun getJoinedClasses(): Result<List<JoinedClassSummary>> = apiCall {
        api.getJoinedClasses().map { it.toJoinedClassSummary() }
    }

    suspend fun joinClass(inviteCode: String): Result<JoinedClassSummary> = apiCall {
        if (inviteCode.isBlank()) throw IllegalArgumentException("请输入邀请码")
        api.joinClass(JoinClassRequest(inviteCode.trim().uppercase())).toJoinedClassSummary()
    }

    suspend fun getStudentClassView(classId: String): Result<StudentClassDetail> = apiCall {
        api.getStudentClassView(classId).toStudentClassDetail()
    }

    suspend fun leaveClass(classId: String): Result<Unit> = apiCall {
        api.leaveClass(classId)
    }

    suspend fun checkAppUpdate(): Result<AppReleaseInfo?> = apiCall {
        val release = api.getAppRelease()
        val info = release.toAppReleaseInfo()
        if (info.isNewerThanInstalled()) info else null
    }

    private fun AppReleaseDto.toAppReleaseInfo(): AppReleaseInfo = AppReleaseInfo(
        versionCode = versionCode,
        versionName = versionName,
        apkUrl = apkUrl,
        changelog = changelog,
        forceUpdate = forceUpdate
    )

    private suspend fun <T> apiCall(block: suspend () -> T): Result<T> = try {
        Result.success(block())
    } catch (e: IllegalArgumentException) {
        Result.failure(e)
    } catch (e: HttpException) {
        Result.failure(Exception(parseHttpError(e)))
    } catch (e: IOException) {
        Result.failure(Exception("网络连接失败，请检查网络后重试"))
    } catch (e: Exception) {
        Result.failure(Exception(e.message ?: "请求失败"))
    }

    private fun parseHttpError(e: HttpException): String {
        val body = e.response()?.errorBody()?.string().orEmpty()
        if (body.isNotBlank()) {
            runCatching {
                errorMoshi.adapter(ErrorDetail::class.java).fromJson(body)?.detail
            }.getOrNull()?.let { return it }
            if (body.length < 120) return body
        }
        return when (e.code()) {
            401 -> "登录已过期，请重新登录"
            404 -> "资源不存在"
            else -> "请求失败 (${e.code()})"
        }
    }
}
