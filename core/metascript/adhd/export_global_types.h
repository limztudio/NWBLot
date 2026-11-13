// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_SCALAR_TYPE
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, cppType, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat)
#define NWB_ADHD_EXPORT_SCALAR_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SCALAR_LIMIT
#define NWB_ADHD_EXPORT_SCALAR_LIMIT(memberName, cppLimitSuffix, isMin)
#define NWB_ADHD_EXPORT_SCALAR_LIMIT_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc)
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, elemCount, byteSize, byteAlign, elemScalarType, rowVectorType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc)
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, laneScalarType, isHalfFloat)
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_OBJECT_TYPE
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation, arenaCtorMode, isCopyMoveConstructible, isAssignable, isAbstract, isPolymorphicBase, formatArgWrapKind, requiredHeader)
#define NWB_ADHD_EXPORT_OBJECT_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_BUILTIN_ENUM
#define NWB_ADHD_EXPORT_BUILTIN_ENUM(adhdName, cppTypeName, cppScopeName, byteSize, byteAlign, isFlag, requiredHeader)
#define NWB_ADHD_EXPORT_BUILTIN_ENUM_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY
#define NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY(adhdEnumName, entryName)
#define NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_GENERIC_TYPE
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, inlineLayoutKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, reflectedLinearKind, isReflectedMap, isAssetHandle, isCopyMoveConstructible, emitsFirstTypeArgOnlyInCpp, arenaFirstInCtor, requiredHeader)
#define NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE
#define NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE(ifaceName)
#define NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_MOVE_ONLY_TYPE
#define NWB_ADHD_EXPORT_MOVE_ONLY_TYPE(typeName)
#define NWB_ADHD_EXPORT_MOVE_ONLY_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_BINARY_HASH_TYPE
#define NWB_ADHD_EXPORT_BINARY_HASH_TYPE(adhdName, memberSuffix)
#define NWB_ADHD_EXPORT_BINARY_HASH_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_NWB_VALUE_TYPE
#define NWB_ADHD_EXPORT_NWB_VALUE_TYPE(adhdName, nwbCheckMethod, nwbAccessorMethod, valuePrefix, valueSuffix, directFieldPrefix, directFieldSuffix, optionalFieldPrefix, optionalFieldSuffix, requiresNameAdmission)
#define NWB_ADHD_EXPORT_NWB_VALUE_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_IMPLICIT_CONVERSION
#define NWB_ADHD_EXPORT_IMPLICIT_CONVERSION(fromTypeName, toTypeName, propagatesFirstTypeArg)
#define NWB_ADHD_EXPORT_IMPLICIT_CONVERSION_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex, cppLaneCallSuffix)
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_LITERAL_TOKEN
#define NWB_ADHD_EXPORT_LITERAL_TOKEN(tokenType, exprKind, adhdTypeName)
#define NWB_ADHD_EXPORT_LITERAL_TOKEN_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_SCALAR_TYPE("b8",    b8,    1u, 1u, false, false, false, false)
NWB_ADHD_EXPORT_SCALAR_TYPE("i8",    i8,    1u, 1u, true,  true,  true,  false)
NWB_ADHD_EXPORT_SCALAR_TYPE("u8",    u8,    1u, 1u, true,  true,  false, false)
NWB_ADHD_EXPORT_SCALAR_TYPE("i16",   i16,   2u, 2u, true,  true,  true,  false)
NWB_ADHD_EXPORT_SCALAR_TYPE("u16",   u16,   2u, 2u, true,  true,  false, false)
NWB_ADHD_EXPORT_SCALAR_TYPE("i32",   i32,   4u, 4u, true,  true,  true,  false)
NWB_ADHD_EXPORT_SCALAR_TYPE("u32",   u32,   4u, 4u, true,  true,  false, false)
NWB_ADHD_EXPORT_SCALAR_TYPE("i64",   i64,   8u, 8u, true,  true,  true,  false)
NWB_ADHD_EXPORT_SCALAR_TYPE("u64",   u64,   8u, 8u, true,  true,  false, false)
NWB_ADHD_EXPORT_SCALAR_TYPE("isize", isize, 8u, 8u, true,  false, true,  false)
NWB_ADHD_EXPORT_SCALAR_TYPE("usize", usize, 8u, 8u, true,  false, false, false)
NWB_ADHD_EXPORT_SCALAR_TYPE("f32",   f32,   4u, 4u, false, false, true,  true)
NWB_ADHD_EXPORT_SCALAR_TYPE("f64",   f64,   8u, 8u, false, false, true,  true)

NWB_ADHD_EXPORT_SCALAR_LIMIT("min", ">::s_Min", true)
NWB_ADHD_EXPORT_SCALAR_LIMIT("max", ">::s_Max", false)

NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("VectorSingle", "::SIMDVectorSingle", 4u, 3u, 16u, 16u, "f32", "f32", true,  false, false, false, false, "::SIMDVectorSingle", "::LoadFloat",     "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("VectorDouble", "::SIMDVectorDouble", 4u, 3u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x4",        "Float4",             4u, 3u, 16u, 16u, "f32", "f32", true,  false, false, false, false, "::SIMDVectorSingle", "::LoadFloat",     "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("i32x4",        "Int4",               4u, 0u, 16u, 16u, "i32", "i32", false, false, true,  true,  false, "::SIMDVectorSingle", "::LoadInt",       "::StoreInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("u32x4",        "UInt4",              4u, 0u, 16u, 16u, "u32", "u32", false, false, true,  false, false, "::SIMDVectorSingle", "::LoadInt",       "::StoreInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x3_i32",    "Float3Int",          4u, 0u, 16u, 16u, "f32", "i32", false, false, false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x3_u32",    "Float3UInt",         4u, 0u, 16u, 16u, "f32", "u32", false, false, false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x2",        "Double2",            2u, 0u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3",        "Double3",            3u, 0u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x4",        "Double4",            4u, 3u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("i64x4",        "Int64x4",            4u, 0u, 32u, 32u, "i64", "i64", false, true,  true,  true,  false, "::SIMDVectorDouble", "::LoadDoubleInt", "::StoreDoubleInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("u64x4",        "UInt64x4",           4u, 0u, 32u, 32u, "u64", "u64", false, true,  true,  false, false, "::SIMDVectorDouble", "::LoadDoubleInt", "::StoreDoubleInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3_i64",    "Double3Int",         4u, 0u, 32u, 32u, "f64", "i64", false, true,  false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3_u64",    "Double3UInt",        4u, 0u, 32u, 32u, "f64", "u64", false, true,  false, false, true,  "",                   "",                "")

NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("MatrixSingle", "::SIMDMatrix",       16u, 64u,  16u, "f32", "VectorSingle", false, "::SIMDMatrix",       "::LoadFloat",  "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("MatrixDouble", "::SIMDMatrixDouble", 16u, 128u, 32u, "f64", "VectorDouble", true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x3x4",      "Float34",            12u, 48u,  16u, "f32", "f32x4",        false, "::SIMDMatrix",       "::LoadFloat",  "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x4x4",      "Float44",            16u, 64u,  16u, "f32", "f32x4",        false, "::SIMDMatrix",       "::LoadFloat",  "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x3",      "Double33",           9u,  96u,  32u, "f64", "f64x3u",       true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x4",      "Double34",           12u, 96u,  32u, "f64", "f64x4",        true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x4x4",      "Double44",           16u, 128u, 32u, "f64", "f64x4",        true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x3x3u", "Float33U", 9u, 36u, 4u, "f32", "f32x3u", false, "::SIMDMatrix", "::LoadFloat", "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x3x4u", "Float34U", 12u, 48u, 4u, "f32", "f32x4u", false, "::SIMDMatrix", "::LoadFloat", "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x4x4u", "Float44U", 16u, 64u, 4u, "f32", "f32x4u", false, "::SIMDMatrix", "::LoadFloat", "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x3u", "Double33U", 9u, 72u, 8u, "f64", "f64x3u", true, "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x4u", "Double34U", 12u, 96u, 8u, "f64", "f64x4u", true, "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x4x4u", "Double44U", 16u, 128u, 8u, "f64", "f64x4u", true, "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")

NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f16x2u", "Half2U",    2u, 0u, 4u,  4u, "u16", true)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f16x4u", "Half4U",    4u, 2u, 8u,  4u, "u16", true)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f32x2u", "Float2U",   2u, 0u, 8u,  4u, "f32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f32x3u", "Float3U",   3u, 0u, 12u, 4u, "f32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f32x4u", "Float4U",   4u, 0u, 16u, 4u, "f32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i32x2u", "Int2U",     2u, 0u, 8u,  4u, "i32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i32x3u", "Int3U",     3u, 0u, 12u, 4u, "i32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i32x4u", "Int4U",     4u, 0u, 16u, 4u, "i32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u32x2u", "UInt2U",    2u, 0u, 8u,  4u, "u32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u32x3u", "UInt3U",    3u, 0u, 12u, 4u, "u32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u32x4u", "UInt4U",    4u, 0u, 16u, 4u, "u32", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f64x2u", "Double2U",  2u, 0u, 16u, 8u, "f64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f64x3u", "Double3U",  3u, 0u, 24u, 8u, "f64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f64x4u", "Double4U",  4u, 0u, 32u, 8u, "f64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i64x2u", "Int64x2U",  2u, 0u, 16u, 8u, "i64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i64x3u", "Int64x3U",  3u, 0u, 24u, 8u, "i64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i64x4u", "Int64x4U",  4u, 0u, 32u, 8u, "i64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u64x2u", "UInt64x2U", 2u, 0u, 16u, 8u, "u64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u64x3u", "UInt64x3U", 3u, 0u, 24u, 8u, "u64", false)
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u64x4u", "UInt64x4U", 4u, 0u, 32u, 8u, "u64", false)

NWB_ADHD_EXPORT_OBJECT_TYPE("String",                   "AString",                    32u,   8u, true,  true,  false, false, false, false, false, true,  true,  true,  true,  false, false, true,  true,  false, false, 1u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("StringView",               "AStringView",                16u,   8u, false, false, true,  true,  false, true,  true,  true,  true,  false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("CompactString",            "ACompactString",             256u,  1u, false, true,  false, false, true,  false, false, true,  true,  false, true,  false, false, true,  true,  false, false, 1u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("Name",                     "",                           8u,    8u, false, true,  true,  false, true,  true,  false, false, true,  false, true,  false, false, true,  true,  false, false, 3u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("Path",                     "::Path",                     32u,   8u, true,  true,  false, false, false, true,  false, false, true,  true,  true,  false, true,  true,  true,  false, false, 2u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ExecutablePaths",          "::ExecutablePaths",          8u,    8u, true,  true,  false, false, false, false, false, false, false, false, false, false, true,  true,  false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("StagedDirectoryPaths",     "::StagedDirectoryPaths",     64u,   8u, true,  true,  false, false, false, false, false, false, false, false, false, false, true,  true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("NameHash",                 "::NameHash",                 64u,   8u, false, true,  false, false, true,  false, true,  false, true,  false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("Sha256Digest",             "::Sha256Digest",             32u,   1u, false, true,  false, false, true,  false, true,  false, true,  false, false, false, false, true,  true,  false, false, 0u, "<global/sha256.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("Failure",                  "::Failure",                  1u,    1u, false, true,  true,  false, true,  false, false, false, false, false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ErrorCode",                "::ErrorCode",                16u,   8u, false, true,  true,  false, true,  false, false, false, true,  false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("FileRemovalFailure",       "::FileRemovalFailure",       24u,   8u, false, true,  false, false, false, false, true,  false, false, false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("SizedFileCreateFailure",   "::SizedFileCreateFailure",   24u,   8u, false, true,  false, false, false, false, true,  false, false, false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("Timer",                    "::Timer",                    8u,    8u, false, true,  true,  false, false, false, true,  false, true,  false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("TimerDelta",               "::TimerDelta",               8u,    8u, false, true,  true,  false, false, true,  false, false, true,  false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("AtomicFlag",               "::AtomicFlag",               4u,    4u, false, true,  false, false, false, false, true,  false, false, false, false, false, false, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("DiagnosticEventRecord",    "::DiagnosticEventRecord",    96u,   8u, false, true,  false, false, false, false, true,  false, false, false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("DiagnosticEventText",      "::DiagnosticEventText",      2048u, 1u, false, true,  false, false, false, false, true,  false, false, false, false, false, false, true,  true,  false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("FloatingPointEnvironment", "::FloatingPointEnvironment", 32u,   8u, false, true,  false, false, false, false, true,  false, false, false, false, false, false, true,  true,  false, false, 0u, "<global/floating_point_environment.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("CpuWorkerPlacement",       "::CpuWorkerPlacement",       16u,   4u, false, true,  true,  false, true,  false, true,  false, false, false, false, false, false, true,  true,  false, false, 0u, "<global/cpu_topology.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("GraphemeBoundaryState",    "::GraphemeBoundaryState",    16u,   8u, false, true,  true,  false, false, false, true,  false, false, false, false, false, false, true,  true,  false, false, 0u, "<global/unicode/grapheme.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("AabbDoubleBounds", "::AabbDoubleTests::Bounds", 64u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("AabbBounds", "::AabbTests::Bounds", 32u, 16u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ArenaBase", "::ArenaBase", 88u, 8u, false, false, false, false, false, false, false, false, false, false, false, false, 0u, false, false, true, true, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ArenaMemoryOwnerIdentity", "::ArenaMemoryOwnerIdentity", 72u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ArenaMemoryOwnerSnapshot", "::ArenaMemoryOwnerSnapshot", 120u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ArenaMemoryStats", "::ArenaMemoryStats", 48u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ArenaMemoryTracker", "::ArenaMemoryTracker", 80u, 8u, false, true, false, false, false, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("AtomicBackOff", "::AtomicBackOff", 4u, 4u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BalancedPartitionRange", "::BalancedPartitionRange", 16u, 8u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/range_partition.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingBox", "::BoundingBox", 32u, 16u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingDoubleBox", "::BoundingDoubleBox", 64u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingDoubleFrustum", "::BoundingDoubleFrustum", 128u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingDoubleOrientedBox", "::BoundingDoubleOrientedBox", 96u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingDoubleSphere", "::BoundingDoubleSphere", 32u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingFrustum", "::BoundingFrustum", 64u, 16u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingOrientedBox", "::BoundingOrientedBox", 48u, 16u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("BoundingSphere", "::BoundingSphere", 16u, 16u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("CallTextParts", "::CallTextParts", 32u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/call_text.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("ConditionVariableAny", "::ConditionVariableAny", 64u, 8u, false, true, false, false, false, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("CountingByteOutput", "::CountingByteOutput", 8u, 8u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/counting_output.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("CrlfOutputFileStream", "::CrlfOutputFileStream", 8u, 8u, false, false, false, false, true, false, false, false, false, false, false, false, 0u, false, false, false, false, 0u, "<global/filesystem/crlf_output_stream.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("DecodedUtf8CodePoint", "::DecodedUtf8CodePoint", 8u, 4u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("DiagnosticEventTextFormatter", "::DiagnosticEventTextFormatter", 1u, 1u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("DirectoryEntry", "::DirectoryEntry", 40u, 8u, true, false, false, false, false, false, false, false, false, true, false, false, 2u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("DirectoryIterator", "::DirectoryIterator", 32u, 8u, true, false, false, false, false, false, false, false, false, true, false, false, 2u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("FilePublicationResult", "::FilePublicationResult", 24u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/filesystem/exclusive_publication.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("Futex", "::Futex", 4u, 4u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("HierarchicalIndexSet", "::HierarchicalIndexSet", 96u, 8u, true, true, false, false, false, false, false, false, false, true, false, false, 1u, false, false, false, false, 0u, "<global/hierarchical_index_set.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("IntegerRectBounds", "::IntegerRectBounds", 32u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/integer_rect.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("JoiningThread", "::JoiningThread", 16u, 8u, false, true, false, false, false, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("Latch", "::Latch", 4u, 4u, false, false, false, false, true, false, false, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("LinuxProcessMemoryMapEntry", "::LinuxProcessMemoryMapEntry", 40u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/process_memory_map.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("LocalTime", "::LocalTime", 56u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("MallocMutex", "::MallocMutex", 4u, 4u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("MatrixDecomposition", "::MatrixDecomposition", 48u, 16u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("MatrixDoubleDecomposition", "::MatrixDoubleDecomposition", 96u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ActiveSceneSubsystemsConstPtr", "::NWB::Impl::ActiveSceneSubsystemsConstPtr", 8u, 8u, false, true, true, false, false, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("NameHashTextCodec", "::NameHashTextCodec", 1u, 1u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("OnlineF64Statistics", "::OnlineF64Statistics", 32u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/statistics.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("PathIdentity", "::PathIdentity", 16u, 8u, true, false, false, true, false, false, false, false, false, true, false, false, 2u, true, false, false, false, 0u, "<global/filesystem/path_identity.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("ProcessCaptureFailure", "::ProcessCaptureFailure", 20u, 4u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/process_capture.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("RecursiveDirectoryIterator", "::RecursiveDirectoryIterator", 32u, 8u, true, false, false, false, false, false, false, false, false, true, false, false, 2u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("RecursiveMutex", "::RecursiveMutex", 40u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("RigidDualQuaternion", "::RigidDualQuaternion", 32u, 16u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("RigidDualQuaternionDouble", "::RigidDualQuaternionDouble", 64u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("SIMDVectorConstF", "::SIMDVectorConstF", 16u, 16u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("SIMDVectorConstI", "::SIMDVectorConstI", 16u, 16u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("SIMDVectorDoubleConstF", "::SIMDVectorDoubleConstF", 32u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("SIMDVectorDoubleConstI", "::SIMDVectorDoubleConstI", 32u, 32u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("Sha1Digest", "::Sha1Digest", 20u, 1u, false, true, false, false, true, false, true, false, true, false, false, false, 0u, true, true, false, false, 0u, "<global/sha1.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("SharedLibrary", "::SharedLibrary", 8u, 8u, false, true, false, false, false, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "<global/shared_library.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("SharedLibraryFailure", "::SharedLibraryFailure", 2072u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/shared_library.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("StagedFilePairPaths", "::StagedFilePairPaths", 240u, 8u, true, false, false, false, false, false, false, false, false, true, false, false, 1u, true, true, false, false, 0u, "<global/filesystem/file_staging.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("StagedFilePairResult", "::StagedFilePairResult", 176u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/filesystem/file_staging.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("StagedFileWriter", "::StagedFileWriter", 8u, 8u, true, false, false, true, false, false, false, false, false, true, false, false, 2u, true, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("StopToken", "::StopToken", 8u, 8u, false, true, false, false, false, false, true, false, true, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("TangentFrameDirections", "::TangentFrameDirections", 32u, 16u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("TexturePayloadHeaderBinary", "::TextureBinaryPayload::HeaderBinary", 44u, 1u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("TexturePayloadMipLevelBinary", "::TextureBinaryPayload::MipLevelBinary", 40u, 1u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("TextureMipChainLayoutError", "::TextureFormat::MipChainLayoutError", 8u, 4u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("TextureMipLevelLayout", "::TextureFormat::MipLevelLayout", 40u, 8u, false, true, false, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("TextureMipPlaneBlockLayout", "::TextureFormat::TextureMipPlaneBlockLayout", 16u, 8u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_OBJECT_TYPE("Thread", "::Thread", 8u, 8u, false, true, false, false, false, false, true, false, false, false, false, false, 0u, false, false, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("ThreadId", "::ThreadId", 8u, 8u, false, true, true, false, true, false, true, false, true, false, true, false, 0u, true, true, false, false, 0u, "")
NWB_ADHD_EXPORT_OBJECT_TYPE("UnicodePropertyRange", "::UnicodePropertyRange", 12u, 4u, false, true, true, false, true, false, true, false, false, false, false, false, 0u, true, true, false, false, 0u, "<global/unicode/property_table.h>")

NWB_ADHD_EXPORT_BUILTIN_ENUM("MemoryOrder",                "::MemoryOrder",                        "::MemoryOrder",                4u, 4u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("ErrorCondition",             "::ErrorCondition",                     "::ErrorCondition",             4u, 4u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("ProcessSignal",              "::ProcessSignal::Enum",                "::ProcessSignal",              4u, 4u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("BinaryVectorPayloadFailure", "::BinaryVectorPayloadFailure::Enum",   "::BinaryVectorPayloadFailure", 1u, 1u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("ContainmentType",            "::ContainmentType::Enum",              "::ContainmentType",            1u, 1u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("PlaneIntersectionType",      "::PlaneIntersectionType::Enum",        "::PlaneIntersectionType",      1u, 1u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("CpuAffinity",                "::CpuAffinity::Enum",                  "::CpuAffinity",                1u, 1u, false, "<global/cpu_topology.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("GraphemeBreak",              "::GraphemeBreak::Enum",                "::GraphemeBreak",              1u, 1u, false, "<global/unicode/properties.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("IndicConjunct",              "::IndicConjunct::Enum",                "::IndicConjunct",              1u, 1u, false, "<global/unicode/properties.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("ArenaMemoryReservation", "::ArenaMemoryReservation::Enum", "::ArenaMemoryReservation", 1u, 1u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("ArenaMemorySource", "::ArenaMemorySource::Enum", "::ArenaMemorySource", 1u, 1u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("Latin1TextError", "::Latin1TextError::Enum", "::Latin1TextError", 1u, 1u, false, "<global/latin1_text.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("NameTextError", "::NameTextError::Enum", "::NameTextError", 1u, 1u, false, "")
NWB_ADHD_EXPORT_BUILTIN_ENUM("ProcessCaptureFailureKind", "::ProcessCaptureFailureKind::Enum", "::ProcessCaptureFailureKind", 1u, 1u, false, "<global/process_capture.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("ProcessExecutionFailure", "::ProcessExecutionFailure::Enum", "::ProcessExecutionFailure", 1u, 1u, false, "<global/process_execution.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("StagedFilePairFailureStage", "::StagedFilePairFailureStage::Enum", "::StagedFilePairFailureStage", 1u, 1u, false, "<global/filesystem/file_staging.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("TextureAlphaMode", "::TextureAlphaMode::Enum", "::TextureAlphaMode", 1u, 1u, false, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("TexturePayloadDimension", "::TextureDimension::Enum", "::TextureDimension", 1u, 1u, false, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("MipChainLayoutFailure", "::TextureFormat::MipChainLayoutFailure::Enum", "::TextureFormat::MipChainLayoutFailure", 1u, 1u, false, "<global/texture_payload.h>")
NWB_ADHD_EXPORT_BUILTIN_ENUM("TexturePayloadFormat", "::TexturePayloadFormat::Enum", "::TexturePayloadFormat", 1u, 1u, false, "<global/texture_payload.h>")

NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MemoryOrder", "relaxed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MemoryOrder", "consume")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MemoryOrder", "acquire")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MemoryOrder", "release")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MemoryOrder", "acq_rel")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MemoryOrder", "seq_cst")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "address_family_not_supported")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "address_in_use")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "address_not_available")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "already_connected")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "argument_list_too_long")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "argument_out_of_domain")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "bad_address")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "bad_file_descriptor")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "bad_message")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "broken_pipe")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "connection_aborted")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "connection_already_in_progress")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "connection_refused")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "connection_reset")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "cross_device_link")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "destination_address_required")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "device_or_resource_busy")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "directory_not_empty")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "executable_format_error")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "file_exists")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "file_too_large")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "filename_too_long")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "function_not_supported")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "host_unreachable")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "identifier_removed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "illegal_byte_sequence")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "inappropriate_io_control_operation")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "interrupted")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "invalid_argument")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "invalid_seek")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "io_error")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "is_a_directory")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "message_size")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "network_down")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "network_reset")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "network_unreachable")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_buffer_space")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_child_process")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_link")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_lock_available")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_message_available")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_message")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_protocol_option")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_space_on_device")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_stream_resources")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_such_device_or_address")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_such_device")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_such_file_or_directory")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "no_such_process")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "not_a_directory")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "not_a_socket")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "not_a_stream")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "not_connected")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "not_enough_memory")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "not_supported")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "operation_canceled")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "operation_in_progress")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "operation_not_permitted")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "operation_not_supported")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "operation_would_block")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "owner_dead")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "permission_denied")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "protocol_error")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "protocol_not_supported")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "read_only_file_system")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "resource_deadlock_would_occur")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "result_out_of_range")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "state_not_recoverable")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "stream_timeout")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "text_file_busy")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "timed_out")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "too_many_files_open_in_system")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "too_many_files_open")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "too_many_links")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "too_many_symbolic_link_levels")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "value_too_large")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ErrorCondition", "wrong_protocol_type")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessSignal", "Interrupt")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessSignal", "Terminate")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("BinaryVectorPayloadFailure", "CountOverflow")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("BinaryVectorPayloadFailure", "SourceTruncated")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("BinaryVectorPayloadFailure", "OutputOverflow")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ContainmentType", "Disjoint")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ContainmentType", "Intersects")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ContainmentType", "Contains")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("PlaneIntersectionType", "Front")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("PlaneIntersectionType", "Intersecting")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("PlaneIntersectionType", "Back")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("CpuAffinity", "Any")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("CpuAffinity", "Performance")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("CpuAffinity", "Efficiency")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "Other")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "CR")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "LF")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "Control")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "Extend")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "ZWJ")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "Regional_Indicator")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "Prepend")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "SpacingMark")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "L")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "V")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "T")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "LV")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("GraphemeBreak", "LVT")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("IndicConjunct", "None")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("IndicConjunct", "Consonant")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("IndicConjunct", "Extend")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("IndicConjunct", "Linker")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ArenaMemoryReservation", "Separate")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ArenaMemoryReservation", "FollowsUsage")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ArenaMemorySource", "Arena")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ArenaMemorySource", "HeapBacking")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("Latin1TextError", "InvalidText")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("Latin1TextError", "TooLarge")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("Latin1TextError", "Unsupported")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("NameTextError", "NullText")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("NameTextError", "EmbeddedNull")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "InvalidArguments")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "PipeFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "SpawnFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "ClockFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "PipeFlagsFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "PollFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "ReadFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "TimedOut")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "OutputTruncated")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "ChildWaitFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "ChildExitedUnsuccessfully")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessCaptureFailureKind", "ChildTerminated")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessExecutionFailure", "InvalidArguments")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessExecutionFailure", "OutputOpenFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessExecutionFailure", "CreateFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessExecutionFailure", "ExitQueryFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessExecutionFailure", "CloseFailed")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("ProcessExecutionFailure", "Unsupported")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "None")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "QueryOutput")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "InspectOutput")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "NonRegularOutput")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "IncompleteExistingPair")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "OutputAlreadyExists")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "QueryWorkPath")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "WorkPathAlreadyExists")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "StagePrimary")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "StageSecondary")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "VerifyPrimary")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "VerifySecondary")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "PreserveExistingOutput")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("StagedFilePairFailureStage", "FinalizeOutput")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TextureAlphaMode", "Opaque")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TextureAlphaMode", "EmbeddedLdr")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TextureAlphaMode", "ConstantUnorm8")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TextureAlphaMode", "SeparateUastcLdr4x4")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TexturePayloadDimension", "Texture2D")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TexturePayloadDimension", "TextureCube")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TexturePayloadDimension", "Texture3D")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "None")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "InvalidMipChain")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "MipCountMismatch")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "BlockLayoutOverflow")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "SliceCountInvalid")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "SliceCountMismatch")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "ByteSizeOverflow")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "OffsetOverflow")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "ResolutionMismatch")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "BlockGridMismatch")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "ByteRangeMismatch")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("MipChainLayoutFailure", "kCount")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TexturePayloadFormat", "UastcLdr4x4")
NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY("TexturePayloadFormat", "UastcHdr4x4")

NWB_ADHD_EXPORT_GENERIC_TYPE("Vector",             "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  false, true,  DynamicBack, false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelVector",     "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, false, false, true,  DynamicBack, false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("ForwardList",        "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, ForwardList, false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("List",               "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, DynamicBack, false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Deque",              "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  false, true,  DynamicBack, false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Set",                "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, Set,         false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("HashSet",            "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, Set,         false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelHashSet",    "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, Set,         false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Map",                "",                              TwoType,      None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, None,        true,  false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("HashMap",            "",                              TwoType,      None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, None,        true,  false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelHashMap",    "",                              TwoType,      None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, None,        true,  false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Queue",              "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, None,        false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelQueue",      "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, None,        false, false, false, false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelBlockQueue", "",                              TypeAndCount, None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, None,        false, false, false, true, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Array",              "",                              TypeAndCount, Array,       0u,  0u, false, true,  false, false, true,  false, true,  false, false, true,  FixedArray,  false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("FixedVector",        "",                              TypeAndCount, FixedVector, 0u,  0u, false, true,  false, false, true,  false, false, true,  false, true,  FixedVector, false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Span",               "",                              SingleType,   None,        16u, 8u, false, true,  true,  true,  false, false, true,  false, false, true,  None,        false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Optional",           "",                              SingleType,   Optional,    0u,  0u, false, true,  false, false, true,  true,  false, false, false, false, None,        false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Atomic",             "",                              SingleType,   Atomic,      0u,  0u, false, false, false, false, false, false, false, false, false, false, None,        false, false, false, false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("Expected",           "",                              OneOrTwoType, Expected,    0u,  0u, false, false, false, false, true,  false, false, false, false, false, None,        false, false, true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("CompressedPair", "::CompressedPair", TwoType, None, 0u, 0u, false, true, false, false, false, false, false, false, false, false, None, false, false, true, false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("FixedIndexMap", "::FixedIndexMap", SingleType, None, 40u, 8u, true, false, false, false, false, false, false, true, true, false, None, false, false, false, false, true, "<global/fixed_index_map.h>")
NWB_ADHD_EXPORT_GENERIC_TYPE("RefCountPtr", "::RefCountPtr", SingleType, None, 8u, 8u, false, true, true, false, false, false, false, false, false, false, None, false, false, true, false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("UniquePtr", "::UniquePtr", SingleType, None, 8u, 8u, false, true, false, false, false, false, false, false, false, false, None, false, false, true, false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("NotNullUniquePtr", "::NotNullUniquePtr", SingleType, None, 8u, 8u, false, false, false, false, false, false, false, false, false, false, None, false, false, true, false, false, "")

NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE("NoCopy")

NWB_ADHD_EXPORT_MOVE_ONLY_TYPE("UniquePtr")
NWB_ADHD_EXPORT_MOVE_ONLY_TYPE("NotNullUniquePtr")
NWB_ADHD_EXPORT_MOVE_ONLY_TYPE("MoveOnlyFunction")
NWB_ADHD_EXPORT_MOVE_ONLY_TYPE("NWB::UniquePtr")
NWB_ADHD_EXPORT_MOVE_ONLY_TYPE("NWB::NotNullUniquePtr")
NWB_ADHD_EXPORT_MOVE_ONLY_TYPE("NWB::MoveOnlyFunction")

NWB_ADHD_EXPORT_BINARY_HASH_TYPE("Name",     "")

NWB_ADHD_EXPORT_NWB_VALUE_TYPE("b8",            ".isInteger()", ".asInteger()", "(",                                            " != 0)",                   "__f->isInteger()) this->",                                                             " = (__f->asInteger() != 0);\r\n",                                        "__f->isInteger()) this->",                                                             " = (__f->asInteger() != 0);\r\n",                   false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("String",        ".isString()",  ".asString()",  "AString<::ArenaBase>(",                        ", __adhd_nwb_arena)",      "__f->isString()) this->",                                                              ".assign(__f->asString());\r\n",                                          "__f->isString()) this->",                                                              " = AString<::ArenaBase>(__f->asString(), __adhd_nwb_arena);\r\n", false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("CompactString", ".isString()",  ".asString()",  "ACompactString(",                              ")",                        "__f->isString()) this->",                                                              ".assign(__f->asString());\r\n",                                          "__f->isString()) this->",                                                              " = ACompactString(__f->asString());\r\n",          false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("Path",          ".isString()",  ".asString()",  "::Path<::ArenaBase>(__adhd_nwb_arena, ",       ")",                        "__f->isString()) this->", " = __f->asString();\r\n", "__f->isString()) this->", " = ::Path<::ArenaBase>(__adhd_nwb_arena, __f->asString());\r\n", false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("Name",          ".isString()",  ".asString()",  "::Name::FromText(",                            ").value_or(::Name{})",     "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   " = *__name; }\r\n",                                                      "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   " = *__name; }\r\n",                                 true)

NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("String",         "StringView", false)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("CompactString",  "StringView", false)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("Vector",         "Span",       true)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("Array",          "Span",       true)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("FixedVector",    "Span",       true)

NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('x', 'r', 0u, "X(")
NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('y', 'g', 1u, "Y(")
NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('z', 'b', 2u, "Z(")
NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('w', 'a', 3u, "W(")

NWB_ADHD_EXPORT_LITERAL_TOKEN(TokenType::IntegerLiteral, ExprKind::IntegerLiteral, "")
NWB_ADHD_EXPORT_LITERAL_TOKEN(TokenType::FloatLiteral,   ExprKind::FloatLiteral,   "f32")
NWB_ADHD_EXPORT_LITERAL_TOKEN(TokenType::DoubleLiteral,  ExprKind::DoubleLiteral,  "f64")
NWB_ADHD_EXPORT_LITERAL_TOKEN(TokenType::StringLiteral,  ExprKind::StringLiteral,  "StringView")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_LITERAL_TOKEN_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_LITERAL_TOKEN_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_LITERAL_TOKEN
#endif

#ifdef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
#endif

#ifdef NWB_ADHD_EXPORT_IMPLICIT_CONVERSION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_IMPLICIT_CONVERSION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_IMPLICIT_CONVERSION
#endif

#ifdef NWB_ADHD_EXPORT_NWB_VALUE_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NWB_VALUE_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NWB_VALUE_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_BINARY_HASH_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BINARY_HASH_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BINARY_HASH_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_MOVE_ONLY_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_MOVE_ONLY_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_MOVE_ONLY_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE
#endif

#ifdef NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM_ENTRY
#endif

#ifdef NWB_ADHD_EXPORT_BUILTIN_ENUM_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BUILTIN_ENUM
#endif

#ifdef NWB_ADHD_EXPORT_OBJECT_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_OBJECT_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_SCALAR_LIMIT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCALAR_LIMIT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCALAR_LIMIT
#endif

#ifdef NWB_ADHD_EXPORT_SCALAR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCALAR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

