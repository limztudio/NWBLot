// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_FUNCTION
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule)
#define NWB_ADHD_EXPORT_FUNCTION_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC
#define NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC(adhdName, floatIntrinsicPrefix, doubleIntrinsicPrefix)
#define NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC
#define NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC(adhdName, laneIntrinsicSuffix, maxLanes)
#define NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_INT_FALLBACK
#define NWB_ADHD_EXPORT_SIMD_INT_FALLBACK(adhdName)
#define NWB_ADHD_EXPORT_SIMD_INT_FALLBACK_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION
#define NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION(adhdName, laneIntrinsicSuffix)
#define NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_FUNCTION("min",       "::Min",                  2u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("max",       "::Max",                  2u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("abs",       "::Abs",                  1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("clamp",     "::Clamp",                3u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("floor",     "::Floor",                1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("ceil",      "::Ceil",                 1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("round",     "::Round",                1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("sqrt",      "::Sqrt",                 1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("rsqrt",     "::VectorReciprocalSqrt", 1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("lerp",      "::VectorLerp",           3u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("dot",       "::Vector4Dot",           2u, LaneScalar)
NWB_ADHD_EXPORT_FUNCTION("cross",     "::Vector3Cross",         2u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("length",    "::Vector4Length",        1u, LaneScalar)
NWB_ADHD_EXPORT_FUNCTION("lengthSq",  "::Vector4LengthSq",      1u, LaneScalar)
NWB_ADHD_EXPORT_FUNCTION("normalize", "::Vector4Normalize",     1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("mul",       "::MatrixMultiply",       2u, MatrixMultiply)
NWB_ADHD_EXPORT_FUNCTION("transpose", "::MatrixTranspose",      1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("inverse",   "::MatrixInverse",        1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("sin",       "::Sin",                  1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("cos",       "::Cos",                  1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("tan",       "::Tan",                  1u, PromoteSimd)
NWB_ADHD_EXPORT_FUNCTION("isFinite",  "::IsFinite",             1u, Bool)
NWB_ADHD_EXPORT_FUNCTION("divideUp",  "::DivideUp",             2u, PromoteSimd)

NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("min",     "::VectorMin(",                  "::VectorDoubleMin(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("max",     "::VectorMax(",                  "::VectorDoubleMax(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("abs",     "::VectorAbs(",                  "::VectorDoubleAbs(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("clamp",   "::VectorClamp(",                "::VectorDoubleClamp(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("floor",   "::VectorFloor(",                "::VectorDoubleFloor(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("ceil",    "::VectorCeiling(",              "::VectorDoubleCeiling(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("round",   "::VectorRound(",                "::VectorDoubleRound(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("sqrt",    "::VectorSqrt(",                 "::VectorDoubleSqrt(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("rsqrt",   "::VectorReciprocalSqrt(",       "::VectorDoubleReciprocalSqrt(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("inverse", "::MatrixInverse(nullptr, ",     "::MatrixDoubleInverse(nullptr, ")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("sin",     "::VectorSin(",                  "::VectorDoubleSin(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("cos",     "::VectorCos(",                  "::VectorDoubleCos(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("tan",     "::VectorTan(",                  "::VectorDoubleTan(")

NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC("cross",     "Cross",     3u)
NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC("normalize", "Normalize", 4u)

NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("min")
NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("max")
NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("clamp")
NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("abs")

NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION("dot",      "Dot")
NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION("length",   "Length")
NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION("lengthSq", "LengthSq")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION
#endif

#ifdef NWB_ADHD_EXPORT_SIMD_INT_FALLBACK_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_INT_FALLBACK_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_INT_FALLBACK
#endif

#ifdef NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC
#endif

#ifdef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC
#endif

#ifdef NWB_ADHD_EXPORT_FUNCTION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

