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

#ifndef NWB_ADHD_EXPORT_FUNCTION_EX
#define NWB_ADHD_EXPORT_FUNCTION_EX(adhdName, cppExportName, minArity, arity, returnRule, returnTypeName, returnTypeArgName, returnNestedTypeArgName, returnErrorTypeName, requiredHeader, prependsScratchArena, isVariadic, wrapsFormatArgs, arg0Rule, arg1Rule, arg2Rule, arg3Rule, arg4Rule)
#define NWB_ADHD_EXPORT_FUNCTION_EX_AUTO_UNDEF
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

#ifndef NWB_ADHD_EXPORT_RECEIVER_METHOD_EX
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_EX(receiverTypeName, methodName, returnTypeName, returnTypeArgName, returnErrorTypeName, synthesizeTypeRef)
#define NWB_ADHD_EXPORT_RECEIVER_METHOD_EX_AUTO_UNDEF
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
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutVectorSingleArg, "",   true,  "VectorSingle", false, false, "built-in function argument must be a mutable VectorSingle variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MatrixSingleArg, "",      false, "MatrixSingle", false, false, "built-in function argument must be MatrixSingle")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(NameHashValue, "",       false, "NameHash", false, false, "built-in function argument must be NameHash")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutNameHashValue, "",    true,  "NameHash", false, false, "built-in function argument must be a mutable NameHash variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(NameValue,     "",       false, "Name",     false, false, "built-in function argument must be Name")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(F64Scalar, "", false, "f64", false, false, "built-in function argument must be f64")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(BoolScalar, "", false, "b8", false, false, "built-in function argument must be b8")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(U64Scalar, "", false, "u64", false, false, "built-in function argument must be u64")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(U32Scalar, "", false, "u32", false, false, "built-in function argument must be u32")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(F32Scalar, "", false, "f32", false, false, "built-in function argument must be f32")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutByteFixedVector, "", true, "FixedVector", false, false, "built-in function argument must be a mutable FixedVector<u8, N>")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(ByteSpan,   "",           false, "Span",  false, false, "built-in function argument must be Span<u8>")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(UsizeSpan,  "",           false, "Span",  false, false, "built-in function argument must be Span<usize>")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutAny,             "",           true,  "",                      false, false, "built-in function argument must be a mutable variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutString,          "",           true,  "String",                false, false, "built-in function argument must be a mutable String variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(PathValue,          "",           false, "Path",                  false, false, "built-in function argument must be Path")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutPathValue,       "",           true,  "Path",                  false, false, "built-in function argument must be a mutable Path variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(TimerValue,         "",           false, "Timer",                 false, false, "built-in function argument must be Timer")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(MutTimerValue,      "",           true,  "Timer",                 false, false, "built-in function argument must be a mutable Timer variable")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(FpEnvValue,         "",           false, "FloatingPointEnvironment", false, false, "built-in function argument must be FloatingPointEnvironment")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(CpuPlacementValue,  "",           false, "CpuWorkerPlacement",    false, false, "built-in function argument must be CpuWorkerPlacement")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(DiagnosticRecordValue, "",        false, "DiagnosticEventRecord", false, false, "built-in function argument must be DiagnosticEventRecord")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(I32Scalar,          "i32",        false, "",                      false, true,  "built-in function argument must be an integer type")
NWB_ADHD_EXPORT_FUNCTION_ARG_RULE(I64Scalar,          "i64",        false, "",                      false, true,  "built-in function argument must be an integer type")

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
NWB_ADHD_EXPORT_FUNCTION("ASin",                   "::ASin",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ACos",                   "::ACos",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ATan",                   "::ATan",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("ATan2",                  "::ATan2",                  2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Exp",                    "::Exp",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Exp2",                   "::Exp2",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Log",                    "::Log",                    1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Log2",                   "::Log2",                   1u, PromoteSimd,    "",           "",    Any,        Any,     Any)
NWB_ADHD_EXPORT_FUNCTION("Pow",                    "::Pow",                    2u, PromoteSimd,    "",           "",    Any,        Any,     Any)
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
NWB_ADHD_EXPORT_FUNCTION("Vector3Transform",                 "::Vector3Transform",                    2u, FixedType, "VectorSingle", "", VectorSingleArg, MatrixSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION("MatrixDeterminant",                "::MatrixDeterminant",                   1u, FixedType, "VectorSingle", "", MatrixSingleArg, Any,             Any)
NWB_ADHD_EXPORT_FUNCTION("AabbValid",                        "::AabbTests::Valid",                    2u, FixedType, "b8",           "", VectorSingleArg, VectorSingleArg, Any)
NWB_ADHD_EXPORT_FUNCTION5("TryTransformAabb",                "::TryTransformAabb",                    5u, FixedType, "b8",           "", MatrixSingleArg, VectorSingleArg, VectorSingleArg, MutVectorSingleArg, MutVectorSingleArg)
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
NWB_ADHD_EXPORT_FUNCTION("EqualsAsciiIgnoreCase",  "::EqualsAsciiIgnoreCase<char>",   2u, FixedType, "b8",       "",     TextView,          TextView, Any)
NWB_ADHD_EXPORT_FUNCTION("ContainsAsciiIgnoreCase", "::ContainsAsciiIgnoreCase<char>", 2u, FixedType, "b8",       "",     TextView,          TextView, Any)
NWB_ADHD_EXPORT_FUNCTION("ParseName",              "::ParseNameText",                 1u, FixedType, "Expected", "Name", TextView,          Any,      Any)
NWB_ADHD_EXPORT_FUNCTION("DeriveName",             "::DeriveName<char>",              2u, FixedType, "Expected", "Name", NameValue,         TextView, Any)
NWB_ADHD_EXPORT_FUNCTION("BeginDerivedNameHash",   "::BeginDerivedNameHash",          1u, FixedType, "Expected", "NameHash", NameValue,     Any,      Any)
NWB_ADHD_EXPORT_FUNCTION("UpdateDerivedNameHashText", "::UpdateDerivedNameHashText<char>", 2u, FixedType, "b8",   "",     MutNameHashValue,  TextView, Any)
NWB_ADHD_EXPORT_FUNCTION("UpdateDerivedNameHashDecimal", "::UpdateDerivedNameHashDecimal", 2u, FixedType, "b8",   "",     MutNameHashValue,  Integer,  Any)
NWB_ADHD_EXPORT_FUNCTION("FinishDerivedNameHash",  "::FinishDerivedNameHash",         1u, FixedType, "Expected", "Name", NameHashValue,     Any,      Any)
NWB_ADHD_EXPORT_FUNCTION("ConvertFloatToHalf",     "::ConvertFloatToHalf",            1u, FixedType, "u16",      "",     F32Scalar,         Any,      Any)
NWB_ADHD_EXPORT_FUNCTION("ConvertHalfToFloat",     "::ConvertHalfToFloat",            1u, FixedType, "f32",      "",     Integer,           Any,      Any)
NWB_ADHD_EXPORT_FUNCTION("AllocateServiceIdentity", "::AllocateServiceIdentity",      0u, FixedType, "u64",      "",     Any,               Any,      Any)
NWB_ADHD_EXPORT_FUNCTION("AdvanceNonWrappingRevision", "::AdvanceNonWrappingRevision", 1u, FixedType, "void",    "",     MutU64,            Any,      Any)

NWB_ADHD_EXPORT_FUNCTION_EX("StringFormat",                  "::StringFormat",                              1u, 1u, FixedType,        "String",                    "",         "",          "",                      "",                                    true,  true,  true,  TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("StringAppendFormat",            "::StringAppendFormat",                        2u, 2u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, true,  true,  MutString,             TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("MakeDiagnosticEventText",       "::MakeDiagnosticEventText",                   1u, 1u, FixedType,        "DiagnosticEventText",       "",         "",          "",                      "",                                    false, true,  true,  TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("GetCurrentPath",                "::GetCurrentPath",                            0u, 0u, FixedType,        "Expected",                  "Path",     "",          "ErrorCode",             "",                                    true,  false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AbsolutePath",                  "::AbsolutePath",                              1u, 1u, FixedType,        "Expected",                  "Path",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ReadSymlink",                   "::ReadSymlink",                               1u, 1u, FixedType,        "Expected",                  "Path",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ResolveAbsolutePath",           "::ResolveAbsolutePath",                       2u, 2u, FixedType,        "Expected",                  "Path",     "",          "ErrorCode",             "",                                    true,  false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("LexicallyNormal",               "::LexicallyNormal",                           1u, 1u, FixedType,        "Path",                      "",         "",          "",                      "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CreateDirectories",             "::CreateDirectories",                         1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("EnsureDirectories",             "::EnsureDirectories",                         1u, 1u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("EnsureEmptyDirectory",          "::EnsureEmptyDirectory",                      1u, 1u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("RemoveFile",                    "::RemoveFile",                                1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("RemoveAll",                     "::RemoveAll",                                 1u, 1u, FixedType,        "Expected",                  "u64",      "",          "FileRemovalFailure",    "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("RemoveAllIfExists",             "::RemoveAllIfExists",                         1u, 1u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("RemoveAllForCleanup",           "::RemoveAllForCleanup",                       1u, 1u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "<global/filesystem/cleanup.h>",       false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("MovePathToDirectory",           "::MovePathToDirectory",                       2u, 2u, FixedType,        "Expected",                  "Path",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             PathValue, Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("RenamePath",                    "::RenamePath",                                2u, 2u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             PathValue, Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ResizeFile",                    "::ResizeFile",                                2u, 2u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("FileExists",                    "::FileExists",                                1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("FileExistsNoFollow",            "::FileExistsNoFollow",                        1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsDirectory",                   "::IsDirectory",                               1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsDirectoryNoFollow",           "::IsDirectoryNoFollow",                       1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsRegularFile",                 "::IsRegularFile",                             1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("FileSize",                      "::FileSize",                                  1u, 1u, FixedType,        "Expected",                  "u64",      "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CreateSizedFileExclusive",      "::CreateSizedFileExclusive",                  2u, 2u, FixedType,        "Expected",                  "void",     "",          "SizedFileCreateFailure","",                                    false, false, false, PathValue,             Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ReadTextFile",                  "::ReadTextFile",                              2u, 2u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             MutString, Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("WriteTextFile",                 "::WriteTextFile",                             2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, PathValue,             TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("WriteTextFileAtomic",           "::StagedFileWriter",                          2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, PathValue,             TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ReadBinaryFile",                "::ReadBinaryFile",                            2u, 2u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             MutAny,    Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("WriteBinaryFile",               "::WriteBinaryFile",                           2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("WriteBinaryFileAtomic",         "::StagedFileWriter",                          2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("WriteAtomicChecked",            "::StagedFileWriter",                          2u, 2u, FixedType,        "Expected",                  "void",     "",          "ErrorCode",             "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("VerifyBinaryFileBytes",         "::VerifyBinaryFileBytes",                     2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("PathToString",                  "::PathToString<char>",                        1u, 1u, FixedType,        "String",                    "",         "",          "",                      "",                                    true,  false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("PathToGenericString",           "::PathToGenericString<char>",                 1u, 1u, FixedType,        "String",                    "",         "",          "",                      "",                                    true,  false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("BuildCanonicalSafeCacheName",   "::BuildCanonicalSafeCacheName<char>",         1u, 1u, FixedType,        "String",                    "",         "",          "",                      "",                                    true,  false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("PathHasListedExtension",        "::PathHasListedExtension",                    2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, PathValue,             Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsPathPrefixText",              "::IsPathPrefixText<char>",                    2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, TextView,              TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsSingleLinePathText",          "::IsSingleLinePathText<char>",                1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CreateStagedDirectoryPaths",    "::StagedDirectoryPaths<::NWB::Core::Alloc::GlobalArena>::Create", 2u, 2u, FixedType, "StagedDirectoryPaths", "", "", "",                                  "",                                    true,  false, false, PathValue,             TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CanRepresentStreamSize",        "::CanRepresentStreamSize",                    1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("LastSystemError",               "::LastSystemError",                           0u, 0u, FixedType,        "ErrorCode",                 "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("MakeErrorCode",                 "::MakeErrorCode",                             1u, 1u, FixedType,        "ErrorCode",                 "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsMissingPathError",            "::IsMissingPathError",                        1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ReadEnvironmentVariable",       "::ReadEnvironmentVariable",                   1u, 1u, FixedType,        "Expected",                  "Optional", "String",    "ErrorCode",             "",                                    true,  false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("EnvironmentVariableEquals",     "::EnvironmentVariableEquals",                 2u, 2u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    true,  false, false, TextView,              TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("HasEnvironmentValue",           "::HasEnvironmentValue",                       1u, 1u, FixedType,        "Expected",                  "b8",       "",          "ErrorCode",             "",                                    true,  false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ComputeSha256",                 "::ComputeSha256",                             1u, 1u, FixedType,        "Sha256Digest",              "",         "",          "",                      "<global/sha256.h>",                   false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ParseSha256",                   "::ParseSha256",                               1u, 1u, FixedType,        "Expected",                  "Sha256Digest", "",      "",                      "<global/sha256.h>",                   false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ComputeFnv32TextHash",          "::ComputeFnv32TextHash",                      1u, 1u, FixedType,        "u32",                       "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("UpdateFnv64TextCanonical",      "::UpdateFnv64TextCanonical<char>",            2u, 2u, FixedType,        "u64",                       "",         "",          "",                      "",                                    false, false, false, Integer,               TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("UpdateFnv64TextExact",          "::UpdateFnv64TextExact<char>",                2u, 2u, FixedType,        "u64",                       "",         "",          "",                      "",                                    false, false, false, Integer,               TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsAsciiDigit",                  "::IsAsciiDigit",                              1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsAsciiLower",                  "::IsAsciiLower",                              1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsAsciiUpper",                  "::IsAsciiUpper",                              1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsAsciiAlphaNumeric",           "::IsAsciiAlphaNumeric",                       1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ToAsciiLower",                  "::ToAsciiLower",                              1u, 1u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ToAsciiUpper",                  "::ToAsciiUpper",                              1u, 1u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("Canonicalize",                  "::Canonicalize",                              1u, 1u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsAsciiIdentifier",             "::IsAsciiIdentifier<char>",                   1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("StartsWith",                    "::StartsWith<char>",                          2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, TextView,              TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("TruncateView",                  "::TruncateView<char>",                        2u, 2u, FixedType,        "StringView",                "",         "",          "",                      "",                                    false, false, false, TextView,              Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("UnquoteDoubleQuotedView",       "::UnquoteDoubleQuotedView<char>",             1u, 1u, FixedType,        "StringView",                "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("HasLineBreak",                  "::HasLineBreak<char>",                        1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("HasCrlfLineEndings",            "::HasCrlfLineEndings<char>",                 1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CountBareLineFeeds",            "::CountBareLineFeeds<char>",                  1u, 1u, FixedType,        "usize",                     "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("NextTextLine",                  "::NextTextLine<char>",                        2u, 2u, FixedType,        "Expected",                  "StringView", "",        "",                      "",                                    false, false, false, TextView,              MutUsize,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("NextTrimmedTextLine",           "::NextTrimmedTextLine<char>",                 2u, 2u, FixedType,        "Expected",                  "StringView", "",        "",                      "",                                    false, false, false, TextView,              MutUsize,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ParseDecimalPrefixU64",         "::ParseDecimalPrefixU64<char>",               1u, 1u, FixedType,        "u64",                       "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ParseHexU64",                   "::ParseHexU64<char>",                         1u, 1u, FixedType,        "Expected",                  "u64",      "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ParseVariableHexU64",           "::ParseVariableHexU64<char>",                 1u, 1u, FixedType,        "Expected",                  "u64",      "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("FormatHex64A",                  "::FormatHex64A",                              1u, 1u, FixedType,        "String",                    "",         "",          "",                      "",                                    true,  false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AppendUnsignedU64Text",         "::AppendUnsignedU64Text",                     2u, 2u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, false, false, MutAny,                Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AppendUnsignedU64Bytes",        "::AppendUnsignedU64Bytes",                    2u, 2u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, false, false, MutAny,                Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsValidUtf8TextWithoutNul",     "::IsValidUtf8TextWithoutNul",                 1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "<global/utf8_text.h>",                false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CountUtf8ScalarsAssumingValid", "::CountUtf8ScalarsAssumingValid",             1u, 1u, FixedType,        "usize",                     "",         "",          "",                      "<global/utf8_text.h>",                false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("Utf8ByteOffset",                "::Utf8ByteOffset",                            2u, 2u, FixedType,        "Expected",                  "usize",    "",          "",                      "<global/utf8_text.h>",                false, false, false, TextView,              Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("BuildGraphemeBoundaries",       "::BuildGraphemeBoundaries",                   1u, 1u, FixedType,        "Expected",                  "Vector",   "usize",     "",                      "<global/unicode/grapheme.h>",         true,  false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("LookupGraphemeBreak",           "::LookupGraphemeBreak",                       1u, 1u, FixedType,        "GraphemeBreak",             "",         "",          "",                      "<global/unicode/properties.h>",       false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("LookupIndicConjunct",           "::LookupIndicConjunct",                       1u, 1u, FixedType,        "IndicConjunct",             "",         "",          "",                      "<global/unicode/properties.h>",       false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsExtendedPictographic",        "::IsExtendedPictographic",                    1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "<global/unicode/properties.h>",       false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AppendDotQuotedText",           "::AppendDotQuotedText",                       2u, 2u, FixedType,        "void",                      "",         "",          "",                      "<global/dot_text.h>",                 false, false, false, MutString,             TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AppendJsonEscapedText",         "::AppendJsonEscapedText",                     2u, 2u, FixedType,        "void",                      "",         "",          "",                      "<global/json_text.h>",                false, false, false, MutString,             TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AppendJsonQuotedText",          "::AppendJsonQuotedText",                      2u, 2u, FixedType,        "void",                      "",         "",          "",                      "<global/json_text.h>",                false, false, false, MutString,             TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("Truncate",                      "::Truncate",                                  1u, 1u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("FMod",                          "::FMod",                                      2u, 2u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("Remainder",                     "::Remainder",                                 2u, 2u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("TanH",                          "::TanH",                                      1u, 1u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsNaN",                         "::IsNaN",                                     1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SignBit",                       "::SignBit",                                   1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DivideFloorI64",                "::DivideFloorI64",                            2u, 2u, FixedType,        "i64",                       "",         "",          "",                      "",                                    false, false, false, I64Scalar,             I64Scalar, Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DivideCeilI64",                 "::DivideCeilI64",                             2u, 2u, FixedType,        "i64",                       "",         "",          "",                      "",                                    false, false, false, I64Scalar,             I64Scalar, Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("FloorLog2",                     "::FloorLog2",                                 1u, 1u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("BitMask",                       "::BitMask",                                   1u, 1u, CallTemplateArg0, "",                          "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("HasSingleSetBit",               "::HasSingleSetBit",                           1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("LowestSetBit",                  "::LowestSetBit",                              1u, 1u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AlignmentOffsetPowerOfTwo",     "::AlignmentOffsetPowerOfTwo",                 2u, 2u, FixedType,        "usize",                     "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("PackU32Words",                  "::PackU32Words",                              2u, 2u, FixedType,        "u64",                       "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("LowU32Word",                    "::LowU32Word",                                1u, 1u, FixedType,        "u32",                       "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("HighU32Word",                   "::HighU32Word",                               1u, 1u, FixedType,        "u32",                       "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CountTrailingZeros",            "::CountTrailingZeros",                        1u, 1u, FixedType,        "i32",                       "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CountSetBits",                  "::CountSetBits",                              1u, 1u, FixedType,        "i32",                       "",         "",          "",                      "",                                    false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("BitCast",                       "::BitCast",                                   1u, 1u, CallTemplateArg0, "",                          "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DifferentF64Bits",              "::DifferentF64Bits",                          2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, F64Scalar,             F64Scalar, Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsNewerSerialU32",              "::IsNewerSerialU32",                          2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsNotOlderSerialU32",           "::IsNotOlderSerialU32",                       2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("IsSerialU32InWindow",           "::IsSerialU32InWindow",                       3u, 3u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Integer, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("NegateOverflows",               "::NegateOverflows",                           1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AddOverflows",                  "::AddOverflows",                              2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SubtractOverflows",             "::SubtractOverflows",                         2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("MultiplyOverflows",             "::MultiplyOverflows",                         2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DivideOverflows",               "::DivideOverflows",                           2u, 2u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("TryMultiply",                   "::TryMultiply",                               2u, 2u, CallTemplateArg0, "Expected",                  "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AddSaturating",                 "::AddSaturating",                             2u, 2u, CallTemplateArg0, "",                          "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SubtractSaturating",            "::SubtractSaturating",                        2u, 2u, PromoteSimd,      "",                          "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("MultiplySaturatingU64",         "::MultiplySaturatingU64",                     2u, 2u, FixedType,        "u64",                       "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AddSignedOffset",               "::AddSignedOffset",                           2u, 2u, FixedType,        "Expected",                  "u64",      "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SignedDifference",              "::SignedDifference",                          2u, 2u, FixedType,        "Expected",                  "i64",      "",          "",                      "",                                    false, false, false, Integer,               Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AccumulateBoundedCount",        "::AccumulateBoundedCount",                    3u, 3u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   MutU64, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("UnsignedRangesOverlap",         "::UnsignedRangesOverlap",                     4u, 4u, FixedType,        "b8",                        "",         "",          "",                      "",                                    false, false, false, Integer,               Integer,   Integer, Integer, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ComputeStridedRangeByteSize",   "::ComputeStridedRangeByteSize",               3u, 3u, FixedType,        "Expected",                  "u64",      "",          "",                      "",                                    false, false, false, Integer,               Integer,   Integer, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("TimerNow",                      "::TimerNow",                                  0u, 0u, FixedType,        "Timer",                     "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("TimerAddMS",                    "::TimerAddMS",                                2u, 2u, FixedType,        "Timer",                     "",         "",          "",                      "",                                    false, false, false, TimerValue,            Integer,   Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DurationInSeconds",             "::DurationInSeconds",                         1u, 2u, CallTemplateArg0, "",                          "",         "",          "",                      "",                                    false, false, false, TimerValue,            TimerValue,Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ConsumeTimerDeltaSeconds",      "::ConsumeTimerDeltaSeconds",                  1u, 1u, CallTemplateArg0, "",                          "",         "",          "",                      "",                                    false, false, false, MutTimerValue,         Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DurationInMS",                  "::DurationInMS",                              1u, 2u, CallTemplateArg0, "",                          "",         "",          "",                      "",                                    false, false, false, TimerValue,            TimerValue,Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DurationInNS",                  "::DurationInNS",                              1u, 2u, CallTemplateArg0, "",                          "",         "",          "",                      "",                                    false, false, false, TimerValue,            TimerValue,Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DurationInTimeDelta",           "::DurationInTimeDelta",                       1u, 2u, FixedType,        "TimerDelta",                "",         "",          "",                      "",                                    false, false, false, TimerValue,            TimerValue,Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SleepMS",                       "::SleepMS",                                   1u, 1u, FixedType,        "void",                      "",         "",          "",                      "<global/thread.h>",                   false, false, false, Integer,               Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CurrentThreadId",               "::CurrentThreadId",                           0u, 0u, FixedType,        "u32",                       "",         "",          "",                      "<global/thread.h>",                   false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CurrentProcessId",              "::CurrentProcessId",                          0u, 0u, FixedType,        "u32",                       "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("UpdateAtomicMaxRelaxed",        "::UpdateAtomicMaxRelaxed",                    2u, 2u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, false, false, MutAny,                Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DecrementAtomicOrTerminate",    "::DecrementAtomicOrTerminate",                1u, 1u, FixedType,        "u64",                       "",         "",          "",                      "",                                    false, false, false, MutAny,                Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AtomicThreadFence",             "::AtomicThreadFence",                         1u, 1u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AtomicSignalFence",             "::AtomicSignalFence",                         1u, 1u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("QueryCpuCoreCount",             "::QueryCpuCoreCount",                         1u, 1u, FixedType,        "u32",                       "",         "",          "",                      "<global/cpu_topology.h>",             false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SetCurrentThreadCpuPlacement",  "::SetCurrentThreadCpuPlacement",              1u, 1u, FixedType,        "b8",                        "",         "",          "",                      "<global/cpu_topology.h>",             false, false, false, CpuPlacementValue,     Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("GetFloatingPointEnvironment",   "::GetFloatingPointEnvironment",               0u, 0u, FixedType,        "Expected",                  "FloatingPointEnvironment", "", "",               "<global/floating_point_environment.h>",false, false, false, Any,                  Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("HoldFloatingPointExceptions",   "::HoldFloatingPointExceptions",               0u, 0u, FixedType,        "Expected",                  "FloatingPointEnvironment", "", "",               "<global/floating_point_environment.h>",false, false, false, Any,                  Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SetFloatingPointEnvironment",   "::SetFloatingPointEnvironment",               1u, 1u, FixedType,        "Expected",                  "void",     "",          "",                      "<global/floating_point_environment.h>",false, false, false, FpEnvValue,           Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("ClearFloatingPointExceptions",  "::ClearFloatingPointExceptions",              1u, 1u, FixedType,        "Expected",                  "void",     "",          "",                      "<global/floating_point_environment.h>",false, false, false, Integer,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("RaiseFloatingPointExceptions",  "::RaiseFloatingPointExceptions",              1u, 1u, FixedType,        "Expected",                  "void",     "",          "",                      "<global/floating_point_environment.h>",false, false, false, Integer,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("SetFloatingPointRoundingMode",  "::SetFloatingPointRoundingMode",              1u, 1u, FixedType,        "Expected",                  "void",     "",          "",                      "<global/floating_point_environment.h>",false, false, false, Integer,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("TestFloatingPointExceptions",   "::TestFloatingPointExceptions",               1u, 1u, FixedType,        "i32",                       "",         "",          "",                      "<global/floating_point_environment.h>",false, false, false, Integer,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CaptureDiagnosticEvent",        "::CaptureDiagnosticEvent",                    1u, 1u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, false, false, DiagnosticRecordValue, Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CaptureDiagnosticMessage",      "::CaptureDiagnosticEvent",                    2u, 2u, FixedType,        "void",                      "",         "",          "",                      "",                                    false, false, false, TextView,              TextView,  Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DiagnosticEventNameFromCategory", "::DiagnosticEventNameFromCategory",         1u, 1u, FixedType,        "StringView",                "",         "",          "",                      "",                                    false, false, false, TextView,              Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("DiagnosticEventNameFromRecord", "::DiagnosticEventNameFromRecord",             1u, 1u, FixedType,        "StringView",                "",         "",          "",                      "",                                    false, false, false, DiagnosticRecordValue, Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("CountMissingCacheEntries",      "::CountMissingCacheEntries",                  2u, 3u, FixedType,        "usize",                     "",         "",          "",                      "<global/range_algorithms.h>",         false, false, false, Any,                   Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("RetainReferencedCacheEntries",  "::RetainReferencedCacheEntries",              2u, 3u, FixedType,        "void",                      "",         "",          "",                      "<global/range_algorithms.h>",         false, false, false, MutAny,                Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("StableDeduplicate",             "::StableDeduplicate",                         2u, 2u, FixedType,        "void",                      "",         "",          "",                      "<global/range_algorithms.h>",         false, false, false, MutAny,                Any,       Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("FindDirectionalIndex",          "::FindDirectionalIndex",                      4u, 4u, FixedType,        "Expected",                  "u64",      "",          "",                      "<global/range_algorithms.h>",         false, false, false, Integer,               BoolScalar,Any, Any, Any)
NWB_ADHD_EXPORT_FUNCTION_EX("AllValidAndDistinct",           "::AllValidAndDistinct",                       3u, 3u, FixedType,        "b8",                        "",         "",          "",                      "<global/range_algorithms.h>",         false, false, false, Any,                   Any,       Any, Any, Any)

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
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("Vector3Transform")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("MatrixDeterminant")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("AabbValid")
NWB_ADHD_EXPORT_SIMD_REGISTER_CALL("TryTransformAabb")

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
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("ASin",    "::VectorASin(",                 "::VectorDoubleASin(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("ACos",    "::VectorACos(",                 "::VectorDoubleACos(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("ATan",    "::VectorATan(",                 "::VectorDoubleATan(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("ATan2",   "::VectorATan2(",                "::VectorDoubleATan2(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Exp",     "::VectorExp(",                  "::VectorDoubleExp(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Exp2",    "::VectorExp2(",                 "::VectorDoubleExp2(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Log",     "::VectorLog(",                  "::VectorDoubleLog(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Log2",    "::VectorLog2(",                 "::VectorDoubleLog2(")
NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC("Pow",     "::VectorPow(",                  "::VectorDoublePow(")

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
NWB_ADHD_EXPORT_MEMBER_METHOD("isAbsolute",              false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("hasFilename",             false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("parentPath",              false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("filename",                false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("stem",                    false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("extension",               false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("lexicallyNormal",         false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("lexicallyRelative",       false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("replaceExtension",        true,  false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("test",                    false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("testAndSet",              true,  false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("wait",                    false, false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("notifyAll",               true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("breaksBefore",            false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("advance",                 true,  false, false, false, FixedType,              "void")
NWB_ADHD_EXPORT_MEMBER_METHOD("path",                    false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("directory",               false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("name",                    false, false, false, false, FixedType,              "Name")
NWB_ADHD_EXPORT_MEMBER_METHOD("defaultStorageDirectory", false, false, false, false, FixedType,              "Path")
NWB_ADHD_EXPORT_MEMBER_METHOD("storageDirectory",        false, false, false, false, FixedType,              "Path")

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
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "parentPath",        "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "filename",          "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "stem",              "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "extension",         "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "lexicallyNormal",   "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "lexicallyRelative", "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "replaceExtension",  "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("Path",                  "append",            "Path",       true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("ErrorCode",             "value",             "i32",        false)
NWB_ADHD_EXPORT_RECEIVER_METHOD("DiagnosticEventText",   "view",              "StringView", true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("DiagnosticEventText",   "c_str",             "StringView", true)
NWB_ADHD_EXPORT_RECEIVER_METHOD_EX("ExecutablePaths",    "path",              "Expected",   "Path", "ErrorCode", true)
NWB_ADHD_EXPORT_RECEIVER_METHOD_EX("ExecutablePaths",    "directory",         "Expected",   "Path", "ErrorCode", true)
NWB_ADHD_EXPORT_RECEIVER_METHOD_EX("ExecutablePaths",    "name",              "Expected",   "Path", "ErrorCode", true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("ExecutablePaths",       "defaultStorageDirectory", "Path", true)
NWB_ADHD_EXPORT_RECEIVER_METHOD("ExecutablePaths",       "storageDirectory",        "Path", true)

NWB_ADHD_EXPORT_BUILTIN_FIELD("AssetRef", "virtualPath", "Name",  "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("EntityID", "id",          "u32",   "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("NameHash", "qwords",      "Array", "u64", "8u")
NWB_ADHD_EXPORT_BUILTIN_FIELD("Sha256Digest",          "bytes",                      "Array",          "u8",  "32u")
NWB_ADHD_EXPORT_BUILTIN_FIELD("StagedDirectoryPaths",  "stageDirectory",             "Path",           "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("StagedDirectoryPaths",  "backupDirectory",            "Path",           "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("FileRemovalFailure",    "code",                       "ErrorCode",      "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("FileRemovalFailure",    "removedCount",               "u64",            "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("SizedFileCreateFailure","code",                       "ErrorCode",      "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("SizedFileCreateFailure","created",                    "b8",             "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "event",                      "StringView",     "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "category",                   "StringView",     "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "expression",                 "StringView",     "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "message",                    "StringView",     "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "file",                       "StringView",     "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "instructionPointer",         "u64",            "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "line",                       "u32",            "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("DiagnosticEventRecord", "terminatesProcess",          "b8",             "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("CpuWorkerPlacement",    "logicalProcessorIndex",      "u32",            "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("CpuWorkerPlacement",    "processorGroup",             "u32",            "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("CpuWorkerPlacement",    "performanceClass",           "u32",            "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("CpuWorkerPlacement",    "affinity",                   "CpuAffinity",    "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("GraphemeBoundaryState", "regionalRun",                "usize",          "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("GraphemeBoundaryState", "previous",                   "GraphemeBreak",  "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("GraphemeBoundaryState", "pictographExtendRun",        "b8",             "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("GraphemeBoundaryState", "previousZwjAfterPictograph", "b8",             "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("GraphemeBoundaryState", "indicConsonantRun",          "b8",             "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("GraphemeBoundaryState", "indicLinker",                "b8",             "",    "")
NWB_ADHD_EXPORT_BUILTIN_FIELD("GraphemeBoundaryState", "first",                      "b8",             "",    "")

NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_InvalidEntityId", "::NWB::Core::ECS::s_InvalidEntityId", "EntityID")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NumericGrammarComplete", "::s_NumericGrammarComplete", "u8")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NumericGrammarIncomplete", "::s_NumericGrammarIncomplete", "u8")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NumericGrammarInvalid", "::s_NumericGrammarInvalid", "u8")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DecimalTextBufferBytes", "::s_DecimalTextBufferBytes", "usize")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_Utf8MaxSequenceBytes", "::s_Utf8MaxSequenceBytes", "usize")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_Fnv32OffsetBasis", "::s_Fnv32OffsetBasis", "u32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_Fnv32Prime",      "::s_Fnv32Prime",       "u32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_Fnv64OffsetBasis", "::s_Fnv64OffsetBasis", "u64")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_Fnv64Prime",      "::s_Fnv64Prime",       "u64")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_NameNone",        "s_NameNone",                          "Name")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_PI",              "::s_PI",               "f32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_2PI",             "::s_2PI",              "f32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_PIDIV2",          "::s_PIDIV2",           "f32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_PIDIV4",          "::s_PIDIV4",           "f32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_3PIDIV4",         "::s_3PIDIV4",          "f32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_1DIV2PI",         "::s_1DIV2PI",          "f32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DoublePI",        "::s_DoublePI",         "f64")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_Double2PI",       "::s_Double2PI",        "f64")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DoublePIDIV2",    "::s_DoublePIDIV2",     "f64")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DoublePIDIV4",    "::s_DoublePIDIV4",     "f64")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_MaxF32",          "::s_MaxF32",           "f32")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_MillisecondsInSecond", "::s_MillisecondsInSecond", "u64")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DiagnosticEventAssert",     "::DiagnosticEventName::s_Assert",     "StringView")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DiagnosticEventWarning",    "::DiagnosticEventName::s_Warning",    "StringView")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DiagnosticEventError",      "::DiagnosticEventName::s_Error",      "StringView")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DiagnosticEventFatal",      "::DiagnosticEventName::s_Fatal",      "StringView")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DiagnosticEventManualDump", "::DiagnosticEventName::s_ManualDump", "StringView")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DiagnosticEventCrash",      "::DiagnosticEventName::s_Crash",      "StringView")
NWB_ADHD_EXPORT_GLOBAL_CONSTANT("s_DiagnosticEventGpuCrash",   "::DiagnosticEventName::s_GpuCrash",   "StringView")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GLOBAL_CONSTANT
#endif

#ifdef NWB_ADHD_EXPORT_BUILTIN_FIELD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_FIELD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_FIELD
#endif

#ifdef NWB_ADHD_EXPORT_RECEIVER_METHOD_EX_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_EX_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_RECEIVER_METHOD_EX
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

#ifdef NWB_ADHD_EXPORT_FUNCTION_EX_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION_EX_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FUNCTION_EX
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

