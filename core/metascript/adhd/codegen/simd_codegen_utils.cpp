// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_codegen_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Array<MStringView, 4> s_LaneCallSuffixes = {
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex, cppLaneCallSuffix) \
    cppLaneCallSuffix,
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsRegisterPromotableSimdType(const MStringView simdType)noexcept{
    return SymbolTable::IsSimdRegisterType(simdType);
}

[[nodiscard]] b8 IsDoubleSimdType(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType))
        return vecInfo->isDoubleReg && (vecInfo->isFloatReg || vecInfo->isIntReg);
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->isDoubleReg;
    return false;
}

[[nodiscard]] b8 IsIntSimdType(const MStringView simdType)noexcept{
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType);
    return vecInfo && vecInfo->isIntReg;
}

[[nodiscard]] b8 IsSignedIntSimdType(const MStringView simdType)noexcept{
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType);
    return vecInfo && vecInfo->isIntReg && vecInfo->isSignedInt;
}

[[nodiscard]] MStringView IntSimdLaneType(const MStringView simdType)noexcept{
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType);
    if(vecInfo && vecInfo->isIntReg)
        return vecInfo->xyzScalarType;
    return BuiltinTypeSemantics::UInt64ScalarTypeName();
}

[[nodiscard]] MStringView SimdLaneCallSuffix(const u32 lane)noexcept{
    return __hidden_adhd_simd_codegen_utils::s_LaneCallSuffixes[lane < 4u ? lane : 3u];
}

[[nodiscard]] MStringView SimdSplatIntrinsic(const u32 lane, const b8 isDouble)noexcept{
    const u32 idx = lane < 4u ? lane : 3u;
    return isDouble
        ? CodegenSymbols::s_DoubleSplatIntrinsics[idx]
        : CodegenSymbols::s_SingleSplatIntrinsics[idx];
}

[[nodiscard]] MStringView SimdInPlaceLaneMaskConstant(const u8 laneMask, const b8 isDouble)noexcept{
    return CodegenSymbols::LookupMaskSymbol(CodegenSymbols::s_InPlaceLaneMasks, laneMask, isDouble);
}

[[nodiscard]] MStringView SimdInPlaceSignFlipMaskConstant(const u8 negMask, const b8 isDouble)noexcept{
    return CodegenSymbols::LookupMaskSymbol(CodegenSymbols::s_SignFlipMasks, negMask, isDouble);
}

[[nodiscard]] MStringView SimdComponentMaskConstant(const u8 laneMask, const b8 isDouble)noexcept{
    return CodegenSymbols::LookupMaskSymbol(CodegenSymbols::s_ComponentMasks, laneMask, isDouble);
}

void AppendIntSimdLaneExtract(MString& out, const MStringView varName, const u32 lane, const b8 isDouble, const MStringView laneType){
    const MStringView laneSuffix = SimdLaneCallSuffix(lane);
    const MStringView getIntPrefix = isDouble ? CodegenSymbols::s_SimdGetIntDoublePrefix : CodegenSymbols::s_SimdGetIntSinglePrefix;
    out.append("static_cast<");
    out.append(laneType.data(), laneType.size());
    out.append(">(");
    out.append(getIntPrefix.data(), getIntPrefix.size());
    out.append(laneSuffix.data(), laneSuffix.size());
    out.append(varName.data(), varName.size());
    out.append("))");
}

[[nodiscard]] MStringView SimdRegisterCppType(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->regCppType.empty())
        return vecInfo->regCppType;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->regCppType;
    return BuiltinTypeSemantics::SpecialTypeCppName(SpecialBuiltinTypeKind::VectorSingle);
}

[[nodiscard]] MStringView SimdLoadFunctionCpp(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->loadCppFunc.empty())
        return vecInfo->loadCppFunc;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->loadCppFunc;
    return CodegenSymbols::s_SimdLoadFloat;
}

[[nodiscard]] MStringView SimdStoreFunctionCpp(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->storeCppFunc.empty())
        return vecInfo->storeCppFunc;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->storeCppFunc;
    return CodegenSymbols::s_SimdStoreFloat;
}

[[nodiscard]] MStringView MapBuiltinMathFunction(const MStringView name)noexcept{
    const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(name);
    return fnInfo ? fnInfo->cppExportName : MStringView{};
}

[[nodiscard]] MStringView MapSimdRegisterCall(const MStringView callee)noexcept{
#define NWB_ADHD_EXPORT_SIMD_REGISTER_CALL(adhdName) \
    if(callee == adhdName) return MapBuiltinMathFunction(callee);
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_REGISTER_CALL
    return {};
}

[[nodiscard]] MStringView MapSimdScalarReductionSuffix(const MStringView callee)noexcept{
#define NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION(adhdName, laneIntrinsicSuffix) \
    if(callee == adhdName) return laneIntrinsicSuffix;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION
    return {};
}

[[nodiscard]] b8 IsSimdScalarReductionFunction(const MStringView callee)noexcept{
    return !MapSimdScalarReductionSuffix(callee).empty();
}

[[nodiscard]] b8 IsSimdZeroPreservingCall(const MStringView callee)noexcept{
#define NWB_ADHD_EXPORT_SIMD_ZERO_PRESERVING_CALL(adhdName) \
    if(callee == adhdName) return true;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_ZERO_PRESERVING_CALL
    return false;
}

[[nodiscard]] MStringView MapSimdUnaryFloatIntrinsic(const MStringView callee, const b8 isDouble)noexcept{
#define NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC(adhdName, floatIntrinsicPrefix, doubleIntrinsicPrefix) \
    if(callee == adhdName) return isDouble ? doubleIntrinsicPrefix : floatIntrinsicPrefix;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC
    return {};
}

void AppendSimdLaneCountIntrinsicPrefix(MString& out, const b8 isDouble, const u32 effLanes, const MStringView suffix){
    const MStringView prefix = CodegenSymbols::SimdPrefix(isDouble);
    out.append(prefix.data(), prefix.size());
    out.push_back(effLanes == 2u ? '2' : (effLanes == 3u ? '3' : '4'));
    out.append(suffix.data(), suffix.size());
    out.push_back('(');
}

[[nodiscard]] TokenType::Enum MapSimdAssignToBinaryOp(const TokenType::Enum op)noexcept{
    switch(op){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case assignToken: return binaryToken;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return op;
    }
}

[[nodiscard]] MStringView SimdBinaryScalarOpSymbol(const TokenType::Enum binaryOp)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case binaryToken: return paddedBinarySymbol;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return {};
    }
}

[[nodiscard]] b8 IsSimdIntLaneFallbackBinaryOp(const TokenType::Enum binaryOp)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case binaryToken: return isIntLaneFallback;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return false;
    }
}

[[nodiscard]] MStringView MapSimdBitwiseBinaryIntrinsic(const TokenType::Enum binaryOp, const b8 isDouble)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case binaryToken: return isDouble ? MStringView(bitwiseIntrinsicDouble) : MStringView(bitwiseIntrinsicSingle);
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

