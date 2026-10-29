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

#ifndef NWB_ADHD_EXPORT_MEMBER_METHOD
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, adaptsFirstArgToElement, returnRule)
#define NWB_ADHD_EXPORT_MEMBER_METHOD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT(adhdName, cppExportName, adhdTypeName)
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
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

NWB_ADHD_EXPORT_MEMBER_METHOD("push_back",               true,  true,  Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("emplace_back",            true,  true,  Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("pop_back",                true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("push_front",              true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("emplace_front",           true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("pop_front",               true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("push",                    true,  true,  Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("pop",                     true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("emplace",                 true,  true,  Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("try_emplace",             true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("insert_or_assign",        true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("clear",                   true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("insert",                  true,  true,  Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("erase",                   true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("resize",                  true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("reserve",                 true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("shrink_to_fit",           true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("swap",                    true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("assign",                  true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("append",                  true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("reset",                   true,  false, Void)
NWB_ADHD_EXPORT_MEMBER_METHOD("find",                    false, false, None)
NWB_ADHD_EXPORT_MEMBER_METHOD("size",                    false, false, Usize)
NWB_ADHD_EXPORT_MEMBER_METHOD("length",                  false, false, Usize)
NWB_ADHD_EXPORT_MEMBER_METHOD("capacity",                false, false, Usize)
NWB_ADHD_EXPORT_MEMBER_METHOD("count",                   false, false, Usize)
NWB_ADHD_EXPORT_MEMBER_METHOD("max_size",                false, false, Usize)
NWB_ADHD_EXPORT_MEMBER_METHOD("hash",                    false, false, Usize)
NWB_ADHD_EXPORT_MEMBER_METHOD("empty",                   false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("hasValue",                false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("has_value",               false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("valid",                   false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("contains",                false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("starts_with",             false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("ends_with",               false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("compare_exchange_weak",   false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("compare_exchange_strong", false, false, Bool)
NWB_ADHD_EXPORT_MEMBER_METHOD("c_str",                   false, false, StringView)
NWB_ADHD_EXPORT_MEMBER_METHOD("subview",                 false, false, StringView)
NWB_ADHD_EXPORT_MEMBER_METHOD("substr",                  false, false, StringView)
NWB_ADHD_EXPORT_MEMBER_METHOD("value",                   false, false, OptionalOrExpectedValue)
NWB_ADHD_EXPORT_MEMBER_METHOD("error",                   false, false, ExpectedError)
NWB_ADHD_EXPORT_MEMBER_METHOD("front",                   false, false, FirstTypeArg)
NWB_ADHD_EXPORT_MEMBER_METHOD("back",                    false, false, FirstTypeArg)
NWB_ADHD_EXPORT_MEMBER_METHOD("load",                    false, false, AtomicValue)
NWB_ADHD_EXPORT_MEMBER_METHOD("exchange",                false, false, AtomicValue)
NWB_ADHD_EXPORT_MEMBER_METHOD("fetch_add",               false, false, AtomicValue)
NWB_ADHD_EXPORT_MEMBER_METHOD("fetch_sub",               false, false, AtomicValue)

NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_InvalidEntityId", "::NWB::Core::ECS::s_InvalidEntityId", "EntityID")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NameNone",        "s_NameNone",                         "Name")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
#endif

#ifdef NWB_ADHD_EXPORT_MEMBER_METHOD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_MEMBER_METHOD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
#endif

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
