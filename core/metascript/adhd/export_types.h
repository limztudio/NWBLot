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

#ifndef NWB_ADHD_EXPORT_GENERIC_TYPE
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, inlineLayoutKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, reflectedLinearKind, isReflectedMap, isAssetHandle, isCopyMoveConstructible, emitsFirstTypeArgOnlyInCpp, arenaFirstInCtor, requiredHeader)
#define NWB_ADHD_EXPORT_GENERIC_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE
#define NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE(ifaceName, cppExportName)
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

#ifndef NWB_ADHD_EXPORT_SPECIAL_TYPE
#define NWB_ADHD_EXPORT_SPECIAL_TYPE(kind, adhdName, cppName, isReservedPrimitive)
#define NWB_ADHD_EXPORT_SPECIAL_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE
#define NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE(adhdName, allowsInterfaceArg, isSmartPtrDeref)
#define NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE
#define NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE(adhdName)
#define NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE
#define NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE(adhdName)
#define NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_FORMAT_ARG_WRAP
#define NWB_ADHD_EXPORT_FORMAT_ARG_WRAP(wrapKindId, wrapInDiagnosticFormat, cppPrefix, cppSuffix)
#define NWB_ADHD_EXPORT_FORMAT_ARG_WRAP_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "export_global_types.h"
#include "export_core_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_FORMAT_ARG_WRAP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FORMAT_ARG_WRAP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FORMAT_ARG_WRAP
#endif

#ifdef NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_LITERAL_CONST_OBJECT_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SUBSYSTEM_VECTOR_CONTAINER_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE
#endif

#ifdef NWB_ADHD_EXPORT_SPECIAL_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SPECIAL_TYPE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SPECIAL_TYPE
#endif

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

