// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_SCALAR_TYPE
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat, defaultLiteral)
#define NWB_ADHD_EXPORT_SCALAR_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc)
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc)
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, laneScalarType)
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_OBJECT_TYPE
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey)
#define NWB_ADHD_EXPORT_OBJECT_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_GENERIC_TYPE
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap)
#define NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex)
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_ASSET_INCLUDE
#define NWB_ADHD_EXPORT_ASSET_INCLUDE(assetTypeName, includeDirective)
#define NWB_ADHD_EXPORT_ASSET_INCLUDE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE
#define NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE(assetTypeSuffix, includeDirective)
#define NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_SCALAR_TYPE("b8",    1u, 1u, false, false, false, false, "false")
NWB_ADHD_EXPORT_SCALAR_TYPE("i8",    1u, 1u, true,  true,  true,  false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("u8",    1u, 1u, true,  true,  false, false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("i16",   2u, 2u, true,  true,  true,  false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("u16",   2u, 2u, true,  true,  false, false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("i32",   4u, 4u, true,  true,  true,  false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("u32",   4u, 4u, true,  true,  false, false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("i64",   8u, 8u, true,  true,  true,  false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("u64",   8u, 8u, true,  true,  false, false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("isize", 8u, 8u, true,  false, true,  false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("usize", 8u, 8u, true,  false, false, false, "0")
NWB_ADHD_EXPORT_SCALAR_TYPE("f32",   4u, 4u, false, false, true,  true,  "0.0f")
NWB_ADHD_EXPORT_SCALAR_TYPE("f64",   8u, 8u, false, false, true,  true,  "0.0")

NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x4",     "Float4",      4u, 16u, 16u, "f32", "f32", true,  false, false, false, false, "::SIMDVectorSingle", "::LoadFloat",     "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("i32x4",     "Int4",        4u, 16u, 16u, "i32", "i32", false, false, true,  true,  false, "::SIMDVectorSingle", "::LoadInt",       "::StoreInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("u32x4",     "UInt4",       4u, 16u, 16u, "u32", "u32", false, false, true,  false, false, "::SIMDVectorSingle", "::LoadInt",       "::StoreInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x3_i32", "Float3Int",   4u, 16u, 16u, "f32", "i32", false, false, false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f32x3_u32", "Float3UInt",  4u, 16u, 16u, "f32", "u32", false, false, false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x2",     "Double2",     2u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3",     "Double3",     3u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x4",     "Double4",     4u, 32u, 32u, "f64", "f64", true,  true,  false, false, false, "::SIMDVectorDouble", "::LoadDouble",    "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("i64x4",     "Int64x4",     4u, 32u, 32u, "i64", "i64", false, true,  true,  true,  false, "::SIMDVectorDouble", "::LoadDoubleInt", "::StoreDoubleInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("u64x4",     "UInt64x4",    4u, 32u, 32u, "u64", "u64", false, true,  true,  false, false, "::SIMDVectorDouble", "::LoadDoubleInt", "::StoreDoubleInt")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3_i64", "Double3Int",  4u, 32u, 32u, "f64", "i64", false, true,  false, false, true,  "",                   "",                "")
NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE("f64x3_u64", "Double3UInt", 4u, 32u, 32u, "f64", "u64", false, true,  false, false, true,  "",                   "",                "")

NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x3x4", "Float34",  3u, 4u, 12u, 48u,  16u, "f32", false, "::SIMDMatrix",       "::LoadFloat",  "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f32x4x4", "Float44",  4u, 4u, 16u, 64u,  16u, "f32", false, "::SIMDMatrix",       "::LoadFloat",  "::StoreFloat")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x3", "Double33", 3u, 3u, 9u,  96u,  32u, "f64", true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x3x4", "Double34", 3u, 4u, 12u, 96u,  32u, "f64", true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")
NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE("f64x4x4", "Double44", 4u, 4u, 16u, 128u, 32u, "f64", true,  "::SIMDMatrixDouble", "::LoadDouble", "::StoreDouble")

NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f32x2u", "Float2U",   2u, 8u,  4u, "f32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f32x3u", "Float3U",   3u, 12u, 4u, "f32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f32x4u", "Float4U",   4u, 16u, 4u, "f32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i32x2u", "Int2U",     2u, 8u,  4u, "i32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i32x3u", "Int3U",     3u, 12u, 4u, "i32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i32x4u", "Int4U",     4u, 16u, 4u, "i32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u32x2u", "UInt2U",    2u, 8u,  4u, "u32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u32x3u", "UInt3U",    3u, 12u, 4u, "u32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u32x4u", "UInt4U",    4u, 16u, 4u, "u32")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f64x2u", "Double2U",  2u, 16u, 8u, "f64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f64x3u", "Double3U",  3u, 24u, 8u, "f64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("f64x4u", "Double4U",  4u, 32u, 8u, "f64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i64x2u", "Int64x2U",  2u, 16u, 8u, "i64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i64x3u", "Int64x3U",  3u, 24u, 8u, "i64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("i64x4u", "Int64x4U",  4u, 32u, 8u, "i64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u64x2u", "UInt64x2U", 2u, 16u, 8u, "u64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u64x3u", "UInt64x3U", 3u, 24u, 8u, "u64")
NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE("u64x4u", "UInt64x4U", 4u, 32u, 8u, "u64")

NWB_ADHD_EXPORT_OBJECT_TYPE("String",        "AString",                    32u,  8u, true,  true,  false, true,  true,  true)
NWB_ADHD_EXPORT_OBJECT_TYPE("StringView",    "AStringView",                16u,  8u, false, false, true,  true,  false, false)
NWB_ADHD_EXPORT_OBJECT_TYPE("CompactString", "ACompactString",             256u, 1u, false, true,  false, true,  false, true)
NWB_ADHD_EXPORT_OBJECT_TYPE("Name",          "",                           8u,   8u, false, true,  true,  true,  false, true)
NWB_ADHD_EXPORT_OBJECT_TYPE("Path",          "",                           32u,  8u, false, true,  false, false, false, true)
NWB_ADHD_EXPORT_OBJECT_TYPE("EntityID",      "::NWB::Core::ECS::EntityID", 4u,   4u, false, true,  true,  false, false, false)

NWB_ADHD_EXPORT_GENERIC_TYPE("Vector",             "",                              SingleType,   32u, 8u, true,  true,  false, true,  false, true,  true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelVector",     "",                              SingleType,   32u, 8u, true,  true,  false, false, false, true,  true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ForwardList",        "",                              SingleType,   32u, 8u, true,  true,  false, true,  true,  false, true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("List",               "",                              SingleType,   32u, 8u, true,  true,  false, true,  true,  false, true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Deque",              "",                              SingleType,   32u, 8u, true,  true,  false, true,  false, true,  true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Set",                "",                              SingleType,   32u, 8u, true,  true,  false, true,  true,  false, true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("HashSet",            "",                              SingleType,   32u, 8u, true,  true,  false, true,  true,  false, true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelHashSet",    "",                              SingleType,   32u, 8u, true,  true,  false, false, true,  false, true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Map",                "",                              TwoType,      32u, 8u, true,  true,  false, true,  true,  false, false, true)
NWB_ADHD_EXPORT_GENERIC_TYPE("HashMap",            "",                              TwoType,      32u, 8u, true,  true,  false, true,  true,  false, false, true)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelHashMap",    "",                              TwoType,      32u, 8u, true,  true,  false, false, true,  false, false, true)
NWB_ADHD_EXPORT_GENERIC_TYPE("Queue",              "",                              SingleType,   32u, 8u, true,  true,  false, true,  true,  false, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelQueue",      "",                              SingleType,   32u, 8u, true,  true,  false, false, true,  false, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("ParallelBlockQueue", "",                              TypeAndCount, 32u, 8u, true,  true,  false, false, false, false, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Array",              "",                              TypeAndCount, 0u,  0u, false, true,  false, false, false, true,  true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("FixedVector",        "",                              TypeAndCount, 0u,  0u, false, true,  false, true,  false, true,  true,  false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Span",               "",                              SingleType,   16u, 8u, false, false, false, false, false, true,  false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Optional",           "",                              SingleType,   0u,  0u, false, true,  false, false, false, false, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("AssetRef",           "::NWB::Core::Assets::AssetRef", SingleType,   8u,  8u, false, true,  false, false, false, false, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Atomic",             "",                              SingleType,   0u,  0u, false, false, false, false, false, false, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("Expected",           "",                              TwoType,      0u,  0u, false, false, false, false, false, false, false, false)
NWB_ADHD_EXPORT_GENERIC_TYPE("NotNull",            "",                              NotNullType,  8u,  8u, false, false, false, false, false, false, false, false)

NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('x', 'r', 0u)
NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('y', 'g', 1u)
NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('z', 'b', 2u)
NWB_ADHD_EXPORT_SWIZZLE_COMPONENT('w', 'a', 3u)

NWB_ADHD_EXPORT_ASSET_INCLUDE("CsgShape",  "#include <impl/assets_csg/asset.h>\r\n")
NWB_ADHD_EXPORT_ASSET_INCLUDE("FontAtlas", "#include <impl/assets_font_atlas/asset.h>\r\n")
NWB_ADHD_EXPORT_ASSET_INCLUDE("Sampler",   "#include <impl/assets_sampler/asset.h>\r\n")
NWB_ADHD_EXPORT_ASSET_INCLUDE("UiSkin",    "#include <impl/assets_ui_skin/asset.h>\r\n")

NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE("Shader", "#include <impl/assets_shader/asset.h>\r\n")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ASSET_SUFFIX_INCLUDE
#endif

#ifdef NWB_ADHD_EXPORT_ASSET_INCLUDE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ASSET_INCLUDE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ASSET_INCLUDE
#endif

#ifdef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
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

#ifdef NWB_ADHD_EXPORT_SCALAR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCALAR_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
