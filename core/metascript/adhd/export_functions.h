// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE
#define NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(ruleName, expectedParamType, isMut, requiredExactType, requiresStringLike, requiresIntegerScalar, errorMessage)
#define NWB_ADHD_EXPORT_FUNCTION_ARG_RULE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_FUNCTION
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, arg0Rule, arg1Rule, arg2Rule)
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
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, firstArgIsMut, adaptsFirstArgToElement, adaptsSecondArgToElement, returnRule, returnTypeName)
#define NWB_ADHD_EXPORT_MEMBER_METHOD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_RECEIVER_METHOD
#define NWB_ADHD_EXPORT_RECEIVER_METHOD(receiverTypeName, methodName, returnTypeName, synthesizeTypeRef)
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_BUILTIN_FIELD
#define NWB_ADHD_EXPORT_BUILTIN_FIELD(receiverTypeName, fieldName, fieldTypeName, elemTypeName, countText)
#define NWB_ADHD_EXPORT_BUILTIN_FIELD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT(adhdName, cppExportName, adhdTypeName)
#define NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(Any,        "",           false, "",      false, false, "")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(StringView, "StringView", false, "",      false, false, "")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(TextView,   "StringView", false, "",      true,  false, "built-in function argument must be StringView, String, or CompactString")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutU32,     "",           true,  "u32",   false, false, "built-in function argument must be a mutable 'u32' variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutUsize,   "",           true,  "usize", false, false, "built-in function argument must be a mutable 'usize' variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(Integer,    "",           false, "",      false, true,  "built-in function argument must be an integer type")

NWB_ADHD_EXPORT_FUNCTION("min",                    "::Min",                    2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("max",                    "::Max",                    2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("abs",                    "::Abs",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("clamp",                  "::Clamp",                  3u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("saturate",               "::Saturate",               1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("floor",                  "::Floor",                  1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ceil",                   "::Ceil",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("round",                  "::Round",                  1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("sqrt",                   "::Sqrt",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("rsqrt",                  "::VectorReciprocalSqrt",   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("lerp",                   "::VectorLerp",             3u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("dot",                    "::Vector4Dot",             2u, LaneScalar,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("cross",                  "::Vector3Cross",           2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("length",                 "::Vector4Length",          1u, LaneScalar,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("lengthSq",               "::Vector4LengthSq",        1u, LaneScalar,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("normalize",              "::Vector4Normalize",       1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("mul",                    "::MatrixMultiply",         2u, MatrixMultiply, "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("transpose",              "::MatrixTranspose",        1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("inverse",                "::MatrixInverse",          1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("sin",                    "::Sin",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("cos",                    "::Cos",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("tan",                    "::Tan",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("isFinite",               "::IsFinite",               1u, FixedType,      "b8",         "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("divideUp",               "::DivideUp",               2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("alignUp",                "::AlignUp",                2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("hashValue",              "::HashValue",              1u, FixedType,      "usize",      "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("hashCombineValue",       "::HashCombineValue",       2u, FixedType,      "usize",      "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("makeUnexpected",         "::MakeUnexpected",         1u, Unexpected,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("trimView",               "::TrimView",               1u, FixedType,      "StringView", "",    StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("trimLeftView",           "::TrimLeftView",           1u, FixedType,      "StringView", "",    StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("parseI64",               "::ParseI64",               1u, FixedType,      "Expected",   "i64", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("parseU64",               "::ParseU64",               1u, FixedType,      "Expected",   "u64", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("parseF32",               "::ParseF32FromChars",      1u, FixedType,      "Expected",   "f32", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("parseF64",               "::ParseF64FromChars",      1u, FixedType,      "Expected",   "f64", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("tryDecodeUtf8CodePoint", "::TryDecodeUtf8CodePoint", 3u, FixedType,      "b8",         "",    TextView,   MutU32,  MutUsize)
NWB_ADHD_EXPORT_FUNCTION("tryDecodeUtf8Scalar",    "::TryDecodeUtf8Scalar",    3u, FixedType,      "b8",         "",    TextView,   MutU32,  MutUsize)
NWB_ADHD_EXPORT_FUNCTION("isUtf8ScalarBoundary",   "::IsUtf8ScalarBoundary",   2u, FixedType,      "b8",         "",    TextView,   Integer, Any)

NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("min",     "::VectorMin(",                  "::VectorDoubleMin(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("max",     "::VectorMax(",                  "::VectorDoubleMax(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("abs",      "::VectorAbs(",                  "::VectorDoubleAbs(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("clamp",    "::VectorClamp(",                "::VectorDoubleClamp(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("saturate", "::VectorSaturate(",             "::VectorDoubleSaturate(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("floor",    "::VectorFloor(",                "::VectorDoubleFloor(")
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

NWB_ADHD_EXPORT_MEMBER_METHOD("push_back",               true,  false, true,  false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("emplace_back",            true,  false, true,  false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("pop_back",                true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("push_front",              true,  false, true,  false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("emplace_front",           true,  false, true,  false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("pop_front",               true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("push",                    true,  false, true,  false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("pop",                     true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("emplace",                 true,  false, true,  false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("try_emplace",             true,  false, true,  true,  FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("insert_or_assign",        true,  false, true,  true,  FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("clear",                   true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("insert",                  true,  false, true,  false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("erase",                   true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("resize",                  true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("reserve",                 true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("shrink_to_fit",           true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("swap",                    true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("assign",                  true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("append",                  true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("reset",                   true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("store",                   true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("find",                    false, false, false, false, None,                   "")
NWB_ADHD_EXPORT_MEMBER_METHOD("size",                    false, false, false, false, FixedType,              "usize")
NWB_ADHD_EXPORT_MEMBER_METHOD("length",                  false, false, false, false, FixedType,              "usize")
NWB_ADHD_EXPORT_MEMBER_METHOD("capacity",                false, false, false, false, FixedType,              "usize")
NWB_ADHD_EXPORT_MEMBER_METHOD("count",                   false, false, false, false, FixedType,              "usize")
NWB_ADHD_EXPORT_MEMBER_METHOD("max_size",                false, false, false, false, FixedType,              "usize")
NWB_ADHD_EXPORT_MEMBER_METHOD("hash",                    false, false, false, false, FixedType,              "usize")
NWB_ADHD_EXPORT_MEMBER_METHOD("empty",                   false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("has_value",               false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("valid",                   false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("contains",                false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("starts_with",             false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("ends_with",               false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("compare_exchange_weak",   true,  true,  false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("compare_exchange_strong", true,  true,  false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("c_str",                   false, false, false, false, FixedType,              "StringView")
NWB_ADHD_EXPORT_MEMBER_METHOD("view",                    false, false, false, false, FixedType,              "StringView")
NWB_ADHD_EXPORT_MEMBER_METHOD("substr",                  false, false, false, false, FixedType,              "StringView")
NWB_ADHD_EXPORT_MEMBER_METHOD("subspan",                 false, false, false, false, ReceiverType,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("value",                   false, false, false, false, FirstTypeArg,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("error",                   false, false, false, false, SecondTypeArgOrDefault, "Failure")
NWB_ADHD_EXPORT_MEMBER_METHOD("front",                   false, false, false, false, FirstTypeArg,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("back",                    false, false, false, false, FirstTypeArg,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("load",                    false, false, false, false, FirstTypeArg,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("exchange",                true,  false, false, false, FirstTypeArg,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("fetch_add",               true,  false, false, false, FirstTypeArg,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("fetch_sub",               true,  false, false, false, FirstTypeArg,           "")

NWB_ADHD_EXPORT_RECEIVER_METHOD("Name",          "hash",         "NameHash",      false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Name",          "identityHash", "NameHash",      false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Name",          "resolvedText", "StringView",    true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("AssetRef",      "name",         "Name",          false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("EntityID",      "index",        "u32",           false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("EntityID",      "generation",   "u32",           false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("String",        "find",         "usize",         false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "find",         "usize",         false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("StringView",    "find",         "usize",         false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("String",        "substr",       "String",        true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "substr",       "CompactString", true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("StringView",    "substr",       "StringView",    true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("String",        "view",         "StringView",    true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "view",         "StringView",    true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("String",        "c_str",        "StringView",    true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "c_str",        "StringView",    true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("String",        "front",        "u8",            true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("String",        "back",         "u8",            true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "front",        "u8",            true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "back",         "u8",            true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("StringView",    "front",        "u8",            true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("StringView",    "back",         "u8",            true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "assign",       "b8",            false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("CompactString", "append",       "b8",            false)

NWB_ADHD_EXPORT_BUILTIN_FIELD("AssetRef", "virtualPath", "Name",  "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("NameHash", "qwords",      "Array", "u64", "8u")

NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_InvalidEntityId", "::NWB::Core::ECS::s_InvalidEntityId", "EntityID")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NameNone",        "s_NameNone",                          "Name")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
#endif

#ifdef NWB_ADHD_EXPORT_BUILTIN_FIELD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_FIELD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_FIELD
#endif

#ifdef NWB_ADHD_EXPORT_RECEIVER_METHOD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD
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

#ifdef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION_ARG_RULE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

