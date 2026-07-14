package com.example.hellokotlin.data.api

import com.example.hellokotlin.data.api.dto.AddMemberRequest
import com.example.hellokotlin.data.api.dto.AppReleaseDto
import com.example.hellokotlin.data.api.dto.ClassCreateRequest
import com.example.hellokotlin.data.api.dto.ClassDetailDto
import com.example.hellokotlin.data.api.dto.ClassSummaryDto
import com.example.hellokotlin.data.api.dto.CommentDto
import com.example.hellokotlin.data.api.dto.CreatePostRequest
import com.example.hellokotlin.data.api.dto.DrillDto
import com.example.hellokotlin.data.api.dto.JoinClassRequest
import com.example.hellokotlin.data.api.dto.JoinedClassDto
import com.example.hellokotlin.data.api.dto.LoginRequest
import com.example.hellokotlin.data.api.dto.StudentClassDetailDto
import com.example.hellokotlin.data.api.dto.MatchDto
import com.example.hellokotlin.data.api.dto.PostDto
import com.example.hellokotlin.data.api.dto.RankingEntryDto
import com.example.hellokotlin.data.api.dto.RegisterRequest
import com.example.hellokotlin.data.api.dto.RoleRequest
import com.example.hellokotlin.data.api.dto.StudentSummaryDto
import com.example.hellokotlin.data.api.dto.StrokeDto
import com.example.hellokotlin.data.api.dto.TokenResponse
import com.example.hellokotlin.data.api.dto.UserDto
import com.example.hellokotlin.data.api.dto.UploadResponse
import okhttp3.MultipartBody
import retrofit2.http.Body
import retrofit2.http.DELETE
import retrofit2.http.GET
import retrofit2.http.Multipart
import retrofit2.http.POST
import retrofit2.http.PUT
import retrofit2.http.Part
import retrofit2.http.Path
import retrofit2.http.Query

interface XingyuApiService {
    @POST("auth/register")
    suspend fun register(@Body body: RegisterRequest): TokenResponse

    @POST("auth/login")
    suspend fun login(@Body body: LoginRequest): TokenResponse

    @GET("auth/me")
    suspend fun me(): UserDto

    @PUT("auth/role")
    suspend fun setRole(@Body body: RoleRequest): UserDto

    @GET("matches")
    suspend fun getMatches(): List<MatchDto>

    @GET("matches/{matchId}/strokes")
    suspend fun getStrokes(@Path("matchId") matchId: String): List<StrokeDto>

    @GET("drills")
    suspend fun getDrills(@Query("action") action: String? = null): List<DrillDto>

    @GET("community/posts")
    suspend fun getPosts(): List<PostDto>

    @GET("community/posts/{postId}")
    suspend fun getPost(@Path("postId") postId: String): PostDto

    @GET("community/posts/{postId}/comments")
    suspend fun getPostComments(@Path("postId") postId: String): List<CommentDto>

    @DELETE("community/posts/{postId}")
    suspend fun deletePost(@Path("postId") postId: String)

    @POST("community/posts")
    suspend fun createPost(@Body body: CreatePostRequest): PostDto

    @GET("community/rankings")
    suspend fun getRankings(@Query("type") type: String): List<RankingEntryDto>

    @Multipart
    @POST("community/upload")
    suspend fun uploadImage(@Part file: MultipartBody.Part): UploadResponse

    @GET("app/release")
    suspend fun getAppRelease(): AppReleaseDto

    @GET("classes/joined")
    suspend fun getJoinedClasses(): List<JoinedClassDto>

    @POST("classes/join")
    suspend fun joinClass(@Body body: JoinClassRequest): JoinedClassDto

    @GET("classes/{classId}/student-view")
    suspend fun getStudentClassView(@Path("classId") classId: String): StudentClassDetailDto

    @DELETE("classes/{classId}/leave")
    suspend fun leaveClass(@Path("classId") classId: String)

    @GET("classes")
    suspend fun getClasses(): List<ClassSummaryDto>

    @POST("classes")
    suspend fun createClass(@Body body: ClassCreateRequest): ClassSummaryDto

    @GET("classes/{classId}")
    suspend fun getClass(@Path("classId") classId: String): ClassDetailDto

    @DELETE("classes/{classId}")
    suspend fun deleteClass(@Path("classId") classId: String)

    @POST("classes/{classId}/members")
    suspend fun addClassMember(@Path("classId") classId: String, @Body body: AddMemberRequest): StudentSummaryDto

    @DELETE("classes/{classId}/members/{studentId}")
    suspend fun removeClassMember(@Path("classId") classId: String, @Path("studentId") studentId: String)

    @GET("classes/{classId}/students/{studentId}/matches")
    suspend fun getStudentMatches(
        @Path("classId") classId: String,
        @Path("studentId") studentId: String
    ): List<MatchDto>

    @GET("classes/{classId}/students/{studentId}/matches/{matchId}/strokes")
    suspend fun getStudentStrokes(
        @Path("classId") classId: String,
        @Path("studentId") studentId: String,
        @Path("matchId") matchId: String
    ): List<StrokeDto>

    @GET("classes/{classId}/students/{studentId}/drills")
    suspend fun getStudentDrills(
        @Path("classId") classId: String,
        @Path("studentId") studentId: String,
        @Query("action") action: String? = null
    ): List<DrillDto>
}
