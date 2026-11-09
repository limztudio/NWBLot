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

#ifndef NWB_ADHD_EXPORT_FUNCTION5
#define NWB_ADHD_EXPORT_FUNCTION5(adhdName, cppExportName, arity, returnRule, returnTypeName, returnTypeArgName, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule)
#define NWB_ADHD_EXPORT_FUNCTION5_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_REGISTER_CALL
#define NWB_ADHD_EXPORT_SIMD_REGISTER_CALL(adhdName)
#define NWB_ADHD_EXPORT_SIMD_REGISTER_CALL_AUTO_UNDEF
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
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutU64, "", true, "u64", false, false, "built-in function argument must be a mutable u64 variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutU32,     "",           true,  "u32",   false, false, "built-in function argument must be a mutable 'u32' variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutUsize,   "",           true,  "usize", false, false, "built-in function argument must be a mutable 'usize' variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(Integer,    "",           false, "",      false, true,  "built-in function argument must be an integer type")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(VectorSingleArg, "",      false, "VectorSingle", false, false, "built-in function argument must be VectorSingle")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MatrixSingleArg, "",      false, "MatrixSingle", false, false, "built-in function argument must be MatrixSingle")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(NameHashValue, "",       false, "NameHash", false, false, "built-in function argument must be NameHash")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(F64Scalar, "", false, "f64", false, false, "built-in function argument must be f64")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(BoolScalar, "", false, "b8", false, false, "built-in function argument must be b8")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(U64Scalar, "", false, "u64", false, false, "built-in function argument must be u64")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(U32Scalar, "", false, "u32", false, false, "built-in function argument must be u32")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(F32Scalar, "", false, "f32", false, false, "built-in function argument must be f32")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutByteFixedVector, "", true, "FixedVector", false, false, "built-in function argument must be a mutable FixedVector<u8, N>")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(ByteSpan,   "",           false, "Span",  false, false, "built-in function argument must be Span<u8>")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(UsizeSpan,  "",           false, "Span",  false, false, "built-in function argument must be Span<usize>")

NWB_ADHD_EXPORT_FUNCTION("Min",                    "::Min",                    2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Max",                    "::Max",                    2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Abs",                    "::Abs",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Clamp",                  "::Clamp",                  3u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Saturate",               "::Saturate",               1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Floor",                  "::Floor",                  1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Ceil",                   "::Ceil",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Round",                  "::Round",                  1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Sqrt",                   "::Sqrt",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Rsqrt",                  "::VectorReciprocalSqrt",   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Lerp",                   "::VectorLerp",             3u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Dot",                    "::Vector4Dot",             2u, LaneScalar,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Cross",                  "::Vector3Cross",           2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Length",                 "::Vector4Length",          1u, LaneScalar,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("LengthSq",               "::Vector4LengthSq",        1u, LaneScalar,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Normalize",              "::Vector4Normalize",       1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Mul",                    "::MatrixMultiply",         2u, MatrixMultiply, "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Transpose",              "::MatrixTranspose",        1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Inverse",                "::MatrixInverse",          1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Sin",                    "::Sin",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Cos",                    "::Cos",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Tan",                    "::Tan",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("IsFinite",               "::IsFinite",               1u, FixedType,      "b8",         "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("DivideUp",               "::DivideUp",               2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("AlignUp",                "::AlignUp",                2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("HashValue",              "::HashValue",              1u, FixedType,      "usize",      "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("HashCombineValue",       "::HashCombineValue",       2u, FixedType,      "usize",      "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("MakeUnexpected",         "::MakeUnexpected",         1u, Unexpected,     "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("TerminateInvariant",     "::TerminateInvariant",     0u, FixedType,      "void",       "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("IsUnicodeWhitespace", "::IsUnicodeWhitespace", 1u, FixedType, "b8", "", Integer, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("IsAsciiPunctuation", "::IsAsciiPunctuation", 1u, FixedType, "b8", "", Integer, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("ComputeFnv64AsciiLowerText", "::ComputeFnv64AsciiLowerText", 1u, FixedType, "u64", "", TextView, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("IsAsciiSpace",           "::IsAsciiSpace",           1u, FixedType,      "b8",         "",    Integer,    Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("IsAsciiAlpha",           "::IsAsciiAlpha",           1u, FixedType,      "b8",         "",    Integer,    Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("IsAsciiIdentifierChar",  "::IsAsciiIdentifierChar",  1u, FixedType,      "b8",         "",    Integer,    Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("HasEmbeddedNull",       "::HasEmbeddedNull",        1u, FixedType,      "b8",         "",    TextView,   Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("WriteF32LittleEndianAt", "::WriteF32LittleEndianAt", 3u, FixedType, "void", "", F32Scalar, Integer, MutByteFixedVector)
NWB_ADHD_EXPORT_FUNCTION("AlignU32PowerOfTwoChecked", "::AlignU32PowerOfTwoChecked", 2u, FixedType, "Expected", "u32", Integer, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("StripMatchingTrailingByte", "::StripMatchingTrailingByte", 3u, FixedType, "StringView", "", TextView, Integer, Integer)
NWB_ADHD_EXPORT_FUNCTION("ReadCommaOrSpaceDelimitedToken", "::ReadCommaOrSpaceDelimitedToken", 2u, FixedType, "Expected", "StringView", TextView, MutUsize, Any)
NWB_ADHD_EXPORT_FUNCTION("ReadAdmittedU16BigEndian", "::ReadAdmittedU16BigEndian", 2u, FixedType, "u16", "", ByteSpan, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("ReadAdmittedU32BigEndian", "::ReadAdmittedU32BigEndian", 2u, FixedType, "u32", "", ByteSpan, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("ReadPartialU32LittleEndian", "::ReadPartialU32LittleEndian", 2u, FixedType, "u32", "", ByteSpan, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("AlignUpChecked", "::AlignUpCheckedSize", 2u, FixedType, "Expected", "usize", Integer, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("LowerBoundIndex",       "::LowerBoundIndex",        2u, FixedType,      "usize",      "",    UsizeSpan,  Integer, Any)
NWB_ADHD_EXPORT_FUNCTION5("StepF64Bounded", "::StepF64Bounded", 5u, FixedType, "f64", "", F64Scalar, F64Scalar, F64Scalar, F64Scalar, BoolScalar)
NWB_ADHD_EXPORT_FUNCTION("NormalizeFiniteRange", "::NormalizeFiniteRange", 3u, FixedType, "Expected", "f64", F64Scalar, F64Scalar, F64Scalar)
NWB_ADHD_EXPORT_FUNCTION("InterpolateFiniteRange", "::InterpolateFiniteRange", 3u, FixedType, "Expected", "f64", F64Scalar, F64Scalar, F64Scalar)
NWB_ADHD_EXPORT_FUNCTION("AdvanceWrappingNonzeroU64", "::AdvanceWrappingNonzero", 1u, FixedType, "void", "", MutU64, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("DivideUpWrappingU32", "::DivideUpWrapping", 2u, FixedType, "u32", "", U32Scalar, U32Scalar, Any)
NWB_ADHD_EXPORT_FUNCTION("DivideUpWrappingU64", "::DivideUpWrapping", 2u, FixedType, "u64", "", U64Scalar, U64Scalar, Any)
NWB_ADHD_EXPORT_FUNCTION("BitWidthLowU32", "::BitWidthLowU32", 2u, FixedType, "u32", "", U32Scalar, U32Scalar, Any)
NWB_ADHD_EXPORT_FUNCTION("ComputeSobol2U32", "::ComputeSobol2U32", 1u, FixedType, "u64", "", U32Scalar, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("RoundF64ToF32Bound", "::RoundF64ToF32Bound", 2u, FixedType, "f32", "", F64Scalar, BoolScalar, Any)
NWB_ADHD_EXPORT_FUNCTION("ClassifyIntegerGrammar", "::ClassifyIntegerGrammar", 1u, FixedType, "u8", "", TextView, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("ClassifyFloatGrammar", "::ClassifyFloatGrammar", 1u, FixedType, "u8", "", TextView, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("SkipNumericSign",        "::SkipNumericSign",        1u, FixedType,      "usize",      "",    TextView,   Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("IntersectRectBoundsVec",           "::IntersectRectBoundsValue",            2u, FixedType, "VectorSingle", "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("HasVisibleRectIntersectionVec",    "::HasVisibleRectIntersection",          2u, FixedType, "b8",           "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("IntersectRectVec",                 "::IntersectRectValuePreservingExtent",  2u, FixedType, "VectorSingle", "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("InsetRectVec",                     "::InsetRectValuePreservingExtent",      2u, FixedType, "VectorSingle", "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("FoldOctahedralAxis",               "::FoldOctahedralAxis",                  1u, FixedType, "VectorSingle", "", VectorSingleArg, Any,             Any)
NWB_ADHD_EXPORT_FUNCTION("MatrixRotationRollPitchYawFromVector", "::MatrixRotationRollPitchYawFromVector", 1u, FixedType, "MatrixSingle", "", VectorSingleArg, Any, Any)
NWB_ADHD_EXPORT_FUNCTION5("MatrixAffineTransformation",      "::MatrixAffineTransformation",          4u, FixedType, "MatrixSingle", "", VectorSingleArg, VectorSingleArg, VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("MatrixIsInvertibleAffine",         "::MatrixIsInvertibleAffine",            3u, FixedType, "b8",           "", MatrixSingleArg, F32Scalar,       F32Scalar)
NWB_ADHD_EXPORT_FUNCTION("QuaternionIdentity",               "::QuaternionIdentity",                  0u, FixedType, "VectorSingle", "", Any,             Any,             Any)
NWB_ADHD_EXPORT_FUNCTION("QuaternionRotationRollPitchYawFromVector", "::QuaternionRotationRollPitchYawFromVector", 1u, FixedType, "VectorSingle", "", VectorSingleArg, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("QuaternionMultiply",               "::QuaternionMultiply",                  2u, FixedType, "VectorSingle", "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("Vector3Rotate",                    "::Vector3Rotate",                       2u, FixedType, "VectorSingle", "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("UpdateFnv64Byte",     "::UpdateFnv64Byte",            2u, FixedType, "u64", "", Integer, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("UpdateFnv64U32",      "::UpdateFnv64U32LittleEndian", 2u, FixedType, "u64", "", Integer, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("UpdateFnv64U64",      "::UpdateFnv64U64LittleEndian", 2u, FixedType, "u64", "", Integer, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("FoldNameHash64", "::FoldNameHash64", 1u, FixedType, "u64", "", NameHashValue, Any, Any)
NWB_ADHD_EXPORT_FUNCTION("UpdateFnv64NameHash", "::UpdateFnv64NameHash",        2u, FixedType, "u64", "", Integer, NameHashValue, Any)
NWB_ADHD_EXPORT_FUNCTION("EqualF32x4Bits", "::Vector4EqualInt", 2u, FixedType, "b8", "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("HashCombineF32x4", "::HashCombineF32x4", 2u, FixedType, "void", "", VectorSingleArg, MutUsize, Any)
NWB_ADHD_EXPORT_FUNCTION("PackUnorderedU32Pair", "::PackUnorderedU32Pair", 2u, FixedType, "u64", "", Integer, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION("TrimView",               "::TrimView",               1u, FixedType,      "StringView", "",    StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("TrimLeftView",           "::TrimLeftView",           1u, FixedType,      "StringView", "",    StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ParseI64",               "::ParseI64",               1u, FixedType,      "Expected",   "i64", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ParseU64",               "::ParseU64",               1u, FixedType,      "Expected",   "u64", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ParseF32",               "::ParseF32FromChars",      1u, FixedType,      "Expected",   "f32", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ParseF64",               "::ParseF64FromChars",      1u, FixedType,      "Expected",   "f64", StringView, Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("TryDecodeUtf8CodePoint", "::TryDecodeUtf8CodePoint", 3u, FixedType,      "b8",         "",    TextView,   MutU32,  MutUsize)
NWB_ADHD_EXPORT_FUNCTION("TryDecodeUtf8Scalar",    "::TryDecodeUtf8Scalar",    3u, FixedType,      "b8",         "",    TextView,   MutU32,  MutUsize)
NWB_ADHD_EXPORT_FUNCTION("IsUtf8ScalarBoundary",   "::IsUtf8ScalarBoundary",   2u, FixedType,      "b8",         "",    TextView,   Integer, Any)

NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("EqualF32x4Bits")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("HashCombineF32x4")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("IntersectRectBoundsVec")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("HasVisibleRectIntersectionVec")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("IntersectRectVec")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("InsetRectVec")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("FoldOctahedralAxis")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("MatrixRotationRollPitchYawFromVector")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("MatrixAffineTransformation")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("MatrixIsInvertibleAffine")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("QuaternionIdentity")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("QuaternionRotationRollPitchYawFromVector")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("QuaternionMultiply")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("Vector3Rotate")

NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Min",     "::VectorMin(",                  "::VectorDoubleMin(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Max",     "::VectorMax(",                  "::VectorDoubleMax(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Abs",      "::VectorAbs(",                  "::VectorDoubleAbs(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Clamp",    "::VectorClamp(",                "::VectorDoubleClamp(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Saturate", "::VectorSaturate(",             "::VectorDoubleSaturate(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Floor",    "::VectorFloor(",                "::VectorDoubleFloor(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Ceil",    "::VectorCeiling(",              "::VectorDoubleCeiling(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Round",   "::VectorRound(",                "::VectorDoubleRound(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Sqrt",    "::VectorSqrt(",                 "::VectorDoubleSqrt(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Rsqrt",   "::VectorReciprocalSqrt(",       "::VectorDoubleReciprocalSqrt(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Inverse", "::MatrixInverse(nullptr, ",     "::MatrixDoubleInverse(nullptr, ")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Sin",     "::VectorSin(",                  "::VectorDoubleSin(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Cos",     "::VectorCos(",                  "::VectorDoubleCos(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Tan",     "::VectorTan(",                  "::VectorDoubleTan(")

NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC("Cross",     "Cross",     3u)
NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC("Normalize", "Normalize", 4u)

NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("Min")
NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("Max")
NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("Clamp")
NWB_ADHD_EXPORT_SIMD_INT_FALLBACK("Abs")

NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION("Dot",      "Dot")
NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION("Length",   "Length")
NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION("LengthSq", "LengthSq")

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
NWB_ADHD_EXPORT_MEMBER_METHOD("fill",                    true,  false, true,  false, FixedType,              "void")
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
NWB_ADHD_EXPORT_BUILTIN_FIELD("EntityID", "id",          "u32",   "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("NameHash", "qwords",      "Array", "u64", "8u")

NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_InvalidEntityId", "::NWB::Core::ECS::s_InvalidEntityId", "EntityID")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NumericGrammarComplete", "::s_NumericGrammarComplete", "u8")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NumericGrammarIncomplete", "::s_NumericGrammarIncomplete", "u8")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_Fnv64OffsetBasis", "::s_Fnv64OffsetBasis", "u64")
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

#ifdef NWB_ADHD_EXPORT_SIMD_REGISTER_CALL_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_REGISTER_CALL_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_REGISTER_CALL
#endif

#ifdef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC
#endif

#ifdef NWB_ADHD_EXPORT_FUNCTION5_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION5_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION5
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

