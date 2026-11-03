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
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation)
#define NWB_ADHD_EXPORT_OBJECT_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_GENERIC_TYPE
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, inlineLayoutKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, reflectedLinearKind, isReflectedMap, isAssetHandle)
#define NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
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

NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x4",     "Float4",      4u, 3u, 16u, 16u, "f32", "f32", true,  false, false, false, false, "::SIMDVectorSingle", "::LoadFloat",     "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("i32x4",     "Int4",        4u, 0u, 16u, 16u, "i32", "i32", false, false, true,  true,  false, "::SIMDVectorSingle", "::LoadInt",       "::StoreInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("u32x4",     "UInt4",       4u, 0u, 16u, 16u, "u32", "u32", false, false, true,  false, false, "::SIMDVectorSingle", "::LoadInt",       "::StoreInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x3_i32", "Float3Int",   4u, 0u, 16u, 16u, "f32", "i32", false, false, false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x3_u32", "Float3UInt",  4u, 0u, 16u, 16u, "f32", "u32", false, false, false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x2",     "Double2",     2u, 0u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3",     "Double3",     3u, 0u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x4",     "Double4",     4u, 3u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("i64x4",     "Int64x4",     4u, 0u, 32u, 32u, "i64", "i64", false, true,  true,  true,  false, "::SIMDVectorDouble", "::LoadDoubleInt", "::StoreDoubleInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("u64x4",     "UInt64x4",    4u, 0u, 32u, 32u, "u64", "u64", false, true,  true,  false, false, "::SIMDVectorDouble", "::LoadDoubleInt", "::StoreDoubleInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3_i64", "Double3Int",  4u, 0u, 32u, 32u, "f64", "i64", false, true,  false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3_u64", "Double3UInt", 4u, 0u, 32u, 32u, "f64", "u64", false, true,  false, false, true,  "",                   "",                "")

NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x3x4", "Float34",  12u, 48u,  16u, "f32", "f32x4",  false, "::SIMDMatrix",       "::LoadFloat",  "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x4x4", "Float44",  16u, 64u,  16u, "f32", "f32x4",  false, "::SIMDMatrix",       "::LoadFloat",  "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x3", "Double33", 9u,  96u,  32u, "f64", "f64x3u", true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x4", "Double34", 12u, 96u,  32u, "f64", "f64x4",  true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x4x4", "Double44", 16u, 128u, 32u, "f64", "f64x4",  true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")

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

NWB_ADHD_EXPORT_OBJECT_TYPE("String",        "AString<>",                  32u,  8u, true,  true,  false, false, false, false, false, true,  true,  true,  true,  false)
NWB_ADHD_EXPORT_OBJECT_TYPE("StringView",    "AStringView",                16u,  8u, false, false, true,  true,  false, true,  true,  true,  true,  false, false, false)
NWB_ADHD_EXPORT_OBJECT_TYPE("CompactString", "ACompactString",             256u, 1u, false, true,  false, false, true,  false, false, true,  true,  false, true,  false)
NWB_ADHD_EXPORT_OBJECT_TYPE("Name",          "",                           8u,   8u, false, true,  true,  false, true,  true,  false, false, true,  false, true,  false)
NWB_ADHD_EXPORT_OBJECT_TYPE("Path",          "",                           32u,  8u, false, true,  false, false, false, true,  false, false, false, false, true,  true)
NWB_ADHD_EXPORT_OBJECT_TYPE("NameHash",      "::NameHash",                 64u,  8u, false, true,  false, false, true,  false, true,  false, false, false, false, false)
NWB_ADHD_EXPORT_OBJECT_TYPE("EntityID",      "::NWB::Core::ECS::EntityID", 4u,   4u, false, true,  true,  false, true,  false, false, false, false, false, false, false)
NWB_ADHD_EXPORT_OBJECT_TYPE("Failure",       "::Failure",                  1u,   1u, false, true,  true,  false, true,  false, false, false, false, false, false, false)
NWB_ADHD_EXPORT_OBJECT_TYPE("ErrorCode",     "::ErrorCode",                16u,  8u, false, true,  true,  false, true,  false, false, false, false, false, false, false)

NWB_ADHD_EXPORT_GENERIC_TYPE("Vector",             "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  false, true,  DynamicBack, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelVector",     "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, false, false, true,  DynamicBack, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ForwardList",        "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, ForwardList, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("List",               "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, DynamicBack, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Deque",              "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  false, true,  DynamicBack, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Set",                "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, Set,         false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("HashSet",            "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, Set,         false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelHashSet",    "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, Set,         false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Map",                "",                              TwoType,      None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, None,        true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("HashMap",            "",                              TwoType,      None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, None,        true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelHashMap",    "",                              TwoType,      None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, None,        true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Queue",              "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, true,  true,  false, None,        false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelQueue",      "",                              SingleType,   None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, None,        false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelBlockQueue", "",                              TypeAndCount, None,        32u, 8u, true,  true,  false, false, false, false, false, false, true,  false, None,        false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Array",              "",                              TypeAndCount, Array,       0u,  0u, false, true,  false, false, true,  false, true,  false, false, true,  FixedArray,  false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("FixedVector",        "",                              TypeAndCount, FixedVector, 0u,  0u, false, true,  false, false, true,  false, false, true,  false, true,  FixedVector, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Span",               "",                              SingleType,   None,        16u, 8u, false, true,  true,  true,  false, false, true,  false, false, true,  None,        false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Optional",           "",                              SingleType,   Optional,    0u,  0u, false, true,  false, false, true,  true,  false, false, false, false, None,        false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("AssetRef",           "::NWB::Core::Assets::AssetRef", SingleType,   None,        8u,  8u, false, true,  false, false, true,  false, false, false, false, false, None,        false, true)
NWB_ADHD_EXPORT_GENERIC_TYPE("Atomic",             "",                              SingleType,   Atomic,      0u,  0u, false, false, false, false, false, false, false, false, false, false, None,        false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Expected",           "",                              OneOrTwoType, Expected,    0u,  0u, false, false, false, false, true,  false, false, false, false, false, None,        false, false)

NWB_ADHD_EXPORT_BINARY_HASH_TYPE("Name",     "")
NWB_ADHD_EXPORT_BINARY_HASH_TYPE("AssetRef", ".virtualPath")

NWB_ADHD_EXPORT_NWB_VALUE_TYPE("b8",            ".isInteger()", ".asInteger()", "(",                                            " != 0)",                   "__f->isInteger()) this->",                                                             " = (__f->asInteger() != 0);\r\n",                                        "__f->isInteger()) this->",                                                             " = (__f->asInteger() != 0);\r\n",                   false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("EntityID",      ".isInteger()", ".asInteger()", "::NWB::Core::ECS::EntityID(static_cast<u32>(", "))",                       "__f->isInteger()) this->",                                                             " = ::NWB::Core::ECS::EntityID(static_cast<u32>(__f->asInteger()));\r\n", "__f->isInteger()) this->",                                                             " = ::NWB::Core::ECS::EntityID(static_cast<u32>(__f->asInteger()));\r\n", false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("String",        ".isString()",  ".asString()",  "AString<>(",                                   ", s_ClassArena)",          "__f->isString()) this->",                                                              ".assign(__f->asString());\r\n",                                          "__f->isString()) this->",                                                              " = AString<>(__f->asString(), s_ClassArena);\r\n",  false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("CompactString", ".isString()",  ".asString()",  "ACompactString(",                              ")",                        "__f->isString()) this->",                                                              ".assign(__f->asString());\r\n",                                          "__f->isString()) this->",                                                              " = ACompactString(__f->asString());\r\n",          false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("Path",          ".isString()",  ".asString()",  "Path(::ConvertString(",                        "))",                       "__f->isString()) this->",                                                              " = ::ConvertString(__f->asString());\r\n",                               "__f->isString()) this->",                                                              " = Path(::ConvertString(__f->asString()));\r\n",    false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("Name",          ".isString()",  ".asString()",  "::Name::FromText(",                            ").value_or(::Name{})",     "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   " = *__name; }\r\n",                                                      "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   " = *__name; }\r\n",                                 true)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("AssetRef",      ".isString()",  ".asString()",  "{::Name::FromText(",                           ").value_or(::Name{})}",    "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   ".virtualPath = *__name; }\r\n",                                          "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   " = {*__name}; }\r\n",                               true)

NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("String",         "StringView", false)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("CompactString",  "StringView", false)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("StringView",     "Path",       false)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("Vector",         "Span",       true)
NWB_ADHD_EXPORT_IMPLICIT_CONVERSION("ParallelVector", "Span",       true)
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

#ifdef NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
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

