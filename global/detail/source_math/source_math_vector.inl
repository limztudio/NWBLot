// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(_MATH_NO_INTRINSICS_)
#define MATH_IS_NAN(x)  isnan(x)
#define MATH_IS_INF(x)  isinf(x)
#endif

#if defined(_MATH_SSE_INTRINSICS_)

#define MATH3UNPACK3INTO4(l1, l2, l3) \
    Vector V3 = _mm_shuffle_ps(l2, l3, _MM_SHUFFLE(0, 0, 3, 2));\
    Vector V2 = _mm_shuffle_ps(l2, l1, _MM_SHUFFLE(3, 3, 1, 0));\
    V2 = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 1, 0, 2));\
    Vector V4 = _mm_castsi128_ps(_mm_srli_si128(_mm_castps_si128(l3), 32 / 8))

#define MATH3PACK4INTO3(v2x) \
    v2x = _mm_shuffle_ps(V2, V3, _MM_SHUFFLE(1, 0, 2, 1));\
    V2 = _mm_shuffle_ps(V2, V1, _MM_SHUFFLE(2, 2, 0, 0));\
    V1 = _mm_shuffle_ps(V1, V2, _MM_SHUFFLE(0, 2, 1, 0));\
    V3 = _mm_shuffle_ps(V3, V4, _MM_SHUFFLE(0, 0, 2, 2));\
    V3 = _mm_shuffle_ps(V3, V4, _MM_SHUFFLE(2, 1, 2, 0))

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Return a vector with all elements equaling zero
inline Vector MathCallConv VectorZero()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult = { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vdupq_n_f32(0);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_setzero_ps();
#endif
}

// Initialize a vector with four floating point values
inline Vector MathCallConv VectorSet
(
    float x,
    float y,
    float z,
    float w
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult = { { { x, y, z, w } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t V0 = vcreate_f32(
        static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&x))
        | (static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&y)) << 32));
    float32x2_t V1 = vcreate_f32(
        static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&z))
        | (static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&w)) << 32));
    return vcombine_f32(V0, V1);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_set_ps(w, z, y, x);
#endif
}

// Initialize a vector with four integer values
inline Vector MathCallConv VectorSetInt
(
    uint32_t x,
    uint32_t y,
    uint32_t z,
    uint32_t w
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 vResult = { { { x, y, z, w } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t V0 = vcreate_u32(static_cast<uint64_t>(x) | (static_cast<uint64_t>(y) << 32));
    uint32x2_t V1 = vcreate_u32(static_cast<uint64_t>(z) | (static_cast<uint64_t>(w) << 32));
    return vreinterpretq_f32_u32(vcombine_u32(V0, V1));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_set_epi32(static_cast<int>(w), static_cast<int>(z), static_cast<int>(y), static_cast<int>(x));
    return _mm_castsi128_ps(V);
#endif
}

// Initialize a vector with a replicated floating point value
inline Vector MathCallConv VectorReplicate(float Value)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = Value;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vdupq_n_f32(Value);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_set_ps1(Value);
#endif
}

// Initialize a vector with a replicated floating point value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorReplicatePtr(const float* pValue)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float Value = pValue[0];
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = Value;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vld1q_dup_f32(pValue);
#elif defined(_MATH_AVX_INTRINSICS_)
    return _mm_broadcast_ss(pValue);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_load_ps1(pValue);
#endif
}

// Initialize a vector with a replicated integer value
inline Vector MathCallConv VectorReplicateInt(uint32_t Value)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 vResult;
    vResult.u[0] =
        vResult.u[1] =
        vResult.u[2] =
        vResult.u[3] = Value;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vdupq_n_u32(Value));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_set1_epi32(static_cast<int>(Value));
    return _mm_castsi128_ps(vTemp);
#endif
}

// Initialize a vector with a replicated integer value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorReplicateIntPtr(const uint32_t* pValue)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t Value = pValue[0];
    VectorU32 vResult;
    vResult.u[0] =
        vResult.u[1] =
        vResult.u[2] =
        vResult.u[3] = Value;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vld1q_dup_u32(pValue));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_load_ps1(reinterpret_cast<const float*>(pValue));
#endif
}

// Initialize a vector with all bits set (true mask)
inline Vector MathCallConv VectorTrueInt()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 vResult = { { { 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_s32(vdupq_n_s32(-1));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_set1_epi32(-1);
    return _mm_castsi128_ps(V);
#endif
}

// Initialize a vector with all bits clear (false mask)
inline Vector MathCallConv VectorFalseInt()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult = { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
    return vResult;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vdupq_n_u32(0));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_setzero_ps();
#endif
}

// Replicate the x component of the vector
inline Vector MathCallConv VectorSplatX(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = V.vector4_f32[0];
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vdupq_lane_f32(vget_low_f32(V), 0);
#elif defined(_MATH_AVX2_INTRINSICS_) && defined(_MATH_FAVOR_INTEL_)
    return _mm_broadcastss_ps(V);
#elif defined(_MATH_SSE_INTRINSICS_)
    return MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));
#endif
}

// Replicate the y component of the vector
inline Vector MathCallConv VectorSplatY(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = V.vector4_f32[1];
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vdupq_lane_f32(vget_low_f32(V), 1);
#elif defined(_MATH_SSE_INTRINSICS_)
    return MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
#endif
}

// Replicate the z component of the vector
inline Vector MathCallConv VectorSplatZ(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = V.vector4_f32[2];
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vdupq_lane_f32(vget_high_f32(V), 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    return MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
#endif
}

// Replicate the w component of the vector
inline Vector MathCallConv VectorSplatW(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = V.vector4_f32[3];
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vdupq_lane_f32(vget_high_f32(V), 1);
#elif defined(_MATH_SSE_INTRINSICS_)
    return MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
#endif
}

// Return a vector of 1.0f,1.0f,1.0f,1.0f
inline Vector MathCallConv VectorSplatOne()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = 1.0f;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vdupq_n_f32(1.0f);
#elif defined(_MATH_SSE_INTRINSICS_)
    return g_One;
#endif
}

// Return a vector of INF,INF,INF,INF
inline Vector MathCallConv VectorSplatInfinity()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 vResult;
    vResult.u[0] =
        vResult.u[1] =
        vResult.u[2] =
        vResult.u[3] = 0x7F800000;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vdupq_n_u32(0x7F800000));
#elif defined(_MATH_SSE_INTRINSICS_)
    return g_Infinity;
#endif
}

// Return a vector of Q_NAN,Q_NAN,Q_NAN,Q_NAN
inline Vector MathCallConv VectorSplatQNaN()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 vResult;
    vResult.u[0] =
        vResult.u[1] =
        vResult.u[2] =
        vResult.u[3] = 0x7FC00000;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vdupq_n_u32(0x7FC00000));
#elif defined(_MATH_SSE_INTRINSICS_)
    return g_QNaN;
#endif
}

// Return a vector of 1.192092896e-7f,1.192092896e-7f,1.192092896e-7f,1.192092896e-7f
inline Vector MathCallConv VectorSplatEpsilon()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 vResult;
    vResult.u[0] =
        vResult.u[1] =
        vResult.u[2] =
        vResult.u[3] = 0x34000000;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vdupq_n_u32(0x34000000));
#elif defined(_MATH_SSE_INTRINSICS_)
    return g_Epsilon;
#endif
}

// Return a vector of -0.0f (0x80000000),-0.0f,-0.0f,-0.0f
inline Vector MathCallConv VectorSplatSignMask()noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 vResult;
    vResult.u[0] =
        vResult.u[1] =
        vResult.u[2] =
        vResult.u[3] = 0x80000000U;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vdupq_n_u32(0x80000000U));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_set1_epi32(static_cast<int>(0x80000000));
    return _mm_castsi128_ps(V);
#endif
}

// Return a floating point value via an index. This is not a recommended
// function to use due to performance loss.
inline float MathCallConv VectorGetByIndex(VectorArg V, size_t i)noexcept{
    assert(i < 4);
    _Analysis_assume_(i < 4);
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_f32[i];
#else
    VectorF32 U;
    U.v = V;
    return U.f[i];
#endif
}

// Return the X component in an FPU register.
inline float MathCallConv VectorGetX(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_f32[0];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_f32(V, 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_cvtss_f32(V);
#endif
}

// Return the Y component in an FPU register.
inline float MathCallConv VectorGetY(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_f32[1];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_f32(V, 1);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
    return _mm_cvtss_f32(vTemp);
#endif
}

// Return the Z component in an FPU register.
inline float MathCallConv VectorGetZ(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_f32[2];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_f32(V, 2);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
    return _mm_cvtss_f32(vTemp);
#endif
}

// Return the W component in an FPU register.
inline float MathCallConv VectorGetW(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_f32[3];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_f32(V, 3);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
    return _mm_cvtss_f32(vTemp);
#endif
}

// Store a component indexed by i into a 32 bit float location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetByIndexPtr(float* f, VectorArg V, size_t i)noexcept{
    assert(f != nullptr);
    assert(i < 4);
    _Analysis_assume_(i < 4);
#if defined(_MATH_NO_INTRINSICS_)
    *f = V.vector4_f32[i];
#else
    VectorF32 U;
    U.v = V;
    *f = U.f[i];
#endif
}

// Store the X component into a 32 bit float location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetXPtr(float* x, VectorArg V)noexcept{
    assert(x != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *x = V.vector4_f32[0];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_f32(x, V, 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    _mm_store_ss(x, V);
#endif
}

// Store the Y component into a 32 bit float location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetYPtr(float* y, VectorArg V)noexcept{
    assert(y != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *y = V.vector4_f32[1];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_f32(y, V, 1);
#elif defined(_MATH_SSE4_INTRINSICS_)
    * (reinterpret_cast<int*>(y)) = _mm_extract_ps(V, 1);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
    _mm_store_ss(y, vResult);
#endif
}

// Store the Z component into a 32 bit float location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetZPtr(float* z, VectorArg V)noexcept{
    assert(z != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *z = V.vector4_f32[2];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_f32(z, V, 2);
#elif defined(_MATH_SSE4_INTRINSICS_)
    * (reinterpret_cast<int*>(z)) = _mm_extract_ps(V, 2);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
    _mm_store_ss(z, vResult);
#endif
}

// Store the W component into a 32 bit float location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetWPtr(float* w, VectorArg V)noexcept{
    assert(w != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *w = V.vector4_f32[3];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_f32(w, V, 3);
#elif defined(_MATH_SSE4_INTRINSICS_)
    * (reinterpret_cast<int*>(w)) = _mm_extract_ps(V, 3);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
    _mm_store_ss(w, vResult);
#endif
}

// Return an integer value via an index. This is not a recommended
// function to use due to performance loss.
inline uint32_t MathCallConv VectorGetIntByIndex(VectorArg V, size_t i)noexcept{
    assert(i < 4);
    _Analysis_assume_(i < 4);
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_u32[i];
#else
    VectorU32 U;
    U.v = V;
    return U.u[i];
#endif
}

// Return the X component in an integer register.
inline uint32_t MathCallConv VectorGetIntX(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_u32[0];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_u32(vreinterpretq_u32_f32(V), 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(V)));
#endif
}

// Return the Y component in an integer register.
inline uint32_t MathCallConv VectorGetIntY(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_u32[1];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_u32(vreinterpretq_u32_f32(V), 1);
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i V1 = _mm_castps_si128(V);
    return static_cast<uint32_t>(_mm_extract_epi32(V1, 1));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vResulti = _mm_shuffle_epi32(_mm_castps_si128(V), _MM_SHUFFLE(1, 1, 1, 1));
    return static_cast<uint32_t>(_mm_cvtsi128_si32(vResulti));
#endif
}

// Return the Z component in an integer register.
inline uint32_t MathCallConv VectorGetIntZ(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_u32[2];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_u32(vreinterpretq_u32_f32(V), 2);
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i V1 = _mm_castps_si128(V);
    return static_cast<uint32_t>(_mm_extract_epi32(V1, 2));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vResulti = _mm_shuffle_epi32(_mm_castps_si128(V), _MM_SHUFFLE(2, 2, 2, 2));
    return static_cast<uint32_t>(_mm_cvtsi128_si32(vResulti));
#endif
}

// Return the W component in an integer register.
inline uint32_t MathCallConv VectorGetIntW(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return V.vector4_u32[3];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vgetq_lane_u32(vreinterpretq_u32_f32(V), 3);
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i V1 = _mm_castps_si128(V);
    return static_cast<uint32_t>(_mm_extract_epi32(V1, 3));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vResulti = _mm_shuffle_epi32(_mm_castps_si128(V), _MM_SHUFFLE(3, 3, 3, 3));
    return static_cast<uint32_t>(_mm_cvtsi128_si32(vResulti));
#endif
}

// Store a component indexed by i into a 32 bit integer location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetIntByIndexPtr(uint32_t* x, VectorArg V, size_t i)noexcept{
    assert(x != nullptr);
    assert(i < 4);
    _Analysis_assume_(i < 4);
#if defined(_MATH_NO_INTRINSICS_)
    *x = V.vector4_u32[i];
#else
    VectorU32 U;
    U.v = V;
    *x = U.u[i];
#endif
}

// Store the X component into a 32 bit integer location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetIntXPtr(uint32_t* x, VectorArg V)noexcept{
    assert(x != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *x = V.vector4_u32[0];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_u32(x, *reinterpret_cast<const uint32x4_t*>(&V), 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    _mm_store_ss(reinterpret_cast<float*>(x), V);
#endif
}

// Store the Y component into a 32 bit integer location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetIntYPtr(uint32_t* y, VectorArg V)noexcept{
    assert(y != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *y = V.vector4_u32[1];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_u32(y, *reinterpret_cast<const uint32x4_t*>(&V), 1);
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i V1 = _mm_castps_si128(V);
    *y = static_cast<uint32_t>(_mm_extract_epi32(V1, 1));
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
    _mm_store_ss(reinterpret_cast<float*>(y), vResult);
#endif
}

// Store the Z component into a 32 bit integer location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetIntZPtr(uint32_t* z, VectorArg V)noexcept{
    assert(z != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *z = V.vector4_u32[2];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_u32(z, *reinterpret_cast<const uint32x4_t*>(&V), 2);
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i V1 = _mm_castps_si128(V);
    *z = static_cast<uint32_t>(_mm_extract_epi32(V1, 2));
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
    _mm_store_ss(reinterpret_cast<float*>(z), vResult);
#endif
}

// Store the W component into a 32 bit integer location in memory.
_Use_decl_annotations_
inline void MathCallConv VectorGetIntWPtr(uint32_t* w, VectorArg V)noexcept{
    assert(w != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    *w = V.vector4_u32[3];
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    vst1q_lane_u32(w, *reinterpret_cast<const uint32x4_t*>(&V), 3);
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i V1 = _mm_castps_si128(V);
    *w = static_cast<uint32_t>(_mm_extract_epi32(V1, 3));
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
    _mm_store_ss(reinterpret_cast<float*>(w), vResult);
#endif
}

// Set a single indexed floating point component
inline Vector MathCallConv VectorSetByIndex(VectorArg V, float f, size_t i)noexcept{
    assert(i < 4);
    _Analysis_assume_(i < 4);
    VectorF32 U;
    U.v = V;
    U.f[i] = f;
    return U.v;
}

// Sets the X component of a vector to a passed floating point value
inline Vector MathCallConv VectorSetX(VectorArg V, float x)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            x,
            V.vector4_f32[1],
            V.vector4_f32[2],
            V.vector4_f32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vsetq_lane_f32(x, V, 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = _mm_set_ss(x);
    vResult = _mm_move_ss(V, vResult);
    return vResult;
#endif
}

// Sets the Y component of a vector to a passed floating point value
inline Vector MathCallConv VectorSetY(VectorArg V, float y)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            V.vector4_f32[0],
            y,
            V.vector4_f32[2],
            V.vector4_f32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vsetq_lane_f32(y, V, 1);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vResult = _mm_set_ss(y);
    vResult = _mm_insert_ps(V, vResult, 0x10);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap y and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 2, 0, 1));
    // Convert input to vector
    Vector vTemp = _mm_set_ss(y);
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap y and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 2, 0, 1));
    return vResult;
#endif
}
// Sets the Z component of a vector to a passed floating point value
inline Vector MathCallConv VectorSetZ(VectorArg V, float z)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            V.vector4_f32[0],
            V.vector4_f32[1],
            z,
            V.vector4_f32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vsetq_lane_f32(z, V, 2);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vResult = _mm_set_ss(z);
    vResult = _mm_insert_ps(V, vResult, 0x20);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap z and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 0, 1, 2));
    // Convert input to vector
    Vector vTemp = _mm_set_ss(z);
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap z and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 0, 1, 2));
    return vResult;
#endif
}

// Sets the W component of a vector to a passed floating point value
inline Vector MathCallConv VectorSetW(VectorArg V, float w)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            V.vector4_f32[0],
            V.vector4_f32[1],
            V.vector4_f32[2],
            w
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vsetq_lane_f32(w, V, 3);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vResult = _mm_set_ss(w);
    vResult = _mm_insert_ps(V, vResult, 0x30);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap w and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 2, 1, 3));
    // Convert input to vector
    Vector vTemp = _mm_set_ss(w);
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap w and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(0, 2, 1, 3));
    return vResult;
#endif
}

// Sets a component of a vector to a floating point value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetByIndexPtr(VectorArg V, const float* f, size_t i)noexcept{
    assert(f != nullptr);
    assert(i < 4);
    _Analysis_assume_(i < 4);
    VectorF32 U;
    U.v = V;
    U.f[i] = *f;
    return U.v;
}

// Sets the X component of a vector to a floating point value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetXPtr(VectorArg V, const float* x)noexcept{
    assert(x != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            *x,
            V.vector4_f32[1],
            V.vector4_f32[2],
            V.vector4_f32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vld1q_lane_f32(x, V, 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = _mm_load_ss(x);
    vResult = _mm_move_ss(V, vResult);
    return vResult;
#endif
}

// Sets the Y component of a vector to a floating point value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetYPtr(VectorArg V, const float* y)noexcept{
    assert(y != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            V.vector4_f32[0],
            *y,
            V.vector4_f32[2],
            V.vector4_f32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vld1q_lane_f32(y, V, 1);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap y and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 2, 0, 1));
    // Convert input to vector
    Vector vTemp = _mm_load_ss(y);
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap y and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 2, 0, 1));
    return vResult;
#endif
}

// Sets the Z component of a vector to a floating point value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetZPtr(VectorArg V, const float* z)noexcept{
    assert(z != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            V.vector4_f32[0],
            V.vector4_f32[1],
            *z,
            V.vector4_f32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vld1q_lane_f32(z, V, 2);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap z and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 0, 1, 2));
    // Convert input to vector
    Vector vTemp = _mm_load_ss(z);
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap z and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 0, 1, 2));
    return vResult;
#endif
}

// Sets the W component of a vector to a floating point value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetWPtr(VectorArg V, const float* w)noexcept{
    assert(w != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 U = { { {
            V.vector4_f32[0],
            V.vector4_f32[1],
            V.vector4_f32[2],
            *w
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vld1q_lane_f32(w, V, 3);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap w and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 2, 1, 3));
    // Convert input to vector
    Vector vTemp = _mm_load_ss(w);
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap w and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(0, 2, 1, 3));
    return vResult;
#endif
}

// Sets a component of a vector to an integer passed by value
inline Vector MathCallConv VectorSetIntByIndex(VectorArg V, uint32_t x, size_t i)noexcept{
    assert(i < 4);
    _Analysis_assume_(i < 4);
    VectorU32 tmp;
    tmp.v = V;
    tmp.u[i] = x;
    return tmp;
}

// Sets the X component of a vector to an integer passed by value
inline Vector MathCallConv VectorSetIntX(VectorArg V, uint32_t x)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            x,
            V.vector4_u32[1],
            V.vector4_u32[2],
            V.vector4_u32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vsetq_lane_u32(x, vreinterpretq_u32_f32(V), 0));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cvtsi32_si128(static_cast<int>(x));
    Vector vResult = _mm_move_ss(V, _mm_castsi128_ps(vTemp));
    return vResult;
#endif
}

// Sets the Y component of a vector to an integer passed by value
inline Vector MathCallConv VectorSetIntY(VectorArg V, uint32_t y)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            V.vector4_u32[0],
            y,
            V.vector4_u32[2],
            V.vector4_u32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vsetq_lane_u32(y, vreinterpretq_u32_f32(V), 1));
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i vResult = _mm_castps_si128(V);
    vResult = _mm_insert_epi32(vResult, static_cast<int>(y), 1);
    return _mm_castsi128_ps(vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap y and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 2, 0, 1));
    // Convert input to vector
    __m128i vTemp = _mm_cvtsi32_si128(static_cast<int>(y));
    // Replace the x component
    vResult = _mm_move_ss(vResult, _mm_castsi128_ps(vTemp));
    // Swap y and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 2, 0, 1));
    return vResult;
#endif
}

// Sets the Z component of a vector to an integer passed by value
inline Vector MathCallConv VectorSetIntZ(VectorArg V, uint32_t z)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            V.vector4_u32[0],
            V.vector4_u32[1],
            z,
            V.vector4_u32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vsetq_lane_u32(z, vreinterpretq_u32_f32(V), 2));
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i vResult = _mm_castps_si128(V);
    vResult = _mm_insert_epi32(vResult, static_cast<int>(z), 2);
    return _mm_castsi128_ps(vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap z and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 0, 1, 2));
    // Convert input to vector
    __m128i vTemp = _mm_cvtsi32_si128(static_cast<int>(z));
    // Replace the x component
    vResult = _mm_move_ss(vResult, _mm_castsi128_ps(vTemp));
    // Swap z and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 0, 1, 2));
    return vResult;
#endif
}

// Sets the W component of a vector to an integer passed by value
inline Vector MathCallConv VectorSetIntW(VectorArg V, uint32_t w)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            V.vector4_u32[0],
            V.vector4_u32[1],
            V.vector4_u32[2],
            w
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vsetq_lane_u32(w, vreinterpretq_u32_f32(V), 3));
#elif defined(_MATH_SSE4_INTRINSICS_)
    __m128i vResult = _mm_castps_si128(V);
    vResult = _mm_insert_epi32(vResult, static_cast<int>(w), 3);
    return _mm_castsi128_ps(vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap w and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 2, 1, 3));
    // Convert input to vector
    __m128i vTemp = _mm_cvtsi32_si128(static_cast<int>(w));
    // Replace the x component
    vResult = _mm_move_ss(vResult, _mm_castsi128_ps(vTemp));
    // Swap w and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(0, 2, 1, 3));
    return vResult;
#endif
}

// Sets a component of a vector to an integer value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetIntByIndexPtr(VectorArg V, const uint32_t* x, size_t i)noexcept{
    assert(x != nullptr);
    assert(i < 4);
    _Analysis_assume_(i < 4);
    VectorU32 tmp;
    tmp.v = V;
    tmp.u[i] = *x;
    return tmp;
}

// Sets the X component of a vector to an integer value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetIntXPtr(VectorArg V, const uint32_t* x)noexcept{
    assert(x != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            *x,
            V.vector4_u32[1],
            V.vector4_u32[2],
            V.vector4_u32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vld1q_lane_u32(x, *reinterpret_cast<const uint32x4_t*>(&V), 0));
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_load_ss(reinterpret_cast<const float*>(x));
    Vector vResult = _mm_move_ss(V, vTemp);
    return vResult;
#endif
}

// Sets the Y component of a vector to an integer value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetIntYPtr(VectorArg V, const uint32_t* y)noexcept{
    assert(y != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            V.vector4_u32[0],
            *y,
            V.vector4_u32[2],
            V.vector4_u32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vld1q_lane_u32(y, *reinterpret_cast<const uint32x4_t*>(&V), 1));
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap y and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 2, 0, 1));
    // Convert input to vector
    Vector vTemp = _mm_load_ss(reinterpret_cast<const float*>(y));
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap y and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 2, 0, 1));
    return vResult;
#endif
}

// Sets the Z component of a vector to an integer value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetIntZPtr(VectorArg V, const uint32_t* z)noexcept{
    assert(z != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            V.vector4_u32[0],
            V.vector4_u32[1],
            *z,
            V.vector4_u32[3]
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vld1q_lane_u32(z, *reinterpret_cast<const uint32x4_t*>(&V), 2));
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap z and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 0, 1, 2));
    // Convert input to vector
    Vector vTemp = _mm_load_ss(reinterpret_cast<const float*>(z));
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap z and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 0, 1, 2));
    return vResult;
#endif
}

// Sets the W component of a vector to an integer value passed by pointer
_Use_decl_annotations_
inline Vector MathCallConv VectorSetIntWPtr(VectorArg V, const uint32_t* w)noexcept{
    assert(w != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    VectorU32 U = { { {
            V.vector4_u32[0],
            V.vector4_u32[1],
            V.vector4_u32[2],
            *w
        } } };
    return U.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vld1q_lane_u32(w, *reinterpret_cast<const uint32x4_t*>(&V), 3));
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap w and x
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 2, 1, 3));
    // Convert input to vector
    Vector vTemp = _mm_load_ss(reinterpret_cast<const float*>(w));
    // Replace the x component
    vResult = _mm_move_ss(vResult, vTemp);
    // Swap w and x again
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(0, 2, 1, 3));
    return vResult;
#endif
}

inline Vector MathCallConv VectorSwizzle
(
    VectorArg V,
    uint32_t E0,
    uint32_t E1,
    uint32_t E2,
    uint32_t E3
)noexcept{
    assert((E0 < 4) && (E1 < 4) && (E2 < 4) && (E3 < 4));
    _Analysis_assume_((E0 < 4) && (E1 < 4) && (E2 < 4) && (E3 < 4));
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            V.vector4_f32[E0],
            V.vector4_f32[E1],
            V.vector4_f32[E2],
            V.vector4_f32[E3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const uint32_t ControlElement[4] =
    {
        0x03020100, // MATH_SWIZZLE_X
        0x07060504, // MATH_SWIZZLE_Y
        0x0B0A0908, // MATH_SWIZZLE_Z
        0x0F0E0D0C, // MATH_SWIZZLE_W
    };

    uint8x8x2_t tbl;
    tbl.val[0] = vreinterpret_u8_f32(vget_low_f32(V));
    tbl.val[1] = vreinterpret_u8_f32(vget_high_f32(V));

    uint32x2_t idx = vcreate_u32(static_cast<uint64_t>(ControlElement[E0]) | (static_cast<uint64_t>(ControlElement[E1]) << 32));
    const uint8x8_t rL = vtbl2_u8(tbl, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<uint64_t>(ControlElement[E2]) | (static_cast<uint64_t>(ControlElement[E3]) << 32));
    const uint8x8_t rH = vtbl2_u8(tbl, vreinterpret_u8_u32(idx));

    return vcombine_f32(vreinterpret_f32_u8(rL), vreinterpret_f32_u8(rH));
#elif defined(_MATH_AVX_INTRINSICS_)
    unsigned int elem[4] = { E0, E1, E2, E3 };
    __m128i vControl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&elem[0]));
    return _mm_permutevar_ps(V, vControl);
#else
    const uint32_t* aPtr = reinterpret_cast<const uint32_t*>(&V);

    Vector Result;
    uint32_t* pWork = reinterpret_cast<uint32_t*>(&Result);

    pWork[0] = aPtr[E0];
    pWork[1] = aPtr[E1];
    pWork[2] = aPtr[E2];
    pWork[3] = aPtr[E3];

    return Result;
#endif
}

inline Vector MathCallConv VectorPermute
(
    VectorArg V1,
    VectorArg V2,
    uint32_t PermuteX,
    uint32_t PermuteY,
    uint32_t PermuteZ,
    uint32_t PermuteW
)noexcept{
    assert(PermuteX <= 7 && PermuteY <= 7 && PermuteZ <= 7 && PermuteW <= 7);
    _Analysis_assume_(PermuteX <= 7 && PermuteY <= 7 && PermuteZ <= 7 && PermuteW <= 7);

#if defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    static const uint32_t ControlElement[8] =
    {
        0x03020100, // MATH_PERMUTE_0X
        0x07060504, // MATH_PERMUTE_0Y
        0x0B0A0908, // MATH_PERMUTE_0Z
        0x0F0E0D0C, // MATH_PERMUTE_0W
        0x13121110, // MATH_PERMUTE_1X
        0x17161514, // MATH_PERMUTE_1Y
        0x1B1A1918, // MATH_PERMUTE_1Z
        0x1F1E1D1C, // MATH_PERMUTE_1W
    };

    uint8x8x4_t tbl;
    tbl.val[0] = vreinterpret_u8_f32(vget_low_f32(V1));
    tbl.val[1] = vreinterpret_u8_f32(vget_high_f32(V1));
    tbl.val[2] = vreinterpret_u8_f32(vget_low_f32(V2));
    tbl.val[3] = vreinterpret_u8_f32(vget_high_f32(V2));

    uint32x2_t idx = vcreate_u32(static_cast<uint64_t>(ControlElement[PermuteX]) | (static_cast<uint64_t>(ControlElement[PermuteY]) << 32));
    const uint8x8_t rL = vtbl4_u8(tbl, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<uint64_t>(ControlElement[PermuteZ]) | (static_cast<uint64_t>(ControlElement[PermuteW]) << 32));
    const uint8x8_t rH = vtbl4_u8(tbl, vreinterpret_u8_u32(idx));

    return vcombine_f32(vreinterpret_f32_u8(rL), vreinterpret_f32_u8(rH));
#elif defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    static const VectorU32 three = { { { 3, 3, 3, 3 } } };

    MATH_ALIGNED_DATA(16) unsigned int elem[4] = { PermuteX, PermuteY, PermuteZ, PermuteW };
    __m128i vControl = _mm_load_si128(reinterpret_cast<const __m128i*>(&elem[0]));

    __m128i vSelect = _mm_cmpgt_epi32(vControl, three);
    vControl = _mm_castps_si128(_mm_and_ps(_mm_castsi128_ps(vControl), three));

    __m128 shuffled1 = _mm_permutevar_ps(V1, vControl);
    __m128 shuffled2 = _mm_permutevar_ps(V2, vControl);

    __m128 masked1 = _mm_andnot_ps(_mm_castsi128_ps(vSelect), shuffled1);
    __m128 masked2 = _mm_and_ps(_mm_castsi128_ps(vSelect), shuffled2);

    return _mm_or_ps(masked1, masked2);
#else

    const uint32_t* aPtr[2];
    aPtr[0] = reinterpret_cast<const uint32_t*>(&V1);
    aPtr[1] = reinterpret_cast<const uint32_t*>(&V2);

    Vector Result;
    uint32_t* pWork = reinterpret_cast<uint32_t*>(&Result);

    const uint32_t i0 = PermuteX & 3;
    const uint32_t vi0 = PermuteX >> 2;
    pWork[0] = aPtr[vi0][i0];

    const uint32_t i1 = PermuteY & 3;
    const uint32_t vi1 = PermuteY >> 2;
    pWork[1] = aPtr[vi1][i1];

    const uint32_t i2 = PermuteZ & 3;
    const uint32_t vi2 = PermuteZ >> 2;
    pWork[2] = aPtr[vi2][i2];

    const uint32_t i3 = PermuteW & 3;
    const uint32_t vi3 = PermuteW >> 2;
    pWork[3] = aPtr[vi3][i3];

    return Result;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Define a control vector to be used in VectorSelect
// operations.  The four integers specified in VectorSelectControl
// serve as indices to select between components in two vectors.
// The first index controls selection for the first component of
// the vectors involved in a select operation, the second index
// controls selection for the second component etc.  A value of
// zero for an index causes the corresponding component from the first
// vector to be selected whereas a one causes the component from the
// second vector to be selected instead.

inline Vector MathCallConv VectorSelectControl
(
    uint32_t VectorIndex0,
    uint32_t VectorIndex1,
    uint32_t VectorIndex2,
    uint32_t VectorIndex3
)noexcept{
#if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    // x=Index0,y=Index1,z=Index2,w=Index3
    __m128i vTemp = _mm_set_epi32(static_cast<int>(VectorIndex3), static_cast<int>(VectorIndex2), static_cast<int>(VectorIndex1), static_cast<int>(VectorIndex0));
    // Any non-zero entries become 0xFFFFFFFF else 0
    vTemp = _mm_cmpgt_epi32(vTemp, g_Zero);
    return _mm_castsi128_ps(vTemp);
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    int32x2_t V0 = vcreate_s32(static_cast<uint64_t>(VectorIndex0) | (static_cast<uint64_t>(VectorIndex1) << 32));
    int32x2_t V1 = vcreate_s32(static_cast<uint64_t>(VectorIndex2) | (static_cast<uint64_t>(VectorIndex3) << 32));
    int32x4_t vTemp = vcombine_s32(V0, V1);
    // Any non-zero entries become 0xFFFFFFFF else 0
    return vreinterpretq_f32_u32(vcgtq_s32(vTemp, g_Zero));
#else
    Vector    ControlVector;
    const uint32_t  ControlElement[] =
    {
        MATH_SELECT_0,
        MATH_SELECT_1
    };

    assert(VectorIndex0 < 2);
    assert(VectorIndex1 < 2);
    assert(VectorIndex2 < 2);
    assert(VectorIndex3 < 2);
    _Analysis_assume_(VectorIndex0 < 2);
    _Analysis_assume_(VectorIndex1 < 2);
    _Analysis_assume_(VectorIndex2 < 2);
    _Analysis_assume_(VectorIndex3 < 2);

    ControlVector.vector4_u32[0] = ControlElement[VectorIndex0];
    ControlVector.vector4_u32[1] = ControlElement[VectorIndex1];
    ControlVector.vector4_u32[2] = ControlElement[VectorIndex2];
    ControlVector.vector4_u32[3] = ControlElement[VectorIndex3];

    return ControlVector;

#endif
}

inline Vector MathCallConv VectorSelect
(
    VectorArg V1,
    VectorArg V2,
    VectorArg Control
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            (V1.vector4_u32[0] & ~Control.vector4_u32[0]) | (V2.vector4_u32[0] & Control.vector4_u32[0]),
            (V1.vector4_u32[1] & ~Control.vector4_u32[1]) | (V2.vector4_u32[1] & Control.vector4_u32[1]),
            (V1.vector4_u32[2] & ~Control.vector4_u32[2]) | (V2.vector4_u32[2] & Control.vector4_u32[2]),
            (V1.vector4_u32[3] & ~Control.vector4_u32[3]) | (V2.vector4_u32[3] & Control.vector4_u32[3]),
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vbslq_f32(vreinterpretq_u32_f32(Control), V2, V1);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp1 = _mm_andnot_ps(Control, V1);
    Vector vTemp2 = _mm_and_ps(V2, Control);
    return _mm_or_ps(vTemp1, vTemp2);
#endif
}

inline Vector MathCallConv VectorMergeXY
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            V1.vector4_u32[0],
            V2.vector4_u32[0],
            V1.vector4_u32[1],
            V2.vector4_u32[1],
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vzipq_f32(V1, V2).val[0];
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_unpacklo_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorMergeZW
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            V1.vector4_u32[2],
            V2.vector4_u32[2],
            V1.vector4_u32[3],
            V2.vector4_u32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vzipq_f32(V1, V2).val[1];
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_unpackhi_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorShiftLeft(VectorArg V1, VectorArg V2, uint32_t Elements)noexcept{
    assert(Elements < 4);
    _Analysis_assume_(Elements < 4);
    return VectorPermute(V1, V2, Elements, ((Elements)+1), ((Elements)+2), ((Elements)+3));
}

inline Vector MathCallConv VectorRotateLeft(VectorArg V, uint32_t Elements)noexcept{
    assert(Elements < 4);
    _Analysis_assume_(Elements < 4);
    return VectorSwizzle(V, Elements & 3, (Elements + 1) & 3, (Elements + 2) & 3, (Elements + 3) & 3);
}

inline Vector MathCallConv VectorRotateRight(VectorArg V, uint32_t Elements)noexcept{
    assert(Elements < 4);
    _Analysis_assume_(Elements < 4);
    return VectorSwizzle(V, (4 - (Elements)) & 3, (5 - (Elements)) & 3, (6 - (Elements)) & 3, (7 - (Elements)) & 3);
}

inline Vector MathCallConv VectorInsert(
    VectorArg VD, VectorArg VS,
    uint32_t VSLeftRotateElements,
    uint32_t Select0, uint32_t Select1, uint32_t Select2, uint32_t Select3)noexcept{
    Vector Control = VectorSelectControl(Select0 & 1, Select1 & 1, Select2 & 1, Select3 & 1);
    return VectorSelect(VD, VectorRotateLeft(VS, VSLeftRotateElements), Control);
}

// Comparison operations
inline Vector MathCallConv VectorEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_f32[0] == V2.vector4_f32[0]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[1] == V2.vector4_f32[1]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[2] == V2.vector4_f32[2]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[3] == V2.vector4_f32[3]) ? 0xFFFFFFFF : 0,
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vceqq_f32(V1, V2));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_cmpeq_ps(V1, V2);
#endif
}

_Use_decl_annotations_
inline Vector MathCallConv VectorEqualR
(
    uint32_t* pCR,
    VectorArg V1,
    VectorArg V2
)noexcept{
    assert(pCR != nullptr);
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t ux = (V1.vector4_f32[0] == V2.vector4_f32[0]) ? 0xFFFFFFFFU : 0;
    uint32_t uy = (V1.vector4_f32[1] == V2.vector4_f32[1]) ? 0xFFFFFFFFU : 0;
    uint32_t uz = (V1.vector4_f32[2] == V2.vector4_f32[2]) ? 0xFFFFFFFFU : 0;
    uint32_t uw = (V1.vector4_f32[3] == V2.vector4_f32[3]) ? 0xFFFFFFFFU : 0;
    uint32_t CR = 0;
    if(ux & uy & uz & uw){
        // All elements are greater
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!(ux | uy | uz | uw)){
        // All elements are not greater
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;

    VectorU32 Control = { { { ux, uy, uz, uw } } };
    return Control;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vreinterpret_u8_u32(vget_low_u32(vResult)), vreinterpret_u8_u32(vget_high_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        // All elements are equal
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        // All elements are not equal
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return vreinterpretq_f32_u32(vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    uint32_t CR = 0;
    int iTest = _mm_movemask_ps(vTemp);
    if(iTest == 0xf){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        // All elements are not greater
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return vTemp;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Treat the components of the vectors as unsigned integers and
// compare individual bits between the two.  This is useful for
// comparing control vectors and result vectors returned from
// other comparison operations.

inline Vector MathCallConv VectorEqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_u32[0] == V2.vector4_u32[0]) ? 0xFFFFFFFF : 0,
            (V1.vector4_u32[1] == V2.vector4_u32[1]) ? 0xFFFFFFFF : 0,
            (V1.vector4_u32[2] == V2.vector4_u32[2]) ? 0xFFFFFFFF : 0,
            (V1.vector4_u32[3] == V2.vector4_u32[3]) ? 0xFFFFFFFF : 0,
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vceqq_s32(vreinterpretq_s32_f32(V1), vreinterpretq_s32_f32(V2)));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return _mm_castsi128_ps(V);
#endif
}

_Use_decl_annotations_
inline Vector MathCallConv VectorEqualIntR
(
    uint32_t* pCR,
    VectorArg V1,
    VectorArg V2
)noexcept{
    assert(pCR != nullptr);
#if defined(_MATH_NO_INTRINSICS_)

    Vector Control = VectorEqualInt(V1, V2);

    *pCR = 0;
    if(Vector4EqualInt(Control, VectorTrueInt())){
        // All elements are equal
        *pCR |= MATH_CRMASK_CR6TRUE;
    }else if(Vector4EqualInt(Control, VectorFalseInt())){
        // All elements are not equal
        *pCR |= MATH_CRMASK_CR6FALSE;
    }
    return Control;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        // All elements are equal
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        // All elements are not equal
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return vreinterpretq_f32_u32(vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    int iTemp = _mm_movemask_ps(_mm_castsi128_ps(V));
    uint32_t CR = 0;
    if(iTemp == 0x0F){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTemp){
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return _mm_castsi128_ps(V);
#endif
}

inline Vector MathCallConv VectorNearEqual
(
    VectorArg V1,
    VectorArg V2,
    VectorArg Epsilon
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    float fDeltax = V1.vector4_f32[0] - V2.vector4_f32[0];
    float fDeltay = V1.vector4_f32[1] - V2.vector4_f32[1];
    float fDeltaz = V1.vector4_f32[2] - V2.vector4_f32[2];
    float fDeltaw = V1.vector4_f32[3] - V2.vector4_f32[3];

    fDeltax = fabsf(fDeltax);
    fDeltay = fabsf(fDeltay);
    fDeltaz = fabsf(fDeltaz);
    fDeltaw = fabsf(fDeltaw);

    VectorU32 Control = { { {
            (fDeltax <= Epsilon.vector4_f32[0]) ? 0xFFFFFFFFU : 0,
            (fDeltay <= Epsilon.vector4_f32[1]) ? 0xFFFFFFFFU : 0,
            (fDeltaz <= Epsilon.vector4_f32[2]) ? 0xFFFFFFFFU : 0,
            (fDeltaw <= Epsilon.vector4_f32[3]) ? 0xFFFFFFFFU : 0,
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vDelta = vsubq_f32(V1, V2);
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    return vacleq_f32(vDelta, Epsilon);
#else
    return vreinterpretq_f32_u32(vcleq_f32(vabsq_f32(vDelta), Epsilon));
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
    // Get the difference
    Vector vDelta = _mm_sub_ps(V1, V2);
    // Get the absolute value of the difference
    Vector vTemp = _mm_setzero_ps();
    vTemp = _mm_sub_ps(vTemp, vDelta);
    vTemp = _mm_max_ps(vTemp, vDelta);
    vTemp = _mm_cmple_ps(vTemp, Epsilon);
    return vTemp;
#endif
}

inline Vector MathCallConv VectorNotEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_f32[0] != V2.vector4_f32[0]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[1] != V2.vector4_f32[1]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[2] != V2.vector4_f32[2]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[3] != V2.vector4_f32[3]) ? 0xFFFFFFFF : 0,
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_f32(V1, V2)));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_cmpneq_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorNotEqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_u32[0] != V2.vector4_u32[0]) ? 0xFFFFFFFFU : 0,
            (V1.vector4_u32[1] != V2.vector4_u32[1]) ? 0xFFFFFFFFU : 0,
            (V1.vector4_u32[2] != V2.vector4_u32[2]) ? 0xFFFFFFFFU : 0,
            (V1.vector4_u32[3] != V2.vector4_u32[3]) ? 0xFFFFFFFFU : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vmvnq_u32(
        vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2))));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return _mm_xor_ps(_mm_castsi128_ps(V), g_NegOneMask);
#endif
}

inline Vector MathCallConv VectorGreater
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_f32[0] > V2.vector4_f32[0]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[1] > V2.vector4_f32[1]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[2] > V2.vector4_f32[2]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[3] > V2.vector4_f32[3]) ? 0xFFFFFFFF : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vcgtq_f32(V1, V2));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_cmpgt_ps(V1, V2);
#endif
}

_Use_decl_annotations_
inline Vector MathCallConv VectorGreaterR
(
    uint32_t* pCR,
    VectorArg V1,
    VectorArg V2
)noexcept{
    assert(pCR != nullptr);
#if defined(_MATH_NO_INTRINSICS_)

    uint32_t ux = (V1.vector4_f32[0] > V2.vector4_f32[0]) ? 0xFFFFFFFFU : 0;
    uint32_t uy = (V1.vector4_f32[1] > V2.vector4_f32[1]) ? 0xFFFFFFFFU : 0;
    uint32_t uz = (V1.vector4_f32[2] > V2.vector4_f32[2]) ? 0xFFFFFFFFU : 0;
    uint32_t uw = (V1.vector4_f32[3] > V2.vector4_f32[3]) ? 0xFFFFFFFFU : 0;
    uint32_t CR = 0;
    if(ux & uy & uz & uw){
        // All elements are greater
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!(ux | uy | uz | uw)){
        // All elements are not greater
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;

    VectorU32 Control = { { { ux, uy, uz, uw } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgtq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        // All elements are greater
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        // All elements are not greater
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return vreinterpretq_f32_u32(vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpgt_ps(V1, V2);
    uint32_t CR = 0;
    int iTest = _mm_movemask_ps(vTemp);
    if(iTest == 0xf){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        // All elements are not greater
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return vTemp;
#endif
}

inline Vector MathCallConv VectorGreaterOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_f32[0] >= V2.vector4_f32[0]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[1] >= V2.vector4_f32[1]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[2] >= V2.vector4_f32[2]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[3] >= V2.vector4_f32[3]) ? 0xFFFFFFFF : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vcgeq_f32(V1, V2));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_cmpge_ps(V1, V2);
#endif
}

_Use_decl_annotations_
inline Vector MathCallConv VectorGreaterOrEqualR
(
    uint32_t* pCR,
    VectorArg V1,
    VectorArg V2
)noexcept{
    assert(pCR != nullptr);
#if defined(_MATH_NO_INTRINSICS_)

    uint32_t ux = (V1.vector4_f32[0] >= V2.vector4_f32[0]) ? 0xFFFFFFFFU : 0;
    uint32_t uy = (V1.vector4_f32[1] >= V2.vector4_f32[1]) ? 0xFFFFFFFFU : 0;
    uint32_t uz = (V1.vector4_f32[2] >= V2.vector4_f32[2]) ? 0xFFFFFFFFU : 0;
    uint32_t uw = (V1.vector4_f32[3] >= V2.vector4_f32[3]) ? 0xFFFFFFFFU : 0;
    uint32_t CR = 0;
    if(ux & uy & uz & uw){
        // All elements are greater
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!(ux | uy | uz | uw)){
        // All elements are not greater
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;

    VectorU32 Control = { { { ux, uy, uz, uw } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgeq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        // All elements are greater or equal
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        // All elements are not greater or equal
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return vreinterpretq_f32_u32(vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpge_ps(V1, V2);
    uint32_t CR = 0;
    int iTest = _mm_movemask_ps(vTemp);
    if(iTest == 0xf){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        // All elements are not greater
        CR = MATH_CRMASK_CR6FALSE;
    }
    *pCR = CR;
    return vTemp;
#endif
}

inline Vector MathCallConv VectorLess
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_f32[0] < V2.vector4_f32[0]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[1] < V2.vector4_f32[1]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[2] < V2.vector4_f32[2]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[3] < V2.vector4_f32[3]) ? 0xFFFFFFFF : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vcltq_f32(V1, V2));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_cmplt_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorLessOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V1.vector4_f32[0] <= V2.vector4_f32[0]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[1] <= V2.vector4_f32[1]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[2] <= V2.vector4_f32[2]) ? 0xFFFFFFFF : 0,
            (V1.vector4_f32[3] <= V2.vector4_f32[3]) ? 0xFFFFFFFF : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vcleq_f32(V1, V2));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_cmple_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorInBounds
(
    VectorArg V,
    VectorArg Bounds
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            (V.vector4_f32[0] <= Bounds.vector4_f32[0] && V.vector4_f32[0] >= -Bounds.vector4_f32[0]) ? 0xFFFFFFFF : 0,
            (V.vector4_f32[1] <= Bounds.vector4_f32[1] && V.vector4_f32[1] >= -Bounds.vector4_f32[1]) ? 0xFFFFFFFF : 0,
            (V.vector4_f32[2] <= Bounds.vector4_f32[2] && V.vector4_f32[2] >= -Bounds.vector4_f32[2]) ? 0xFFFFFFFF : 0,
            (V.vector4_f32[3] <= Bounds.vector4_f32[3] && V.vector4_f32[3] >= -Bounds.vector4_f32[3]) ? 0xFFFFFFFF : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Test if less than or equal
    uint32x4_t vTemp1 = vcleq_f32(V, Bounds);
    // Negate the bounds
    uint32x4_t vTemp2 = vreinterpretq_u32_f32(vnegq_f32(Bounds));
    // Test if greater or equal (Reversed)
    vTemp2 = vcleq_f32(vreinterpretq_f32_u32(vTemp2), V);
    // Blend answers
    vTemp1 = vandq_u32(vTemp1, vTemp2);
    return vreinterpretq_f32_u32(vTemp1);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Test if less than or equal
    Vector vTemp1 = _mm_cmple_ps(V, Bounds);
    // Negate the bounds
    Vector vTemp2 = _mm_mul_ps(Bounds, g_NegativeOne);
    // Test if greater or equal (Reversed)
    vTemp2 = _mm_cmple_ps(vTemp2, V);
    // Blend answers
    vTemp1 = _mm_and_ps(vTemp1, vTemp2);
    return vTemp1;
#endif
}

_Use_decl_annotations_
inline Vector MathCallConv VectorInBoundsR
(
    uint32_t* pCR,
    VectorArg V,
    VectorArg Bounds
)noexcept{
    assert(pCR != nullptr);
#if defined(_MATH_NO_INTRINSICS_)

    uint32_t ux = (V.vector4_f32[0] <= Bounds.vector4_f32[0] && V.vector4_f32[0] >= -Bounds.vector4_f32[0]) ? 0xFFFFFFFFU : 0;
    uint32_t uy = (V.vector4_f32[1] <= Bounds.vector4_f32[1] && V.vector4_f32[1] >= -Bounds.vector4_f32[1]) ? 0xFFFFFFFFU : 0;
    uint32_t uz = (V.vector4_f32[2] <= Bounds.vector4_f32[2] && V.vector4_f32[2] >= -Bounds.vector4_f32[2]) ? 0xFFFFFFFFU : 0;
    uint32_t uw = (V.vector4_f32[3] <= Bounds.vector4_f32[3] && V.vector4_f32[3] >= -Bounds.vector4_f32[3]) ? 0xFFFFFFFFU : 0;

    uint32_t CR = 0;
    if(ux & uy & uz & uw){
        // All elements are in bounds
        CR = MATH_CRMASK_CR6BOUNDS;
    }
    *pCR = CR;

    VectorU32 Control = { { { ux, uy, uz, uw } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Test if less than or equal
    uint32x4_t vTemp1 = vcleq_f32(V, Bounds);
    // Negate the bounds
    uint32x4_t vTemp2 = vreinterpretq_u32_f32(vnegq_f32(Bounds));
    // Test if greater or equal (Reversed)
    vTemp2 = vcleq_f32(vreinterpretq_f32_u32(vTemp2), V);
    // Blend answers
    vTemp1 = vandq_u32(vTemp1, vTemp2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vTemp1)), vget_high_u8(vreinterpretq_u8_u32(vTemp1)));
    uint16x4x2_t vTemp3 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp3.val[1]), 1);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        // All elements are in bounds
        CR = MATH_CRMASK_CR6BOUNDS;
    }
    *pCR = CR;
    return vreinterpretq_f32_u32(vTemp1);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Test if less than or equal
    Vector vTemp1 = _mm_cmple_ps(V, Bounds);
    // Negate the bounds
    Vector vTemp2 = _mm_mul_ps(Bounds, g_NegativeOne);
    // Test if greater or equal (Reversed)
    vTemp2 = _mm_cmple_ps(vTemp2, V);
    // Blend answers
    vTemp1 = _mm_and_ps(vTemp1, vTemp2);

    uint32_t CR = 0;
    if(_mm_movemask_ps(vTemp1) == 0xf){
        // All elements are in bounds
        CR = MATH_CRMASK_CR6BOUNDS;
    }
    *pCR = CR;
    return vTemp1;
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(push)
#pragma float_control(precise, on)
#endif

inline Vector MathCallConv VectorIsNaN(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            MATH_IS_NAN(V.vector4_f32[0]) ? 0xFFFFFFFFU : 0,
            MATH_IS_NAN(V.vector4_f32[1]) ? 0xFFFFFFFFU : 0,
            MATH_IS_NAN(V.vector4_f32[2]) ? 0xFFFFFFFFU : 0,
            MATH_IS_NAN(V.vector4_f32[3]) ? 0xFFFFFFFFU : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    VectorU32 vResult = { { {
        isnan(vgetq_lane_f32(V, 0)) ? 0xFFFFFFFFU : 0,
        isnan(vgetq_lane_f32(V, 1)) ? 0xFFFFFFFFU : 0,
        isnan(vgetq_lane_f32(V, 2)) ? 0xFFFFFFFFU : 0,
        isnan(vgetq_lane_f32(V, 3)) ? 0xFFFFFFFFU : 0 } } };
    return vResult.v;
#else
// Test against itself. NaN is always not equal
    uint32x4_t vTempNan = vceqq_f32(V, V);
    // Flip results
    return vreinterpretq_f32_u32(vmvnq_u32(vTempNan));
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    MATH_ALIGNED_DATA(16) float tmp[4];
    _mm_store_ps(tmp, V);
    VectorU32 vResult = { { {
        isnan(tmp[0]) ? 0xFFFFFFFFU : 0,
        isnan(tmp[1]) ? 0xFFFFFFFFU : 0,
        isnan(tmp[2]) ? 0xFFFFFFFFU : 0,
        isnan(tmp[3]) ? 0xFFFFFFFFU : 0 } } };
    return vResult.v;
#else
// Test against itself. NaN is always not equal
    return _mm_cmpneq_ps(V, V);
#endif
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(pop)
#endif

inline Vector MathCallConv VectorIsInfinite(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Control = { { {
            MATH_IS_INF(V.vector4_f32[0]) ? 0xFFFFFFFFU : 0,
            MATH_IS_INF(V.vector4_f32[1]) ? 0xFFFFFFFFU : 0,
            MATH_IS_INF(V.vector4_f32[2]) ? 0xFFFFFFFFU : 0,
            MATH_IS_INF(V.vector4_f32[3]) ? 0xFFFFFFFFU : 0
        } } };
    return Control.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Mask off the sign bit
    uint32x4_t vTemp = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    // Compare to infinity
    vTemp = vceqq_f32(vreinterpretq_f32_u32(vTemp), g_Infinity);
    // If any are infinity, the signs are true.
    return vreinterpretq_f32_u32(vTemp);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Mask off the sign bit
    __m128 vTemp = _mm_and_ps(V, g_AbsMask);
    // Compare to infinity
    vTemp = _mm_cmpeq_ps(vTemp, g_Infinity);
    // If any are infinity, the signs are true.
    return vTemp;
#endif
}

// Rounding and clamping operations
inline Vector MathCallConv VectorMin
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            (V1.vector4_f32[0] < V2.vector4_f32[0]) ? V1.vector4_f32[0] : V2.vector4_f32[0],
            (V1.vector4_f32[1] < V2.vector4_f32[1]) ? V1.vector4_f32[1] : V2.vector4_f32[1],
            (V1.vector4_f32[2] < V2.vector4_f32[2]) ? V1.vector4_f32[2] : V2.vector4_f32[2],
            (V1.vector4_f32[3] < V2.vector4_f32[3]) ? V1.vector4_f32[3] : V2.vector4_f32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vminq_f32(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_min_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorMax
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            (V1.vector4_f32[0] > V2.vector4_f32[0]) ? V1.vector4_f32[0] : V2.vector4_f32[0],
            (V1.vector4_f32[1] > V2.vector4_f32[1]) ? V1.vector4_f32[1] : V2.vector4_f32[1],
            (V1.vector4_f32[2] > V2.vector4_f32[2]) ? V1.vector4_f32[2] : V2.vector4_f32[2],
            (V1.vector4_f32[3] > V2.vector4_f32[3]) ? V1.vector4_f32[3] : V2.vector4_f32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vmaxq_f32(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_max_ps(V1, V2);
#endif
}

namespace MathInternal{
    // Round to nearest (even) a.k.a. banker's rounding
    inline float round_to_nearest(float x)noexcept{
        float i = floorf(x);
        x -= i;
        if(x < 0.5f)
            return i;
        if(x > 0.5f)
            return i + 1.f;

        float int_part;
        (void)modff(i / 2.f, &int_part);
        if((2.f * int_part) == i){
            return i;
        }

        return i + 1.f;
    }
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(push)
#pragma float_control(precise, on)
#endif

inline Vector MathCallConv VectorRound(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            MathInternal::round_to_nearest(V.vector4_f32[0]),
            MathInternal::round_to_nearest(V.vector4_f32[1]),
            MathInternal::round_to_nearest(V.vector4_f32[2]),
            MathInternal::round_to_nearest(V.vector4_f32[3])
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    return vrndnq_f32(V);
#else
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(V), g_NegativeZero);
    float32x4_t sMagic = vreinterpretq_f32_u32(vorrq_u32(g_NoFraction, sign));
    float32x4_t R1 = vaddq_f32(V, sMagic);
    R1 = vsubq_f32(R1, sMagic);
    float32x4_t R2 = vabsq_f32(V);
    uint32x4_t mask = vcleq_f32(R2, g_NoFraction);
    return vbslq_f32(mask, R1, V);
#endif
#elif defined(_MATH_SSE4_INTRINSICS_)
    return _mm_round_ps(V, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 sign = _mm_and_ps(V, g_NegativeZero);
    __m128 sMagic = _mm_or_ps(g_NoFraction, sign);
    __m128 R1 = _mm_add_ps(V, sMagic);
    R1 = _mm_sub_ps(R1, sMagic);
    __m128 R2 = _mm_and_ps(V, g_AbsMask);
    __m128 mask = _mm_cmple_ps(R2, g_NoFraction);
    R2 = _mm_andnot_ps(mask, V);
    R1 = _mm_and_ps(R1, mask);
    Vector vResult = _mm_xor_ps(R1, R2);
    return vResult;
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(pop)
#endif

inline Vector MathCallConv VectorTruncate(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    Vector Result;
    uint32_t     i;

    // Avoid C4701
    Result.vector4_f32[0] = 0.0f;

    for(i = 0; i < 4; ++i){
        if(MATH_IS_NAN(V.vector4_f32[i])){
            Result.vector4_u32[i] = 0x7FC00000;
        }else if(fabsf(V.vector4_f32[i]) < 8388608.0f){
            Result.vector4_f32[i] = static_cast<float>(static_cast<int32_t>(V.vector4_f32[i]));
        }else{
            Result.vector4_f32[i] = V.vector4_f32[i];
        }
    }
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    return vrndq_f32(V);
#else
    float32x4_t vTest = vabsq_f32(V);
    vTest = vreinterpretq_f32_u32(vcltq_f32(vTest, g_NoFraction));

    int32x4_t vInt = vcvtq_s32_f32(V);
    float32x4_t vResult = vcvtq_f32_s32(vInt);

    // All numbers less than 8388608 will use the round to int
    // All others, use the ORIGINAL value
    return vbslq_f32(vreinterpretq_u32_f32(vTest), vResult, V);
#endif
#elif defined(_MATH_SSE4_INTRINSICS_)
    return _mm_round_ps(V, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
#elif defined(_MATH_SSE_INTRINSICS_)
    // To handle NAN, INF and numbers greater than 8388608, use masking
    // Get the abs value
    __m128i vTest = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    // Test for greater than 8388608 (All floats with NO fractionals, NAN and INF
    vTest = _mm_cmplt_epi32(vTest, g_NoFraction);
    // Convert to int and back to float for rounding with truncation
    __m128i vInt = _mm_cvttps_epi32(V);
    // Convert back to floats
    Vector vResult = _mm_cvtepi32_ps(vInt);
    // All numbers less than 8388608 will use the round to int
    vResult = _mm_and_ps(vResult, _mm_castsi128_ps(vTest));
    // All others, use the ORIGINAL value
    vTest = _mm_andnot_si128(vTest, _mm_castps_si128(V));
    vResult = _mm_or_ps(vResult, _mm_castsi128_ps(vTest));
    return vResult;
#endif
}

inline Vector MathCallConv VectorFloor(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            floorf(V.vector4_f32[0]),
            floorf(V.vector4_f32[1]),
            floorf(V.vector4_f32[2]),
            floorf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    return vrndmq_f32(V);
#else
    float32x4_t vTest = vabsq_f32(V);
    vTest = vreinterpretq_f32_u32(vcltq_f32(vTest, g_NoFraction));
    // Truncate
    int32x4_t vInt = vcvtq_s32_f32(V);
    float32x4_t vResult = vcvtq_f32_s32(vInt);
    uint32x4_t vLargerMask = vcgtq_f32(vResult, V);
    // 0 -> 0, 0xffffffff -> -1.0f
    float32x4_t vLarger = vcvtq_f32_s32(vreinterpretq_s32_u32(vLargerMask));
    vResult = vaddq_f32(vResult, vLarger);
    // All numbers less than 8388608 will use the round to int
    // All others, use the ORIGINAL value
    return vbslq_f32(vreinterpretq_u32_f32(vTest), vResult, V);
#endif
#elif defined(_MATH_SSE4_INTRINSICS_)
    return _mm_floor_ps(V);
#elif defined(_MATH_SSE_INTRINSICS_)
    // To handle NAN, INF and numbers greater than 8388608, use masking
    __m128i vTest = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    vTest = _mm_cmplt_epi32(vTest, g_NoFraction);
    // Truncate
    __m128i vInt = _mm_cvttps_epi32(V);
    Vector vResult = _mm_cvtepi32_ps(vInt);
    __m128 vLarger = _mm_cmpgt_ps(vResult, V);
    // 0 -> 0, 0xffffffff -> -1.0f
    vLarger = _mm_cvtepi32_ps(_mm_castps_si128(vLarger));
    vResult = _mm_add_ps(vResult, vLarger);
    // All numbers less than 8388608 will use the round to int
    vResult = _mm_and_ps(vResult, _mm_castsi128_ps(vTest));
    // All others, use the ORIGINAL value
    vTest = _mm_andnot_si128(vTest, _mm_castps_si128(V));
    vResult = _mm_or_ps(vResult, _mm_castsi128_ps(vTest));
    return vResult;
#endif
}

inline Vector MathCallConv VectorCeiling(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            ceilf(V.vector4_f32[0]),
            ceilf(V.vector4_f32[1]),
            ceilf(V.vector4_f32[2]),
            ceilf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    return vrndpq_f32(V);
#else
    float32x4_t vTest = vabsq_f32(V);
    vTest = vreinterpretq_f32_u32(vcltq_f32(vTest, g_NoFraction));
    // Truncate
    int32x4_t vInt = vcvtq_s32_f32(V);
    float32x4_t vResult = vcvtq_f32_s32(vInt);
    uint32x4_t vSmallerMask = vcltq_f32(vResult, V);
    // 0 -> 0, 0xffffffff -> -1.0f
    float32x4_t vSmaller = vcvtq_f32_s32(vreinterpretq_s32_u32(vSmallerMask));
    vResult = vsubq_f32(vResult, vSmaller);
    // All numbers less than 8388608 will use the round to int
    // All others, use the ORIGINAL value
    return vbslq_f32(vreinterpretq_u32_f32(vTest), vResult, V);
#endif
#elif defined(_MATH_SSE4_INTRINSICS_)
    return _mm_ceil_ps(V);
#elif defined(_MATH_SSE_INTRINSICS_)
    // To handle NAN, INF and numbers greater than 8388608, use masking
    __m128i vTest = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    vTest = _mm_cmplt_epi32(vTest, g_NoFraction);
    // Truncate
    __m128i vInt = _mm_cvttps_epi32(V);
    Vector vResult = _mm_cvtepi32_ps(vInt);
    __m128 vSmaller = _mm_cmplt_ps(vResult, V);
    // 0 -> 0, 0xffffffff -> -1.0f
    vSmaller = _mm_cvtepi32_ps(_mm_castps_si128(vSmaller));
    vResult = _mm_sub_ps(vResult, vSmaller);
    // All numbers less than 8388608 will use the round to int
    vResult = _mm_and_ps(vResult, _mm_castsi128_ps(vTest));
    // All others, use the ORIGINAL value
    vTest = _mm_andnot_si128(vTest, _mm_castps_si128(V));
    vResult = _mm_or_ps(vResult, _mm_castsi128_ps(vTest));
    return vResult;
#endif
}

inline Vector MathCallConv VectorClamp
(
    VectorArg V,
    VectorArg Min,
    VectorArg Max
)noexcept{
    assert(Vector4LessOrEqual(Min, Max));

#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = VectorMax(Min, V);
    Result = VectorMin(Max, Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vResult = vmaxq_f32(Min, V);
    vResult = vminq_f32(Max, vResult);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult;
    vResult = _mm_max_ps(Min, V);
    vResult = _mm_min_ps(Max, vResult);
    return vResult;
#endif
}

inline Vector MathCallConv VectorSaturate(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    const Vector Zero = VectorZero();

    return VectorClamp(V, Zero, g_One.v);

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Set <0 to 0
    float32x4_t vResult = vmaxq_f32(V, vdupq_n_f32(0));
    // Set>1 to 1
    return vminq_f32(vResult, vdupq_n_f32(1.0f));
#elif defined(_MATH_SSE_INTRINSICS_)
    // Set <0 to 0
    Vector vResult = _mm_max_ps(V, g_Zero);
    // Set>1 to 1
    return _mm_min_ps(vResult, g_One);
#endif
}

// Bitwise logical operations
inline Vector MathCallConv VectorAndInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            V1.vector4_u32[0] & V2.vector4_u32[0],
            V1.vector4_u32[1] & V2.vector4_u32[1],
            V1.vector4_u32[2] & V2.vector4_u32[2],
            V1.vector4_u32[3] & V2.vector4_u32[3]
        } } };
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2)));
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_and_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorAndCInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            V1.vector4_u32[0] & ~V2.vector4_u32[0],
            V1.vector4_u32[1] & ~V2.vector4_u32[1],
            V1.vector4_u32[2] & ~V2.vector4_u32[2],
            V1.vector4_u32[3] & ~V2.vector4_u32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2)));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_andnot_si128(_mm_castps_si128(V2), _mm_castps_si128(V1));
    return _mm_castsi128_ps(V);
#endif
}

inline Vector MathCallConv VectorOrInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            V1.vector4_u32[0] | V2.vector4_u32[0],
            V1.vector4_u32[1] | V2.vector4_u32[1],
            V1.vector4_u32[2] | V2.vector4_u32[2],
            V1.vector4_u32[3] | V2.vector4_u32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2)));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_or_si128(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return _mm_castsi128_ps(V);
#endif
}

inline Vector MathCallConv VectorNorInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            ~(V1.vector4_u32[0] | V2.vector4_u32[0]),
            ~(V1.vector4_u32[1] | V2.vector4_u32[1]),
            ~(V1.vector4_u32[2] | V2.vector4_u32[2]),
            ~(V1.vector4_u32[3] | V2.vector4_u32[3])
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t Result = vorrq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    return vreinterpretq_f32_u32(vbicq_u32(g_NegOneMask, Result));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i Result;
    Result = _mm_or_si128(_mm_castps_si128(V1), _mm_castps_si128(V2));
    Result = _mm_andnot_si128(Result, g_NegOneMask);
    return _mm_castsi128_ps(Result);
#endif
}

inline Vector MathCallConv VectorXorInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorU32 Result = { { {
            V1.vector4_u32[0] ^ V2.vector4_u32[0],
            V1.vector4_u32[1] ^ V2.vector4_u32[1],
            V1.vector4_u32[2] ^ V2.vector4_u32[2],
            V1.vector4_u32[3] ^ V2.vector4_u32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2)));
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i V = _mm_xor_si128(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return _mm_castsi128_ps(V);
#endif
}

// Computation operations
inline Vector MathCallConv VectorNegate(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            -V.vector4_f32[0],
            -V.vector4_f32[1],
            -V.vector4_f32[2],
            -V.vector4_f32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vnegq_f32(V);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector Z;

    Z = _mm_setzero_ps();

    return _mm_sub_ps(Z, V);
#endif
}

inline Vector MathCallConv VectorAdd
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            V1.vector4_f32[0] + V2.vector4_f32[0],
            V1.vector4_f32[1] + V2.vector4_f32[1],
            V1.vector4_f32[2] + V2.vector4_f32[2],
            V1.vector4_f32[3] + V2.vector4_f32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vaddq_f32(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_add_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorSum(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result;
    Result.f[0] =
        Result.f[1] =
        Result.f[2] =
        Result.f[3] = V.vector4_f32[0] + V.vector4_f32[1] + V.vector4_f32[2] + V.vector4_f32[3];
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    float32x4_t vTemp = vpaddq_f32(V, V);
    return vpaddq_f32(vTemp, vTemp);
#else
    float32x2_t v1 = vget_low_f32(V);
    float32x2_t v2 = vget_high_f32(V);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    return vcombine_f32(v1, v1);
#endif
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vTemp = _mm_hadd_ps(V, V);
    return _mm_hadd_ps(vTemp, vTemp);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 3, 0, 1));
    Vector vTemp2 = _mm_add_ps(V, vTemp);
    vTemp = MATH_PERMUTE_PS(vTemp2, _MM_SHUFFLE(1, 0, 3, 2));
    return _mm_add_ps(vTemp, vTemp2);
#endif
}

inline Vector MathCallConv VectorAddAngles
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    const Vector Zero = VectorZero();

    // Add the given angles together.  If the range of V1 is such
    // that -Pi <= V1 < Pi and the range of V2 is such that
    // -2Pi <= V2 <= 2Pi, then the range of the resulting angle
    // will be -Pi <= Result < Pi.
    Vector Result = VectorAdd(V1, V2);

    Vector Mask = VectorLess(Result, g_NegativePi.v);
    Vector Offset = VectorSelect(Zero, g_TwoPi.v, Mask);

    Mask = VectorGreaterOrEqual(Result, g_Pi.v);
    Offset = VectorSelect(Offset, g_NegativeTwoPi.v, Mask);

    Result = VectorAdd(Result, Offset);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Adjust the angles
    float32x4_t vResult = vaddq_f32(V1, V2);
    // Less than Pi?
    uint32x4_t vOffset = vcltq_f32(vResult, g_NegativePi);
    vOffset = vandq_u32(vOffset, g_TwoPi);
    // Add 2Pi to all entries less than -Pi
    vResult = vaddq_f32(vResult, vreinterpretq_f32_u32(vOffset));
    // Greater than or equal to Pi?
    vOffset = vcgeq_f32(vResult, g_Pi);
    vOffset = vandq_u32(vOffset, g_TwoPi);
    // Sub 2Pi to all entries greater than Pi
    vResult = vsubq_f32(vResult, vreinterpretq_f32_u32(vOffset));
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Adjust the angles
    Vector vResult = _mm_add_ps(V1, V2);
    // Less than Pi?
    Vector vOffset = _mm_cmplt_ps(vResult, g_NegativePi);
    vOffset = _mm_and_ps(vOffset, g_TwoPi);
    // Add 2Pi to all entries less than -Pi
    vResult = _mm_add_ps(vResult, vOffset);
    // Greater than or equal to Pi?
    vOffset = _mm_cmpge_ps(vResult, g_Pi);
    vOffset = _mm_and_ps(vOffset, g_TwoPi);
    // Sub 2Pi to all entries greater than Pi
    vResult = _mm_sub_ps(vResult, vOffset);
    return vResult;
#endif
}

inline Vector MathCallConv VectorSubtract
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            V1.vector4_f32[0] - V2.vector4_f32[0],
            V1.vector4_f32[1] - V2.vector4_f32[1],
            V1.vector4_f32[2] - V2.vector4_f32[2],
            V1.vector4_f32[3] - V2.vector4_f32[3]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vsubq_f32(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_sub_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorSubtractAngles
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    const Vector Zero = VectorZero();

    // Subtract the given angles.  If the range of V1 is such
    // that -Pi <= V1 < Pi and the range of V2 is such that
    // -2Pi <= V2 <= 2Pi, then the range of the resulting angle
    // will be -Pi <= Result < Pi.
    Vector Result = VectorSubtract(V1, V2);

    Vector Mask = VectorLess(Result, g_NegativePi.v);
    Vector Offset = VectorSelect(Zero, g_TwoPi.v, Mask);

    Mask = VectorGreaterOrEqual(Result, g_Pi.v);
    Offset = VectorSelect(Offset, g_NegativeTwoPi.v, Mask);

    Result = VectorAdd(Result, Offset);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Adjust the angles
    Vector vResult = vsubq_f32(V1, V2);
    // Less than Pi?
    uint32x4_t vOffset = vcltq_f32(vResult, g_NegativePi);
    vOffset = vandq_u32(vOffset, g_TwoPi);
    // Add 2Pi to all entries less than -Pi
    vResult = vaddq_f32(vResult, vreinterpretq_f32_u32(vOffset));
    // Greater than or equal to Pi?
    vOffset = vcgeq_f32(vResult, g_Pi);
    vOffset = vandq_u32(vOffset, g_TwoPi);
    // Sub 2Pi to all entries greater than Pi
    vResult = vsubq_f32(vResult, vreinterpretq_f32_u32(vOffset));
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Adjust the angles
    Vector vResult = _mm_sub_ps(V1, V2);
    // Less than Pi?
    Vector vOffset = _mm_cmplt_ps(vResult, g_NegativePi);
    vOffset = _mm_and_ps(vOffset, g_TwoPi);
    // Add 2Pi to all entries less than -Pi
    vResult = _mm_add_ps(vResult, vOffset);
    // Greater than or equal to Pi?
    vOffset = _mm_cmpge_ps(vResult, g_Pi);
    vOffset = _mm_and_ps(vOffset, g_TwoPi);
    // Sub 2Pi to all entries greater than Pi
    vResult = _mm_sub_ps(vResult, vOffset);
    return vResult;
#endif
}

inline Vector MathCallConv VectorMultiply
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            V1.vector4_f32[0] * V2.vector4_f32[0],
            V1.vector4_f32[1] * V2.vector4_f32[1],
            V1.vector4_f32[2] * V2.vector4_f32[2],
            V1.vector4_f32[3] * V2.vector4_f32[3]
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vmulq_f32(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_mul_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorMultiplyAdd
(
    VectorArg V1,
    VectorArg V2,
    VectorArg V3
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            V1.vector4_f32[0] * V2.vector4_f32[0] + V3.vector4_f32[0],
            V1.vector4_f32[1] * V2.vector4_f32[1] + V3.vector4_f32[1],
            V1.vector4_f32[2] * V2.vector4_f32[2] + V3.vector4_f32[2],
            V1.vector4_f32[3] * V2.vector4_f32[3] + V3.vector4_f32[3]
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    return vfmaq_f32(V3, V1, V2);
#else
    return vmlaq_f32(V3, V1, V2);
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
    return MATH_FMADD_PS(V1, V2, V3);
#endif
}

inline Vector MathCallConv VectorDivide
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            V1.vector4_f32[0] / V2.vector4_f32[0],
            V1.vector4_f32[1] / V2.vector4_f32[1],
            V1.vector4_f32[2] / V2.vector4_f32[2],
            V1.vector4_f32[3] / V2.vector4_f32[3]
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    return vdivq_f32(V1, V2);
#else
    // 2 iterations of Newton-Raphson refinement of reciprocal
    float32x4_t Reciprocal = vrecpeq_f32(V2);
    float32x4_t S = vrecpsq_f32(Reciprocal, V2);
    Reciprocal = vmulq_f32(S, Reciprocal);
    S = vrecpsq_f32(Reciprocal, V2);
    Reciprocal = vmulq_f32(S, Reciprocal);
    return vmulq_f32(V1, Reciprocal);
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_div_ps(V1, V2);
#endif
}

inline Vector MathCallConv VectorNegativeMultiplySubtract
(
    VectorArg V1,
    VectorArg V2,
    VectorArg V3
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            V3.vector4_f32[0] - (V1.vector4_f32[0] * V2.vector4_f32[0]),
            V3.vector4_f32[1] - (V1.vector4_f32[1] * V2.vector4_f32[1]),
            V3.vector4_f32[2] - (V1.vector4_f32[2] * V2.vector4_f32[2]),
            V3.vector4_f32[3] - (V1.vector4_f32[3] * V2.vector4_f32[3])
        } } };
    return Result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    return vfmsq_f32(V3, V1, V2);
#else
    return vmlsq_f32(V3, V1, V2);
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
    return MATH_FNMADD_PS(V1, V2, V3);
#endif
}

inline Vector MathCallConv VectorScale
(
    VectorArg V,
    float    ScaleFactor
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            V.vector4_f32[0] * ScaleFactor,
            V.vector4_f32[1] * ScaleFactor,
            V.vector4_f32[2] * ScaleFactor,
            V.vector4_f32[3] * ScaleFactor
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vmulq_n_f32(V, ScaleFactor);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = _mm_set_ps1(ScaleFactor);
    return _mm_mul_ps(vResult, V);
#endif
}

inline Vector MathCallConv VectorReciprocalEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            1.f / V.vector4_f32[0],
            1.f / V.vector4_f32[1],
            1.f / V.vector4_f32[2],
            1.f / V.vector4_f32[3]
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vrecpeq_f32(V);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_rcp_ps(V);
#endif
}

inline Vector MathCallConv VectorReciprocal(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            1.f / V.vector4_f32[0],
            1.f / V.vector4_f32[1],
            1.f / V.vector4_f32[2],
            1.f / V.vector4_f32[3]
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    float32x4_t one = vdupq_n_f32(1.0f);
    return vdivq_f32(one, V);
#else
    // 2 iterations of Newton-Raphson refinement
    float32x4_t Reciprocal = vrecpeq_f32(V);
    float32x4_t S = vrecpsq_f32(Reciprocal, V);
    Reciprocal = vmulq_f32(S, Reciprocal);
    S = vrecpsq_f32(Reciprocal, V);
    return vmulq_f32(S, Reciprocal);
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_div_ps(g_One, V);
#endif
}

// Return an estimated square root
inline Vector MathCallConv VectorSqrtEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            sqrtf(V.vector4_f32[0]),
            sqrtf(V.vector4_f32[1]),
            sqrtf(V.vector4_f32[2]),
            sqrtf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // 1 iteration of Newton-Raphson refinment of sqrt
    float32x4_t S0 = vrsqrteq_f32(V);
    float32x4_t P0 = vmulq_f32(V, S0);
    float32x4_t R0 = vrsqrtsq_f32(P0, S0);
    float32x4_t S1 = vmulq_f32(S0, R0);

    Vector VEqualsInfinity = VectorEqualInt(V, g_Infinity.v);
    Vector VEqualsZero = VectorEqual(V, vdupq_n_f32(0));
    Vector Result = vmulq_f32(V, S1);
    Vector Select = VectorEqualInt(VEqualsInfinity, VEqualsZero);
    return VectorSelect(V, Result, Select);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_sqrt_ps(V);
#endif
}

inline Vector MathCallConv VectorSqrt(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            sqrtf(V.vector4_f32[0]),
            sqrtf(V.vector4_f32[1]),
            sqrtf(V.vector4_f32[2]),
            sqrtf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // 3 iterations of Newton-Raphson refinment of sqrt
    float32x4_t S0 = vrsqrteq_f32(V);
    float32x4_t P0 = vmulq_f32(V, S0);
    float32x4_t R0 = vrsqrtsq_f32(P0, S0);
    float32x4_t S1 = vmulq_f32(S0, R0);
    float32x4_t P1 = vmulq_f32(V, S1);
    float32x4_t R1 = vrsqrtsq_f32(P1, S1);
    float32x4_t S2 = vmulq_f32(S1, R1);
    float32x4_t P2 = vmulq_f32(V, S2);
    float32x4_t R2 = vrsqrtsq_f32(P2, S2);
    float32x4_t S3 = vmulq_f32(S2, R2);

    Vector VEqualsInfinity = VectorEqualInt(V, g_Infinity.v);
    Vector VEqualsZero = VectorEqual(V, vdupq_n_f32(0));
    Vector Result = vmulq_f32(V, S3);
    Vector Select = VectorEqualInt(VEqualsInfinity, VEqualsZero);
    return VectorSelect(V, Result, Select);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_sqrt_ps(V);
#endif
}

inline Vector MathCallConv VectorReciprocalSqrtEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            1.f / sqrtf(V.vector4_f32[0]),
            1.f / sqrtf(V.vector4_f32[1]),
            1.f / sqrtf(V.vector4_f32[2]),
            1.f / sqrtf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vrsqrteq_f32(V);
#elif defined(_MATH_SSE_INTRINSICS_)
    return _mm_rsqrt_ps(V);
#endif
}

inline Vector MathCallConv VectorReciprocalSqrt(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            1.f / sqrtf(V.vector4_f32[0]),
            1.f / sqrtf(V.vector4_f32[1]),
            1.f / sqrtf(V.vector4_f32[2]),
            1.f / sqrtf(V.vector4_f32[3])
        } } };
    return Result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // 2 iterations of Newton-Raphson refinement of reciprocal
    float32x4_t S0 = vrsqrteq_f32(V);

    float32x4_t P0 = vmulq_f32(V, S0);
    float32x4_t R0 = vrsqrtsq_f32(P0, S0);

    float32x4_t S1 = vmulq_f32(S0, R0);
    float32x4_t P1 = vmulq_f32(V, S1);
    float32x4_t R1 = vrsqrtsq_f32(P1, S1);

    return vmulq_f32(S1, R1);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = _mm_sqrt_ps(V);
    vResult = _mm_div_ps(g_One, vResult);
    return vResult;
#endif
}

inline Vector MathCallConv VectorExp2(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            exp2f(V.vector4_f32[0]),
            exp2f(V.vector4_f32[1]),
            exp2f(V.vector4_f32[2]),
            exp2f(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    int32x4_t itrunc = vcvtq_s32_f32(V);
    float32x4_t ftrunc = vcvtq_f32_s32(itrunc);
    float32x4_t y = vsubq_f32(V, ftrunc);

    float32x4_t poly = vmlaq_f32(g_ExpEst6, g_ExpEst7, y);
    poly = vmlaq_f32(g_ExpEst5, poly, y);
    poly = vmlaq_f32(g_ExpEst4, poly, y);
    poly = vmlaq_f32(g_ExpEst3, poly, y);
    poly = vmlaq_f32(g_ExpEst2, poly, y);
    poly = vmlaq_f32(g_ExpEst1, poly, y);
    poly = vmlaq_f32(g_One, poly, y);

    int32x4_t biased = vaddq_s32(itrunc, g_ExponentBias);
    biased = vshlq_n_s32(biased, 23);
    float32x4_t result0 = VectorDivide(vreinterpretq_f32_s32(biased), poly);

    biased = vaddq_s32(itrunc, g_253);
    biased = vshlq_n_s32(biased, 23);
    float32x4_t result1 = VectorDivide(vreinterpretq_f32_s32(biased), poly);
    result1 = vmulq_f32(g_MinNormal.v, result1);

    // Use selection to handle the cases
    //  if(V is NaN) -> QNaN;
    //  else if(V sign bit set)
    //      if(V > -150)
    //         if(V.exponent < -126) -> result1
    //         else -> result0
    //      else -> +0
    //  else
    //      if(V < 128) -> result0
    //      else -> +inf

    uint32x4_t comp = vcltq_s32(vreinterpretq_s32_f32(V), g_Bin128);
    float32x4_t result2 = vbslq_f32(comp, result0, g_Infinity);

    comp = vcltq_s32(itrunc, g_SubnormalExponent);
    float32x4_t result3 = vbslq_f32(comp, result1, result0);

    comp = vcltq_s32(vreinterpretq_s32_f32(V), g_BinNeg150);
    float32x4_t result4 = vbslq_f32(comp, result3, g_Zero);

    int32x4_t sign = vandq_s32(vreinterpretq_s32_f32(V), g_NegativeZero);
    comp = vceqq_s32(sign, g_NegativeZero);
    float32x4_t result5 = vbslq_f32(comp, result4, result2);

    int32x4_t t0 = vandq_s32(vreinterpretq_s32_f32(V), g_QNaNTest);
    int32x4_t t1 = vandq_s32(vreinterpretq_s32_f32(V), g_Infinity);
    t0 = vreinterpretq_s32_u32(vceqq_s32(t0, g_Zero));
    t1 = vreinterpretq_s32_u32(vceqq_s32(t1, g_Infinity));
    int32x4_t isNaN = vbicq_s32(t1, t0);

    float32x4_t vResult = vbslq_f32(vreinterpretq_u32_s32(isNaN), g_QNaN, result5);
    return vResult;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_exp2_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i itrunc = _mm_cvttps_epi32(V);
    __m128 ftrunc = _mm_cvtepi32_ps(itrunc);
    __m128 y = _mm_sub_ps(V, ftrunc);

    __m128 poly = MATH_FMADD_PS(g_ExpEst7, y, g_ExpEst6);
    poly = MATH_FMADD_PS(poly, y, g_ExpEst5);
    poly = MATH_FMADD_PS(poly, y, g_ExpEst4);
    poly = MATH_FMADD_PS(poly, y, g_ExpEst3);
    poly = MATH_FMADD_PS(poly, y, g_ExpEst2);
    poly = MATH_FMADD_PS(poly, y, g_ExpEst1);
    poly = MATH_FMADD_PS(poly, y, g_One);

    __m128i biased = _mm_add_epi32(itrunc, g_ExponentBias);
    biased = _mm_slli_epi32(biased, 23);
    __m128 result0 = _mm_div_ps(_mm_castsi128_ps(biased), poly);

    biased = _mm_add_epi32(itrunc, g_253);
    biased = _mm_slli_epi32(biased, 23);
    __m128 result1 = _mm_div_ps(_mm_castsi128_ps(biased), poly);
    result1 = _mm_mul_ps(g_MinNormal.v, result1);

    // Use selection to handle the cases
    //  if(V is NaN) -> QNaN;
    //  else if(V sign bit set)
    //      if(V > -150)
    //         if(V.exponent < -126) -> result1
    //         else -> result0
    //      else -> +0
    //  else
    //      if(V < 128) -> result0
    //      else -> +inf

    __m128i comp = _mm_cmplt_epi32(_mm_castps_si128(V), g_Bin128);
    __m128i select0 = _mm_and_si128(comp, _mm_castps_si128(result0));
    __m128i select1 = _mm_andnot_si128(comp, g_Infinity);
    __m128i result2 = _mm_or_si128(select0, select1);

    comp = _mm_cmplt_epi32(itrunc, g_SubnormalExponent);
    select1 = _mm_and_si128(comp, _mm_castps_si128(result1));
    select0 = _mm_andnot_si128(comp, _mm_castps_si128(result0));
    __m128i result3 = _mm_or_si128(select0, select1);

    comp = _mm_cmplt_epi32(_mm_castps_si128(V), g_BinNeg150);
    select0 = _mm_and_si128(comp, result3);
    select1 = _mm_andnot_si128(comp, g_Zero);
    __m128i result4 = _mm_or_si128(select0, select1);

    __m128i sign = _mm_and_si128(_mm_castps_si128(V), g_NegativeZero);
    comp = _mm_cmpeq_epi32(sign, g_NegativeZero);
    select0 = _mm_and_si128(comp, result4);
    select1 = _mm_andnot_si128(comp, result2);
    __m128i result5 = _mm_or_si128(select0, select1);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(V), g_QNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(V), g_Infinity);
    t0 = _mm_cmpeq_epi32(t0, g_Zero);
    t1 = _mm_cmpeq_epi32(t1, g_Infinity);
    __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isNaN, g_QNaN);
    select1 = _mm_andnot_si128(isNaN, result5);
    __m128i vResult = _mm_or_si128(select0, select1);

    return _mm_castsi128_ps(vResult);
#endif
}

inline Vector MathCallConv VectorExp10(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            powf(10.0f, V.vector4_f32[0]),
            powf(10.0f, V.vector4_f32[1]),
            powf(10.0f, V.vector4_f32[2]),
            powf(10.0f, V.vector4_f32[3])
        } } };
    return Result.v;

#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_exp10_ps(V);
    return Result;
#else
    // exp10(V) = exp2(vin*log2(10))
    Vector Vten = VectorMultiply(g_Lg10, V);
    return VectorExp2(Vten);
#endif
}

inline Vector MathCallConv VectorExpE(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            expf(V.vector4_f32[0]),
            expf(V.vector4_f32[1]),
            expf(V.vector4_f32[2]),
            expf(V.vector4_f32[3])
        } } };
    return Result.v;

#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_exp_ps(V);
    return Result;
#else
    // expE(V) = exp2(vin*log2(e))
    Vector Ve = VectorMultiply(g_LgE, V);
    return VectorExp2(Ve);
#endif
}

inline Vector MathCallConv VectorExp(VectorArg V)noexcept{
    return VectorExp2(V);
}

#if defined(_MATH_SSE_INTRINSICS_)

namespace MathInternal{
    inline __m128i multi_sll_epi32(__m128i value, __m128i count)noexcept{
        __m128i v = _mm_shuffle_epi32(value, _MM_SHUFFLE(0, 0, 0, 0));
        __m128i c = _mm_shuffle_epi32(count, _MM_SHUFFLE(0, 0, 0, 0));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r0 = _mm_sll_epi32(v, c);

        v = _mm_shuffle_epi32(value, _MM_SHUFFLE(1, 1, 1, 1));
        c = _mm_shuffle_epi32(count, _MM_SHUFFLE(1, 1, 1, 1));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r1 = _mm_sll_epi32(v, c);

        v = _mm_shuffle_epi32(value, _MM_SHUFFLE(2, 2, 2, 2));
        c = _mm_shuffle_epi32(count, _MM_SHUFFLE(2, 2, 2, 2));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r2 = _mm_sll_epi32(v, c);

        v = _mm_shuffle_epi32(value, _MM_SHUFFLE(3, 3, 3, 3));
        c = _mm_shuffle_epi32(count, _MM_SHUFFLE(3, 3, 3, 3));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r3 = _mm_sll_epi32(v, c);

        // (r0,r0,r1,r1)
        __m128 r01 = _mm_shuffle_ps(_mm_castsi128_ps(r0), _mm_castsi128_ps(r1), _MM_SHUFFLE(0, 0, 0, 0));
        // (r2,r2,r3,r3)
        __m128 r23 = _mm_shuffle_ps(_mm_castsi128_ps(r2), _mm_castsi128_ps(r3), _MM_SHUFFLE(0, 0, 0, 0));
        // (r0,r1,r2,r3)
        __m128 result = _mm_shuffle_ps(r01, r23, _MM_SHUFFLE(2, 0, 2, 0));
        return _mm_castps_si128(result);
    }

    inline __m128i multi_srl_epi32(__m128i value, __m128i count)noexcept{
        __m128i v = _mm_shuffle_epi32(value, _MM_SHUFFLE(0, 0, 0, 0));
        __m128i c = _mm_shuffle_epi32(count, _MM_SHUFFLE(0, 0, 0, 0));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r0 = _mm_srl_epi32(v, c);

        v = _mm_shuffle_epi32(value, _MM_SHUFFLE(1, 1, 1, 1));
        c = _mm_shuffle_epi32(count, _MM_SHUFFLE(1, 1, 1, 1));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r1 = _mm_srl_epi32(v, c);

        v = _mm_shuffle_epi32(value, _MM_SHUFFLE(2, 2, 2, 2));
        c = _mm_shuffle_epi32(count, _MM_SHUFFLE(2, 2, 2, 2));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r2 = _mm_srl_epi32(v, c);

        v = _mm_shuffle_epi32(value, _MM_SHUFFLE(3, 3, 3, 3));
        c = _mm_shuffle_epi32(count, _MM_SHUFFLE(3, 3, 3, 3));
        c = _mm_and_si128(c, g_MaskX);
        __m128i r3 = _mm_srl_epi32(v, c);

        // (r0,r0,r1,r1)
        __m128 r01 = _mm_shuffle_ps(_mm_castsi128_ps(r0), _mm_castsi128_ps(r1), _MM_SHUFFLE(0, 0, 0, 0));
        // (r2,r2,r3,r3)
        __m128 r23 = _mm_shuffle_ps(_mm_castsi128_ps(r2), _mm_castsi128_ps(r3), _MM_SHUFFLE(0, 0, 0, 0));
        // (r0,r1,r2,r3)
        __m128 result = _mm_shuffle_ps(r01, r23, _MM_SHUFFLE(2, 0, 2, 0));
        return _mm_castps_si128(result);
    }

    inline __m128i GetLeadingBit(const __m128i value)noexcept{
        static const VectorI32 g_0000FFFF = { { { 0x0000FFFF, 0x0000FFFF, 0x0000FFFF, 0x0000FFFF } } };
        static const VectorI32 g_000000FF = { { { 0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF } } };
        static const VectorI32 g_0000000F = { { { 0x0000000F, 0x0000000F, 0x0000000F, 0x0000000F } } };
        static const VectorI32 g_00000003 = { { { 0x00000003, 0x00000003, 0x00000003, 0x00000003 } } };

        __m128i v = value, r, c, b, s;

        c = _mm_cmpgt_epi32(v, g_0000FFFF);   // c = (v > 0xFFFF)
        b = _mm_srli_epi32(c, 31);              // b = (c ? 1 : 0)
        r = _mm_slli_epi32(b, 4);               // r = (b << 4)
        v = multi_srl_epi32(v, r);              // v = (v >> r)

        c = _mm_cmpgt_epi32(v, g_000000FF);   // c = (v > 0xFF)
        b = _mm_srli_epi32(c, 31);              // b = (c ? 1 : 0)
        s = _mm_slli_epi32(b, 3);               // s = (b << 3)
        v = multi_srl_epi32(v, s);              // v = (v >> s)
        r = _mm_or_si128(r, s);                 // r = (r | s)

        c = _mm_cmpgt_epi32(v, g_0000000F);   // c = (v > 0xF)
        b = _mm_srli_epi32(c, 31);              // b = (c ? 1 : 0)
        s = _mm_slli_epi32(b, 2);               // s = (b << 2)
        v = multi_srl_epi32(v, s);              // v = (v >> s)
        r = _mm_or_si128(r, s);                 // r = (r | s)

        c = _mm_cmpgt_epi32(v, g_00000003);   // c = (v > 0x3)
        b = _mm_srli_epi32(c, 31);              // b = (c ? 1 : 0)
        s = _mm_slli_epi32(b, 1);               // s = (b << 1)
        v = multi_srl_epi32(v, s);              // v = (v >> s)
        r = _mm_or_si128(r, s);                 // r = (r | s)

        s = _mm_srli_epi32(v, 1);
        r = _mm_or_si128(r, s);
        return r;
    }
} // namespace MathInternal

#endif // _MATH_SSE_INTRINSICS_

#if defined(_MATH_ARM_NEON_INTRINSICS_)

namespace MathInternal{
    inline int32x4_t GetLeadingBit(const int32x4_t value)noexcept{
        static const VectorI32 g_0000FFFF = { { { 0x0000FFFF, 0x0000FFFF, 0x0000FFFF, 0x0000FFFF } } };
        static const VectorI32 g_000000FF = { { { 0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF } } };
        static const VectorI32 g_0000000F = { { { 0x0000000F, 0x0000000F, 0x0000000F, 0x0000000F } } };
        static const VectorI32 g_00000003 = { { { 0x00000003, 0x00000003, 0x00000003, 0x00000003 } } };

        uint32x4_t c = vcgtq_s32(value, g_0000FFFF);              // c = (v > 0xFFFF)
        int32x4_t b = vshrq_n_s32(vreinterpretq_s32_u32(c), 31);    // b = (c ? 1 : 0)
        int32x4_t r = vshlq_n_s32(b, 4);                            // r = (b << 4)
        r = vnegq_s32(r);
        int32x4_t v = vshlq_s32(value, r);                          // v = (v >> r)

        c = vcgtq_s32(v, g_000000FF);                             // c = (v > 0xFF)
        b = vshrq_n_s32(vreinterpretq_s32_u32(c), 31);              // b = (c ? 1 : 0)
        int32x4_t s = vshlq_n_s32(b, 3);                            // s = (b << 3)
        s = vnegq_s32(s);
        v = vshlq_s32(v, s);                                        // v = (v >> s)
        r = vorrq_s32(r, s);                                        // r = (r | s)

        c = vcgtq_s32(v, g_0000000F);                             // c = (v > 0xF)
        b = vshrq_n_s32(vreinterpretq_s32_u32(c), 31);              // b = (c ? 1 : 0)
        s = vshlq_n_s32(b, 2);                                      // s = (b << 2)
        s = vnegq_s32(s);
        v = vshlq_s32(v, s);                                        // v = (v >> s)
        r = vorrq_s32(r, s);                                        // r = (r | s)

        c = vcgtq_s32(v, g_00000003);                             // c = (v > 0x3)
        b = vshrq_n_s32(vreinterpretq_s32_u32(c), 31);              // b = (c ? 1 : 0)
        s = vshlq_n_s32(b, 1);                                      // s = (b << 1)
        s = vnegq_s32(s);
        v = vshlq_s32(v, s);                                        // v = (v >> s)
        r = vorrq_s32(r, s);                                        // r = (r | s)

        s = vshrq_n_s32(v, 1);
        r = vorrq_s32(r, s);
        return r;
    }

} // namespace MathInternal

#endif

inline Vector MathCallConv VectorLog2(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            log2f(V.vector4_f32[0]),
            log2f(V.vector4_f32[1]),
            log2f(V.vector4_f32[2]),
            log2f(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    int32x4_t rawBiased = vandq_s32(vreinterpretq_s32_f32(V), g_Infinity);
    int32x4_t trailing = vandq_s32(vreinterpretq_s32_f32(V), g_QNaNTest);
    uint32x4_t isExponentZero = vceqq_s32(vreinterpretq_s32_f32(g_Zero), rawBiased);

    // Compute exponent and significand for normals.
    int32x4_t biased = vshrq_n_s32(rawBiased, 23);
    int32x4_t exponentNor = vsubq_s32(biased, g_ExponentBias);
    int32x4_t trailingNor = trailing;

    // Compute exponent and significand for subnormals.
    int32x4_t leading = MathInternal::GetLeadingBit(trailing);
    int32x4_t shift = vsubq_s32(g_NumTrailing, leading);
    int32x4_t exponentSub = vsubq_s32(g_SubnormalExponent, shift);
    int32x4_t trailingSub = vshlq_s32(trailing, shift);
    trailingSub = vandq_s32(trailingSub, g_QNaNTest);
    int32x4_t e = vbslq_s32(isExponentZero, exponentSub, exponentNor);
    int32x4_t t = vbslq_s32(isExponentZero, trailingSub, trailingNor);

    // Compute the approximation.
    int32x4_t tmp = vorrq_s32(vreinterpretq_s32_f32(g_One), t);
    float32x4_t y = vsubq_f32(vreinterpretq_f32_s32(tmp), g_One);

    float32x4_t log2 = vmlaq_f32(g_LogEst6, g_LogEst7, y);
    log2 = vmlaq_f32(g_LogEst5, log2, y);
    log2 = vmlaq_f32(g_LogEst4, log2, y);
    log2 = vmlaq_f32(g_LogEst3, log2, y);
    log2 = vmlaq_f32(g_LogEst2, log2, y);
    log2 = vmlaq_f32(g_LogEst1, log2, y);
    log2 = vmlaq_f32(g_LogEst0, log2, y);
    log2 = vmlaq_f32(vcvtq_f32_s32(e), log2, y);

    //  if(x is NaN) -> QNaN
    //  else if(V is positive)
    //      if(V is infinite) -> +inf
    //      else -> log2(V)
    //  else
    //      if(V is zero) -> -inf
    //      else -> -QNaN

    uint32x4_t isInfinite = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    isInfinite = vceqq_u32(isInfinite, g_Infinity);

    uint32x4_t isGreaterZero = vcgtq_f32(V, g_Zero);
    uint32x4_t isNotFinite = vcgtq_f32(V, g_Infinity);
    uint32x4_t isPositive = vbicq_u32(isGreaterZero, isNotFinite);

    uint32x4_t isZero = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    isZero = vceqq_u32(isZero, g_Zero);

    uint32x4_t t0 = vandq_u32(vreinterpretq_u32_f32(V), g_QNaNTest);
    uint32x4_t t1 = vandq_u32(vreinterpretq_u32_f32(V), g_Infinity);
    t0 = vceqq_u32(t0, g_Zero);
    t1 = vceqq_u32(t1, g_Infinity);
    uint32x4_t isNaN = vbicq_u32(t1, t0);

    float32x4_t result = vbslq_f32(isInfinite, g_Infinity, log2);
    float32x4_t tmp2 = vbslq_f32(isZero, g_NegInfinity, g_NegQNaN);
    result = vbslq_f32(isPositive, result, tmp2);
    result = vbslq_f32(isNaN, g_QNaN, result);
    return result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_log2_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i rawBiased = _mm_and_si128(_mm_castps_si128(V), g_Infinity);
    __m128i trailing = _mm_and_si128(_mm_castps_si128(V), g_QNaNTest);
    __m128i isExponentZero = _mm_cmpeq_epi32(g_Zero, rawBiased);

    // Compute exponent and significand for normals.
    __m128i biased = _mm_srli_epi32(rawBiased, 23);
    __m128i exponentNor = _mm_sub_epi32(biased, g_ExponentBias);
    __m128i trailingNor = trailing;

    // Compute exponent and significand for subnormals.
    __m128i leading = MathInternal::GetLeadingBit(trailing);
    __m128i shift = _mm_sub_epi32(g_NumTrailing, leading);
    __m128i exponentSub = _mm_sub_epi32(g_SubnormalExponent, shift);
    __m128i trailingSub = MathInternal::multi_sll_epi32(trailing, shift);
    trailingSub = _mm_and_si128(trailingSub, g_QNaNTest);

    __m128i select0 = _mm_and_si128(isExponentZero, exponentSub);
    __m128i select1 = _mm_andnot_si128(isExponentZero, exponentNor);
    __m128i e = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isExponentZero, trailingSub);
    select1 = _mm_andnot_si128(isExponentZero, trailingNor);
    __m128i t = _mm_or_si128(select0, select1);

    // Compute the approximation.
    __m128i tmp = _mm_or_si128(g_One, t);
    __m128 y = _mm_sub_ps(_mm_castsi128_ps(tmp), g_One);

    __m128 log2 = MATH_FMADD_PS(g_LogEst7, y, g_LogEst6);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst5);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst4);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst3);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst2);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst1);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst0);
    log2 = MATH_FMADD_PS(log2, y, _mm_cvtepi32_ps(e));

    //  if(x is NaN) -> QNaN
    //  else if(V is positive)
    //      if(V is infinite) -> +inf
    //      else -> log2(V)
    //  else
    //      if(V is zero) -> -inf
    //      else -> -QNaN

    __m128i isInfinite = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    isInfinite = _mm_cmpeq_epi32(isInfinite, g_Infinity);

    __m128i isGreaterZero = _mm_cmpgt_epi32(_mm_castps_si128(V), g_Zero);
    __m128i isNotFinite = _mm_cmpgt_epi32(_mm_castps_si128(V), g_Infinity);
    __m128i isPositive = _mm_andnot_si128(isNotFinite, isGreaterZero);

    __m128i isZero = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    isZero = _mm_cmpeq_epi32(isZero, g_Zero);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(V), g_QNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(V), g_Infinity);
    t0 = _mm_cmpeq_epi32(t0, g_Zero);
    t1 = _mm_cmpeq_epi32(t1, g_Infinity);
    __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isInfinite, g_Infinity);
    select1 = _mm_andnot_si128(isInfinite, _mm_castps_si128(log2));
    __m128i result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isZero, g_NegInfinity);
    select1 = _mm_andnot_si128(isZero, g_NegQNaN);
    tmp = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isPositive, result);
    select1 = _mm_andnot_si128(isPositive, tmp);
    result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isNaN, g_QNaN);
    select1 = _mm_andnot_si128(isNaN, result);
    result = _mm_or_si128(select0, select1);

    return _mm_castsi128_ps(result);
#endif
}

inline Vector MathCallConv VectorLog10(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            log10f(V.vector4_f32[0]),
            log10f(V.vector4_f32[1]),
            log10f(V.vector4_f32[2]),
            log10f(V.vector4_f32[3])
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    int32x4_t rawBiased = vandq_s32(vreinterpretq_s32_f32(V), g_Infinity);
    int32x4_t trailing = vandq_s32(vreinterpretq_s32_f32(V), g_QNaNTest);
    uint32x4_t isExponentZero = vceqq_s32(g_Zero, rawBiased);

    // Compute exponent and significand for normals.
    int32x4_t biased = vshrq_n_s32(rawBiased, 23);
    int32x4_t exponentNor = vsubq_s32(biased, g_ExponentBias);
    int32x4_t trailingNor = trailing;

    // Compute exponent and significand for subnormals.
    int32x4_t leading = MathInternal::GetLeadingBit(trailing);
    int32x4_t shift = vsubq_s32(g_NumTrailing, leading);
    int32x4_t exponentSub = vsubq_s32(g_SubnormalExponent, shift);
    int32x4_t trailingSub = vshlq_s32(trailing, shift);
    trailingSub = vandq_s32(trailingSub, g_QNaNTest);
    int32x4_t e = vbslq_s32(isExponentZero, exponentSub, exponentNor);
    int32x4_t t = vbslq_s32(isExponentZero, trailingSub, trailingNor);

    // Compute the approximation.
    int32x4_t tmp = vorrq_s32(g_One, t);
    float32x4_t y = vsubq_f32(vreinterpretq_f32_s32(tmp), g_One);

    float32x4_t log2 = vmlaq_f32(g_LogEst6, g_LogEst7, y);
    log2 = vmlaq_f32(g_LogEst5, log2, y);
    log2 = vmlaq_f32(g_LogEst4, log2, y);
    log2 = vmlaq_f32(g_LogEst3, log2, y);
    log2 = vmlaq_f32(g_LogEst2, log2, y);
    log2 = vmlaq_f32(g_LogEst1, log2, y);
    log2 = vmlaq_f32(g_LogEst0, log2, y);
    log2 = vmlaq_f32(vcvtq_f32_s32(e), log2, y);

    log2 = vmulq_f32(g_InvLg10, log2);

    //  if(x is NaN) -> QNaN
    //  else if(V is positive)
    //      if(V is infinite) -> +inf
    //      else -> log2(V)
    //  else
    //      if(V is zero) -> -inf
    //      else -> -QNaN

    uint32x4_t isInfinite = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    isInfinite = vceqq_u32(isInfinite, g_Infinity);

    uint32x4_t isGreaterZero = vcgtq_s32(vreinterpretq_s32_f32(V), g_Zero);
    uint32x4_t isNotFinite = vcgtq_s32(vreinterpretq_s32_f32(V), g_Infinity);
    uint32x4_t isPositive = vbicq_u32(isGreaterZero, isNotFinite);

    uint32x4_t isZero = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    isZero = vceqq_u32(isZero, g_Zero);

    uint32x4_t t0 = vandq_u32(vreinterpretq_u32_f32(V), g_QNaNTest);
    uint32x4_t t1 = vandq_u32(vreinterpretq_u32_f32(V), g_Infinity);
    t0 = vceqq_u32(t0, g_Zero);
    t1 = vceqq_u32(t1, g_Infinity);
    uint32x4_t isNaN = vbicq_u32(t1, t0);

    float32x4_t result = vbslq_f32(isInfinite, g_Infinity, log2);
    float32x4_t tmp2 = vbslq_f32(isZero, g_NegInfinity, g_NegQNaN);
    result = vbslq_f32(isPositive, result, tmp2);
    result = vbslq_f32(isNaN, g_QNaN, result);
    return result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_log10_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i rawBiased = _mm_and_si128(_mm_castps_si128(V), g_Infinity);
    __m128i trailing = _mm_and_si128(_mm_castps_si128(V), g_QNaNTest);
    __m128i isExponentZero = _mm_cmpeq_epi32(g_Zero, rawBiased);

    // Compute exponent and significand for normals.
    __m128i biased = _mm_srli_epi32(rawBiased, 23);
    __m128i exponentNor = _mm_sub_epi32(biased, g_ExponentBias);
    __m128i trailingNor = trailing;

    // Compute exponent and significand for subnormals.
    __m128i leading = MathInternal::GetLeadingBit(trailing);
    __m128i shift = _mm_sub_epi32(g_NumTrailing, leading);
    __m128i exponentSub = _mm_sub_epi32(g_SubnormalExponent, shift);
    __m128i trailingSub = MathInternal::multi_sll_epi32(trailing, shift);
    trailingSub = _mm_and_si128(trailingSub, g_QNaNTest);

    __m128i select0 = _mm_and_si128(isExponentZero, exponentSub);
    __m128i select1 = _mm_andnot_si128(isExponentZero, exponentNor);
    __m128i e = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isExponentZero, trailingSub);
    select1 = _mm_andnot_si128(isExponentZero, trailingNor);
    __m128i t = _mm_or_si128(select0, select1);

    // Compute the approximation.
    __m128i tmp = _mm_or_si128(g_One, t);
    __m128 y = _mm_sub_ps(_mm_castsi128_ps(tmp), g_One);

    __m128 log2 = MATH_FMADD_PS(g_LogEst7, y, g_LogEst6);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst5);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst4);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst3);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst2);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst1);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst0);
    log2 = MATH_FMADD_PS(log2, y, _mm_cvtepi32_ps(e));

    log2 = _mm_mul_ps(g_InvLg10, log2);

    //  if(x is NaN) -> QNaN
    //  else if(V is positive)
    //      if(V is infinite) -> +inf
    //      else -> log2(V)
    //  else
    //      if(V is zero) -> -inf
    //      else -> -QNaN

    __m128i isInfinite = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    isInfinite = _mm_cmpeq_epi32(isInfinite, g_Infinity);

    __m128i isGreaterZero = _mm_cmpgt_epi32(_mm_castps_si128(V), g_Zero);
    __m128i isNotFinite = _mm_cmpgt_epi32(_mm_castps_si128(V), g_Infinity);
    __m128i isPositive = _mm_andnot_si128(isNotFinite, isGreaterZero);

    __m128i isZero = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    isZero = _mm_cmpeq_epi32(isZero, g_Zero);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(V), g_QNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(V), g_Infinity);
    t0 = _mm_cmpeq_epi32(t0, g_Zero);
    t1 = _mm_cmpeq_epi32(t1, g_Infinity);
    __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isInfinite, g_Infinity);
    select1 = _mm_andnot_si128(isInfinite, _mm_castps_si128(log2));
    __m128i result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isZero, g_NegInfinity);
    select1 = _mm_andnot_si128(isZero, g_NegQNaN);
    tmp = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isPositive, result);
    select1 = _mm_andnot_si128(isPositive, tmp);
    result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isNaN, g_QNaN);
    select1 = _mm_andnot_si128(isNaN, result);
    result = _mm_or_si128(select0, select1);

    return _mm_castsi128_ps(result);
#endif
}

inline Vector MathCallConv VectorLogE(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            logf(V.vector4_f32[0]),
            logf(V.vector4_f32[1]),
            logf(V.vector4_f32[2]),
            logf(V.vector4_f32[3])
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    int32x4_t rawBiased = vandq_s32(vreinterpretq_s32_f32(V), g_Infinity);
    int32x4_t trailing = vandq_s32(vreinterpretq_s32_f32(V), g_QNaNTest);
    uint32x4_t isExponentZero = vceqq_s32(g_Zero, rawBiased);

    // Compute exponent and significand for normals.
    int32x4_t biased = vshrq_n_s32(rawBiased, 23);
    int32x4_t exponentNor = vsubq_s32(biased, g_ExponentBias);
    int32x4_t trailingNor = trailing;

    // Compute exponent and significand for subnormals.
    int32x4_t leading = MathInternal::GetLeadingBit(trailing);
    int32x4_t shift = vsubq_s32(g_NumTrailing, leading);
    int32x4_t exponentSub = vsubq_s32(g_SubnormalExponent, shift);
    int32x4_t trailingSub = vshlq_s32(trailing, shift);
    trailingSub = vandq_s32(trailingSub, g_QNaNTest);
    int32x4_t e = vbslq_s32(isExponentZero, exponentSub, exponentNor);
    int32x4_t t = vbslq_s32(isExponentZero, trailingSub, trailingNor);

    // Compute the approximation.
    int32x4_t tmp = vorrq_s32(g_One, t);
    float32x4_t y = vsubq_f32(vreinterpretq_f32_s32(tmp), g_One);

    float32x4_t log2 = vmlaq_f32(g_LogEst6, g_LogEst7, y);
    log2 = vmlaq_f32(g_LogEst5, log2, y);
    log2 = vmlaq_f32(g_LogEst4, log2, y);
    log2 = vmlaq_f32(g_LogEst3, log2, y);
    log2 = vmlaq_f32(g_LogEst2, log2, y);
    log2 = vmlaq_f32(g_LogEst1, log2, y);
    log2 = vmlaq_f32(g_LogEst0, log2, y);
    log2 = vmlaq_f32(vcvtq_f32_s32(e), log2, y);

    log2 = vmulq_f32(g_InvLgE, log2);

    //  if(x is NaN) -> QNaN
    //  else if(V is positive)
    //      if(V is infinite) -> +inf
    //      else -> log2(V)
    //  else
    //      if(V is zero) -> -inf
    //      else -> -QNaN

    uint32x4_t isInfinite = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    isInfinite = vceqq_u32(isInfinite, g_Infinity);

    uint32x4_t isGreaterZero = vcgtq_s32(vreinterpretq_s32_f32(V), g_Zero);
    uint32x4_t isNotFinite = vcgtq_s32(vreinterpretq_s32_f32(V), g_Infinity);
    uint32x4_t isPositive = vbicq_u32(isGreaterZero, isNotFinite);

    uint32x4_t isZero = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    isZero = vceqq_u32(isZero, g_Zero);

    uint32x4_t t0 = vandq_u32(vreinterpretq_u32_f32(V), g_QNaNTest);
    uint32x4_t t1 = vandq_u32(vreinterpretq_u32_f32(V), g_Infinity);
    t0 = vceqq_u32(t0, g_Zero);
    t1 = vceqq_u32(t1, g_Infinity);
    uint32x4_t isNaN = vbicq_u32(t1, t0);

    float32x4_t result = vbslq_f32(isInfinite, g_Infinity, log2);
    float32x4_t tmp2 = vbslq_f32(isZero, g_NegInfinity, g_NegQNaN);
    result = vbslq_f32(isPositive, result, tmp2);
    result = vbslq_f32(isNaN, g_QNaN, result);
    return result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_log_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i rawBiased = _mm_and_si128(_mm_castps_si128(V), g_Infinity);
    __m128i trailing = _mm_and_si128(_mm_castps_si128(V), g_QNaNTest);
    __m128i isExponentZero = _mm_cmpeq_epi32(g_Zero, rawBiased);

    // Compute exponent and significand for normals.
    __m128i biased = _mm_srli_epi32(rawBiased, 23);
    __m128i exponentNor = _mm_sub_epi32(biased, g_ExponentBias);
    __m128i trailingNor = trailing;

    // Compute exponent and significand for subnormals.
    __m128i leading = MathInternal::GetLeadingBit(trailing);
    __m128i shift = _mm_sub_epi32(g_NumTrailing, leading);
    __m128i exponentSub = _mm_sub_epi32(g_SubnormalExponent, shift);
    __m128i trailingSub = MathInternal::multi_sll_epi32(trailing, shift);
    trailingSub = _mm_and_si128(trailingSub, g_QNaNTest);

    __m128i select0 = _mm_and_si128(isExponentZero, exponentSub);
    __m128i select1 = _mm_andnot_si128(isExponentZero, exponentNor);
    __m128i e = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isExponentZero, trailingSub);
    select1 = _mm_andnot_si128(isExponentZero, trailingNor);
    __m128i t = _mm_or_si128(select0, select1);

    // Compute the approximation.
    __m128i tmp = _mm_or_si128(g_One, t);
    __m128 y = _mm_sub_ps(_mm_castsi128_ps(tmp), g_One);

    __m128 log2 = MATH_FMADD_PS(g_LogEst7, y, g_LogEst6);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst5);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst4);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst3);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst2);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst1);
    log2 = MATH_FMADD_PS(log2, y, g_LogEst0);
    log2 = MATH_FMADD_PS(log2, y, _mm_cvtepi32_ps(e));

    log2 = _mm_mul_ps(g_InvLgE, log2);

    //  if(x is NaN) -> QNaN
    //  else if(V is positive)
    //      if(V is infinite) -> +inf
    //      else -> log2(V)
    //  else
    //      if(V is zero) -> -inf
    //      else -> -QNaN

    __m128i isInfinite = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    isInfinite = _mm_cmpeq_epi32(isInfinite, g_Infinity);

    __m128i isGreaterZero = _mm_cmpgt_epi32(_mm_castps_si128(V), g_Zero);
    __m128i isNotFinite = _mm_cmpgt_epi32(_mm_castps_si128(V), g_Infinity);
    __m128i isPositive = _mm_andnot_si128(isNotFinite, isGreaterZero);

    __m128i isZero = _mm_and_si128(_mm_castps_si128(V), g_AbsMask);
    isZero = _mm_cmpeq_epi32(isZero, g_Zero);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(V), g_QNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(V), g_Infinity);
    t0 = _mm_cmpeq_epi32(t0, g_Zero);
    t1 = _mm_cmpeq_epi32(t1, g_Infinity);
    __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isInfinite, g_Infinity);
    select1 = _mm_andnot_si128(isInfinite, _mm_castps_si128(log2));
    __m128i result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isZero, g_NegInfinity);
    select1 = _mm_andnot_si128(isZero, g_NegQNaN);
    tmp = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isPositive, result);
    select1 = _mm_andnot_si128(isPositive, tmp);
    result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isNaN, g_QNaN);
    select1 = _mm_andnot_si128(isNaN, result);
    result = _mm_or_si128(select0, select1);

    return _mm_castsi128_ps(result);
#endif
}

inline Vector MathCallConv VectorLog(VectorArg V)noexcept{
    return VectorLog2(V);
}

inline Vector MathCallConv VectorPow
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            powf(V1.vector4_f32[0], V2.vector4_f32[0]),
            powf(V1.vector4_f32[1], V2.vector4_f32[1]),
            powf(V1.vector4_f32[2], V2.vector4_f32[2]),
            powf(V1.vector4_f32[3], V2.vector4_f32[3])
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    VectorF32 vResult = { { {
            powf(vgetq_lane_f32(V1, 0), vgetq_lane_f32(V2, 0)),
            powf(vgetq_lane_f32(V1, 1), vgetq_lane_f32(V2, 1)),
            powf(vgetq_lane_f32(V1, 2), vgetq_lane_f32(V2, 2)),
            powf(vgetq_lane_f32(V1, 3), vgetq_lane_f32(V2, 3))
        } } };
    return vResult.v;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_pow_ps(V1, V2);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    MATH_ALIGNED_DATA(16) float a[4];
    MATH_ALIGNED_DATA(16) float b[4];
    _mm_store_ps(a, V1);
    _mm_store_ps(b, V2);
    Vector vResult = _mm_setr_ps(
        powf(a[0], b[0]),
        powf(a[1], b[1]),
        powf(a[2], b[2]),
        powf(a[3], b[3]));
    return vResult;
#endif
}

inline Vector MathCallConv VectorAbs(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult = { { {
            fabsf(V.vector4_f32[0]),
            fabsf(V.vector4_f32[1]),
            fabsf(V.vector4_f32[2]),
            fabsf(V.vector4_f32[3])
        } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return vabsq_f32(V);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = _mm_setzero_ps();
    vResult = _mm_sub_ps(vResult, V);
    vResult = _mm_max_ps(vResult, V);
    return vResult;
#endif
}

inline Vector MathCallConv VectorMod
(
    VectorArg V1,
    VectorArg V2
)noexcept{
    // V1 % V2 = V1 - V2 * truncate(V1 / V2)

#if defined(_MATH_NO_INTRINSICS_)

    Vector Quotient = VectorDivide(V1, V2);
    Quotient = VectorTruncate(Quotient);
    Vector Result = VectorNegativeMultiplySubtract(V2, Quotient, V1);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector vResult = VectorDivide(V1, V2);
    vResult = VectorTruncate(vResult);
    return vmlsq_f32(V1, vResult, V2);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = _mm_div_ps(V1, V2);
    vResult = VectorTruncate(vResult);
    return MATH_FNMADD_PS(vResult, V2, V1);
#endif
}

inline Vector MathCallConv VectorModAngles(VectorArg Angles)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector V;
    Vector Result;

    // Modulo the range of the given angles such that -MATH_PI <= Angles < MATH_PI
    V = VectorMultiply(Angles, g_ReciprocalTwoPi.v);
    V = VectorRound(V);
    Result = VectorNegativeMultiplySubtract(g_TwoPi.v, V, Angles);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Modulo the range of the given angles such that -MATH_PI <= Angles < MATH_PI
    Vector vResult = vmulq_f32(Angles, g_ReciprocalTwoPi);
    // Use the inline function due to complexity for rounding
    vResult = VectorRound(vResult);
    return vmlsq_f32(Angles, vResult, g_TwoPi);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Modulo the range of the given angles such that -MATH_PI <= Angles < MATH_PI
    Vector vResult = _mm_mul_ps(Angles, g_ReciprocalTwoPi);
    // Use the inline function due to complexity for rounding
    vResult = VectorRound(vResult);
    return MATH_FNMADD_PS(vResult, g_TwoPi, Angles);
#endif
}

inline Vector MathCallConv VectorSin(VectorArg V)noexcept{
    // 11-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            sinf(V.vector4_f32[0]),
            sinf(V.vector4_f32[1]),
            sinf(V.vector4_f32[2]),
            sinf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with sin(y) = sin(x).
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_NegativeZero);
    uint32x4_t c = vorrq_u32(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_HalfPi);
    x = vbslq_f32(comp, x, rflx);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation
    const Vector SC1 = g_SinCoefficients1;
    const Vector SC0 = g_SinCoefficients0;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(SC0), 1);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_low_f32(SC1), 0);

    vConstants = vdupq_lane_f32(vget_high_f32(SC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(SC0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(SC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    Result = vmulq_f32(Result, x);
    return Result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_sin_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with sin(y) = sin(x).
    __m128 sign = _mm_and_ps(x, g_NegativeZero);
    __m128 c = _mm_or_ps(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    __m128 absx = _mm_andnot_ps(sign, x);  // |x|
    __m128 rflx = _mm_sub_ps(c, x);
    __m128 comp = _mm_cmple_ps(absx, g_HalfPi);
    __m128 select0 = _mm_and_ps(comp, x);
    __m128 select1 = _mm_andnot_ps(comp, rflx);
    x = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    const Vector SC1 = g_SinCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(SC1, _MM_SHUFFLE(0, 0, 0, 0));
    const Vector SC0 = g_SinCoefficients0;
    __m128 vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(2, 2, 2, 2));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(0, 0, 0, 0));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, x);
    return Result;
#endif
}

inline Vector MathCallConv VectorCos(VectorArg V)noexcept{
    // 10-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            cosf(V.vector4_f32[0]),
            cosf(V.vector4_f32[1]),
            cosf(V.vector4_f32[2]),
            cosf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Map V to x in [-pi,pi].
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_NegativeZero);
    uint32x4_t c = vorrq_u32(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_HalfPi);
    x = vbslq_f32(comp, x, rflx);
    float32x4_t fsign = vbslq_f32(comp, g_One, g_NegativeOne);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation
    const Vector CC1 = g_CosCoefficients1;
    const Vector CC0 = g_CosCoefficients0;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(CC0), 1);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_low_f32(CC1), 0);

    vConstants = vdupq_lane_f32(vget_high_f32(CC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(CC0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(CC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    Result = vmulq_f32(Result, fsign);
    return Result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_cos_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Map V to x in [-pi,pi].
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    Vector sign = _mm_and_ps(x, g_NegativeZero);
    __m128 c = _mm_or_ps(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    __m128 absx = _mm_andnot_ps(sign, x);  // |x|
    __m128 rflx = _mm_sub_ps(c, x);
    __m128 comp = _mm_cmple_ps(absx, g_HalfPi);
    __m128 select0 = _mm_and_ps(comp, x);
    __m128 select1 = _mm_andnot_ps(comp, rflx);
    x = _mm_or_ps(select0, select1);
    select0 = _mm_and_ps(comp, g_One);
    select1 = _mm_andnot_ps(comp, g_NegativeOne);
    sign = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    const Vector CC1 = g_CosCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(CC1, _MM_SHUFFLE(0, 0, 0, 0));
    const Vector CC0 = g_CosCoefficients0;
    __m128 vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(2, 2, 2, 2));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(0, 0, 0, 0));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, sign);
    return Result;
#endif
}

_Use_decl_annotations_
inline void MathCallConv VectorSinCos
(
    Vector* pSin,
    Vector* pCos,
    VectorArg V
)noexcept{
    assert(pSin != nullptr);
    assert(pCos != nullptr);

    // 11/10-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Sin = { { {
            sinf(V.vector4_f32[0]),
            sinf(V.vector4_f32[1]),
            sinf(V.vector4_f32[2]),
            sinf(V.vector4_f32[3])
        } } };

    VectorF32 Cos = { { {
            cosf(V.vector4_f32[0]),
            cosf(V.vector4_f32[1]),
            cosf(V.vector4_f32[2]),
            cosf(V.vector4_f32[3])
        } } };

    *pSin = Sin.v;
    *pCos = Cos.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_NegativeZero);
    uint32x4_t c = vorrq_u32(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t  rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_HalfPi);
    x = vbslq_f32(comp, x, rflx);
    float32x4_t fsign = vbslq_f32(comp, g_One, g_NegativeOne);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation for sine
    const Vector SC1 = g_SinCoefficients1;
    const Vector SC0 = g_SinCoefficients0;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(SC0), 1);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_low_f32(SC1), 0);

    vConstants = vdupq_lane_f32(vget_high_f32(SC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(SC0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(SC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    *pSin = vmulq_f32(Result, x);

    // Compute polynomial approximation for cosine
    const Vector CC1 = g_CosCoefficients1;
    const Vector CC0 = g_CosCoefficients0;
    vConstants = vdupq_lane_f32(vget_high_f32(CC0), 1);
    Result = vmlaq_lane_f32(vConstants, x2, vget_low_f32(CC1), 0);

    vConstants = vdupq_lane_f32(vget_high_f32(CC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(CC0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(CC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    *pCos = vmulq_f32(Result, fsign);
#elif defined(_MATH_SVML_INTRINSICS_)
    *pSin = _mm_sincos_ps(pCos, V);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with sin(y) = sin(x), cos(y) = sign*cos(x).
    Vector sign = _mm_and_ps(x, g_NegativeZero);
    __m128 c = _mm_or_ps(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    __m128 absx = _mm_andnot_ps(sign, x);  // |x|
    __m128 rflx = _mm_sub_ps(c, x);
    __m128 comp = _mm_cmple_ps(absx, g_HalfPi);
    __m128 select0 = _mm_and_ps(comp, x);
    __m128 select1 = _mm_andnot_ps(comp, rflx);
    x = _mm_or_ps(select0, select1);
    select0 = _mm_and_ps(comp, g_One);
    select1 = _mm_andnot_ps(comp, g_NegativeOne);
    sign = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation of sine
    const Vector SC1 = g_SinCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(SC1, _MM_SHUFFLE(0, 0, 0, 0));
    const Vector SC0 = g_SinCoefficients0;
    __m128 vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(2, 2, 2, 2));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SC0, _MM_SHUFFLE(0, 0, 0, 0));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, x);
    *pSin = Result;

    // Compute polynomial approximation of cosine
    const Vector CC1 = g_CosCoefficients1;
    vConstantsB = MATH_PERMUTE_PS(CC1, _MM_SHUFFLE(0, 0, 0, 0));
    const Vector CC0 = g_CosCoefficients0;
    vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(3, 3, 3, 3));
    Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(2, 2, 2, 2));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CC0, _MM_SHUFFLE(0, 0, 0, 0));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, sign);
    *pCos = Result;
#endif
}

inline Vector MathCallConv VectorTan(VectorArg V)noexcept{
    // Cody and Waite algorithm to compute tangent.

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            tanf(V.vector4_f32[0]),
            tanf(V.vector4_f32[1]),
            tanf(V.vector4_f32[2]),
            tanf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_tan_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    static const VectorF32 TanCoefficients0 = { { { 1.0f, -4.667168334e-1f, 2.566383229e-2f, -3.118153191e-4f } } };
    static const VectorF32 TanCoefficients1 = { { { 4.981943399e-7f, -1.333835001e-1f, 3.424887824e-3f, -1.786170734e-5f } } };
    static const VectorF32 TanConstants = { { { 1.570796371f, 6.077100628e-11f, 0.000244140625f, 0.63661977228f /*2 / Pi*/ } } };
    static const VectorU32 Mask = { { { 0x1, 0x1, 0x1, 0x1 } } };

    Vector TwoDivPi = VectorSplatW(TanConstants.v);

    Vector Zero = VectorZero();

    Vector C0 = VectorSplatX(TanConstants.v);
    Vector C1 = VectorSplatY(TanConstants.v);
    Vector Epsilon = VectorSplatZ(TanConstants.v);

    Vector VA = VectorMultiply(V, TwoDivPi);

    VA = VectorRound(VA);

    Vector VC = VectorNegativeMultiplySubtract(VA, C0, V);

    Vector VB = VectorAbs(VA);

    VC = VectorNegativeMultiplySubtract(VA, C1, VC);

#if defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    VB = vreinterpretq_f32_u32(vcvtq_u32_f32(VB));
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    reinterpret_cast<__m128i*>(&VB)[0] = _mm_cvttps_epi32(VB);
#else
    for(size_t i = 0; i < 4; ++i){
        VB.vector4_u32[i] = static_cast<uint32_t>(VB.vector4_f32[i]);
    }
#endif

    Vector VC2 = VectorMultiply(VC, VC);

    Vector T7 = VectorSplatW(TanCoefficients1.v);
    Vector T6 = VectorSplatZ(TanCoefficients1.v);
    Vector T4 = VectorSplatX(TanCoefficients1.v);
    Vector T3 = VectorSplatW(TanCoefficients0.v);
    Vector T5 = VectorSplatY(TanCoefficients1.v);
    Vector T2 = VectorSplatZ(TanCoefficients0.v);
    Vector T1 = VectorSplatY(TanCoefficients0.v);
    Vector T0 = VectorSplatX(TanCoefficients0.v);

    Vector VBIsEven = VectorAndInt(VB, Mask.v);
    VBIsEven = VectorEqualInt(VBIsEven, Zero);

    Vector N = VectorMultiplyAdd(VC2, T7, T6);
    Vector D = VectorMultiplyAdd(VC2, T4, T3);
    N = VectorMultiplyAdd(VC2, N, T5);
    D = VectorMultiplyAdd(VC2, D, T2);
    N = VectorMultiply(VC2, N);
    D = VectorMultiplyAdd(VC2, D, T1);
    N = VectorMultiplyAdd(VC, N, VC);
    Vector VCNearZero = VectorInBounds(VC, Epsilon);
    D = VectorMultiplyAdd(VC2, D, T0);

    N = VectorSelect(N, VC, VCNearZero);
    D = VectorSelect(D, g_One.v, VCNearZero);

    Vector R0 = VectorNegate(N);
    Vector R1 = VectorDivide(N, D);
    R0 = VectorDivide(D, R0);

    Vector VIsZero = VectorEqual(V, Zero);

    Vector Result = VectorSelect(R0, R1, VBIsEven);

    Result = VectorSelect(Result, Zero, VIsZero);

    return Result;

#endif
}

inline Vector MathCallConv VectorSinH(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            sinhf(V.vector4_f32[0]),
            sinhf(V.vector4_f32[1]),
            sinhf(V.vector4_f32[2]),
            sinhf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Scale = { { { 1.442695040888963f, 1.442695040888963f, 1.442695040888963f, 1.442695040888963f } } }; // 1.0f / ln(2.0f)

    Vector V1 = vmlaq_f32(g_NegativeOne.v, V, Scale.v);
    Vector V2 = vmlsq_f32(g_NegativeOne.v, V, Scale.v);
    Vector E1 = VectorExp(V1);
    Vector E2 = VectorExp(V2);

    return vsubq_f32(E1, E2);
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_sinh_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 Scale = { { { 1.442695040888963f, 1.442695040888963f, 1.442695040888963f, 1.442695040888963f } } }; // 1.0f / ln(2.0f)

    Vector V1 = MATH_FMADD_PS(V, Scale, g_NegativeOne);
    Vector V2 = MATH_FNMADD_PS(V, Scale, g_NegativeOne);
    Vector E1 = VectorExp(V1);
    Vector E2 = VectorExp(V2);

    return _mm_sub_ps(E1, E2);
#endif
}

inline Vector MathCallConv VectorCosH(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            coshf(V.vector4_f32[0]),
            coshf(V.vector4_f32[1]),
            coshf(V.vector4_f32[2]),
            coshf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Scale = { { { 1.442695040888963f, 1.442695040888963f, 1.442695040888963f, 1.442695040888963f } } }; // 1.0f / ln(2.0f)

    Vector V1 = vmlaq_f32(g_NegativeOne.v, V, Scale.v);
    Vector V2 = vmlsq_f32(g_NegativeOne.v, V, Scale.v);
    Vector E1 = VectorExp(V1);
    Vector E2 = VectorExp(V2);
    return vaddq_f32(E1, E2);
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_cosh_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 Scale = { { { 1.442695040888963f, 1.442695040888963f, 1.442695040888963f, 1.442695040888963f } } }; // 1.0f / ln(2.0f)

    Vector V1 = MATH_FMADD_PS(V, Scale.v, g_NegativeOne.v);
    Vector V2 = MATH_FNMADD_PS(V, Scale.v, g_NegativeOne.v);
    Vector E1 = VectorExp(V1);
    Vector E2 = VectorExp(V2);
    return _mm_add_ps(E1, E2);
#endif
}

inline Vector MathCallConv VectorTanH(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            tanhf(V.vector4_f32[0]),
            tanhf(V.vector4_f32[1]),
            tanhf(V.vector4_f32[2]),
            tanhf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Scale = { { { 2.8853900817779268f, 2.8853900817779268f, 2.8853900817779268f, 2.8853900817779268f } } }; // 2.0f / ln(2.0f)

    Vector E = vmulq_f32(V, Scale.v);
    E = VectorExp(E);
    E = vmlaq_f32(g_OneHalf.v, E, g_OneHalf.v);
    E = VectorReciprocal(E);
    return vsubq_f32(g_One.v, E);
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_tanh_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 Scale = { { { 2.8853900817779268f, 2.8853900817779268f, 2.8853900817779268f, 2.8853900817779268f } } }; // 2.0f / ln(2.0f)

    Vector E = _mm_mul_ps(V, Scale.v);
    E = VectorExp(E);
    E = MATH_FMADD_PS(E, g_OneHalf.v, g_OneHalf.v);
    E = _mm_div_ps(g_One.v, E);
    return _mm_sub_ps(g_One.v, E);
#endif
}

inline Vector MathCallConv VectorASin(VectorArg V)noexcept{
    // 7-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            asinf(V.vector4_f32[0]),
            asinf(V.vector4_f32[1]),
            asinf(V.vector4_f32[2]),
            asinf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t nonnegative = vcgeq_f32(V, g_Zero);
    float32x4_t x = vabsq_f32(V);

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    float32x4_t oneMValue = vsubq_f32(g_One, x);
    float32x4_t clampOneMValue = vmaxq_f32(g_Zero, oneMValue);
    float32x4_t root = VectorSqrt(clampOneMValue);

    // Compute polynomial approximation
    const Vector AC1 = g_ArcCoefficients1;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(AC1), 0);
    Vector t0 = vmlaq_lane_f32(vConstants, x, vget_high_f32(AC1), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(AC1), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AC1), 0);
    t0 = vmlaq_f32(vConstants, t0, x);

    const Vector AC0 = g_ArcCoefficients0;
    vConstants = vdupq_lane_f32(vget_high_f32(AC0), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_high_f32(AC0), 0);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AC0), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AC0), 0);
    t0 = vmlaq_f32(vConstants, t0, x);
    t0 = vmulq_f32(t0, root);

    float32x4_t t1 = vsubq_f32(g_Pi, t0);
    t0 = vbslq_f32(nonnegative, t0, t1);
    t0 = vsubq_f32(g_HalfPi, t0);
    return t0;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_asin_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 nonnegative = _mm_cmpge_ps(V, g_Zero);
    __m128 mvalue = _mm_sub_ps(g_Zero, V);
    __m128 x = _mm_max_ps(V, mvalue);  // |V|

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    __m128 oneMValue = _mm_sub_ps(g_One, x);
    __m128 clampOneMValue = _mm_max_ps(g_Zero, oneMValue);
    __m128 root = _mm_sqrt_ps(clampOneMValue);  // sqrt(1-|V|)

    // Compute polynomial approximation
    const Vector AC1 = g_ArcCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 t0 = MATH_FMADD_PS(vConstantsB, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(1, 1, 1, 1));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(0, 0, 0, 0));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    const Vector AC0 = g_ArcCoefficients0;
    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(3, 3, 3, 3));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(2, 2, 2, 2));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(1, 1, 1, 1));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(0, 0, 0, 0));
    t0 = MATH_FMADD_PS(t0, x, vConstants);
    t0 = _mm_mul_ps(t0, root);

    __m128 t1 = _mm_sub_ps(g_Pi, t0);
    t0 = _mm_and_ps(nonnegative, t0);
    t1 = _mm_andnot_ps(nonnegative, t1);
    t0 = _mm_or_ps(t0, t1);
    t0 = _mm_sub_ps(g_HalfPi, t0);
    return t0;
#endif
}

inline Vector MathCallConv VectorACos(VectorArg V)noexcept{
    // 7-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            acosf(V.vector4_f32[0]),
            acosf(V.vector4_f32[1]),
            acosf(V.vector4_f32[2]),
            acosf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t nonnegative = vcgeq_f32(V, g_Zero);
    float32x4_t x = vabsq_f32(V);

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    float32x4_t oneMValue = vsubq_f32(g_One, x);
    float32x4_t clampOneMValue = vmaxq_f32(g_Zero, oneMValue);
    float32x4_t root = VectorSqrt(clampOneMValue);

    // Compute polynomial approximation
    const Vector AC1 = g_ArcCoefficients1;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(AC1), 0);
    Vector t0 = vmlaq_lane_f32(vConstants, x, vget_high_f32(AC1), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(AC1), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AC1), 0);
    t0 = vmlaq_f32(vConstants, t0, x);

    const Vector AC0 = g_ArcCoefficients0;
    vConstants = vdupq_lane_f32(vget_high_f32(AC0), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_high_f32(AC0), 0);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AC0), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AC0), 0);
    t0 = vmlaq_f32(vConstants, t0, x);
    t0 = vmulq_f32(t0, root);

    float32x4_t t1 = vsubq_f32(g_Pi, t0);
    t0 = vbslq_f32(nonnegative, t0, t1);
    return t0;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_acos_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 nonnegative = _mm_cmpge_ps(V, g_Zero);
    __m128 mvalue = _mm_sub_ps(g_Zero, V);
    __m128 x = _mm_max_ps(V, mvalue);  // |V|

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    __m128 oneMValue = _mm_sub_ps(g_One, x);
    __m128 clampOneMValue = _mm_max_ps(g_Zero, oneMValue);
    __m128 root = _mm_sqrt_ps(clampOneMValue);  // sqrt(1-|V|)

    // Compute polynomial approximation
    const Vector AC1 = g_ArcCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 t0 = MATH_FMADD_PS(vConstantsB, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(1, 1, 1, 1));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC1, _MM_SHUFFLE(0, 0, 0, 0));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    const Vector AC0 = g_ArcCoefficients0;
    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(3, 3, 3, 3));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(2, 2, 2, 2));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(1, 1, 1, 1));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AC0, _MM_SHUFFLE(0, 0, 0, 0));
    t0 = MATH_FMADD_PS(t0, x, vConstants);
    t0 = _mm_mul_ps(t0, root);

    __m128 t1 = _mm_sub_ps(g_Pi, t0);
    t0 = _mm_and_ps(nonnegative, t0);
    t1 = _mm_andnot_ps(nonnegative, t1);
    t0 = _mm_or_ps(t0, t1);
    return t0;
#endif
}

inline Vector MathCallConv VectorATan(VectorArg V)noexcept{
    // 17-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            atanf(V.vector4_f32[0]),
            atanf(V.vector4_f32[1]),
            atanf(V.vector4_f32[2]),
            atanf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t absV = vabsq_f32(V);
    float32x4_t invV = VectorReciprocal(V);
    uint32x4_t comp = vcgtq_f32(V, g_One);
    float32x4_t sign = vbslq_f32(comp, g_One, g_NegativeOne);
    comp = vcleq_f32(absV, g_One);
    sign = vbslq_f32(comp, g_Zero, sign);
    float32x4_t x = vbslq_f32(comp, V, invV);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation
    const Vector TC1 = g_ATanCoefficients1;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(TC1), 0);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_high_f32(TC1), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(TC1), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(TC1), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    const Vector TC0 = g_ATanCoefficients0;
    vConstants = vdupq_lane_f32(vget_high_f32(TC0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_high_f32(TC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(TC0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(TC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    Result = vmulq_f32(Result, x);

    float32x4_t result1 = vmulq_f32(sign, g_HalfPi);
    result1 = vsubq_f32(result1, Result);

    comp = vceqq_f32(sign, g_Zero);
    Result = vbslq_f32(comp, Result, result1);
    return Result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_atan_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 absV = VectorAbs(V);
    __m128 invV = _mm_div_ps(g_One, V);
    __m128 comp = _mm_cmpgt_ps(V, g_One);
    __m128 select0 = _mm_and_ps(comp, g_One);
    __m128 select1 = _mm_andnot_ps(comp, g_NegativeOne);
    __m128 sign = _mm_or_ps(select0, select1);
    comp = _mm_cmple_ps(absV, g_One);
    select0 = _mm_and_ps(comp, g_Zero);
    select1 = _mm_andnot_ps(comp, sign);
    sign = _mm_or_ps(select0, select1);
    select0 = _mm_and_ps(comp, V);
    select1 = _mm_andnot_ps(comp, invV);
    __m128 x = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    const Vector TC1 = g_ATanCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(TC1, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(TC1, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(TC1, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(TC1, _MM_SHUFFLE(0, 0, 0, 0));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    const Vector TC0 = g_ATanCoefficients0;
    vConstants = MATH_PERMUTE_PS(TC0, _MM_SHUFFLE(3, 3, 3, 3));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(TC0, _MM_SHUFFLE(2, 2, 2, 2));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(TC0, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(TC0, _MM_SHUFFLE(0, 0, 0, 0));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    Result = MATH_FMADD_PS(Result, x2, g_One);

    Result = _mm_mul_ps(Result, x);
    __m128 result1 = _mm_mul_ps(sign, g_HalfPi);
    result1 = _mm_sub_ps(result1, Result);

    comp = _mm_cmpeq_ps(sign, g_Zero);
    select0 = _mm_and_ps(comp, Result);
    select1 = _mm_andnot_ps(comp, result1);
    Result = _mm_or_ps(select0, select1);
    return Result;
#endif
}

inline Vector MathCallConv VectorATan2
(
    VectorArg Y,
    VectorArg X
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            atan2f(Y.vector4_f32[0], X.vector4_f32[0]),
            atan2f(Y.vector4_f32[1], X.vector4_f32[1]),
            atan2f(Y.vector4_f32[2], X.vector4_f32[2]),
            atan2f(Y.vector4_f32[3], X.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_atan2_ps(Y, X);
    return Result;
#else

    // Return the inverse tangent of Y / X in the range of -Pi to Pi with the following exceptions:

    //     Y == 0 and X is Negative         -> Pi with the sign of Y
    //     y == 0 and x is positive         -> 0 with the sign of y
    //     Y != 0 and X == 0                -> Pi / 2 with the sign of Y
    //     Y != 0 and X is Negative         -> atan(y/x) + (PI with the sign of Y)
    //     X == -Infinity and Finite Y      -> Pi with the sign of Y
    //     X == +Infinity and Finite Y      -> 0 with the sign of Y
    //     Y == Infinity and X is Finite    -> Pi / 2 with the sign of Y
    //     Y == Infinity and X == -Infinity -> 3Pi / 4 with the sign of Y
    //     Y == Infinity and X == +Infinity -> Pi / 4 with the sign of Y

    static const VectorF32 ATan2Constants = { { { MATH_PI, MATH_PIDIV2, MATH_PIDIV4, MATH_PI * 3.0f / 4.0f } } };

    Vector Zero = VectorZero();
    Vector ATanResultValid = VectorTrueInt();

    Vector Pi = VectorSplatX(ATan2Constants);
    Vector PiOverTwo = VectorSplatY(ATan2Constants);
    Vector PiOverFour = VectorSplatZ(ATan2Constants);
    Vector ThreePiOverFour = VectorSplatW(ATan2Constants);

    Vector YEqualsZero = VectorEqual(Y, Zero);
    Vector XEqualsZero = VectorEqual(X, Zero);
    Vector XIsPositive = VectorAndInt(X, g_NegativeZero.v);
    XIsPositive = VectorEqualInt(XIsPositive, Zero);
    Vector YEqualsInfinity = VectorIsInfinite(Y);
    Vector XEqualsInfinity = VectorIsInfinite(X);

    Vector YSign = VectorAndInt(Y, g_NegativeZero.v);
    Pi = VectorOrInt(Pi, YSign);
    PiOverTwo = VectorOrInt(PiOverTwo, YSign);
    PiOverFour = VectorOrInt(PiOverFour, YSign);
    ThreePiOverFour = VectorOrInt(ThreePiOverFour, YSign);

    Vector R1 = VectorSelect(Pi, YSign, XIsPositive);
    Vector R2 = VectorSelect(ATanResultValid, PiOverTwo, XEqualsZero);
    Vector R3 = VectorSelect(R2, R1, YEqualsZero);
    Vector R4 = VectorSelect(ThreePiOverFour, PiOverFour, XIsPositive);
    Vector R5 = VectorSelect(PiOverTwo, R4, XEqualsInfinity);
    Vector Result = VectorSelect(R3, R5, YEqualsInfinity);
    ATanResultValid = VectorEqualInt(Result, ATanResultValid);

    Vector V = VectorDivide(Y, X);

    Vector R0 = VectorATan(V);

    R1 = VectorSelect(Pi, g_NegativeZero, XIsPositive);
    R2 = VectorAdd(R0, R1);

    return VectorSelect(Result, R2, ATanResultValid);

#endif
}

inline Vector MathCallConv VectorSinEst(VectorArg V)noexcept{
    // 7-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            sinf(V.vector4_f32[0]),
            sinf(V.vector4_f32[1]),
            sinf(V.vector4_f32[2]),
            sinf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with sin(y) = sin(x).
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_NegativeZero);
    uint32x4_t c = vorrq_u32(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_HalfPi);
    x = vbslq_f32(comp, x, rflx);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation
    const Vector SEC = g_SinCoefficients1;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(SEC), 0);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_high_f32(SEC), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(SEC), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    Result = vmulq_f32(Result, x);
    return Result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_sin_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with sin(y) = sin(x).
    __m128 sign = _mm_and_ps(x, g_NegativeZero);
    __m128 c = _mm_or_ps(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    __m128 absx = _mm_andnot_ps(sign, x);  // |x|
    __m128 rflx = _mm_sub_ps(c, x);
    __m128 comp = _mm_cmple_ps(absx, g_HalfPi);
    __m128 select0 = _mm_and_ps(comp, x);
    __m128 select1 = _mm_andnot_ps(comp, rflx);
    x = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    const Vector SEC = g_SinCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(SEC, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(SEC, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SEC, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);
    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, x);
    return Result;
#endif
}

inline Vector MathCallConv VectorCosEst(VectorArg V)noexcept{
    // 6-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            cosf(V.vector4_f32[0]),
            cosf(V.vector4_f32[1]),
            cosf(V.vector4_f32[2]),
            cosf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Map V to x in [-pi,pi].
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_NegativeZero);
    uint32x4_t c = vorrq_u32(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_HalfPi);
    x = vbslq_f32(comp, x, rflx);
    float32x4_t fsign = vbslq_f32(comp, g_One, g_NegativeOne);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation
    const Vector CEC = g_CosCoefficients1;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(CEC), 0);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_high_f32(CEC), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(CEC), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    Result = vmulq_f32(Result, fsign);
    return Result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_cos_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Map V to x in [-pi,pi].
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    Vector sign = _mm_and_ps(x, g_NegativeZero);
    __m128 c = _mm_or_ps(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    __m128 absx = _mm_andnot_ps(sign, x);  // |x|
    __m128 rflx = _mm_sub_ps(c, x);
    __m128 comp = _mm_cmple_ps(absx, g_HalfPi);
    __m128 select0 = _mm_and_ps(comp, x);
    __m128 select1 = _mm_andnot_ps(comp, rflx);
    x = _mm_or_ps(select0, select1);
    select0 = _mm_and_ps(comp, g_One);
    select1 = _mm_andnot_ps(comp, g_NegativeOne);
    sign = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    const Vector CEC = g_CosCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(CEC, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(CEC, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CEC, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);
    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, sign);
    return Result;
#endif
}

_Use_decl_annotations_
inline void MathCallConv VectorSinCosEst
(
    Vector* pSin,
    Vector* pCos,
    VectorArg  V
)noexcept{
    assert(pSin != nullptr);
    assert(pCos != nullptr);

    // 7/6-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Sin = { { {
            sinf(V.vector4_f32[0]),
            sinf(V.vector4_f32[1]),
            sinf(V.vector4_f32[2]),
            sinf(V.vector4_f32[3])
        } } };

    VectorF32 Cos = { { {
            cosf(V.vector4_f32[0]),
            cosf(V.vector4_f32[1]),
            cosf(V.vector4_f32[2]),
            cosf(V.vector4_f32[3])
        } } };

    *pSin = Sin.v;
    *pCos = Cos.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_NegativeZero);
    uint32x4_t c = vorrq_u32(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_HalfPi);
    x = vbslq_f32(comp, x, rflx);
    float32x4_t fsign = vbslq_f32(comp, g_One, g_NegativeOne);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation for sine
    const Vector SEC = g_SinCoefficients1;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(SEC), 0);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_high_f32(SEC), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(SEC), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    *pSin = vmulq_f32(Result, x);

    // Compute polynomial approximation
    const Vector CEC = g_CosCoefficients1;
    vConstants = vdupq_lane_f32(vget_high_f32(CEC), 0);
    Result = vmlaq_lane_f32(vConstants, x2, vget_high_f32(CEC), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(CEC), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    Result = vmlaq_f32(g_One, Result, x2);
    *pCos = vmulq_f32(Result, fsign);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Force the value within the bounds of pi
    Vector x = VectorModAngles(V);

    // Map in [-pi/2,pi/2] with sin(y) = sin(x), cos(y) = sign*cos(x).
    Vector sign = _mm_and_ps(x, g_NegativeZero);
    __m128 c = _mm_or_ps(g_Pi, sign);  // pi when x >= 0, -pi when x < 0
    __m128 absx = _mm_andnot_ps(sign, x);  // |x|
    __m128 rflx = _mm_sub_ps(c, x);
    __m128 comp = _mm_cmple_ps(absx, g_HalfPi);
    __m128 select0 = _mm_and_ps(comp, x);
    __m128 select1 = _mm_andnot_ps(comp, rflx);
    x = _mm_or_ps(select0, select1);
    select0 = _mm_and_ps(comp, g_One);
    select1 = _mm_andnot_ps(comp, g_NegativeOne);
    sign = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation for sine
    const Vector SEC = g_SinCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(SEC, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(SEC, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(SEC, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);
    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, x);
    *pSin = Result;

    // Compute polynomial approximation for cosine
    const Vector CEC = g_CosCoefficients1;
    vConstantsB = MATH_PERMUTE_PS(CEC, _MM_SHUFFLE(3, 3, 3, 3));
    vConstants = MATH_PERMUTE_PS(CEC, _MM_SHUFFLE(2, 2, 2, 2));
    Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(CEC, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);
    Result = MATH_FMADD_PS(Result, x2, g_One);
    Result = _mm_mul_ps(Result, sign);
    *pCos = Result;
#endif
}

inline Vector MathCallConv VectorTanEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            tanf(V.vector4_f32[0]),
            tanf(V.vector4_f32[1]),
            tanf(V.vector4_f32[2]),
            tanf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_tan_ps(V);
    return Result;
#else

    Vector OneOverPi = VectorSplatW(g_TanEstCoefficients.v);

    Vector V1 = VectorMultiply(V, OneOverPi);
    V1 = VectorRound(V1);

    V1 = VectorNegativeMultiplySubtract(g_Pi.v, V1, V);

    Vector T0 = VectorSplatX(g_TanEstCoefficients.v);
    Vector T1 = VectorSplatY(g_TanEstCoefficients.v);
    Vector T2 = VectorSplatZ(g_TanEstCoefficients.v);

    Vector V2T2 = VectorNegativeMultiplySubtract(V1, V1, T2);
    Vector V2 = VectorMultiply(V1, V1);
    Vector V1T0 = VectorMultiply(V1, T0);
    Vector V1T1 = VectorMultiply(V1, T1);

    Vector D = VectorReciprocalEst(V2T2);
    Vector N = VectorMultiplyAdd(V2, V1T1, V1T0);

    return VectorMultiply(N, D);

#endif
}

inline Vector MathCallConv VectorASinEst(VectorArg V)noexcept{
    // 3-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result;
    Result.f[0] = asinf(V.vector4_f32[0]);
    Result.f[1] = asinf(V.vector4_f32[1]);
    Result.f[2] = asinf(V.vector4_f32[2]);
    Result.f[3] = asinf(V.vector4_f32[3]);
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t nonnegative = vcgeq_f32(V, g_Zero);
    float32x4_t x = vabsq_f32(V);

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    float32x4_t oneMValue = vsubq_f32(g_One, x);
    float32x4_t clampOneMValue = vmaxq_f32(g_Zero, oneMValue);
    float32x4_t root = VectorSqrt(clampOneMValue);

    // Compute polynomial approximation
    const Vector AEC = g_ArcEstCoefficients;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(AEC), 0);
    Vector t0 = vmlaq_lane_f32(vConstants, x, vget_high_f32(AEC), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(AEC), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AEC), 0);
    t0 = vmlaq_f32(vConstants, t0, x);
    t0 = vmulq_f32(t0, root);

    float32x4_t t1 = vsubq_f32(g_Pi, t0);
    t0 = vbslq_f32(nonnegative, t0, t1);
    t0 = vsubq_f32(g_HalfPi, t0);
    return t0;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_asin_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 nonnegative = _mm_cmpge_ps(V, g_Zero);
    __m128 mvalue = _mm_sub_ps(g_Zero, V);
    __m128 x = _mm_max_ps(V, mvalue);  // |V|

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    __m128 oneMValue = _mm_sub_ps(g_One, x);
    __m128 clampOneMValue = _mm_max_ps(g_Zero, oneMValue);
    __m128 root = _mm_sqrt_ps(clampOneMValue);  // sqrt(1-|V|)

    // Compute polynomial approximation
    const Vector AEC = g_ArcEstCoefficients;
    __m128 vConstantsB = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 t0 = MATH_FMADD_PS(vConstantsB, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(1, 1, 1, 1));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(0, 0, 0, 0));
    t0 = MATH_FMADD_PS(t0, x, vConstants);
    t0 = _mm_mul_ps(t0, root);

    __m128 t1 = _mm_sub_ps(g_Pi, t0);
    t0 = _mm_and_ps(nonnegative, t0);
    t1 = _mm_andnot_ps(nonnegative, t1);
    t0 = _mm_or_ps(t0, t1);
    t0 = _mm_sub_ps(g_HalfPi, t0);
    return t0;
#endif
}

inline Vector MathCallConv VectorACosEst(VectorArg V)noexcept{
    // 3-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            acosf(V.vector4_f32[0]),
            acosf(V.vector4_f32[1]),
            acosf(V.vector4_f32[2]),
            acosf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t nonnegative = vcgeq_f32(V, g_Zero);
    float32x4_t x = vabsq_f32(V);

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    float32x4_t oneMValue = vsubq_f32(g_One, x);
    float32x4_t clampOneMValue = vmaxq_f32(g_Zero, oneMValue);
    float32x4_t root = VectorSqrt(clampOneMValue);

    // Compute polynomial approximation
    const Vector AEC = g_ArcEstCoefficients;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(AEC), 0);
    Vector t0 = vmlaq_lane_f32(vConstants, x, vget_high_f32(AEC), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(AEC), 1);
    t0 = vmlaq_f32(vConstants, t0, x);

    vConstants = vdupq_lane_f32(vget_low_f32(AEC), 0);
    t0 = vmlaq_f32(vConstants, t0, x);
    t0 = vmulq_f32(t0, root);

    float32x4_t t1 = vsubq_f32(g_Pi, t0);
    t0 = vbslq_f32(nonnegative, t0, t1);
    return t0;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_acos_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 nonnegative = _mm_cmpge_ps(V, g_Zero);
    __m128 mvalue = _mm_sub_ps(g_Zero, V);
    __m128 x = _mm_max_ps(V, mvalue);  // |V|

    // Compute (1-|V|), clamp to zero to avoid sqrt of negative number.
    __m128 oneMValue = _mm_sub_ps(g_One, x);
    __m128 clampOneMValue = _mm_max_ps(g_Zero, oneMValue);
    __m128 root = _mm_sqrt_ps(clampOneMValue);  // sqrt(1-|V|)

    // Compute polynomial approximation
    const Vector AEC = g_ArcEstCoefficients;
    __m128 vConstantsB = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 t0 = MATH_FMADD_PS(vConstantsB, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(1, 1, 1, 1));
    t0 = MATH_FMADD_PS(t0, x, vConstants);

    vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(0, 0, 0, 0));
    t0 = MATH_FMADD_PS(t0, x, vConstants);
    t0 = _mm_mul_ps(t0, root);

    __m128 t1 = _mm_sub_ps(g_Pi, t0);
    t0 = _mm_and_ps(nonnegative, t0);
    t1 = _mm_andnot_ps(nonnegative, t1);
    t0 = _mm_or_ps(t0, t1);
    return t0;
#endif
}

inline Vector MathCallConv VectorATanEst(VectorArg V)noexcept{
    // 9-degree minimax approximation

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            atanf(V.vector4_f32[0]),
            atanf(V.vector4_f32[1]),
            atanf(V.vector4_f32[2]),
            atanf(V.vector4_f32[3])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t absV = vabsq_f32(V);
    float32x4_t invV = VectorReciprocalEst(V);
    uint32x4_t comp = vcgtq_f32(V, g_One);
    float32x4_t sign = vbslq_f32(comp, g_One, g_NegativeOne);
    comp = vcleq_f32(absV, g_One);
    sign = vbslq_f32(comp, g_Zero, sign);
    float32x4_t x = vbslq_f32(comp, V, invV);

    float32x4_t x2 = vmulq_f32(x, x);

    // Compute polynomial approximation
    const Vector AEC = g_ATanEstCoefficients1;
    Vector vConstants = vdupq_lane_f32(vget_high_f32(AEC), 0);
    Vector Result = vmlaq_lane_f32(vConstants, x2, vget_high_f32(AEC), 1);

    vConstants = vdupq_lane_f32(vget_low_f32(AEC), 1);
    Result = vmlaq_f32(vConstants, Result, x2);

    vConstants = vdupq_lane_f32(vget_low_f32(AEC), 0);
    Result = vmlaq_f32(vConstants, Result, x2);

    // ATanEstCoefficients0 is already splatted
    Result = vmlaq_f32(g_ATanEstCoefficients0, Result, x2);
    Result = vmulq_f32(Result, x);

    float32x4_t result1 = vmulq_f32(sign, g_HalfPi);
    result1 = vsubq_f32(result1, Result);

    comp = vceqq_f32(sign, g_Zero);
    Result = vbslq_f32(comp, Result, result1);
    return Result;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_atan_ps(V);
    return Result;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 absV = VectorAbs(V);
    __m128 invV = _mm_div_ps(g_One, V);
    __m128 comp = _mm_cmpgt_ps(V, g_One);
    __m128 select0 = _mm_and_ps(comp, g_One);
    __m128 select1 = _mm_andnot_ps(comp, g_NegativeOne);
    __m128 sign = _mm_or_ps(select0, select1);
    comp = _mm_cmple_ps(absV, g_One);
    select0 = _mm_and_ps(comp, g_Zero);
    select1 = _mm_andnot_ps(comp, sign);
    sign = _mm_or_ps(select0, select1);
    select0 = _mm_and_ps(comp, V);
    select1 = _mm_andnot_ps(comp, invV);
    __m128 x = _mm_or_ps(select0, select1);

    __m128 x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    const Vector AEC = g_ATanEstCoefficients1;
    __m128 vConstantsB = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 Result = MATH_FMADD_PS(vConstantsB, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(1, 1, 1, 1));
    Result = MATH_FMADD_PS(Result, x2, vConstants);

    vConstants = MATH_PERMUTE_PS(AEC, _MM_SHUFFLE(0, 0, 0, 0));
    Result = MATH_FMADD_PS(Result, x2, vConstants);
    // ATanEstCoefficients0 is already splatted
    Result = MATH_FMADD_PS(Result, x2, g_ATanEstCoefficients0);
    Result = _mm_mul_ps(Result, x);
    __m128 result1 = _mm_mul_ps(sign, g_HalfPi);
    result1 = _mm_sub_ps(result1, Result);

    comp = _mm_cmpeq_ps(sign, g_Zero);
    select0 = _mm_and_ps(comp, Result);
    select1 = _mm_andnot_ps(comp, result1);
    Result = _mm_or_ps(select0, select1);
    return Result;
#endif
}

inline Vector MathCallConv VectorATan2Est
(
    VectorArg Y,
    VectorArg X
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            atan2f(Y.vector4_f32[0], X.vector4_f32[0]),
            atan2f(Y.vector4_f32[1], X.vector4_f32[1]),
            atan2f(Y.vector4_f32[2], X.vector4_f32[2]),
            atan2f(Y.vector4_f32[3], X.vector4_f32[3]),
        } } };
    return Result.v;
#elif defined(_MATH_SVML_INTRINSICS_)
    Vector Result = _mm_atan2_ps(Y, X);
    return Result;
#else

    static const VectorF32 ATan2Constants = { { { MATH_PI, MATH_PIDIV2, MATH_PIDIV4, 2.3561944905f /* Pi*3/4 */ } } };

    const Vector Zero = VectorZero();
    Vector ATanResultValid = VectorTrueInt();

    Vector Pi = VectorSplatX(ATan2Constants);
    Vector PiOverTwo = VectorSplatY(ATan2Constants);
    Vector PiOverFour = VectorSplatZ(ATan2Constants);
    Vector ThreePiOverFour = VectorSplatW(ATan2Constants);

    Vector YEqualsZero = VectorEqual(Y, Zero);
    Vector XEqualsZero = VectorEqual(X, Zero);
    Vector XIsPositive = VectorAndInt(X, g_NegativeZero.v);
    XIsPositive = VectorEqualInt(XIsPositive, Zero);
    Vector YEqualsInfinity = VectorIsInfinite(Y);
    Vector XEqualsInfinity = VectorIsInfinite(X);

    Vector YSign = VectorAndInt(Y, g_NegativeZero.v);
    Pi = VectorOrInt(Pi, YSign);
    PiOverTwo = VectorOrInt(PiOverTwo, YSign);
    PiOverFour = VectorOrInt(PiOverFour, YSign);
    ThreePiOverFour = VectorOrInt(ThreePiOverFour, YSign);

    Vector R1 = VectorSelect(Pi, YSign, XIsPositive);
    Vector R2 = VectorSelect(ATanResultValid, PiOverTwo, XEqualsZero);
    Vector R3 = VectorSelect(R2, R1, YEqualsZero);
    Vector R4 = VectorSelect(ThreePiOverFour, PiOverFour, XIsPositive);
    Vector R5 = VectorSelect(PiOverTwo, R4, XEqualsInfinity);
    Vector Result = VectorSelect(R3, R5, YEqualsInfinity);
    ATanResultValid = VectorEqualInt(Result, ATanResultValid);

    Vector Reciprocal = VectorReciprocalEst(X);
    Vector V = VectorMultiply(Y, Reciprocal);
    Vector R0 = VectorATanEst(V);

    R1 = VectorSelect(Pi, g_NegativeZero, XIsPositive);
    R2 = VectorAdd(R0, R1);

    Result = VectorSelect(Result, R2, ATanResultValid);

    return Result;

#endif
}

inline Vector MathCallConv VectorLerp
(
    VectorArg V0,
    VectorArg V1,
    float    t
)noexcept{
    // V0 + t * (V1 - V0)

#if defined(_MATH_NO_INTRINSICS_)

    Vector Scale = VectorReplicate(t);
    Vector Length = VectorSubtract(V1, V0);
    return VectorMultiplyAdd(Length, Scale, V0);

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector L = vsubq_f32(V1, V0);
    return vmlaq_n_f32(V0, L, t);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector L = _mm_sub_ps(V1, V0);
    Vector S = _mm_set_ps1(t);
    return MATH_FMADD_PS(L, S, V0);
#endif
}

inline Vector MathCallConv VectorLerpV
(
    VectorArg V0,
    VectorArg V1,
    VectorArg T
)noexcept{
    // V0 + T * (V1 - V0)

#if defined(_MATH_NO_INTRINSICS_)

    Vector Length = VectorSubtract(V1, V0);
    return VectorMultiplyAdd(Length, T, V0);

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector L = vsubq_f32(V1, V0);
    return vmlaq_f32(V0, L, T);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector Length = _mm_sub_ps(V1, V0);
    return MATH_FMADD_PS(Length, T, V0);
#endif
}

inline Vector MathCallConv VectorHermite
(
    VectorArg Position0,
    VectorArg Tangent0,
    VectorArg Position1,
    VectorArg2 Tangent1,
    float    t
)noexcept{
    // Result = (2 * t^3 - 3 * t^2 + 1) * Position0 +
    //          (t^3 - 2 * t^2 + t) * Tangent0 +
    //          (-2 * t^3 + 3 * t^2) * Position1 +
    //          (t^3 - t^2) * Tangent1

#if defined(_MATH_NO_INTRINSICS_)

    float t2 = t * t;
    float t3 = t * t2;

    Vector P0 = VectorReplicate(2.0f * t3 - 3.0f * t2 + 1.0f);
    Vector T0 = VectorReplicate(t3 - 2.0f * t2 + t);
    Vector P1 = VectorReplicate(-2.0f * t3 + 3.0f * t2);
    Vector T1 = VectorReplicate(t3 - t2);

    Vector Result = VectorMultiply(P0, Position0);
    Result = VectorMultiplyAdd(T0, Tangent0, Result);
    Result = VectorMultiplyAdd(P1, Position1, Result);
    Result = VectorMultiplyAdd(T1, Tangent1, Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float t2 = t * t;
    float t3 = t * t2;

    float p0 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float t0 = t3 - 2.0f * t2 + t;
    float p1 = -2.0f * t3 + 3.0f * t2;
    float t1 = t3 - t2;

    Vector vResult = vmulq_n_f32(Position0, p0);
    vResult = vmlaq_n_f32(vResult, Tangent0, t0);
    vResult = vmlaq_n_f32(vResult, Position1, p1);
    vResult = vmlaq_n_f32(vResult, Tangent1, t1);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    float t2 = t * t;
    float t3 = t * t2;

    Vector P0 = _mm_set_ps1(2.0f * t3 - 3.0f * t2 + 1.0f);
    Vector T0 = _mm_set_ps1(t3 - 2.0f * t2 + t);
    Vector P1 = _mm_set_ps1(-2.0f * t3 + 3.0f * t2);
    Vector T1 = _mm_set_ps1(t3 - t2);

    Vector vResult = _mm_mul_ps(P0, Position0);
    vResult = MATH_FMADD_PS(Tangent0, T0, vResult);
    vResult = MATH_FMADD_PS(Position1, P1, vResult);
    vResult = MATH_FMADD_PS(Tangent1, T1, vResult);
    return vResult;
#endif
}

inline Vector MathCallConv VectorHermiteV
(
    VectorArg Position0,
    VectorArg Tangent0,
    VectorArg Position1,
    VectorArg2 Tangent1,
    VectorArg3 T
)noexcept{
    // Result = (2 * t^3 - 3 * t^2 + 1) * Position0 +
    //          (t^3 - 2 * t^2 + t) * Tangent0 +
    //          (-2 * t^3 + 3 * t^2) * Position1 +
    //          (t^3 - t^2) * Tangent1

#if defined(_MATH_NO_INTRINSICS_)

    Vector T2 = VectorMultiply(T, T);
    Vector T3 = VectorMultiply(T, T2);

    Vector P0 = VectorReplicate(2.0f * T3.vector4_f32[0] - 3.0f * T2.vector4_f32[0] + 1.0f);
    Vector T0 = VectorReplicate(T3.vector4_f32[1] - 2.0f * T2.vector4_f32[1] + T.vector4_f32[1]);
    Vector P1 = VectorReplicate(-2.0f * T3.vector4_f32[2] + 3.0f * T2.vector4_f32[2]);
    Vector T1 = VectorReplicate(T3.vector4_f32[3] - T2.vector4_f32[3]);

    Vector Result = VectorMultiply(P0, Position0);
    Result = VectorMultiplyAdd(T0, Tangent0, Result);
    Result = VectorMultiplyAdd(P1, Position1, Result);
    Result = VectorMultiplyAdd(T1, Tangent1, Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 CatMulT2 = { { { -3.0f, -2.0f, 3.0f, -1.0f } } };
    static const VectorF32 CatMulT3 = { { { 2.0f, 1.0f, -2.0f, 1.0f } } };

    Vector T2 = vmulq_f32(T, T);
    Vector T3 = vmulq_f32(T, T2);
    // Mul by the constants against t^2
    T2 = vmulq_f32(T2, CatMulT2);
    // Mul by the constants against t^3
    T3 = vmlaq_f32(T2, T3, CatMulT3);
    // T3 now has the pre-result.
    // I need to add t.y only
    T2 = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T), g_MaskY));
    T3 = vaddq_f32(T3, T2);
    // Add 1.0f to x
    T3 = vaddq_f32(T3, g_IdentityC0);
    // Now, I have the constants created
    // Mul the x constant to Position0
    Vector vResult = vmulq_lane_f32(Position0, vget_low_f32(T3), 0); // T3[0]
    // Mul the y constant to Tangent0
    vResult = vmlaq_lane_f32(vResult, Tangent0, vget_low_f32(T3), 1); // T3[1]
    // Mul the z constant to Position1
    vResult = vmlaq_lane_f32(vResult, Position1, vget_high_f32(T3), 0); // T3[2]
    // Mul the w constant to Tangent1
    vResult = vmlaq_lane_f32(vResult, Tangent1, vget_high_f32(T3), 1); // T3[3]
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 CatMulT2 = { { { -3.0f, -2.0f, 3.0f, -1.0f } } };
    static const VectorF32 CatMulT3 = { { { 2.0f, 1.0f, -2.0f, 1.0f } } };

    Vector T2 = _mm_mul_ps(T, T);
    Vector T3 = _mm_mul_ps(T, T2);
    // Mul by the constants against t^2
    T2 = _mm_mul_ps(T2, CatMulT2);
    // Mul by the constants against t^3
    T3 = MATH_FMADD_PS(T3, CatMulT3, T2);
    // T3 now has the pre-result.
    // I need to add t.y only
    T2 = _mm_and_ps(T, g_MaskY);
    T3 = _mm_add_ps(T3, T2);
    // Add 1.0f to x
    T3 = _mm_add_ps(T3, g_IdentityC0);
    // Now, I have the constants created
    // Mul the x constant to Position0
    Vector vResult = MATH_PERMUTE_PS(T3, _MM_SHUFFLE(0, 0, 0, 0));
    vResult = _mm_mul_ps(vResult, Position0);
    // Mul the y constant to Tangent0
    T2 = MATH_PERMUTE_PS(T3, _MM_SHUFFLE(1, 1, 1, 1));
    vResult = MATH_FMADD_PS(T2, Tangent0, vResult);
    // Mul the z constant to Position1
    T2 = MATH_PERMUTE_PS(T3, _MM_SHUFFLE(2, 2, 2, 2));
    vResult = MATH_FMADD_PS(T2, Position1, vResult);
    // Mul the w constant to Tangent1
    T3 = MATH_PERMUTE_PS(T3, _MM_SHUFFLE(3, 3, 3, 3));
    vResult = MATH_FMADD_PS(T3, Tangent1, vResult);
    return vResult;
#endif
}

inline Vector MathCallConv VectorCatmullRom
(
    VectorArg Position0,
    VectorArg Position1,
    VectorArg Position2,
    VectorArg2 Position3,
    float    t
)noexcept{
    // Result = ((-t^3 + 2 * t^2 - t) * Position0 +
    //           (3 * t^3 - 5 * t^2 + 2) * Position1 +
    //           (-3 * t^3 + 4 * t^2 + t) * Position2 +
    //           (t^3 - t^2) * Position3) * 0.5

#if defined(_MATH_NO_INTRINSICS_)

    float t2 = t * t;
    float t3 = t * t2;

    Vector P0 = VectorReplicate((-t3 + 2.0f * t2 - t) * 0.5f);
    Vector P1 = VectorReplicate((3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f);
    Vector P2 = VectorReplicate((-3.0f * t3 + 4.0f * t2 + t) * 0.5f);
    Vector P3 = VectorReplicate((t3 - t2) * 0.5f);

    Vector Result = VectorMultiply(P0, Position0);
    Result = VectorMultiplyAdd(P1, Position1, Result);
    Result = VectorMultiplyAdd(P2, Position2, Result);
    Result = VectorMultiplyAdd(P3, Position3, Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float t2 = t * t;
    float t3 = t * t2;

    float p0 = (-t3 + 2.0f * t2 - t) * 0.5f;
    float p1 = (3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f;
    float p2 = (-3.0f * t3 + 4.0f * t2 + t) * 0.5f;
    float p3 = (t3 - t2) * 0.5f;

    Vector P1 = vmulq_n_f32(Position1, p1);
    Vector P0 = vmlaq_n_f32(P1, Position0, p0);
    Vector P3 = vmulq_n_f32(Position3, p3);
    Vector P2 = vmlaq_n_f32(P3, Position2, p2);
    P0 = vaddq_f32(P0, P2);
    return P0;
#elif defined(_MATH_SSE_INTRINSICS_)
    float t2 = t * t;
    float t3 = t * t2;

    Vector P0 = _mm_set_ps1((-t3 + 2.0f * t2 - t) * 0.5f);
    Vector P1 = _mm_set_ps1((3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f);
    Vector P2 = _mm_set_ps1((-3.0f * t3 + 4.0f * t2 + t) * 0.5f);
    Vector P3 = _mm_set_ps1((t3 - t2) * 0.5f);

    P1 = _mm_mul_ps(Position1, P1);
    P0 = MATH_FMADD_PS(Position0, P0, P1);
    P3 = _mm_mul_ps(Position3, P3);
    P2 = MATH_FMADD_PS(Position2, P2, P3);
    P0 = _mm_add_ps(P0, P2);
    return P0;
#endif
}

inline Vector MathCallConv VectorCatmullRomV
(
    VectorArg Position0,
    VectorArg Position1,
    VectorArg Position2,
    VectorArg2 Position3,
    VectorArg3 T
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float fx = T.vector4_f32[0];
    float fy = T.vector4_f32[1];
    float fz = T.vector4_f32[2];
    float fw = T.vector4_f32[3];
    VectorF32 vResult = { { {
            0.5f * ((-fx * fx * fx + 2 * fx * fx - fx) * Position0.vector4_f32[0]
            + (3 * fx * fx * fx - 5 * fx * fx + 2) * Position1.vector4_f32[0]
            + (-3 * fx * fx * fx + 4 * fx * fx + fx) * Position2.vector4_f32[0]
            + (fx * fx * fx - fx * fx) * Position3.vector4_f32[0]),

            0.5f * ((-fy * fy * fy + 2 * fy * fy - fy) * Position0.vector4_f32[1]
            + (3 * fy * fy * fy - 5 * fy * fy + 2) * Position1.vector4_f32[1]
            + (-3 * fy * fy * fy + 4 * fy * fy + fy) * Position2.vector4_f32[1]
            + (fy * fy * fy - fy * fy) * Position3.vector4_f32[1]),

            0.5f * ((-fz * fz * fz + 2 * fz * fz - fz) * Position0.vector4_f32[2]
            + (3 * fz * fz * fz - 5 * fz * fz + 2) * Position1.vector4_f32[2]
            + (-3 * fz * fz * fz + 4 * fz * fz + fz) * Position2.vector4_f32[2]
            + (fz * fz * fz - fz * fz) * Position3.vector4_f32[2]),

            0.5f * ((-fw * fw * fw + 2 * fw * fw - fw) * Position0.vector4_f32[3]
            + (3 * fw * fw * fw - 5 * fw * fw + 2) * Position1.vector4_f32[3]
            + (-3 * fw * fw * fw + 4 * fw * fw + fw) * Position2.vector4_f32[3]
            + (fw * fw * fw - fw * fw) * Position3.vector4_f32[3])
        } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Catmul2 = { { { 2.0f, 2.0f, 2.0f, 2.0f } } };
    static const VectorF32 Catmul3 = { { { 3.0f, 3.0f, 3.0f, 3.0f } } };
    static const VectorF32 Catmul4 = { { { 4.0f, 4.0f, 4.0f, 4.0f } } };
    static const VectorF32 Catmul5 = { { { 5.0f, 5.0f, 5.0f, 5.0f } } };
    // Cache T^2 and T^3
    Vector T2 = vmulq_f32(T, T);
    Vector T3 = vmulq_f32(T, T2);
    // Perform the Position0 term
    Vector vResult = vaddq_f32(T2, T2);
    vResult = vsubq_f32(vResult, T);
    vResult = vsubq_f32(vResult, T3);
    vResult = vmulq_f32(vResult, Position0);
    // Perform the Position1 term and add
    Vector vTemp = vmulq_f32(T3, Catmul3);
    vTemp = vmlsq_f32(vTemp, T2, Catmul5);
    vTemp = vaddq_f32(vTemp, Catmul2);
    vResult = vmlaq_f32(vResult, vTemp, Position1);
    // Perform the Position2 term and add
    vTemp = vmulq_f32(T2, Catmul4);
    vTemp = vmlsq_f32(vTemp, T3, Catmul3);
    vTemp = vaddq_f32(vTemp, T);
    vResult = vmlaq_f32(vResult, vTemp, Position2);
    // Position3 is the last term
    T3 = vsubq_f32(T3, T2);
    vResult = vmlaq_f32(vResult, T3, Position3);
    // Multiply by 0.5f and exit
    vResult = vmulq_f32(vResult, g_OneHalf);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 Catmul2 = { { { 2.0f, 2.0f, 2.0f, 2.0f } } };
    static const VectorF32 Catmul3 = { { { 3.0f, 3.0f, 3.0f, 3.0f } } };
    static const VectorF32 Catmul4 = { { { 4.0f, 4.0f, 4.0f, 4.0f } } };
    static const VectorF32 Catmul5 = { { { 5.0f, 5.0f, 5.0f, 5.0f } } };
    // Cache T^2 and T^3
    Vector T2 = _mm_mul_ps(T, T);
    Vector T3 = _mm_mul_ps(T, T2);
    // Perform the Position0 term
    Vector vResult = _mm_add_ps(T2, T2);
    vResult = _mm_sub_ps(vResult, T);
    vResult = _mm_sub_ps(vResult, T3);
    vResult = _mm_mul_ps(vResult, Position0);
    // Perform the Position1 term and add
    Vector vTemp = _mm_mul_ps(T3, Catmul3);
    vTemp = MATH_FNMADD_PS(T2, Catmul5, vTemp);
    vTemp = _mm_add_ps(vTemp, Catmul2);
    vResult = MATH_FMADD_PS(vTemp, Position1, vResult);
    // Perform the Position2 term and add
    vTemp = _mm_mul_ps(T2, Catmul4);
    vTemp = MATH_FNMADD_PS(T3, Catmul3, vTemp);
    vTemp = _mm_add_ps(vTemp, T);
    vResult = MATH_FMADD_PS(vTemp, Position2, vResult);
    // Position3 is the last term
    T3 = _mm_sub_ps(T3, T2);
    vResult = MATH_FMADD_PS(T3, Position3, vResult);
    // Multiply by 0.5f and exit
    vResult = _mm_mul_ps(vResult, g_OneHalf);
    return vResult;
#endif
}

inline Vector MathCallConv VectorBaryCentric
(
    VectorArg Position0,
    VectorArg Position1,
    VectorArg Position2,
    float    f,
    float    g
)noexcept{
    // Result = Position0 + f * (Position1 - Position0) + g * (Position2 - Position0)

#if defined(_MATH_NO_INTRINSICS_)

    Vector P10 = VectorSubtract(Position1, Position0);
    Vector ScaleF = VectorReplicate(f);

    Vector P20 = VectorSubtract(Position2, Position0);
    Vector ScaleG = VectorReplicate(g);

    Vector Result = VectorMultiplyAdd(P10, ScaleF, Position0);
    Result = VectorMultiplyAdd(P20, ScaleG, Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector R1 = vsubq_f32(Position1, Position0);
    Vector R2 = vsubq_f32(Position2, Position0);
    R1 = vmlaq_n_f32(Position0, R1, f);
    return vmlaq_n_f32(R1, R2, g);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector R1 = _mm_sub_ps(Position1, Position0);
    Vector R2 = _mm_sub_ps(Position2, Position0);
    Vector SF = _mm_set_ps1(f);
    R1 = MATH_FMADD_PS(R1, SF, Position0);
    Vector SG = _mm_set_ps1(g);
    return MATH_FMADD_PS(R2, SG, R1);
#endif
}

inline Vector MathCallConv VectorBaryCentricV
(
    VectorArg Position0,
    VectorArg Position1,
    VectorArg Position2,
    VectorArg2 F,
    VectorArg3 G
)noexcept{
    // Result = Position0 + f * (Position1 - Position0) + g * (Position2 - Position0)

#if defined(_MATH_NO_INTRINSICS_)

    Vector P10 = VectorSubtract(Position1, Position0);
    Vector P20 = VectorSubtract(Position2, Position0);

    Vector Result = VectorMultiplyAdd(P10, F, Position0);
    Result = VectorMultiplyAdd(P20, G, Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector R1 = vsubq_f32(Position1, Position0);
    Vector R2 = vsubq_f32(Position2, Position0);
    R1 = vmlaq_f32(Position0, R1, F);
    return vmlaq_f32(R1, R2, G);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector R1 = _mm_sub_ps(Position1, Position0);
    Vector R2 = _mm_sub_ps(Position2, Position0);
    R1 = MATH_FMADD_PS(R1, F, Position0);
    return MATH_FMADD_PS(R2, G, R1);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Comparison operations
inline bool MathCallConv Vector2Equal
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] == V2.vector4_f32[0]) && (V1.vector4_f32[1] == V2.vector4_f32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vceq_f32(vget_low_f32(V1), vget_low_f32(V2));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    // z and w are don't care
    return (((_mm_movemask_ps(vTemp) & 3) == 3) != 0);
#endif
}


inline uint32_t MathCallConv Vector2EqualR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    uint32_t CR = 0;
    if((V1.vector4_f32[0] == V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] == V2.vector4_f32[1])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] != V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] != V2.vector4_f32[1])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vceq_f32(vget_low_f32(V1), vget_low_f32(V2));
    uint64_t r = vget_lane_u64(vreinterpret_u64_u32(vTemp), 0);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    // z and w are don't care
    int iTest = _mm_movemask_ps(vTemp) & 3;
    uint32_t CR = 0;
    if(iTest == 3){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector2EqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_u32[0] == V2.vector4_u32[0]) && (V1.vector4_u32[1] == V2.vector4_u32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vceq_u32(vget_low_u32(vreinterpretq_u32_f32(V1)), vget_low_u32(vreinterpretq_u32_f32(V2)));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return (((_mm_movemask_ps(_mm_castsi128_ps(vTemp)) & 3) == 3) != 0);
#endif
}

inline uint32_t MathCallConv Vector2EqualIntR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    uint32_t CR = 0;
    if((V1.vector4_u32[0] == V2.vector4_u32[0]) &&
        (V1.vector4_u32[1] == V2.vector4_u32[1])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_u32[0] != V2.vector4_u32[0]) &&
        (V1.vector4_u32[1] != V2.vector4_u32[1])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vceq_u32(vget_low_u32(vreinterpretq_u32_f32(V1)), vget_low_u32(vreinterpretq_u32_f32(V2)));
    uint64_t r = vget_lane_u64(vreinterpret_u64_u32(vTemp), 0);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    int iTest = _mm_movemask_ps(_mm_castsi128_ps(vTemp)) & 3;
    uint32_t CR = 0;
    if(iTest == 3){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector2NearEqual
(
    VectorArg V1,
    VectorArg V2,
    VectorArg Epsilon
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float dx = fabsf(V1.vector4_f32[0] - V2.vector4_f32[0]);
    float dy = fabsf(V1.vector4_f32[1] - V2.vector4_f32[1]);
    return ((dx <= Epsilon.vector4_f32[0]) &&
        (dy <= Epsilon.vector4_f32[1]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t vDelta = vsub_f32(vget_low_f32(V1), vget_low_f32(V2));
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    uint32x2_t vTemp = vacle_f32(vDelta, vget_low_u32(Epsilon));
#else
    uint32x2_t vTemp = vcle_f32(vabs_f32(vDelta), vget_low_f32(Epsilon));
#endif
    uint64_t r = vget_lane_u64(vreinterpret_u64_u32(vTemp), 0);
    return (r == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Get the difference
    Vector vDelta = _mm_sub_ps(V1, V2);
    // Get the absolute value of the difference
    Vector vTemp = _mm_setzero_ps();
    vTemp = _mm_sub_ps(vTemp, vDelta);
    vTemp = _mm_max_ps(vTemp, vDelta);
    vTemp = _mm_cmple_ps(vTemp, Epsilon);
    // z and w are don't care
    return (((_mm_movemask_ps(vTemp) & 3) == 0x3) != 0);
#endif
}

inline bool MathCallConv Vector2NotEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] != V2.vector4_f32[0]) || (V1.vector4_f32[1] != V2.vector4_f32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vceq_f32(vget_low_f32(V1), vget_low_f32(V2));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) != 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    // z and w are don't care
    return (((_mm_movemask_ps(vTemp) & 3) != 3) != 0);
#endif
}

inline bool MathCallConv Vector2NotEqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_u32[0] != V2.vector4_u32[0]) || (V1.vector4_u32[1] != V2.vector4_u32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vceq_u32(vget_low_u32(vreinterpretq_u32_f32(V1)), vget_low_u32(vreinterpretq_u32_f32(V2)));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) != 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return (((_mm_movemask_ps(_mm_castsi128_ps(vTemp)) & 3) != 3) != 0);
#endif
}

inline bool MathCallConv Vector2Greater
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] > V2.vector4_f32[0]) && (V1.vector4_f32[1] > V2.vector4_f32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vcgt_f32(vget_low_f32(V1), vget_low_f32(V2));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpgt_ps(V1, V2);
    // z and w are don't care
    return (((_mm_movemask_ps(vTemp) & 3) == 3) != 0);
#endif
}

inline uint32_t MathCallConv Vector2GreaterR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    uint32_t CR = 0;
    if((V1.vector4_f32[0] > V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] > V2.vector4_f32[1])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] <= V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] <= V2.vector4_f32[1])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vcgt_f32(vget_low_f32(V1), vget_low_f32(V2));
    uint64_t r = vget_lane_u64(vreinterpret_u64_u32(vTemp), 0);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpgt_ps(V1, V2);
    int iTest = _mm_movemask_ps(vTemp) & 3;
    uint32_t CR = 0;
    if(iTest == 3){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector2GreaterOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] >= V2.vector4_f32[0]) && (V1.vector4_f32[1] >= V2.vector4_f32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vcge_f32(vget_low_f32(V1), vget_low_f32(V2));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpge_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 3) == 3) != 0);
#endif
}

inline uint32_t MathCallConv Vector2GreaterOrEqualR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    uint32_t CR = 0;
    if((V1.vector4_f32[0] >= V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] >= V2.vector4_f32[1])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] < V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] < V2.vector4_f32[1])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vcge_f32(vget_low_f32(V1), vget_low_f32(V2));
    uint64_t r = vget_lane_u64(vreinterpret_u64_u32(vTemp), 0);
    uint32_t CR = 0;
    if(r == 0xFFFFFFFFFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpge_ps(V1, V2);
    int iTest = _mm_movemask_ps(vTemp) & 3;
    uint32_t CR = 0;
    if(iTest == 3){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector2Less
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] < V2.vector4_f32[0]) && (V1.vector4_f32[1] < V2.vector4_f32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vclt_f32(vget_low_f32(V1), vget_low_f32(V2));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmplt_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 3) == 3) != 0);
#endif
}

inline bool MathCallConv Vector2LessOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] <= V2.vector4_f32[0]) && (V1.vector4_f32[1] <= V2.vector4_f32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x2_t vTemp = vcle_f32(vget_low_f32(V1), vget_low_f32(V2));
    return (vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmple_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 3) == 3) != 0);
#endif
}

inline bool MathCallConv Vector2InBounds
(
    VectorArg V,
    VectorArg Bounds
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V.vector4_f32[0] <= Bounds.vector4_f32[0] && V.vector4_f32[0] >= -Bounds.vector4_f32[0]) &&
        (V.vector4_f32[1] <= Bounds.vector4_f32[1] && V.vector4_f32[1] >= -Bounds.vector4_f32[1])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    float32x2_t B = vget_low_f32(Bounds);
    // Test if less than or equal
    uint32x2_t ivTemp1 = vcle_f32(VL, B);
    // Negate the bounds
    float32x2_t vTemp2 = vneg_f32(B);
    // Test if greater or equal (Reversed)
    uint32x2_t ivTemp2 = vcle_f32(vTemp2, VL);
    // Blend answers
    ivTemp1 = vand_u32(ivTemp1, ivTemp2);
    // x and y in bounds?
    return (vget_lane_u64(vreinterpret_u64_u32(ivTemp1), 0) == 0xFFFFFFFFFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Test if less than or equal
    Vector vTemp1 = _mm_cmple_ps(V, Bounds);
    // Negate the bounds
    Vector vTemp2 = _mm_mul_ps(Bounds, g_NegativeOne);
    // Test if greater or equal (Reversed)
    vTemp2 = _mm_cmple_ps(vTemp2, V);
    // Blend answers
    vTemp1 = _mm_and_ps(vTemp1, vTemp2);
    // x and y in bounds? (z and w are don't care)
    return (((_mm_movemask_ps(vTemp1) & 0x3) == 0x3) != 0);
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(push)
#pragma float_control(precise, on)
#endif

inline bool MathCallConv Vector2IsNaN(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (MATH_IS_NAN(V.vector4_f32[0]) ||
        MATH_IS_NAN(V.vector4_f32[1]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    return isnan(vgetq_lane_f32(V, 0)) || isnan(vgetq_lane_f32(V, 1));
#else
    float32x2_t VL = vget_low_f32(V);
    // Test against itself. NaN is always not equal
    uint32x2_t vTempNan = vceq_f32(VL, VL);
    // If x or y are NaN, the mask is zero
    return (vget_lane_u64(vreinterpret_u64_u32(vTempNan), 0) != 0xFFFFFFFFFFFFFFFFU);
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    MATH_ALIGNED_DATA(16) float tmp[4];
    _mm_store_ps(tmp, V);
    return isnan(tmp[0]) || isnan(tmp[1]);
#else
// Test against itself. NaN is always not equal
    Vector vTempNan = _mm_cmpneq_ps(V, V);
    // If x or y are NaN, the mask is non-zero
    return ((_mm_movemask_ps(vTempNan) & 3) != 0);
#endif
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(pop)
#endif

inline bool MathCallConv Vector2IsInfinite(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    return (MATH_IS_INF(V.vector4_f32[0]) ||
        MATH_IS_INF(V.vector4_f32[1]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Mask off the sign bit
    uint32x2_t vTemp = vand_u32(vget_low_u32(vreinterpretq_u32_f32(V)), vget_low_u32(g_AbsMask));
    // Compare to infinity
    vTemp = vceq_f32(vreinterpret_f32_u32(vTemp), vget_low_f32(g_Infinity));
    // If any are infinity, the signs are true.
    return vget_lane_u64(vreinterpret_u64_u32(vTemp), 0) != 0;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Mask off the sign bit
    __m128 vTemp = _mm_and_ps(V, g_AbsMask);
    // Compare to infinity
    vTemp = _mm_cmpeq_ps(vTemp, g_Infinity);
    // If x or z are infinity, the signs are true.
    return ((_mm_movemask_ps(vTemp) & 3) != 0);
#endif
}

// Computation operations
inline Vector MathCallConv Vector2Dot
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result;
    Result.f[0] =
        Result.f[1] =
        Result.f[2] =
        Result.f[3] = V1.vector4_f32[0] * V2.vector4_f32[0] + V1.vector4_f32[1] * V2.vector4_f32[1];
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Perform the dot product on x and y
    float32x2_t vTemp = vmul_f32(vget_low_f32(V1), vget_low_f32(V2));
    vTemp = vpadd_f32(vTemp, vTemp);
    return vcombine_f32(vTemp, vTemp);
#elif defined(_MATH_SSE4_INTRINSICS_)
    return _mm_dp_ps(V1, V2, 0x3f);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vDot = _mm_mul_ps(V1, V2);
    vDot = _mm_hadd_ps(vDot, vDot);
    vDot = _mm_moveldup_ps(vDot);
    return vDot;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x and y
    Vector vLengthSq = _mm_mul_ps(V1, V2);
    // vTemp has y splatted
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 1, 1, 1));
    // x+y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector2Cross
(
    VectorArg V1,
    VectorArg V2
)noexcept{
    // [ V1.x*V2.y - V1.y*V2.x, V1.x*V2.y - V1.y*V2.x ]

#if defined(_MATH_NO_INTRINSICS_)
    float fCross = (V1.vector4_f32[0] * V2.vector4_f32[1]) - (V1.vector4_f32[1] * V2.vector4_f32[0]);
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = fCross;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Negate = { { { 1.f, -1.f, 0, 0 } } };

    float32x2_t vTemp = vmul_f32(vget_low_f32(V1), vrev64_f32(vget_low_f32(V2)));
    vTemp = vmul_f32(vTemp, vget_low_f32(Negate));
    vTemp = vpadd_f32(vTemp, vTemp);
    return vcombine_f32(vTemp, vTemp);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Swap x and y
    Vector vResult = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(0, 1, 0, 1));
    // Perform the muls
    vResult = _mm_mul_ps(vResult, V1);
    // Splat y
    Vector vTemp = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(1, 1, 1, 1));
    // Sub the values
    vResult = _mm_sub_ss(vResult, vTemp);
    // Splat the cross product
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(0, 0, 0, 0));
    return vResult;
#endif
}

inline Vector MathCallConv Vector2LengthSq(VectorArg V)noexcept{
    return Vector2Dot(V, V);
}

inline Vector MathCallConv Vector2ReciprocalLengthEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = Vector2LengthSq(V);
    Result = VectorReciprocalSqrtEst(Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    // Dot2
    float32x2_t vTemp = vmul_f32(VL, VL);
    vTemp = vpadd_f32(vTemp, vTemp);
    // Reciprocal sqrt (estimate)
    vTemp = vrsqrte_f32(vTemp);
    return vcombine_f32(vTemp, vTemp);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x3f);
    return _mm_rsqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_rsqrt_ss(vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x and y
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has y splatted
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 1, 1, 1));
    // x+y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = _mm_rsqrt_ss(vLengthSq);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector2ReciprocalLength(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = Vector2LengthSq(V);
    Result = VectorReciprocalSqrt(Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    // Dot2
    float32x2_t vTemp = vmul_f32(VL, VL);
    vTemp = vpadd_f32(vTemp, vTemp);
    // Reciprocal sqrt
    float32x2_t  S0 = vrsqrte_f32(vTemp);
    float32x2_t  P0 = vmul_f32(vTemp, S0);
    float32x2_t  R0 = vrsqrts_f32(P0, S0);
    float32x2_t  S1 = vmul_f32(S0, R0);
    float32x2_t  P1 = vmul_f32(vTemp, S1);
    float32x2_t  R1 = vrsqrts_f32(P1, S1);
    float32x2_t Result = vmul_f32(S1, R1);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x3f);
    Vector vLengthSq = _mm_sqrt_ps(vTemp);
    return _mm_div_ps(g_One, vLengthSq);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ss(vTemp);
    vLengthSq = _mm_div_ss(g_One, vLengthSq);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x and y
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has y splatted
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 1, 1, 1));
    // x+y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = _mm_sqrt_ss(vLengthSq);
    vLengthSq = _mm_div_ss(g_One, vLengthSq);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector2LengthEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = Vector2LengthSq(V);
    Result = VectorSqrtEst(Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    // Dot2
    float32x2_t vTemp = vmul_f32(VL, VL);
    vTemp = vpadd_f32(vTemp, vTemp);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(vTemp, zero);
    // Sqrt (estimate)
    float32x2_t Result = vrsqrte_f32(vTemp);
    Result = vmul_f32(vTemp, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x3f);
    return _mm_sqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ss(vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x and y
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has y splatted
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 1, 1, 1));
    // x+y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = _mm_sqrt_ss(vLengthSq);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector2Length(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = Vector2LengthSq(V);
    Result = VectorSqrt(Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    // Dot2
    float32x2_t vTemp = vmul_f32(VL, VL);
    vTemp = vpadd_f32(vTemp, vTemp);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(vTemp, zero);
    // Sqrt
    float32x2_t S0 = vrsqrte_f32(vTemp);
    float32x2_t P0 = vmul_f32(vTemp, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(vTemp, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    float32x2_t Result = vmul_f32(S1, R1);
    Result = vmul_f32(vTemp, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x3f);
    return _mm_sqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ss(vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x and y
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has y splatted
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 1, 1, 1));
    // x+y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#endif
}

// Vector2NormalizeEst uses a reciprocal estimate and
// returns QNaN on zero and infinite vectors.

inline Vector MathCallConv Vector2NormalizeEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = Vector2ReciprocalLength(V);
    Result = VectorMultiply(V, Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    // Dot2
    float32x2_t vTemp = vmul_f32(VL, VL);
    vTemp = vpadd_f32(vTemp, vTemp);
    // Reciprocal sqrt (estimate)
    vTemp = vrsqrte_f32(vTemp);
    // Normalize
    float32x2_t Result = vmul_f32(VL, vTemp);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x3f);
    Vector vResult = _mm_rsqrt_ps(vTemp);
    return _mm_mul_ps(vResult, V);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_rsqrt_ss(vLengthSq);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    vLengthSq = _mm_mul_ps(vLengthSq, V);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x and y
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has y splatted
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 1, 1, 1));
    // x+y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = _mm_rsqrt_ss(vLengthSq);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    vLengthSq = _mm_mul_ps(vLengthSq, V);
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector2Normalize(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector vResult = Vector2Length(V);
    float fLength = vResult.vector4_f32[0];

    // Prevent divide by zero
    if(fLength > 0){
        fLength = 1.0f / fLength;
    }

    vResult.vector4_f32[0] = V.vector4_f32[0] * fLength;
    vResult.vector4_f32[1] = V.vector4_f32[1] * fLength;
    vResult.vector4_f32[2] = V.vector4_f32[2] * fLength;
    vResult.vector4_f32[3] = V.vector4_f32[3] * fLength;
    return vResult;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    // Dot2
    float32x2_t vTemp = vmul_f32(VL, VL);
    vTemp = vpadd_f32(vTemp, vTemp);
    uint32x2_t VEqualsZero = vceq_f32(vTemp, vdup_n_f32(0));
    uint32x2_t VEqualsInf = vceq_f32(vTemp, vget_low_f32(g_Infinity));
    // Reciprocal sqrt (2 iterations of Newton-Raphson)
    float32x2_t S0 = vrsqrte_f32(vTemp);
    float32x2_t P0 = vmul_f32(vTemp, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(vTemp, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    vTemp = vmul_f32(S1, R1);
    // Normalize
    float32x2_t Result = vmul_f32(VL, vTemp);
    Result = vbsl_f32(VEqualsZero, vdup_n_f32(0), Result);
    Result = vbsl_f32(VEqualsInf, vget_low_f32(g_QNaN), Result);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vLengthSq = _mm_dp_ps(V, V, 0x3f);
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Reciprocal mul to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#elif defined(_MATH_SSE3_INTRINSICS_)
    // Perform the dot product on x and y only
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_moveldup_ps(vLengthSq);
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Reciprocal mul to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x and y only
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 1, 1, 1));
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Reciprocal mul to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#endif
}

inline Vector MathCallConv Vector2ClampLength
(
    VectorArg V,
    float    LengthMin,
    float    LengthMax
)noexcept{
    Vector ClampMax = VectorReplicate(LengthMax);
    Vector ClampMin = VectorReplicate(LengthMin);
    return Vector2ClampLengthV(V, ClampMin, ClampMax);
}

inline Vector MathCallConv Vector2ClampLengthV
(
    VectorArg V,
    VectorArg LengthMin,
    VectorArg LengthMax
)noexcept{
    assert((VectorGetY(LengthMin) == VectorGetX(LengthMin)));
    assert((VectorGetY(LengthMax) == VectorGetX(LengthMax)));
    assert(Vector2GreaterOrEqual(LengthMin, g_Zero));
    assert(Vector2GreaterOrEqual(LengthMax, g_Zero));
    assert(Vector2GreaterOrEqual(LengthMax, LengthMin));

    Vector LengthSq = Vector2LengthSq(V);

    const Vector Zero = VectorZero();

    Vector RcpLength = VectorReciprocalSqrt(LengthSq);

    Vector InfiniteLength = VectorEqualInt(LengthSq, g_Infinity.v);
    Vector ZeroLength = VectorEqual(LengthSq, Zero);

    Vector Length = VectorMultiply(LengthSq, RcpLength);

    Vector Normal = VectorMultiply(V, RcpLength);

    Vector Select = VectorEqualInt(InfiniteLength, ZeroLength);
    Length = VectorSelect(LengthSq, Length, Select);
    Normal = VectorSelect(LengthSq, Normal, Select);

    Vector ControlMax = VectorGreater(Length, LengthMax);
    Vector ControlMin = VectorLess(Length, LengthMin);

    Vector ClampLength = VectorSelect(Length, LengthMax, ControlMax);
    ClampLength = VectorSelect(ClampLength, LengthMin, ControlMin);

    Vector Result = VectorMultiply(Normal, ClampLength);

    // Preserve the original vector (with no precision loss) if the length falls within the given range
    Vector Control = VectorEqualInt(ControlMax, ControlMin);
    Result = VectorSelect(Result, V, Control);

    return Result;
}

inline Vector MathCallConv Vector2Reflect
(
    VectorArg Incident,
    VectorArg Normal
)noexcept{
    // Result = Incident - (2 * dot(Incident, Normal)) * Normal

    Vector Result;
    Result = Vector2Dot(Incident, Normal);
    Result = VectorAdd(Result, Result);
    Result = VectorNegativeMultiplySubtract(Result, Normal, Incident);
    return Result;
}

inline Vector MathCallConv Vector2Refract
(
    VectorArg Incident,
    VectorArg Normal,
    float    RefractionIndex
)noexcept{
    Vector Index = VectorReplicate(RefractionIndex);
    return Vector2RefractV(Incident, Normal, Index);
}

// Return the refraction of a 2D vector
inline Vector MathCallConv Vector2RefractV
(
    VectorArg Incident,
    VectorArg Normal,
    VectorArg RefractionIndex
)noexcept{
    // Result = RefractionIndex * Incident - Normal * (RefractionIndex * dot(Incident, Normal) +
    // sqrt(1 - RefractionIndex * RefractionIndex * (1 - dot(Incident, Normal) * dot(Incident, Normal))))

#if defined(_MATH_NO_INTRINSICS_)

    float IDotN = (Incident.vector4_f32[0] * Normal.vector4_f32[0]) + (Incident.vector4_f32[1] * Normal.vector4_f32[1]);
    // R = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    float RY = 1.0f - (IDotN * IDotN);
    float RX = 1.0f - (RY * RefractionIndex.vector4_f32[0] * RefractionIndex.vector4_f32[0]);
    RY = 1.0f - (RY * RefractionIndex.vector4_f32[1] * RefractionIndex.vector4_f32[1]);
    if(RX >= 0.0f){
        RX = (RefractionIndex.vector4_f32[0] * Incident.vector4_f32[0]) - (Normal.vector4_f32[0] * ((RefractionIndex.vector4_f32[0] * IDotN) + sqrtf(RX)));
    }else{
        RX = 0.0f;
    }
    if(RY >= 0.0f){
        RY = (RefractionIndex.vector4_f32[1] * Incident.vector4_f32[1]) - (Normal.vector4_f32[1] * ((RefractionIndex.vector4_f32[1] * IDotN) + sqrtf(RY)));
    }else{
        RY = 0.0f;
    }

    Vector vResult;
    vResult.vector4_f32[0] = RX;
    vResult.vector4_f32[1] = RY;
    vResult.vector4_f32[2] = 0.0f;
    vResult.vector4_f32[3] = 0.0f;
    return vResult;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t IL = vget_low_f32(Incident);
    float32x2_t NL = vget_low_f32(Normal);
    float32x2_t RIL = vget_low_f32(RefractionIndex);
    // Get the 2D Dot product of Incident-Normal
    float32x2_t vTemp = vmul_f32(IL, NL);
    float32x2_t IDotN = vpadd_f32(vTemp, vTemp);
    // vTemp = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    vTemp = vmls_f32(vget_low_f32(g_One), IDotN, IDotN);
    vTemp = vmul_f32(vTemp, RIL);
    vTemp = vmls_f32(vget_low_f32(g_One), vTemp, RIL);
    // If any terms are <=0, sqrt() will fail, punt to zero
    uint32x2_t vMask = vcgt_f32(vTemp, vget_low_f32(g_Zero));
    // Sqrt(vTemp)
    float32x2_t S0 = vrsqrte_f32(vTemp);
    float32x2_t P0 = vmul_f32(vTemp, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(vTemp, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    float32x2_t S2 = vmul_f32(S1, R1);
    vTemp = vmul_f32(vTemp, S2);
    // R = RefractionIndex * IDotN + sqrt(R)
    vTemp = vmla_f32(vTemp, RIL, IDotN);
    // Result = RefractionIndex * Incident - Normal * R
    float32x2_t vResult = vmul_f32(RIL, IL);
    vResult = vmls_f32(vResult, vTemp, NL);
    vResult = vreinterpret_f32_u32(vand_u32(vreinterpret_u32_f32(vResult), vMask));
    return vcombine_f32(vResult, vResult);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Result = RefractionIndex * Incident - Normal * (RefractionIndex * dot(Incident, Normal) +
    // sqrt(1 - RefractionIndex * RefractionIndex * (1 - dot(Incident, Normal) * dot(Incident, Normal))))
    // Get the 2D Dot product of Incident-Normal
    Vector IDotN = Vector2Dot(Incident, Normal);
    // vTemp = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    Vector vTemp = MATH_FNMADD_PS(IDotN, IDotN, g_One);
    vTemp = _mm_mul_ps(vTemp, RefractionIndex);
    vTemp = MATH_FNMADD_PS(vTemp, RefractionIndex, g_One);
    // If any terms are <=0, sqrt() will fail, punt to zero
    Vector vMask = _mm_cmpgt_ps(vTemp, g_Zero);
    // R = RefractionIndex * IDotN + sqrt(R)
    vTemp = _mm_sqrt_ps(vTemp);
    vTemp = MATH_FMADD_PS(RefractionIndex, IDotN, vTemp);
    // Result = RefractionIndex * Incident - Normal * R
    Vector vResult = _mm_mul_ps(RefractionIndex, Incident);
    vResult = MATH_FNMADD_PS(vTemp, Normal, vResult);
    vResult = _mm_and_ps(vResult, vMask);
    return vResult;
#endif
}

inline Vector MathCallConv Vector2Orthogonal(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            -V.vector4_f32[1],
            V.vector4_f32[0],
            0.f,
            0.f
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Negate = { { { -1.f, 1.f, 0, 0 } } };
    const float32x2_t zero = vdup_n_f32(0);

    float32x2_t VL = vget_low_f32(V);
    float32x2_t Result = vmul_f32(vrev64_f32(VL), vget_low_f32(Negate));
    return vcombine_f32(Result, zero);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 2, 0, 1));
    vResult = _mm_mul_ps(vResult, g_NegateX);
    return vResult;
#endif
}

inline Vector MathCallConv Vector2AngleBetweenNormalsEst
(
    VectorArg N1,
    VectorArg N2
)noexcept{
    Vector Result = Vector2Dot(N1, N2);
    Result = VectorClamp(Result, g_NegativeOne.v, g_One.v);
    Result = VectorACosEst(Result);
    return Result;
}

inline Vector MathCallConv Vector2AngleBetweenNormals
(
    VectorArg N1,
    VectorArg N2
)noexcept{
    Vector Result = Vector2Dot(N1, N2);
    Result = VectorClamp(Result, g_NegativeOne, g_One);
    Result = VectorACos(Result);
    return Result;
}

inline Vector MathCallConv Vector2AngleBetweenVectors
(
    VectorArg V1,
    VectorArg V2
)noexcept{
    Vector L1 = Vector2ReciprocalLength(V1);
    Vector L2 = Vector2ReciprocalLength(V2);

    Vector Dot = Vector2Dot(V1, V2);

    L1 = VectorMultiply(L1, L2);

    Vector CosAngle = VectorMultiply(Dot, L1);
    CosAngle = VectorClamp(CosAngle, g_NegativeOne.v, g_One.v);

    return VectorACos(CosAngle);
}

inline Vector MathCallConv Vector2LinePointDistance
(
    VectorArg LinePoint1,
    VectorArg LinePoint2,
    VectorArg Point
)noexcept{
    // Given a vector PointVector from LinePoint1 to Point and a vector
    // LineVector from LinePoint1 to LinePoint2, the scaled distance
    // PointProjectionScale from LinePoint1 to the perpendicular projection
    // of PointVector onto the line is defined as:
    //
    //     PointProjectionScale = dot(PointVector, LineVector) / LengthSq(LineVector)

    Vector PointVector = VectorSubtract(Point, LinePoint1);
    Vector LineVector = VectorSubtract(LinePoint2, LinePoint1);

    Vector LengthSq = Vector2LengthSq(LineVector);

    Vector PointProjectionScale = Vector2Dot(PointVector, LineVector);
    PointProjectionScale = VectorDivide(PointProjectionScale, LengthSq);

    Vector DistanceVector = VectorMultiply(LineVector, PointProjectionScale);
    DistanceVector = VectorSubtract(PointVector, DistanceVector);

    return Vector2Length(DistanceVector);
}

inline Vector MathCallConv Vector2IntersectLine
(
    VectorArg Line1Point1,
    VectorArg Line1Point2,
    VectorArg Line2Point1,
    VectorArg2 Line2Point2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    Vector V1 = VectorSubtract(Line1Point2, Line1Point1);
    Vector V2 = VectorSubtract(Line2Point2, Line2Point1);
    Vector V3 = VectorSubtract(Line1Point1, Line2Point1);

    Vector C1 = Vector2Cross(V1, V2);
    Vector C2 = Vector2Cross(V2, V3);

    Vector Result;
    const Vector Zero = VectorZero();
    if(Vector2NearEqual(C1, Zero, g_Epsilon.v)){
        if(Vector2NearEqual(C2, Zero, g_Epsilon.v)){
            // Coincident
            Result = g_Infinity.v;
        }else{
            // Parallel
            Result = g_QNaN.v;
        }
    }else{
        // Intersection point = Line1Point1 + V1 * (C2 / C1)
        Vector Scale = VectorReciprocal(C1);
        Scale = VectorMultiply(C2, Scale);
        Result = VectorMultiplyAdd(V1, Scale, Line1Point1);
    }

    return Result;

#elif defined(_MATH_SSE_INTRINSICS_)
    Vector V1 = _mm_sub_ps(Line1Point2, Line1Point1);
    Vector V2 = _mm_sub_ps(Line2Point2, Line2Point1);
    Vector V3 = _mm_sub_ps(Line1Point1, Line2Point1);
    // Generate the cross products
    Vector C1 = Vector2Cross(V1, V2);
    Vector C2 = Vector2Cross(V2, V3);
    // If C1 is not close to epsilon, use the calculated value
    Vector vResultMask = _mm_setzero_ps();
    vResultMask = _mm_sub_ps(vResultMask, C1);
    vResultMask = _mm_max_ps(vResultMask, C1);
    // 0xFFFFFFFF if the calculated value is to be used
    vResultMask = _mm_cmpgt_ps(vResultMask, g_Epsilon);
    // If C1 is close to epsilon, which fail type is it? INFINITY or NAN?
    Vector vFailMask = _mm_setzero_ps();
    vFailMask = _mm_sub_ps(vFailMask, C2);
    vFailMask = _mm_max_ps(vFailMask, C2);
    vFailMask = _mm_cmple_ps(vFailMask, g_Epsilon);
    Vector vFail = _mm_and_ps(vFailMask, g_Infinity);
    vFailMask = _mm_andnot_ps(vFailMask, g_QNaN);
    // vFail is NAN or INF
    vFail = _mm_or_ps(vFail, vFailMask);
    // Intersection point = Line1Point1 + V1 * (C2 / C1)
    Vector vResult = _mm_div_ps(C2, C1);
    vResult = MATH_FMADD_PS(vResult, V1, Line1Point1);
    // Use result, or failure value
    vResult = _mm_and_ps(vResult, vResultMask);
    vResultMask = _mm_andnot_ps(vResultMask, vFail);
    vResult = _mm_or_ps(vResult, vResultMask);
    return vResult;
#endif
}

inline Vector MathCallConv Vector2Transform
(
    VectorArg V,
    FXMMATRIX M
)noexcept{
    // `M.r[0..3]` are the matrix columns here: Result = M * [x y 0 1].
#if defined(_MATH_NO_INTRINSICS_)

    Vector Y = VectorSplatY(V);
    Vector X = VectorSplatX(V);

    Vector Result = VectorMultiplyAdd(Y, M.r[1], M.r[3]);
    Result = VectorMultiplyAdd(X, M.r[0], Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    float32x4_t Result = vmlaq_lane_f32(M.r[3], M.r[1], VL, 1); // Y
    return vmlaq_lane_f32(Result, M.r[0], VL, 0); // X
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1)); // Y
    vResult = MATH_FMADD_PS(vResult, M.r[1], M.r[3]);
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0)); // X
    vResult = MATH_FMADD_PS(vTemp, M.r[0], vResult);
    return vResult;
#endif
}

#include "vector_stream/source_math_vector_stream2.inl"

[[nodiscard]] inline Vector MathCallConv TransformCoordRefinedReciprocal(VectorArg W)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return VectorReciprocal(W);
#else
    Vector reciprocal = VectorReciprocalEst(W);
    reciprocal = VectorMultiply(reciprocal, VectorNegativeMultiplySubtract(W, reciprocal, g_Two.v));
    reciprocal = VectorMultiply(reciprocal, VectorNegativeMultiplySubtract(W, reciprocal, g_Two.v));
    return reciprocal;
#endif
}

inline Vector MathCallConv Vector2TransformCoord
(
    VectorArg V,
    FXMMATRIX M
)noexcept{
    // `M.r[0..3]` are the matrix columns here: Result = M * [x y 0 1], then divide by w.
    Vector Y = VectorSplatY(V);
    Vector X = VectorSplatX(V);

    Vector Result = VectorMultiplyAdd(Y, M.r[1], M.r[3]);
    Result = VectorMultiplyAdd(X, M.r[0], Result);

    Vector W = VectorSplatW(Result);
    return VectorMultiply(Result, TransformCoordRefinedReciprocal(W));
}

#include "vector_stream/source_math_vector2_transform_coord_stream.inl"

inline Vector MathCallConv Vector2TransformNormal
(
    VectorArg V,
    FXMMATRIX M
)noexcept{
    // Direction-vector path: use the first two matrix columns and ignore translation.
#if defined(_MATH_NO_INTRINSICS_)

    Vector Y = VectorSplatY(V);
    Vector X = VectorSplatX(V);

    Vector Result = VectorMultiply(Y, M.r[1]);
    Result = VectorMultiplyAdd(X, M.r[0], Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    float32x4_t Result = vmulq_lane_f32(M.r[1], VL, 1); // Y
    return vmlaq_lane_f32(Result, M.r[0], VL, 0); // X
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1)); // Y
    vResult = _mm_mul_ps(vResult, M.r[1]);
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0)); // X
    vResult = MATH_FMADD_PS(vTemp, M.r[0], vResult);
    return vResult;
#endif
}

#include "vector_stream/source_math_vector2_transform_normal_stream.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Comparison operations
inline bool MathCallConv Vector3Equal
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] == V2.vector4_f32[0]) && (V1.vector4_f32[1] == V2.vector4_f32[1]) && (V1.vector4_f32[2] == V2.vector4_f32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 7) == 7) != 0);
#endif
}

inline uint32_t MathCallConv Vector3EqualR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if((V1.vector4_f32[0] == V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] == V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] == V2.vector4_f32[2])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] != V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] != V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] != V2.vector4_f32[2])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU;

    uint32_t CR = 0;
    if(r == 0xFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    int iTest = _mm_movemask_ps(vTemp) & 7;
    uint32_t CR = 0;
    if(iTest == 7){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector3EqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_u32[0] == V2.vector4_u32[0]) && (V1.vector4_u32[1] == V2.vector4_u32[1]) && (V1.vector4_u32[2] == V2.vector4_u32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return (((_mm_movemask_ps(_mm_castsi128_ps(vTemp)) & 7) == 7) != 0);
#endif
}

inline uint32_t MathCallConv Vector3EqualIntR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if((V1.vector4_u32[0] == V2.vector4_u32[0]) &&
        (V1.vector4_u32[1] == V2.vector4_u32[1]) &&
        (V1.vector4_u32[2] == V2.vector4_u32[2])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_u32[0] != V2.vector4_u32[0]) &&
        (V1.vector4_u32[1] != V2.vector4_u32[1]) &&
        (V1.vector4_u32[2] != V2.vector4_u32[2])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU;

    uint32_t CR = 0;
    if(r == 0xFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    int iTest = _mm_movemask_ps(_mm_castsi128_ps(vTemp)) & 7;
    uint32_t CR = 0;
    if(iTest == 7){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector3NearEqual
(
    VectorArg V1,
    VectorArg V2,
    VectorArg Epsilon
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float dx = fabsf(V1.vector4_f32[0] - V2.vector4_f32[0]);
    float dy = fabsf(V1.vector4_f32[1] - V2.vector4_f32[1]);
    float dz = fabsf(V1.vector4_f32[2] - V2.vector4_f32[2]);
    return ((dx <= Epsilon.vector4_f32[0]) &&
        (dy <= Epsilon.vector4_f32[1]) &&
        (dz <= Epsilon.vector4_f32[2]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vDelta = vsubq_f32(V1, V2);
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    uint32x4_t vResult = vacleq_f32(vDelta, Epsilon);
#else
    uint32x4_t vResult = vcleq_f32(vabsq_f32(vDelta), Epsilon);
#endif
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Get the difference
    Vector vDelta = _mm_sub_ps(V1, V2);
    // Get the absolute value of the difference
    Vector vTemp = _mm_setzero_ps();
    vTemp = _mm_sub_ps(vTemp, vDelta);
    vTemp = _mm_max_ps(vTemp, vDelta);
    vTemp = _mm_cmple_ps(vTemp, Epsilon);
    // w is don't care
    return (((_mm_movemask_ps(vTemp) & 7) == 0x7) != 0);
#endif
}

inline bool MathCallConv Vector3NotEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] != V2.vector4_f32[0]) || (V1.vector4_f32[1] != V2.vector4_f32[1]) || (V1.vector4_f32[2] != V2.vector4_f32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) != 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 7) != 7) != 0);
#endif
}

inline bool MathCallConv Vector3NotEqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_u32[0] != V2.vector4_u32[0]) || (V1.vector4_u32[1] != V2.vector4_u32[1]) || (V1.vector4_u32[2] != V2.vector4_u32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) != 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return (((_mm_movemask_ps(_mm_castsi128_ps(vTemp)) & 7) != 7) != 0);
#endif
}

inline bool MathCallConv Vector3Greater
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] > V2.vector4_f32[0]) && (V1.vector4_f32[1] > V2.vector4_f32[1]) && (V1.vector4_f32[2] > V2.vector4_f32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgtq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpgt_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 7) == 7) != 0);
#endif
}

inline uint32_t MathCallConv Vector3GreaterR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if((V1.vector4_f32[0] > V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] > V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] > V2.vector4_f32[2])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] <= V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] <= V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] <= V2.vector4_f32[2])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgtq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU;

    uint32_t CR = 0;
    if(r == 0xFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpgt_ps(V1, V2);
    uint32_t CR = 0;
    int iTest = _mm_movemask_ps(vTemp) & 7;
    if(iTest == 7){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector3GreaterOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] >= V2.vector4_f32[0]) && (V1.vector4_f32[1] >= V2.vector4_f32[1]) && (V1.vector4_f32[2] >= V2.vector4_f32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgeq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpge_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 7) == 7) != 0);
#endif
}

inline uint32_t MathCallConv Vector3GreaterOrEqualR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if((V1.vector4_f32[0] >= V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] >= V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] >= V2.vector4_f32[2])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] < V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] < V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] < V2.vector4_f32[2])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgeq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU;

    uint32_t CR = 0;
    if(r == 0xFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpge_ps(V1, V2);
    uint32_t CR = 0;
    int iTest = _mm_movemask_ps(vTemp) & 7;
    if(iTest == 7){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector3Less
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] < V2.vector4_f32[0]) && (V1.vector4_f32[1] < V2.vector4_f32[1]) && (V1.vector4_f32[2] < V2.vector4_f32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcltq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmplt_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 7) == 7) != 0);
#endif
}

inline bool MathCallConv Vector3LessOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] <= V2.vector4_f32[0]) && (V1.vector4_f32[1] <= V2.vector4_f32[1]) && (V1.vector4_f32[2] <= V2.vector4_f32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcleq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmple_ps(V1, V2);
    return (((_mm_movemask_ps(vTemp) & 7) == 7) != 0);
#endif
}

inline bool MathCallConv Vector3InBounds
(
    VectorArg V,
    VectorArg Bounds
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V.vector4_f32[0] <= Bounds.vector4_f32[0] && V.vector4_f32[0] >= -Bounds.vector4_f32[0]) &&
        (V.vector4_f32[1] <= Bounds.vector4_f32[1] && V.vector4_f32[1] >= -Bounds.vector4_f32[1]) &&
        (V.vector4_f32[2] <= Bounds.vector4_f32[2] && V.vector4_f32[2] >= -Bounds.vector4_f32[2])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Test if less than or equal
    uint32x4_t ivTemp1 = vcleq_f32(V, Bounds);
    // Negate the bounds
    float32x4_t vTemp2 = vnegq_f32(Bounds);
    // Test if greater or equal (Reversed)
    uint32x4_t ivTemp2 = vcleq_f32(vTemp2, V);
    // Blend answers
    ivTemp1 = vandq_u32(ivTemp1, ivTemp2);
    // in bounds?
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(ivTemp1)), vget_high_u8(vreinterpretq_u8_u32(ivTemp1)));
    uint16x4x2_t vTemp3 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp3.val[1]), 1) & 0xFFFFFFU) == 0xFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Test if less than or equal
    Vector vTemp1 = _mm_cmple_ps(V, Bounds);
    // Negate the bounds
    Vector vTemp2 = _mm_mul_ps(Bounds, g_NegativeOne);
    // Test if greater or equal (Reversed)
    vTemp2 = _mm_cmple_ps(vTemp2, V);
    // Blend answers
    vTemp1 = _mm_and_ps(vTemp1, vTemp2);
    // x, y, and z in bounds? (w is don't care)
    return (((_mm_movemask_ps(vTemp1) & 0x7) == 0x7) != 0);
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(push)
#pragma float_control(precise, on)
#endif

inline bool MathCallConv Vector3IsNaN(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (MATH_IS_NAN(V.vector4_f32[0]) ||
        MATH_IS_NAN(V.vector4_f32[1]) ||
        MATH_IS_NAN(V.vector4_f32[2]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    return isnan(vgetq_lane_f32(V, 0)) || isnan(vgetq_lane_f32(V, 1)) || isnan(vgetq_lane_f32(V, 2));
#else
    // Test against itself. NaN is always not equal
    uint32x4_t vTempNan = vceqq_f32(V, V);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vTempNan)), vget_high_u8(vreinterpretq_u8_u32(vTempNan)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    // If x or y or z are NaN, the mask is zero
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) != 0xFFFFFFU);
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    MATH_ALIGNED_DATA(16) float tmp[4];
    _mm_store_ps(tmp, V);
    return isnan(tmp[0]) || isnan(tmp[1]) || isnan(tmp[2]);
#else
    // Test against itself. NaN is always not equal
    Vector vTempNan = _mm_cmpneq_ps(V, V);
    // If x or y or z are NaN, the mask is non-zero
    return ((_mm_movemask_ps(vTempNan) & 7) != 0);
#endif
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(pop)
#endif

inline bool MathCallConv Vector3IsInfinite(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (MATH_IS_INF(V.vector4_f32[0]) ||
        MATH_IS_INF(V.vector4_f32[1]) ||
        MATH_IS_INF(V.vector4_f32[2]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Mask off the sign bit
    uint32x4_t vTempInf = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    // Compare to infinity
    vTempInf = vceqq_f32(vreinterpretq_f32_u32(vTempInf), g_Infinity);
    // If any are infinity, the signs are true.
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vTempInf)), vget_high_u8(vreinterpretq_u8_u32(vTempInf)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return ((vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) & 0xFFFFFFU) != 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Mask off the sign bit
    __m128 vTemp = _mm_and_ps(V, g_AbsMask);
    // Compare to infinity
    vTemp = _mm_cmpeq_ps(vTemp, g_Infinity);
    // If x,y or z are infinity, the signs are true.
    return ((_mm_movemask_ps(vTemp) & 7) != 0);
#endif
}

// Computation operations
inline Vector MathCallConv Vector3Dot
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float fValue = V1.vector4_f32[0] * V2.vector4_f32[0] + V1.vector4_f32[1] * V2.vector4_f32[1] + V1.vector4_f32[2] * V2.vector4_f32[2];
    VectorF32 vResult;
    vResult.f[0] =
        vResult.f[1] =
        vResult.f[2] =
        vResult.f[3] = fValue;
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vTemp = vmulq_f32(V1, V2);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    return vcombine_f32(v1, v1);
#elif defined(_MATH_SSE4_INTRINSICS_)
    return _mm_dp_ps(V1, V2, 0x7f);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vTemp = _mm_mul_ps(V1, V2);
    vTemp = _mm_and_ps(vTemp, g_Mask3);
    vTemp = _mm_hadd_ps(vTemp, vTemp);
    return _mm_hadd_ps(vTemp, vTemp);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product
    Vector vDot = _mm_mul_ps(V1, V2);
    // x=Dot.vector4_f32[1], y=Dot.vector4_f32[2]
    Vector vTemp = MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(2, 1, 2, 1));
    // Result.vector4_f32[0] = x+y
    vDot = _mm_add_ss(vDot, vTemp);
    // x=Dot.vector4_f32[2]
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    // Result.vector4_f32[0] = (x+y)+z
    vDot = _mm_add_ss(vDot, vTemp);
    // Splat x
    return MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(0, 0, 0, 0));
#endif
}

inline Vector MathCallConv Vector3Cross
(
    VectorArg V1,
    VectorArg V2
)noexcept{
    // [ V1.y*V2.z - V1.z*V2.y, V1.z*V2.x - V1.x*V2.z, V1.x*V2.y - V1.y*V2.x ]

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult = { { {
            (V1.vector4_f32[1] * V2.vector4_f32[2]) - (V1.vector4_f32[2] * V2.vector4_f32[1]),
            (V1.vector4_f32[2] * V2.vector4_f32[0]) - (V1.vector4_f32[0] * V2.vector4_f32[2]),
            (V1.vector4_f32[0] * V2.vector4_f32[1]) - (V1.vector4_f32[1] * V2.vector4_f32[0]),
            0.0f
        } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t v1xy = vget_low_f32(V1);
    float32x2_t v2xy = vget_low_f32(V2);

    float32x2_t v1yx = vrev64_f32(v1xy);
    float32x2_t v2yx = vrev64_f32(v2xy);

    float32x2_t v1zz = vdup_lane_f32(vget_high_f32(V1), 0);
    float32x2_t v2zz = vdup_lane_f32(vget_high_f32(V2), 0);

    Vector vResult = vmulq_f32(vcombine_f32(v1yx, v1xy), vcombine_f32(v2zz, v2yx));
    vResult = vmlsq_f32(vResult, vcombine_f32(v1zz, v1yx), vcombine_f32(v2yx, v2xy));
    vResult = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(vResult), g_FlipY));
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vResult), g_Mask3));
#elif defined(_MATH_SSE_INTRINSICS_)
    // y1,z1,x1,w1
    Vector vTemp1 = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(3, 0, 2, 1));
    // z2,x2,y2,w2
    Vector vTemp2 = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(3, 1, 0, 2));
    // Perform the left operation
    Vector vResult = _mm_mul_ps(vTemp1, vTemp2);
    // z1,x1,y1,w1
    vTemp1 = MATH_PERMUTE_PS(vTemp1, _MM_SHUFFLE(3, 0, 2, 1));
    // y2,z2,x2,w2
    vTemp2 = MATH_PERMUTE_PS(vTemp2, _MM_SHUFFLE(3, 1, 0, 2));
    // Perform the right operation
    vResult = MATH_FNMADD_PS(vTemp1, vTemp2, vResult);
    // Set w to zero
    return _mm_and_ps(vResult, g_Mask3);
#endif
}

inline Vector MathCallConv Vector3LengthSq(VectorArg V)noexcept{
    return Vector3Dot(V, V);
}

inline Vector MathCallConv Vector3ReciprocalLengthEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;

    Result = Vector3LengthSq(V);
    Result = VectorReciprocalSqrtEst(Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot3
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    // Reciprocal sqrt (estimate)
    v2 = vrsqrte_f32(v1);
    return vcombine_f32(v2, v2);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x7f);
    return _mm_rsqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_and_ps(vLengthSq, g_Mask3);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_rsqrt_ps(vLengthSq);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y and z
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has z and y
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 2, 1, 2));
    // x+z, y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    // y,y,y,y
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    // x+z+y,??,??,??
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    // Splat the length squared
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    // Get the reciprocal
    vLengthSq = _mm_rsqrt_ps(vLengthSq);
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector3ReciprocalLength(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;

    Result = Vector3LengthSq(V);
    Result = VectorReciprocalSqrt(Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot3
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    // Reciprocal sqrt
    float32x2_t  S0 = vrsqrte_f32(v1);
    float32x2_t  P0 = vmul_f32(v1, S0);
    float32x2_t  R0 = vrsqrts_f32(P0, S0);
    float32x2_t  S1 = vmul_f32(S0, R0);
    float32x2_t  P1 = vmul_f32(v1, S1);
    float32x2_t  R1 = vrsqrts_f32(P1, S1);
    float32x2_t Result = vmul_f32(S1, R1);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x7f);
    Vector vLengthSq = _mm_sqrt_ps(vTemp);
    return _mm_div_ps(g_One, vLengthSq);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vDot = _mm_mul_ps(V, V);
    vDot = _mm_and_ps(vDot, g_Mask3);
    vDot = _mm_hadd_ps(vDot, vDot);
    vDot = _mm_hadd_ps(vDot, vDot);
    vDot = _mm_sqrt_ps(vDot);
    vDot = _mm_div_ps(g_One, vDot);
    return vDot;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product
    Vector vDot = _mm_mul_ps(V, V);
    // x=Dot.y, y=Dot.z
    Vector vTemp = MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(2, 1, 2, 1));
    // Result.x = x+y
    vDot = _mm_add_ss(vDot, vTemp);
    // x=Dot.z
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    // Result.x = (x+y)+z
    vDot = _mm_add_ss(vDot, vTemp);
    // Splat x
    vDot = MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(0, 0, 0, 0));
    // Get the reciprocal
    vDot = _mm_sqrt_ps(vDot);
    // Get the reciprocal
    vDot = _mm_div_ps(g_One, vDot);
    return vDot;
#endif
}

inline Vector MathCallConv Vector3LengthEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;

    Result = Vector3LengthSq(V);
    Result = VectorSqrtEst(Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot3
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(v1, zero);
    // Sqrt (estimate)
    float32x2_t Result = vrsqrte_f32(v1);
    Result = vmul_f32(v1, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x7f);
    return _mm_sqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_and_ps(vLengthSq, g_Mask3);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y and z
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has z and y
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 2, 1, 2));
    // x+z, y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    // y,y,y,y
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    // x+z+y,??,??,??
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    // Splat the length squared
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    // Get the length
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector3Length(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;

    Result = Vector3LengthSq(V);
    Result = VectorSqrt(Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot3
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(v1, zero);
    // Sqrt
    float32x2_t S0 = vrsqrte_f32(v1);
    float32x2_t P0 = vmul_f32(v1, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(v1, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    float32x2_t Result = vmul_f32(S1, R1);
    Result = vmul_f32(v1, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x7f);
    return _mm_sqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_and_ps(vLengthSq, g_Mask3);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y and z
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has z and y
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 2, 1, 2));
    // x+z, y
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    // y,y,y,y
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    // x+z+y,??,??,??
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    // Splat the length squared
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    // Get the length
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#endif
}

// Vector3NormalizeEst uses a reciprocal estimate and
// returns QNaN on zero and infinite vectors.

inline Vector MathCallConv Vector3NormalizeEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = Vector3ReciprocalLength(V);
    Result = VectorMultiply(V, Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot3
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    // Reciprocal sqrt (estimate)
    v2 = vrsqrte_f32(v1);
    // Normalize
    return vmulq_f32(V, vcombine_f32(v2, v2));
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0x7f);
    Vector vResult = _mm_rsqrt_ps(vTemp);
    return _mm_mul_ps(vResult, V);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vDot = _mm_mul_ps(V, V);
    vDot = _mm_and_ps(vDot, g_Mask3);
    vDot = _mm_hadd_ps(vDot, vDot);
    vDot = _mm_hadd_ps(vDot, vDot);
    vDot = _mm_rsqrt_ps(vDot);
    vDot = _mm_mul_ps(vDot, V);
    return vDot;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product
    Vector vDot = _mm_mul_ps(V, V);
    // x=Dot.y, y=Dot.z
    Vector vTemp = MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(2, 1, 2, 1));
    // Result.x = x+y
    vDot = _mm_add_ss(vDot, vTemp);
    // x=Dot.z
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    // Result.x = (x+y)+z
    vDot = _mm_add_ss(vDot, vTemp);
    // Splat x
    vDot = MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(0, 0, 0, 0));
    // Get the reciprocal
    vDot = _mm_rsqrt_ps(vDot);
    // Perform the normalization
    vDot = _mm_mul_ps(vDot, V);
    return vDot;
#endif
}

inline Vector MathCallConv Vector3Normalize(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float fLength;
    Vector vResult;

    vResult = Vector3Length(V);
    fLength = vResult.vector4_f32[0];

    // Prevent divide by zero
    if(fLength > 0){
        fLength = 1.0f / fLength;
    }

    vResult.vector4_f32[0] = V.vector4_f32[0] * fLength;
    vResult.vector4_f32[1] = V.vector4_f32[1] * fLength;
    vResult.vector4_f32[2] = V.vector4_f32[2] * fLength;
    vResult.vector4_f32[3] = V.vector4_f32[3] * fLength;
    return vResult;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot3
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    uint32x2_t VEqualsZero = vceq_f32(v1, vdup_n_f32(0));
    uint32x2_t VEqualsInf = vceq_f32(v1, vget_low_f32(g_Infinity));
    // Reciprocal sqrt (2 iterations of Newton-Raphson)
    float32x2_t S0 = vrsqrte_f32(v1);
    float32x2_t P0 = vmul_f32(v1, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(v1, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    v2 = vmul_f32(S1, R1);
    // Normalize
    Vector vResult = vmulq_f32(V, vcombine_f32(v2, v2));
    vResult = vbslq_f32(vcombine_u32(VEqualsZero, VEqualsZero), vdupq_n_f32(0), vResult);
    return vbslq_f32(vcombine_u32(VEqualsInf, VEqualsInf), g_QNaN, vResult);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vLengthSq = _mm_dp_ps(V, V, 0x7f);
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Divide to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#elif defined(_MATH_SSE3_INTRINSICS_)
    // Perform the dot product on x,y and z only
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_and_ps(vLengthSq, g_Mask3);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Divide to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y and z only
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 1, 2, 1));
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Divide to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#endif
}

inline Vector MathCallConv Vector3ClampLength
(
    VectorArg V,
    float    LengthMin,
    float    LengthMax
)noexcept{
    Vector ClampMax = VectorReplicate(LengthMax);
    Vector ClampMin = VectorReplicate(LengthMin);

    return Vector3ClampLengthV(V, ClampMin, ClampMax);
}

inline Vector MathCallConv Vector3ClampLengthV
(
    VectorArg V,
    VectorArg LengthMin,
    VectorArg LengthMax
)noexcept{
    assert((VectorGetY(LengthMin) == VectorGetX(LengthMin)) && (VectorGetZ(LengthMin) == VectorGetX(LengthMin)));
    assert((VectorGetY(LengthMax) == VectorGetX(LengthMax)) && (VectorGetZ(LengthMax) == VectorGetX(LengthMax)));
    assert(Vector3GreaterOrEqual(LengthMin, VectorZero()));
    assert(Vector3GreaterOrEqual(LengthMax, VectorZero()));
    assert(Vector3GreaterOrEqual(LengthMax, LengthMin));

    Vector LengthSq = Vector3LengthSq(V);

    const Vector Zero = VectorZero();

    Vector RcpLength = VectorReciprocalSqrt(LengthSq);

    Vector InfiniteLength = VectorEqualInt(LengthSq, g_Infinity.v);
    Vector ZeroLength = VectorEqual(LengthSq, Zero);

    Vector Normal = VectorMultiply(V, RcpLength);

    Vector Length = VectorMultiply(LengthSq, RcpLength);

    Vector Select = VectorEqualInt(InfiniteLength, ZeroLength);
    Length = VectorSelect(LengthSq, Length, Select);
    Normal = VectorSelect(LengthSq, Normal, Select);

    Vector ControlMax = VectorGreater(Length, LengthMax);
    Vector ControlMin = VectorLess(Length, LengthMin);

    Vector ClampLength = VectorSelect(Length, LengthMax, ControlMax);
    ClampLength = VectorSelect(ClampLength, LengthMin, ControlMin);

    Vector Result = VectorMultiply(Normal, ClampLength);

    // Preserve the original vector (with no precision loss) if the length falls within the given range
    Vector Control = VectorEqualInt(ControlMax, ControlMin);
    Result = VectorSelect(Result, V, Control);

    return Result;
}

inline Vector MathCallConv Vector3Reflect
(
    VectorArg Incident,
    VectorArg Normal
)noexcept{
    // Result = Incident - (2 * dot(Incident, Normal)) * Normal

    Vector Result = Vector3Dot(Incident, Normal);
    Result = VectorAdd(Result, Result);
    Result = VectorNegativeMultiplySubtract(Result, Normal, Incident);

    return Result;
}

inline Vector MathCallConv Vector3Refract
(
    VectorArg Incident,
    VectorArg Normal,
    float    RefractionIndex
)noexcept{
    Vector Index = VectorReplicate(RefractionIndex);
    return Vector3RefractV(Incident, Normal, Index);
}

inline Vector MathCallConv Vector3RefractV
(
    VectorArg Incident,
    VectorArg Normal,
    VectorArg RefractionIndex
)noexcept{
    // Result = RefractionIndex * Incident - Normal * (RefractionIndex * dot(Incident, Normal) +
    // sqrt(1 - RefractionIndex * RefractionIndex * (1 - dot(Incident, Normal) * dot(Incident, Normal))))

#if defined(_MATH_NO_INTRINSICS_)

    const Vector  Zero = VectorZero();

    Vector IDotN = Vector3Dot(Incident, Normal);

    // R = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    Vector R = VectorNegativeMultiplySubtract(IDotN, IDotN, g_One.v);
    R = VectorMultiply(R, RefractionIndex);
    R = VectorNegativeMultiplySubtract(R, RefractionIndex, g_One.v);

    if(Vector4LessOrEqual(R, Zero)){
        // Total internal reflection
        return Zero;
    }else{
        // R = RefractionIndex * IDotN + sqrt(R)
        R = VectorSqrt(R);
        R = VectorMultiplyAdd(RefractionIndex, IDotN, R);

        // Result = RefractionIndex * Incident - Normal * R
        Vector Result = VectorMultiply(RefractionIndex, Incident);
        Result = VectorNegativeMultiplySubtract(Normal, R, Result);

        return Result;
    }

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector IDotN = Vector3Dot(Incident, Normal);

    // R = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    float32x4_t R = vmlsq_f32(g_One, IDotN, IDotN);
    R = vmulq_f32(R, RefractionIndex);
    R = vmlsq_f32(g_One, R, RefractionIndex);

    uint32x4_t isrzero = vcleq_f32(R, g_Zero);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(isrzero)), vget_high_u8(vreinterpretq_u8_u32(isrzero)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));

    float32x4_t vResult;
    if(vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU){
        // Total internal reflection
        vResult = g_Zero;
    }else{
        // Sqrt(R)
        float32x4_t S0 = vrsqrteq_f32(R);
        float32x4_t P0 = vmulq_f32(R, S0);
        float32x4_t R0 = vrsqrtsq_f32(P0, S0);
        float32x4_t S1 = vmulq_f32(S0, R0);
        float32x4_t P1 = vmulq_f32(R, S1);
        float32x4_t R1 = vrsqrtsq_f32(P1, S1);
        float32x4_t S2 = vmulq_f32(S1, R1);
        R = vmulq_f32(R, S2);
        // R = RefractionIndex * IDotN + sqrt(R)
        R = vmlaq_f32(R, RefractionIndex, IDotN);
        // Result = RefractionIndex * Incident - Normal * R
        vResult = vmulq_f32(RefractionIndex, Incident);
        vResult = vmlsq_f32(vResult, R, Normal);
    }
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Result = RefractionIndex * Incident - Normal * (RefractionIndex * dot(Incident, Normal) +
    // sqrt(1 - RefractionIndex * RefractionIndex * (1 - dot(Incident, Normal) * dot(Incident, Normal))))
    Vector IDotN = Vector3Dot(Incident, Normal);
    // R = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    Vector R = MATH_FNMADD_PS(IDotN, IDotN, g_One);
    Vector R2 = _mm_mul_ps(RefractionIndex, RefractionIndex);
    R = MATH_FNMADD_PS(R, R2, g_One);

    Vector vResult = _mm_cmple_ps(R, g_Zero);
    if(_mm_movemask_ps(vResult) == 0x0f){
        // Total internal reflection
        vResult = g_Zero;
    }else{
        // R = RefractionIndex * IDotN + sqrt(R)
        R = _mm_sqrt_ps(R);
        R = MATH_FMADD_PS(RefractionIndex, IDotN, R);
        // Result = RefractionIndex * Incident - Normal * R
        vResult = _mm_mul_ps(RefractionIndex, Incident);
        vResult = MATH_FNMADD_PS(R, Normal, vResult);
    }
    return vResult;
#endif
}

inline Vector MathCallConv Vector3Orthogonal(VectorArg V)noexcept{
    Vector Zero = VectorZero();
    Vector Z = VectorSplatZ(V);
    Vector YZYY = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_Z, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(V);

    Vector NegativeV = VectorSubtract(Zero, V);

    Vector ZIsNegative = VectorLess(Z, Zero);
    Vector YZYYIsNegative = VectorLess(YZYY, Zero);

    Vector S = VectorAdd(YZYY, Z);
    Vector D = VectorSubtract(YZYY, Z);

    Vector Select = VectorEqualInt(ZIsNegative, YZYYIsNegative);

    Vector R0 = VectorPermute<MATH_PERMUTE_1X, MATH_PERMUTE_0X, MATH_PERMUTE_0X, MATH_PERMUTE_0X>(NegativeV, S);
    Vector R1 = VectorPermute<MATH_PERMUTE_1X, MATH_PERMUTE_0X, MATH_PERMUTE_0X, MATH_PERMUTE_0X>(V, D);

    return VectorSelect(R1, R0, Select);
}

inline Vector MathCallConv Vector3AngleBetweenNormalsEst
(
    VectorArg N1,
    VectorArg N2
)noexcept{
    Vector Result = Vector3Dot(N1, N2);
    Result = VectorClamp(Result, g_NegativeOne.v, g_One.v);
    Result = VectorACosEst(Result);
    return Result;
}

inline Vector MathCallConv Vector3AngleBetweenNormals
(
    VectorArg N1,
    VectorArg N2
)noexcept{
    Vector Result = Vector3Dot(N1, N2);
    Result = VectorClamp(Result, g_NegativeOne.v, g_One.v);
    Result = VectorACos(Result);
    return Result;
}

inline Vector MathCallConv Vector3AngleBetweenVectors
(
    VectorArg V1,
    VectorArg V2
)noexcept{
    Vector L1 = Vector3ReciprocalLength(V1);
    Vector L2 = Vector3ReciprocalLength(V2);

    Vector Dot = Vector3Dot(V1, V2);

    L1 = VectorMultiply(L1, L2);

    Vector CosAngle = VectorMultiply(Dot, L1);
    CosAngle = VectorClamp(CosAngle, g_NegativeOne.v, g_One.v);

    return VectorACos(CosAngle);
}

inline Vector MathCallConv Vector3LinePointDistance
(
    VectorArg LinePoint1,
    VectorArg LinePoint2,
    VectorArg Point
)noexcept{
    // Given a vector PointVector from LinePoint1 to Point and a vector
    // LineVector from LinePoint1 to LinePoint2, the scaled distance
    // PointProjectionScale from LinePoint1 to the perpendicular projection
    // of PointVector onto the line is defined as:
    //
    //     PointProjectionScale = dot(PointVector, LineVector) / LengthSq(LineVector)

    Vector PointVector = VectorSubtract(Point, LinePoint1);
    Vector LineVector = VectorSubtract(LinePoint2, LinePoint1);

    Vector LengthSq = Vector3LengthSq(LineVector);

    Vector PointProjectionScale = Vector3Dot(PointVector, LineVector);
    PointProjectionScale = VectorDivide(PointProjectionScale, LengthSq);

    Vector DistanceVector = VectorMultiply(LineVector, PointProjectionScale);
    DistanceVector = VectorSubtract(PointVector, DistanceVector);

    return Vector3Length(DistanceVector);
}

_Use_decl_annotations_
inline void MathCallConv Vector3ComponentsFromNormal
(
    Vector* pParallel,
    Vector* pPerpendicular,
    VectorArg  V,
    VectorArg  Normal
)noexcept{
    assert(pParallel != nullptr);
    assert(pPerpendicular != nullptr);

    Vector Scale = Vector3Dot(V, Normal);

    Vector Parallel = VectorMultiply(Normal, Scale);

    *pParallel = Parallel;
    *pPerpendicular = VectorSubtract(V, Parallel);
}

// Transform a vector using a rotation expressed as a unit quaternion

inline Vector MathCallConv Vector3Rotate
(
    VectorArg V,
    VectorArg RotationQuaternion
)noexcept{
    Vector A = VectorSelect(g_Select1110.v, V, g_Select1110.v);
    Vector Q = QuaternionConjugate(RotationQuaternion);
    Vector Result = QuaternionMultiply(Q, A);
    return QuaternionMultiply(Result, RotationQuaternion);
}

// Transform a vector using the inverse of a rotation expressed as a unit quaternion

inline Vector MathCallConv Vector3InverseRotate
(
    VectorArg V,
    VectorArg RotationQuaternion
)noexcept{
    Vector A = VectorSelect(g_Select1110.v, V, g_Select1110.v);
    Vector Result = QuaternionMultiply(RotationQuaternion, A);
    Vector Q = QuaternionConjugate(RotationQuaternion);
    return QuaternionMultiply(Result, Q);
}

inline Vector MathCallConv Vector3Transform
(
    VectorArg V,
    FXMMATRIX M
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Z = VectorSplatZ(V);
    Vector Y = VectorSplatY(V);
    Vector X = VectorSplatX(V);

    Vector Result = VectorMultiplyAdd(Z, M.r[2], M.r[3]);
    Result = VectorMultiplyAdd(Y, M.r[1], Result);
    Result = VectorMultiplyAdd(X, M.r[0], Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    Vector vResult = vmlaq_lane_f32(M.r[3], M.r[0], VL, 0); // X
    vResult = vmlaq_lane_f32(vResult, M.r[1], VL, 1); // Y
    return vmlaq_lane_f32(vResult, M.r[2], vget_high_f32(V), 0); // Z
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2)); // Z
    vResult = MATH_FMADD_PS(vResult, M.r[2], M.r[3]);
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1)); // Y
    vResult = MATH_FMADD_PS(vTemp, M.r[1], vResult);
    vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0)); // X
    vResult = MATH_FMADD_PS(vTemp, M.r[0], vResult);
    return vResult;
#endif
}

#include "vector_stream/source_math_vector_stream3.inl"

inline Vector MathCallConv Vector3TransformCoord
(
    VectorArg V,
    FXMMATRIX M
)noexcept{
    // `M.r[0..3]` are the four matrix columns here: Result = M * [x y z 1].
    Vector Z = VectorSplatZ(V);
    Vector Y = VectorSplatY(V);
    Vector X = VectorSplatX(V);

    Vector Result = VectorMultiplyAdd(Z, M.r[2], M.r[3]);
    Result = VectorMultiplyAdd(Y, M.r[1], Result);
    Result = VectorMultiplyAdd(X, M.r[0], Result);

    Vector W = VectorSplatW(Result);
    return VectorMultiply(Result, TransformCoordRefinedReciprocal(W));
}

#include "vector_stream/source_math_vector3_transform_coord_stream.inl"

inline Vector MathCallConv Vector3TransformNormal
(
    VectorArg V,
    FXMMATRIX M
)noexcept{
    // Direction-vector path: use the first three matrix columns and ignore translation.
#if defined(_MATH_NO_INTRINSICS_)

    Vector Z = VectorSplatZ(V);
    Vector Y = VectorSplatY(V);
    Vector X = VectorSplatX(V);

    Vector Result = VectorMultiply(Z, M.r[2]);
    Result = VectorMultiplyAdd(Y, M.r[1], Result);
    Result = VectorMultiplyAdd(X, M.r[0], Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    Vector vResult = vmulq_lane_f32(M.r[0], VL, 0); // X
    vResult = vmlaq_lane_f32(vResult, M.r[1], VL, 1); // Y
    return vmlaq_lane_f32(vResult, M.r[2], vget_high_f32(V), 0); // Z
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2)); // Z
    vResult = _mm_mul_ps(vResult, M.r[2]);
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1)); // Y
    vResult = MATH_FMADD_PS(vTemp, M.r[1], vResult);
    vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0)); // X
    vResult = MATH_FMADD_PS(vTemp, M.r[0], vResult);
    return vResult;
#endif
}

#include "vector_stream/source_math_vector3_transform_normal_stream.inl"

inline Matrix MathCallConv BuildProjectTransformColumns
(
    FXMMATRIX Projection,
    CXMMATRIX View,
    CXMMATRIX World
)noexcept{
    // Column-vector convention: clip-space transform is Projection * View * World.
    Matrix Transform = MatrixMultiply(Projection, View);
    Transform = MatrixMultiply(Transform, World);
    return Transform;
}

inline Vector MathCallConv Vector3Project
(
    VectorArg V,
    float    ViewportX,
    float    ViewportY,
    float    ViewportWidth,
    float    ViewportHeight,
    float    ViewportMinZ,
    float    ViewportMaxZ,
    FXMMATRIX Projection,
    CXMMATRIX View,
    CXMMATRIX World
)noexcept{
    const float HalfViewportWidth = ViewportWidth * 0.5f;
    const float HalfViewportHeight = ViewportHeight * 0.5f;

    Vector Scale = VectorSet(HalfViewportWidth, -HalfViewportHeight, ViewportMaxZ - ViewportMinZ, 0.0f);
    Vector Offset = VectorSet(ViewportX + HalfViewportWidth, ViewportY + HalfViewportHeight, ViewportMinZ, 0.0f);

    Matrix Transform = BuildProjectTransformColumns(Projection, View, World);

    Vector Result = Vector3TransformCoord(V, Transform);

    Result = VectorMultiplyAdd(Result, Scale, Offset);

    return Result;
}

#include "vector_stream/source_math_vector3_project_stream.inl"

inline Vector MathCallConv Vector3Unproject
(
    VectorArg V,
    float     ViewportX,
    float     ViewportY,
    float     ViewportWidth,
    float     ViewportHeight,
    float     ViewportMinZ,
    float     ViewportMaxZ,
    FXMMATRIX Projection,
    CXMMATRIX View,
    CXMMATRIX World
)noexcept{
    static const VectorF32 D = { { { -1.0f, 1.0f, 0.0f, 0.0f } } };

    Vector Scale = VectorSet(ViewportWidth * 0.5f, -ViewportHeight * 0.5f, ViewportMaxZ - ViewportMinZ, 1.0f);
    Scale = VectorReciprocal(Scale);

    Vector Offset = VectorSet(-ViewportX, -ViewportY, -ViewportMinZ, 0.0f);
    Offset = VectorMultiplyAdd(Scale, Offset, D.v);

    Matrix Transform = BuildProjectTransformColumns(Projection, View, World);
    Transform = MatrixInverse(nullptr, Transform);

    Vector Result = VectorMultiplyAdd(V, Scale, Offset);

    return Vector3TransformCoord(Result, Transform);
}

#include "vector_stream/source_math_vector3_unproject_stream.inl"

inline bool MathCallConv Vector4Equal
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] == V2.vector4_f32[0]) && (V1.vector4_f32[1] == V2.vector4_f32[1]) && (V1.vector4_f32[2] == V2.vector4_f32[2]) && (V1.vector4_f32[3] == V2.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    return ((_mm_movemask_ps(vTemp) == 0x0f) != 0);
#else
    return ComparisonAllTrue(Vector4EqualR(V1, V2));
#endif
}

inline uint32_t MathCallConv Vector4EqualR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if((V1.vector4_f32[0] == V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] == V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] == V2.vector4_f32[2]) &&
        (V1.vector4_f32[3] == V2.vector4_f32[3])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] != V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] != V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] != V2.vector4_f32[2]) &&
        (V1.vector4_f32[3] != V2.vector4_f32[3])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);

    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpeq_ps(V1, V2);
    int iTest = _mm_movemask_ps(vTemp);
    uint32_t CR = 0;
    if(iTest == 0xf){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(iTest == 0){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector4EqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_u32[0] == V2.vector4_u32[0]) && (V1.vector4_u32[1] == V2.vector4_u32[1]) && (V1.vector4_u32[2] == V2.vector4_u32[2]) && (V1.vector4_u32[3] == V2.vector4_u32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return ((_mm_movemask_ps(_mm_castsi128_ps(vTemp)) == 0xf) != 0);
#else
    return ComparisonAllTrue(Vector4EqualIntR(V1, V2));
#endif
}

inline uint32_t MathCallConv Vector4EqualIntR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if(V1.vector4_u32[0] == V2.vector4_u32[0] &&
        V1.vector4_u32[1] == V2.vector4_u32[1] &&
        V1.vector4_u32[2] == V2.vector4_u32[2] &&
        V1.vector4_u32[3] == V2.vector4_u32[3]){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(V1.vector4_u32[0] != V2.vector4_u32[0] &&
        V1.vector4_u32[1] != V2.vector4_u32[1] &&
        V1.vector4_u32[2] != V2.vector4_u32[2] &&
        V1.vector4_u32[3] != V2.vector4_u32[3]){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);

    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    int iTest = _mm_movemask_ps(_mm_castsi128_ps(vTemp));
    uint32_t CR = 0;
    if(iTest == 0xf){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(iTest == 0){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector4NearEqual
(
    VectorArg V1,
    VectorArg V2,
    VectorArg Epsilon
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float dx = fabsf(V1.vector4_f32[0] - V2.vector4_f32[0]);
    float dy = fabsf(V1.vector4_f32[1] - V2.vector4_f32[1]);
    float dz = fabsf(V1.vector4_f32[2] - V2.vector4_f32[2]);
    float dw = fabsf(V1.vector4_f32[3] - V2.vector4_f32[3]);
    return (((dx <= Epsilon.vector4_f32[0]) &&
        (dy <= Epsilon.vector4_f32[1]) &&
        (dz <= Epsilon.vector4_f32[2]) &&
        (dw <= Epsilon.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vDelta = vsubq_f32(V1, V2);
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    uint32x4_t vResult = vacleq_f32(vDelta, Epsilon);
#else
    uint32x4_t vResult = vcleq_f32(vabsq_f32(vDelta), Epsilon);
#endif
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Get the difference
    Vector vDelta = _mm_sub_ps(V1, V2);
    // Get the absolute value of the difference
    Vector vTemp = _mm_setzero_ps();
    vTemp = _mm_sub_ps(vTemp, vDelta);
    vTemp = _mm_max_ps(vTemp, vDelta);
    vTemp = _mm_cmple_ps(vTemp, Epsilon);
    return ((_mm_movemask_ps(vTemp) == 0xf) != 0);
#endif
}

inline bool MathCallConv Vector4NotEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] != V2.vector4_f32[0]) || (V1.vector4_f32[1] != V2.vector4_f32[1]) || (V1.vector4_f32[2] != V2.vector4_f32[2]) || (V1.vector4_f32[3] != V2.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) != 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpneq_ps(V1, V2);
    return ((_mm_movemask_ps(vTemp)) != 0);
#else
    return ComparisonAnyFalse(Vector4EqualR(V1, V2));
#endif
}

inline bool MathCallConv Vector4NotEqualInt
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_u32[0] != V2.vector4_u32[0]) || (V1.vector4_u32[1] != V2.vector4_u32[1]) || (V1.vector4_u32[2] != V2.vector4_u32[2]) || (V1.vector4_u32[3] != V2.vector4_u32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vceqq_u32(vreinterpretq_u32_f32(V1), vreinterpretq_u32_f32(V2));
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) != 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128i vTemp = _mm_cmpeq_epi32(_mm_castps_si128(V1), _mm_castps_si128(V2));
    return ((_mm_movemask_ps(_mm_castsi128_ps(vTemp)) != 0xF) != 0);
#else
    return ComparisonAnyFalse(Vector4EqualIntR(V1, V2));
#endif
}

inline bool MathCallConv Vector4Greater
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] > V2.vector4_f32[0]) && (V1.vector4_f32[1] > V2.vector4_f32[1]) && (V1.vector4_f32[2] > V2.vector4_f32[2]) && (V1.vector4_f32[3] > V2.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgtq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpgt_ps(V1, V2);
    return ((_mm_movemask_ps(vTemp) == 0x0f) != 0);
#else
    return ComparisonAllTrue(Vector4GreaterR(V1, V2));
#endif
}

inline uint32_t MathCallConv Vector4GreaterR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if(V1.vector4_f32[0] > V2.vector4_f32[0] &&
        V1.vector4_f32[1] > V2.vector4_f32[1] &&
        V1.vector4_f32[2] > V2.vector4_f32[2] &&
        V1.vector4_f32[3] > V2.vector4_f32[3]){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(V1.vector4_f32[0] <= V2.vector4_f32[0] &&
        V1.vector4_f32[1] <= V2.vector4_f32[1] &&
        V1.vector4_f32[2] <= V2.vector4_f32[2] &&
        V1.vector4_f32[3] <= V2.vector4_f32[3]){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgtq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);

    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    uint32_t CR = 0;
    Vector vTemp = _mm_cmpgt_ps(V1, V2);
    int iTest = _mm_movemask_ps(vTemp);
    if(iTest == 0xf){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector4GreaterOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] >= V2.vector4_f32[0]) && (V1.vector4_f32[1] >= V2.vector4_f32[1]) && (V1.vector4_f32[2] >= V2.vector4_f32[2]) && (V1.vector4_f32[3] >= V2.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgeq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmpge_ps(V1, V2);
    return ((_mm_movemask_ps(vTemp) == 0x0f) != 0);
#else
    return ComparisonAllTrue(Vector4GreaterOrEqualR(V1, V2));
#endif
}

inline uint32_t MathCallConv Vector4GreaterOrEqualR
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    uint32_t CR = 0;
    if((V1.vector4_f32[0] >= V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] >= V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] >= V2.vector4_f32[2]) &&
        (V1.vector4_f32[3] >= V2.vector4_f32[3])){
        CR = MATH_CRMASK_CR6TRUE;
    }else if((V1.vector4_f32[0] < V2.vector4_f32[0]) &&
        (V1.vector4_f32[1] < V2.vector4_f32[1]) &&
        (V1.vector4_f32[2] < V2.vector4_f32[2]) &&
        (V1.vector4_f32[3] < V2.vector4_f32[3])){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcgeq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);

    uint32_t CR = 0;
    if(r == 0xFFFFFFFFU){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!r){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#elif defined(_MATH_SSE_INTRINSICS_)
    uint32_t CR = 0;
    Vector vTemp = _mm_cmpge_ps(V1, V2);
    int iTest = _mm_movemask_ps(vTemp);
    if(iTest == 0x0f){
        CR = MATH_CRMASK_CR6TRUE;
    }else if(!iTest){
        CR = MATH_CRMASK_CR6FALSE;
    }
    return CR;
#endif
}

inline bool MathCallConv Vector4Less
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] < V2.vector4_f32[0]) && (V1.vector4_f32[1] < V2.vector4_f32[1]) && (V1.vector4_f32[2] < V2.vector4_f32[2]) && (V1.vector4_f32[3] < V2.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcltq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmplt_ps(V1, V2);
    return ((_mm_movemask_ps(vTemp) == 0x0f) != 0);
#else
    return ComparisonAllTrue(Vector4GreaterR(V2, V1));
#endif
}

inline bool MathCallConv Vector4LessOrEqual
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V1.vector4_f32[0] <= V2.vector4_f32[0]) && (V1.vector4_f32[1] <= V2.vector4_f32[1]) && (V1.vector4_f32[2] <= V2.vector4_f32[2]) && (V1.vector4_f32[3] <= V2.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vResult = vcleq_f32(V1, V2);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vResult)), vget_high_u8(vreinterpretq_u8_u32(vResult)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp = _mm_cmple_ps(V1, V2);
    return ((_mm_movemask_ps(vTemp) == 0x0f) != 0);
#else
    return ComparisonAllTrue(Vector4GreaterOrEqualR(V2, V1));
#endif
}

inline bool MathCallConv Vector4InBounds
(
    VectorArg V,
    VectorArg Bounds
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (((V.vector4_f32[0] <= Bounds.vector4_f32[0] && V.vector4_f32[0] >= -Bounds.vector4_f32[0]) &&
        (V.vector4_f32[1] <= Bounds.vector4_f32[1] && V.vector4_f32[1] >= -Bounds.vector4_f32[1]) &&
        (V.vector4_f32[2] <= Bounds.vector4_f32[2] && V.vector4_f32[2] >= -Bounds.vector4_f32[2]) &&
        (V.vector4_f32[3] <= Bounds.vector4_f32[3] && V.vector4_f32[3] >= -Bounds.vector4_f32[3])) != 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Test if less than or equal
    uint32x4_t ivTemp1 = vcleq_f32(V, Bounds);
    // Negate the bounds
    float32x4_t vTemp2 = vnegq_f32(Bounds);
    // Test if greater or equal (Reversed)
    uint32x4_t ivTemp2 = vcleq_f32(vTemp2, V);
    // Blend answers
    ivTemp1 = vandq_u32(ivTemp1, ivTemp2);
    // in bounds?
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(ivTemp1)), vget_high_u8(vreinterpretq_u8_u32(ivTemp1)));
    uint16x4x2_t vTemp3 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp3.val[1]), 1) == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Test if less than or equal
    Vector vTemp1 = _mm_cmple_ps(V, Bounds);
    // Negate the bounds
    Vector vTemp2 = _mm_mul_ps(Bounds, g_NegativeOne);
    // Test if greater or equal (Reversed)
    vTemp2 = _mm_cmple_ps(vTemp2, V);
    // Blend answers
    vTemp1 = _mm_and_ps(vTemp1, vTemp2);
    // All in bounds?
    return ((_mm_movemask_ps(vTemp1) == 0x0f) != 0);
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(push)
#pragma float_control(precise, on)
#endif

inline bool MathCallConv Vector4IsNaN(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (MATH_IS_NAN(V.vector4_f32[0]) ||
        MATH_IS_NAN(V.vector4_f32[1]) ||
        MATH_IS_NAN(V.vector4_f32[2]) ||
        MATH_IS_NAN(V.vector4_f32[3]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    return isnan(vgetq_lane_f32(V, 0)) || isnan(vgetq_lane_f32(V, 1)) || isnan(vgetq_lane_f32(V, 2)) || isnan(vgetq_lane_f32(V, 3));
#else
    // Test against itself. NaN is always not equal
    uint32x4_t vTempNan = vceqq_f32(V, V);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vTempNan)), vget_high_u8(vreinterpretq_u8_u32(vTempNan)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    // If any are NaN, the mask is zero
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) != 0xFFFFFFFFU);
#endif
#elif defined(_MATH_SSE_INTRINSICS_)
#if defined(__clang__) && defined(__FINITE_MATH_ONLY__)
    MATH_ALIGNED_DATA(16) float tmp[4];
    _mm_store_ps(tmp, V);
    return isnan(tmp[0]) || isnan(tmp[1]) || isnan(tmp[2]) || isnan(tmp[3]);
#else
    // Test against itself. NaN is always not equal
    Vector vTempNan = _mm_cmpneq_ps(V, V);
    // If any are NaN, the mask is non-zero
    return (_mm_movemask_ps(vTempNan) != 0);
#endif
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(pop)
#endif

inline bool MathCallConv Vector4IsInfinite(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return (MATH_IS_INF(V.vector4_f32[0]) ||
        MATH_IS_INF(V.vector4_f32[1]) ||
        MATH_IS_INF(V.vector4_f32[2]) ||
        MATH_IS_INF(V.vector4_f32[3]));
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Mask off the sign bit
    uint32x4_t vTempInf = vandq_u32(vreinterpretq_u32_f32(V), g_AbsMask);
    // Compare to infinity
    vTempInf = vceqq_f32(vreinterpretq_f32_u32(vTempInf), g_Infinity);
    // If any are infinity, the signs are true.
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(vTempInf)), vget_high_u8(vreinterpretq_u8_u32(vTempInf)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    return (vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) != 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Mask off the sign bit
    Vector vTemp = _mm_and_ps(V, g_AbsMask);
    // Compare to infinity
    vTemp = _mm_cmpeq_ps(vTemp, g_Infinity);
    // If any are infinity, the signs are true.
    return (_mm_movemask_ps(vTemp) != 0);
#endif
}

inline Vector MathCallConv Vector4Dot
(
    VectorArg V1,
    VectorArg V2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result;
    Result.f[0] =
        Result.f[1] =
        Result.f[2] =
        Result.f[3] = V1.vector4_f32[0] * V2.vector4_f32[0] + V1.vector4_f32[1] * V2.vector4_f32[1] + V1.vector4_f32[2] * V2.vector4_f32[2] + V1.vector4_f32[3] * V2.vector4_f32[3];
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vTemp = vmulq_f32(V1, V2);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    return vcombine_f32(v1, v1);
#elif defined(_MATH_SSE4_INTRINSICS_)
    return _mm_dp_ps(V1, V2, 0xff);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vTemp = _mm_mul_ps(V1, V2);
    vTemp = _mm_hadd_ps(vTemp, vTemp);
    return _mm_hadd_ps(vTemp, vTemp);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp2 = V2;
    Vector vTemp = _mm_mul_ps(V1, vTemp2);
    vTemp2 = _mm_shuffle_ps(vTemp2, vTemp, _MM_SHUFFLE(1, 0, 0, 0));
    vTemp2 = _mm_add_ps(vTemp2, vTemp);
    vTemp = _mm_shuffle_ps(vTemp, vTemp2, _MM_SHUFFLE(0, 3, 0, 0));
    vTemp = _mm_add_ps(vTemp, vTemp2);
    return MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(2, 2, 2, 2));
#endif
}

inline Vector MathCallConv Vector4Cross
(
    VectorArg V1,
    VectorArg V2,
    VectorArg V3
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            (((V2.vector4_f32[2] * V3.vector4_f32[3]) - (V2.vector4_f32[3] * V3.vector4_f32[2])) * V1.vector4_f32[1]) - (((V2.vector4_f32[1] * V3.vector4_f32[3]) - (V2.vector4_f32[3] * V3.vector4_f32[1])) * V1.vector4_f32[2]) + (((V2.vector4_f32[1] * V3.vector4_f32[2]) - (V2.vector4_f32[2] * V3.vector4_f32[1])) * V1.vector4_f32[3]),
            (((V2.vector4_f32[3] * V3.vector4_f32[2]) - (V2.vector4_f32[2] * V3.vector4_f32[3])) * V1.vector4_f32[0]) - (((V2.vector4_f32[3] * V3.vector4_f32[0]) - (V2.vector4_f32[0] * V3.vector4_f32[3])) * V1.vector4_f32[2]) + (((V2.vector4_f32[2] * V3.vector4_f32[0]) - (V2.vector4_f32[0] * V3.vector4_f32[2])) * V1.vector4_f32[3]),
            (((V2.vector4_f32[1] * V3.vector4_f32[3]) - (V2.vector4_f32[3] * V3.vector4_f32[1])) * V1.vector4_f32[0]) - (((V2.vector4_f32[0] * V3.vector4_f32[3]) - (V2.vector4_f32[3] * V3.vector4_f32[0])) * V1.vector4_f32[1]) + (((V2.vector4_f32[0] * V3.vector4_f32[1]) - (V2.vector4_f32[1] * V3.vector4_f32[0])) * V1.vector4_f32[3]),
            (((V2.vector4_f32[2] * V3.vector4_f32[1]) - (V2.vector4_f32[1] * V3.vector4_f32[2])) * V1.vector4_f32[0]) - (((V2.vector4_f32[2] * V3.vector4_f32[0]) - (V2.vector4_f32[0] * V3.vector4_f32[2])) * V1.vector4_f32[1]) + (((V2.vector4_f32[1] * V3.vector4_f32[0]) - (V2.vector4_f32[0] * V3.vector4_f32[1])) * V1.vector4_f32[2]),
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    const uint32x2_t select = vget_low_u32(g_MaskX);
    const float32x2_t v2xy = vget_low_f32(V2);
    const float32x2_t v2zw = vget_high_f32(V2);
    const float32x2_t v2yx = vrev64_f32(v2xy);
    const float32x2_t v2wz = vrev64_f32(v2zw);
    const float32x2_t v2yz = vbsl_f32(select, v2yx, v2wz);

    const float32x2_t v3zw = vget_high_f32(V3);
    const float32x2_t v3wz = vrev64_f32(v3zw);
    const float32x2_t v3xy = vget_low_f32(V3);
    const float32x2_t v3wy = vbsl_f32(select, v3wz, v3xy);

    float32x4_t vTemp1 = vcombine_f32(v2zw, v2yz);
    float32x4_t vTemp2 = vcombine_f32(v3wz, v3wy);
    Vector vResult = vmulq_f32(vTemp1, vTemp2);

    const float32x2_t v2wy = vbsl_f32(select, v2wz, v2xy);
    const float32x2_t v3yx = vrev64_f32(v3xy);
    const float32x2_t v3yz = vbsl_f32(select, v3yx, v3wz);

    vTemp1 = vcombine_f32(v2wz, v2wy);
    vTemp2 = vcombine_f32(v3zw, v3yz);
    vResult = vmlsq_f32(vResult, vTemp1, vTemp2);

    const float32x2_t v1xy = vget_low_f32(V1);
    const float32x2_t v1yx = vrev64_f32(v1xy);

    vTemp1 = vcombine_f32(v1yx, vdup_lane_f32(v1yx, 1));
    vResult = vmulq_f32(vResult, vTemp1);

    const float32x2_t v2yw = vrev64_f32(v2wy);
    const float32x2_t v2xz = vbsl_f32(select, v2xy, v2wz);
    const float32x2_t v3wx = vbsl_f32(select, v3wz, v3yx);

    vTemp1 = vcombine_f32(v2yw, v2xz);
    vTemp2 = vcombine_f32(v3wx, v3wx);
    float32x4_t vTerm = vmulq_f32(vTemp1, vTemp2);

    const float32x2_t v2wx = vbsl_f32(select, v2wz, v2yx);
    const float32x2_t v3yw = vrev64_f32(v3wy);
    const float32x2_t v3xz = vbsl_f32(select, v3xy, v3wz);

    vTemp1 = vcombine_f32(v2wx, v2wx);
    vTemp2 = vcombine_f32(v3yw, v3xz);
    vTerm = vmlsq_f32(vTerm, vTemp1, vTemp2);

    const float32x2_t v1zw = vget_high_f32(V1);
    vTemp1 = vcombine_f32(vdup_lane_f32(v1zw, 0), vdup_lane_f32(v1yx, 0));
    vResult = vmlsq_f32(vResult, vTerm, vTemp1);

    const float32x2_t v3zx = vrev64_f32(v3xz);
    vTemp1 = vcombine_f32(v2yz, v2xy);
    vTemp2 = vcombine_f32(v3zx, v3yx);
    vTerm = vmulq_f32(vTemp1, vTemp2);

    const float32x2_t v2zx = vrev64_f32(v2xz);
    vTemp1 = vcombine_f32(v2zx, v2yx);
    vTemp2 = vcombine_f32(v3yz, v3xy);
    vTerm = vmlsq_f32(vTerm, vTemp1, vTemp2);

    const float32x2_t v1wz = vrev64_f32(v1zw);
    vTemp1 = vcombine_f32(vdup_lane_f32(v1wz, 0), v1wz);
    return vmlaq_f32(vResult, vTerm, vTemp1);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 1, 3, 2));
    Vector vTemp3 = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 3, 2, 3));
    vResult = _mm_mul_ps(vResult, vTemp3);

    Vector vTemp2 = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 3, 2, 3));
    vTemp3 = MATH_PERMUTE_PS(vTemp3, _MM_SHUFFLE(1, 3, 0, 1));
    vResult = MATH_FNMADD_PS(vTemp2, vTemp3, vResult);

    Vector vTemp1 = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 0, 0, 1));
    vResult = _mm_mul_ps(vResult, vTemp1);

    vTemp2 = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 0, 3, 1));
    vTemp3 = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 3, 0, 3));
    vTemp3 = _mm_mul_ps(vTemp3, vTemp2);
    vTemp2 = MATH_PERMUTE_PS(vTemp2, _MM_SHUFFLE(2, 1, 2, 1));
    vTemp1 = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(2, 0, 3, 1));
    vTemp3 = MATH_FNMADD_PS(vTemp2, vTemp1, vTemp3);
    vTemp1 = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(1, 1, 2, 2));
    vResult = MATH_FNMADD_PS(vTemp1, vTemp3, vResult);

    vTemp2 = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 0, 2, 1));
    vTemp3 = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 1, 0, 2));
    vTemp3 = _mm_mul_ps(vTemp3, vTemp2);
    vTemp2 = MATH_PERMUTE_PS(vTemp2, _MM_SHUFFLE(2, 0, 2, 1));
    vTemp1 = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 0, 2, 1));
    vTemp3 = MATH_FNMADD_PS(vTemp1, vTemp2, vTemp3);
    vTemp1 = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 3, 3, 3));
    vResult = MATH_FMADD_PS(vTemp3, vTemp1, vResult);
    return vResult;
#endif
}

inline Vector MathCallConv Vector4LengthSq(VectorArg V)noexcept{
    return Vector4Dot(V, V);
}

inline Vector MathCallConv Vector4ReciprocalLengthEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    Vector Result = Vector4LengthSq(V);
    Result = VectorReciprocalSqrtEst(Result);
    return Result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    v2 = vrsqrte_f32(v1);
    return vcombine_f32(v2, v2);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0xff);
    return _mm_rsqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_rsqrt_ps(vLengthSq);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(3, 2, 3, 2));
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 0, 0, 0));
    vTemp = _mm_shuffle_ps(vTemp, vLengthSq, _MM_SHUFFLE(3, 3, 0, 0));
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 2, 2, 2));
    vLengthSq = _mm_rsqrt_ps(vLengthSq);
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector4ReciprocalLength(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    Vector Result = Vector4LengthSq(V);
    Result = VectorReciprocalSqrt(Result);
    return Result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    float32x2_t S0 = vrsqrte_f32(v1);
    float32x2_t P0 = vmul_f32(v1, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(v1, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    float32x2_t Result = vmul_f32(S1, R1);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0xff);
    Vector vLengthSq = _mm_sqrt_ps(vTemp);
    return _mm_div_ps(g_One, vLengthSq);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    vLengthSq = _mm_div_ps(g_One, vLengthSq);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(3, 2, 3, 2));
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 0, 0, 0));
    vTemp = _mm_shuffle_ps(vTemp, vLengthSq, _MM_SHUFFLE(3, 3, 0, 0));
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 2, 2, 2));
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    vLengthSq = _mm_div_ps(g_One, vLengthSq);
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector4LengthEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    Vector Result = Vector4LengthSq(V);
    Result = VectorSqrtEst(Result);
    return Result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(v1, zero);
    float32x2_t Result = vrsqrte_f32(v1);
    Result = vmul_f32(v1, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0xff);
    return _mm_sqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(3, 2, 3, 2));
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 0, 0, 0));
    vTemp = _mm_shuffle_ps(vTemp, vLengthSq, _MM_SHUFFLE(3, 3, 0, 0));
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 2, 2, 2));
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#endif
}

inline Vector MathCallConv Vector4Length(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;

    Result = Vector4LengthSq(V);
    Result = VectorSqrt(Result);

    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot4
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(v1, zero);
    // Sqrt
    float32x2_t S0 = vrsqrte_f32(v1);
    float32x2_t P0 = vmul_f32(v1, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(v1, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    float32x2_t Result = vmul_f32(S1, R1);
    Result = vmul_f32(v1, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0xff);
    return _mm_sqrt_ps(vTemp);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y,z and w
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has z and w
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(3, 2, 3, 2));
    // x+z, y+w
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    // x+z,x+z,x+z,y+w
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 0, 0, 0));
    // ??,??,y+w,y+w
    vTemp = _mm_shuffle_ps(vTemp, vLengthSq, _MM_SHUFFLE(3, 3, 0, 0));
    // ??,??,x+z+y+w,??
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    // Splat the length
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 2, 2, 2));
    // Get the length
    vLengthSq = _mm_sqrt_ps(vLengthSq);
    return vLengthSq;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Vector4NormalizeEst uses a reciprocal estimate and
// returns QNaN on zero and infinite vectors.

inline Vector MathCallConv Vector4NormalizeEst(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector Result;
    Result = Vector4ReciprocalLength(V);
    Result = VectorMultiply(V, Result);
    return Result;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot4
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    // Reciprocal sqrt (estimate)
    v2 = vrsqrte_f32(v1);
    // Normalize
    return vmulq_f32(V, vcombine_f32(v2, v2));
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(V, V, 0xff);
    Vector vResult = _mm_rsqrt_ps(vTemp);
    return _mm_mul_ps(vResult, V);
#elif defined(_MATH_SSE3_INTRINSICS_)
    Vector vDot = _mm_mul_ps(V, V);
    vDot = _mm_hadd_ps(vDot, vDot);
    vDot = _mm_hadd_ps(vDot, vDot);
    vDot = _mm_rsqrt_ps(vDot);
    vDot = _mm_mul_ps(vDot, V);
    return vDot;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y,z and w
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has z and w
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(3, 2, 3, 2));
    // x+z, y+w
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    // x+z,x+z,x+z,y+w
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 0, 0, 0));
    // ??,??,y+w,y+w
    vTemp = _mm_shuffle_ps(vTemp, vLengthSq, _MM_SHUFFLE(3, 3, 0, 0));
    // ??,??,x+z+y+w,??
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    // Splat the length
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 2, 2, 2));
    // Get the reciprocal
    Vector vResult = _mm_rsqrt_ps(vLengthSq);
    // Reciprocal mul to perform the normalization
    vResult = _mm_mul_ps(vResult, V);
    return vResult;
#endif
}

inline Vector MathCallConv Vector4Normalize(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    float fLength;
    Vector vResult;

    vResult = Vector4Length(V);
    fLength = vResult.vector4_f32[0];

    // Prevent divide by zero
    if(fLength > 0){
        fLength = 1.0f / fLength;
    }

    vResult.vector4_f32[0] = V.vector4_f32[0] * fLength;
    vResult.vector4_f32[1] = V.vector4_f32[1] * fLength;
    vResult.vector4_f32[2] = V.vector4_f32[2] * fLength;
    vResult.vector4_f32[3] = V.vector4_f32[3] * fLength;
    return vResult;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Dot4
    float32x4_t vTemp = vmulq_f32(V, V);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    uint32x2_t VEqualsZero = vceq_f32(v1, vdup_n_f32(0));
    uint32x2_t VEqualsInf = vceq_f32(v1, vget_low_f32(g_Infinity));
    // Reciprocal sqrt (2 iterations of Newton-Raphson)
    float32x2_t S0 = vrsqrte_f32(v1);
    float32x2_t P0 = vmul_f32(v1, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(v1, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    v2 = vmul_f32(S1, R1);
    // Normalize
    Vector vResult = vmulq_f32(V, vcombine_f32(v2, v2));
    vResult = vbslq_f32(vcombine_u32(VEqualsZero, VEqualsZero), vdupq_n_f32(0), vResult);
    return vbslq_f32(vcombine_u32(VEqualsInf, VEqualsInf), g_QNaN, vResult);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vLengthSq = _mm_dp_ps(V, V, 0xff);
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Divide to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#elif defined(_MATH_SSE3_INTRINSICS_)
    // Perform the dot product on x,y,z and w
    Vector vLengthSq = _mm_mul_ps(V, V);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    vLengthSq = _mm_hadd_ps(vLengthSq, vLengthSq);
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Divide to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y,z and w
    Vector vLengthSq = _mm_mul_ps(V, V);
    // vTemp has z and w
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(3, 2, 3, 2));
    // x+z, y+w
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    // x+z,x+z,x+z,y+w
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(1, 0, 0, 0));
    // ??,??,y+w,y+w
    vTemp = _mm_shuffle_ps(vTemp, vLengthSq, _MM_SHUFFLE(3, 3, 0, 0));
    // ??,??,x+z+y+w,??
    vLengthSq = _mm_add_ps(vLengthSq, vTemp);
    // Splat the length
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 2, 2, 2));
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Create zero with a single instruction
    Vector vZeroMask = _mm_setzero_ps();
    // Test for a divide by zero (Must be FP to detect -0.0)
    vZeroMask = _mm_cmpneq_ps(vZeroMask, vResult);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Divide to perform the normalization
    vResult = _mm_div_ps(V, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vZeroMask);
    // Select qnan or result based on infinite length
    Vector vTemp1 = _mm_andnot_ps(vLengthSq, g_QNaN);
    Vector vTemp2 = _mm_and_ps(vResult, vLengthSq);
    vResult = _mm_or_ps(vTemp1, vTemp2);
    return vResult;
#endif
}

inline Vector MathCallConv Vector4ClampLength
(
    VectorArg V,
    float    LengthMin,
    float    LengthMax
)noexcept{
    Vector ClampMax = VectorReplicate(LengthMax);
    Vector ClampMin = VectorReplicate(LengthMin);

    return Vector4ClampLengthV(V, ClampMin, ClampMax);
}

inline Vector MathCallConv Vector4ClampLengthV
(
    VectorArg V,
    VectorArg LengthMin,
    VectorArg LengthMax
)noexcept{
    assert((VectorGetY(LengthMin) == VectorGetX(LengthMin)) && (VectorGetZ(LengthMin) == VectorGetX(LengthMin)) && (VectorGetW(LengthMin) == VectorGetX(LengthMin)));
    assert((VectorGetY(LengthMax) == VectorGetX(LengthMax)) && (VectorGetZ(LengthMax) == VectorGetX(LengthMax)) && (VectorGetW(LengthMax) == VectorGetX(LengthMax)));
    assert(Vector4GreaterOrEqual(LengthMin, VectorZero()));
    assert(Vector4GreaterOrEqual(LengthMax, VectorZero()));
    assert(Vector4GreaterOrEqual(LengthMax, LengthMin));

    Vector LengthSq = Vector4LengthSq(V);

    const Vector Zero = VectorZero();

    Vector RcpLength = VectorReciprocalSqrt(LengthSq);

    Vector InfiniteLength = VectorEqualInt(LengthSq, g_Infinity.v);
    Vector ZeroLength = VectorEqual(LengthSq, Zero);

    Vector Normal = VectorMultiply(V, RcpLength);

    Vector Length = VectorMultiply(LengthSq, RcpLength);

    Vector Select = VectorEqualInt(InfiniteLength, ZeroLength);
    Length = VectorSelect(LengthSq, Length, Select);
    Normal = VectorSelect(LengthSq, Normal, Select);

    Vector ControlMax = VectorGreater(Length, LengthMax);
    Vector ControlMin = VectorLess(Length, LengthMin);

    Vector ClampLength = VectorSelect(Length, LengthMax, ControlMax);
    ClampLength = VectorSelect(ClampLength, LengthMin, ControlMin);

    Vector Result = VectorMultiply(Normal, ClampLength);

    // Preserve the original vector (with no precision loss) if the length falls within the given range
    Vector Control = VectorEqualInt(ControlMax, ControlMin);
    Result = VectorSelect(Result, V, Control);

    return Result;
}

inline Vector MathCallConv Vector4Reflect
(
    VectorArg Incident,
    VectorArg Normal
)noexcept{
    // Result = Incident - (2 * dot(Incident, Normal)) * Normal

    Vector Result = Vector4Dot(Incident, Normal);
    Result = VectorAdd(Result, Result);
    Result = VectorNegativeMultiplySubtract(Result, Normal, Incident);

    return Result;
}

inline Vector MathCallConv Vector4Refract
(
    VectorArg Incident,
    VectorArg Normal,
    float    RefractionIndex
)noexcept{
    Vector Index = VectorReplicate(RefractionIndex);
    return Vector4RefractV(Incident, Normal, Index);
}

inline Vector MathCallConv Vector4RefractV
(
    VectorArg Incident,
    VectorArg Normal,
    VectorArg RefractionIndex
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    Vector        IDotN;
    Vector        R;
    const Vector  Zero = VectorZero();

    // Result = RefractionIndex * Incident - Normal * (RefractionIndex * dot(Incident, Normal) +
    // sqrt(1 - RefractionIndex * RefractionIndex * (1 - dot(Incident, Normal) * dot(Incident, Normal))))

    IDotN = Vector4Dot(Incident, Normal);

    // R = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    R = VectorNegativeMultiplySubtract(IDotN, IDotN, g_One.v);
    R = VectorMultiply(R, RefractionIndex);
    R = VectorNegativeMultiplySubtract(R, RefractionIndex, g_One.v);

    if(Vector4LessOrEqual(R, Zero)){
        // Total internal reflection
        return Zero;
    }else{
        Vector Result;

        // R = RefractionIndex * IDotN + sqrt(R)
        R = VectorSqrt(R);
        R = VectorMultiplyAdd(RefractionIndex, IDotN, R);

        // Result = RefractionIndex * Incident - Normal * R
        Result = VectorMultiply(RefractionIndex, Incident);
        Result = VectorNegativeMultiplySubtract(Normal, R, Result);

        return Result;
    }

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector IDotN = Vector4Dot(Incident, Normal);

    // R = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    float32x4_t R = vmlsq_f32(g_One, IDotN, IDotN);
    R = vmulq_f32(R, RefractionIndex);
    R = vmlsq_f32(g_One, R, RefractionIndex);

    uint32x4_t isrzero = vcleq_f32(R, g_Zero);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(isrzero)), vget_high_u8(vreinterpretq_u8_u32(isrzero)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));

    float32x4_t vResult;
    if(vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1) == 0xFFFFFFFFU){
        // Total internal reflection
        vResult = g_Zero;
    }else{
        // Sqrt(R)
        float32x4_t S0 = vrsqrteq_f32(R);
        float32x4_t P0 = vmulq_f32(R, S0);
        float32x4_t R0 = vrsqrtsq_f32(P0, S0);
        float32x4_t S1 = vmulq_f32(S0, R0);
        float32x4_t P1 = vmulq_f32(R, S1);
        float32x4_t R1 = vrsqrtsq_f32(P1, S1);
        float32x4_t S2 = vmulq_f32(S1, R1);
        R = vmulq_f32(R, S2);
        // R = RefractionIndex * IDotN + sqrt(R)
        R = vmlaq_f32(R, RefractionIndex, IDotN);
        // Result = RefractionIndex * Incident - Normal * R
        vResult = vmulq_f32(RefractionIndex, Incident);
        vResult = vmlsq_f32(vResult, R, Normal);
    }
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector IDotN = Vector4Dot(Incident, Normal);

    // R = 1.0f - RefractionIndex * RefractionIndex * (1.0f - IDotN * IDotN)
    Vector R = MATH_FNMADD_PS(IDotN, IDotN, g_One);
    Vector R2 = _mm_mul_ps(RefractionIndex, RefractionIndex);
    R = MATH_FNMADD_PS(R, R2, g_One);

    Vector vResult = _mm_cmple_ps(R, g_Zero);
    if(_mm_movemask_ps(vResult) == 0x0f){
        // Total internal reflection
        vResult = g_Zero;
    }else{
        // R = RefractionIndex * IDotN + sqrt(R)
        R = _mm_sqrt_ps(R);
        R = MATH_FMADD_PS(RefractionIndex, IDotN, R);
        // Result = RefractionIndex * Incident - Normal * R
        vResult = _mm_mul_ps(RefractionIndex, Incident);
        vResult = MATH_FNMADD_PS(R, Normal, vResult);
    }
    return vResult;
#endif
}

inline Vector MathCallConv Vector4Orthogonal(VectorArg V)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 Result = { { {
            V.vector4_f32[2],
            V.vector4_f32[3],
            -V.vector4_f32[0],
            -V.vector4_f32[1]
        } } };
    return Result.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Negate = { { { 1.f, 1.f, -1.f, -1.f } } };

    float32x4_t Result = vcombine_f32(vget_high_f32(V), vget_low_f32(V));
    return vmulq_f32(Result, Negate);
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 FlipZW = { { { 1.0f, 1.0f, -1.0f, -1.0f } } };
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 0, 3, 2));
    vResult = _mm_mul_ps(vResult, FlipZW);
    return vResult;
#endif
}

inline Vector MathCallConv Vector4AngleBetweenNormalsEst
(
    VectorArg N1,
    VectorArg N2
)noexcept{
    Vector Result = Vector4Dot(N1, N2);
    Result = VectorClamp(Result, g_NegativeOne.v, g_One.v);
    Result = VectorACosEst(Result);
    return Result;
}

inline Vector MathCallConv Vector4AngleBetweenNormals
(
    VectorArg N1,
    VectorArg N2
)noexcept{
    Vector Result = Vector4Dot(N1, N2);
    Result = VectorClamp(Result, g_NegativeOne.v, g_One.v);
    Result = VectorACos(Result);
    return Result;
}

inline Vector MathCallConv Vector4AngleBetweenVectors
(
    VectorArg V1,
    VectorArg V2
)noexcept{
    Vector L1 = Vector4ReciprocalLength(V1);
    Vector L2 = Vector4ReciprocalLength(V2);

    Vector Dot = Vector4Dot(V1, V2);

    L1 = VectorMultiply(L1, L2);

    Vector CosAngle = VectorMultiply(Dot, L1);
    CosAngle = VectorClamp(CosAngle, g_NegativeOne.v, g_One.v);

    return VectorACos(CosAngle);
}

inline Vector MathCallConv Vector4Transform
(
    VectorArg V,
    FXMMATRIX M
)noexcept{
    // `M.r[0..3]` are the four matrix columns here: Result = M * V.
#if defined(_MATH_NO_INTRINSICS_)

    const float fX = (M(0, 0) * V.vector4_f32[0]) + (M(0, 1) * V.vector4_f32[1]) + (M(0, 2) * V.vector4_f32[2]) + (M(0, 3) * V.vector4_f32[3]);
    const float fY = (M(1, 0) * V.vector4_f32[0]) + (M(1, 1) * V.vector4_f32[1]) + (M(1, 2) * V.vector4_f32[2]) + (M(1, 3) * V.vector4_f32[3]);
    const float fZ = (M(2, 0) * V.vector4_f32[0]) + (M(2, 1) * V.vector4_f32[1]) + (M(2, 2) * V.vector4_f32[2]) + (M(2, 3) * V.vector4_f32[3]);
    const float fW = (M(3, 0) * V.vector4_f32[0]) + (M(3, 1) * V.vector4_f32[1]) + (M(3, 2) * V.vector4_f32[2]) + (M(3, 3) * V.vector4_f32[3]);
    VectorF32 vResult = { { { fX, fY, fZ, fW } } };
    return vResult.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(V);
    Vector vResult = vmulq_lane_f32(M.r[0], VL, 0); // X
    vResult = vmlaq_lane_f32(vResult, M.r[1], VL, 1); // Y
    float32x2_t VH = vget_high_f32(V);
    vResult = vmlaq_lane_f32(vResult, M.r[2], VH, 0); // Z
    return vmlaq_lane_f32(vResult, M.r[3], VH, 1); // W
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vResult = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3)); // W
    vResult = _mm_mul_ps(vResult, M.r[3]);
    Vector vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2)); // Z
    vResult = MATH_FMADD_PS(vTemp, M.r[2], vResult);
    vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1)); // Y
    vResult = MATH_FMADD_PS(vTemp, M.r[1], vResult);
    vTemp = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0)); // X
    vResult = MATH_FMADD_PS(vTemp, M.r[0], vResult);
    return vResult;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vector_stream/source_math_vector_stream4.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef _MATH_NO_VECTOR_OVERLOADS_


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv operator+(VectorArg V)noexcept{
    return V;
}

inline Vector MathCallConv operator-(VectorArg V)noexcept{
    return VectorNegate(V);
}

inline Vector& MathCallConv operator+=(
    Vector& V1,
    VectorArg       V2
    )noexcept{
    V1 = VectorAdd(V1, V2);
    return V1;
}

inline Vector& MathCallConv operator-=(
    Vector& V1,
    VectorArg       V2
    )noexcept{
    V1 = VectorSubtract(V1, V2);
    return V1;
}

inline Vector& MathCallConv operator*=(
    Vector& V1,
    VectorArg       V2
    )noexcept{
    V1 = VectorMultiply(V1, V2);
    return V1;
}

inline Vector& MathCallConv operator/=(
    Vector& V1,
    VectorArg       V2
    )noexcept{
    V1 = VectorDivide(V1, V2);
    return V1;
}

inline Vector& operator*=(
    Vector& V,
    const float S
    )noexcept{
    V = VectorScale(V, S);
    return V;
}

inline Vector& operator/=(
    Vector& V,
    const float S
    )noexcept{
    Vector vS = VectorReplicate(S);
    V = VectorDivide(V, vS);
    return V;
}

inline Vector MathCallConv operator+(
    VectorArg V1,
    VectorArg V2
    )noexcept{
    return VectorAdd(V1, V2);
}

inline Vector MathCallConv operator-(
    VectorArg V1,
    VectorArg V2
    )noexcept{
    return VectorSubtract(V1, V2);
}

inline Vector MathCallConv operator*(
    VectorArg V1,
    VectorArg V2
    )noexcept{
    return VectorMultiply(V1, V2);
}

inline Vector MathCallConv operator/(
    VectorArg V1,
    VectorArg V2
    )noexcept{
    return VectorDivide(V1, V2);
}

inline Vector MathCallConv operator*(
    VectorArg      V,
    const float    S
    )noexcept{
    return VectorScale(V, S);
}

inline Vector MathCallConv operator/(
    VectorArg      V,
    const float    S
    )noexcept{
    Vector vS = VectorReplicate(S);
    return VectorDivide(V, vS);
}

inline Vector MathCallConv operator*(
    float           S,
    VectorArg       V
    )noexcept{
    return VectorScale(V, S);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(_MATH_NO_INTRINSICS_)
#undef MATH_IS_NAN
#undef MATH_IS_INF
#endif

#if defined(_MATH_SSE_INTRINSICS_)
#undef MATH3UNPACK3INTO4
#undef MATH3PACK4INTO3
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

