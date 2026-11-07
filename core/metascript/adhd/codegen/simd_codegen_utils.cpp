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

inline constexpr Array<MStringView, 4> s_SingleSplatIntrinsics = {
    "::VectorSplatX(",
    "::VectorSplatY(",
    "::VectorSplatZ(",
    "::VectorSplatW(",
};

inline constexpr Array<MStringView, 4> s_DoubleSplatIntrinsics = {
    "::VectorDoubleSplatX(",
    "::VectorDoubleSplatY(",
    "::VectorDoubleSplatZ(",
    "::VectorDoubleSplatW(",
};

[[nodiscard]] Span<const Expr* const> ExtractGlobalConstCtorArgs(const Stmt& globalConst)noexcept{
    if(globalConst.isCtorInit && !globalConst.arguments.empty())
        return Span<const Expr* const>(globalConst.arguments.data(), globalConst.arguments.size());
    if(globalConst.expr && globalConst.expr->kind == ExprKind::Call && !globalConst.expr->arguments.empty())
        return Span<const Expr* const>(globalConst.expr->arguments.data(), globalConst.expr->arguments.size());
    return {};
}

[[nodiscard]] b8 IsIdentityMatrixRowCtorExpr(const Expr& expr, const u32 rowIndex, const SymbolTable& symbols)noexcept{
    Span<const Expr* const> rowArgs;
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = symbols.resolveBaseTypeName(expr.lhs->text);
        if(!SymbolTable::IsSimdRegisterVectorType(baseCallee))
            return false;
        rowArgs = Span<const Expr* const>(expr.arguments.data(), expr.arguments.size());
    }
    else if(expr.kind == ExprKind::Identifier){
        if(const Stmt* globalConst = symbols.findGlobalConstant(expr.text))
            rowArgs = ExtractGlobalConstCtorArgs(*globalConst);
    }
    const usize argCount = rowArgs.size();
    if(argCount != 3u && argCount != 4u)
        return false;
    if(rowIndex >= argCount)
        return false;
    for(usize i = 0u; i < argCount; ++i){
        const Expr* arg = rowArgs[i];
        if(!arg)
            return false;
        if(i == rowIndex){
            if(!IsUnitFloatLiteral(*arg, &symbols))
                return false;
        }
        else if(!IsZeroScalarLiteral(*arg, &symbols)){
            return false;
        }
    }
    return true;
}


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
    return "u64";
}

[[nodiscard]] MStringView SimdLaneCallSuffix(const u32 lane)noexcept{
    return __hidden_adhd_simd_codegen_utils::s_LaneCallSuffixes[lane < 4u ? lane : 3u];
}

[[nodiscard]] MStringView SimdSplatIntrinsic(const u32 lane, const b8 isDouble)noexcept{
    const u32 idx = lane < 4u ? lane : 3u;
    return isDouble
        ? __hidden_adhd_simd_codegen_utils::s_DoubleSplatIntrinsics[idx]
        : __hidden_adhd_simd_codegen_utils::s_SingleSplatIntrinsics[idx];
}

[[nodiscard]] MStringView SimdInPlaceLaneMaskConstant(const u8 laneMask, const b8 isDouble)noexcept{
    switch(laneMask){
    case 0x1u: return isDouble ? "::s_DoubleSIMDMaskX" : "::s_SIMDMaskX";
    case 0x2u: return isDouble ? "::s_DoubleSIMDMaskY" : "::s_SIMDMaskY";
    case 0x4u: return isDouble ? "::s_DoubleSIMDMaskZ" : "::s_SIMDMaskZ";
    case 0x8u: return isDouble ? "::s_DoubleSIMDMaskW" : "::s_SIMDMaskW";
    case 0x3u: return isDouble ? "::s_DoubleSIMDMaskXY" : "::s_SIMDMaskXY";
    case 0x7u: return isDouble ? "::s_DoubleSIMDMask3" : "::s_SIMDMask3";
    default: return {};
    }
}

[[nodiscard]] MStringView SimdInPlaceSignFlipMaskConstant(const u8 negMask, const b8 isDouble)noexcept{
    if(negMask == 0x7u)
        return isDouble ? "::s_DoubleSIMDNegate3" : "::s_SIMDNegate3";
    if(isDouble)
        return {};
    switch(negMask){
    case 0x2u: return "::s_SIMDFlipY";
    case 0x4u: return "::s_SIMDFlipZ";
    case 0x8u: return "::s_SIMDFlipW";
    case 0xAu: return "::s_SIMDFlipYW";
    default: return {};
    }
}

[[nodiscard]] MStringView SimdComponentMaskConstant(const u8 laneMask, const b8 isDouble)noexcept{
    switch(laneMask){
    case 0x3u: return isDouble ? "::VectorDoubleComponentMask::s_DoubleXY" : "::VectorComponentMask::s_XY";
    case 0x7u: return isDouble ? "::VectorDoubleComponentMask::s_DoubleXYZ" : "::VectorComponentMask::s_XYZ";
    case 0xFu: return isDouble ? "::VectorDoubleComponentMask::s_DoubleXYZW" : "::VectorComponentMask::s_XYZW";
    default: return {};
    }
}

void AppendIntSimdLaneExtract(MString& out, const MStringView varName, const u32 lane, const b8 isDouble, const MStringView laneType){
    const MStringView laneSuffix = SimdLaneCallSuffix(lane);
    out.append("static_cast<");
    out.append(laneType.data(), laneType.size());
    out.append(">(");
    out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
    out.append(laneSuffix.data(), laneSuffix.size());
    out.append(varName.data(), varName.size());
    out.append("))");
}

[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept{
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle && expr.text.size() >= 2u && expr.text.size() <= 4u)
        return static_cast<u32>(expr.text.size());
    u32 lanes = 4u;
    if(SymbolTable::IsSimdVectorType(expr.resolvedSimdType, lanes))
        return lanes;
    return 4u;
}

[[nodiscard]] const Expr& UnwrapConstScalarExpr(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr* cur = &expr;
    if(!symbols)
        return *cur;
    for(u32 depth = 0u; depth < 4u; ++depth){
        if(cur->kind != ExprKind::Identifier || !cur->resolvedSimdType.empty() || !cur->resolvedUnalignedType.empty())
            break;
        const Stmt* globalConst = symbols->findGlobalConstant(cur->text);
        if(!globalConst)
            break;
        if(globalConst->expr){
            cur = globalConst->expr;
        }
        else if(globalConst->isCtorInit && globalConst->arguments.size() == 1u && globalConst->arguments[0]){
            cur = globalConst->arguments[0];
        }
        else{
            break;
        }
    }
    return *cur;
}

[[nodiscard]] b8 IsUnitFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = UnwrapConstScalarExpr(expr, symbols);
    return (unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral)
        && (unwrapped.text == "1.0f" || unwrapped.text == "1.0" || unwrapped.text == "1.f" || unwrapped.text == "1.0F" || unwrapped.text == "1.F");
}

[[nodiscard]] b8 IsNegativeUnitFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = UnwrapConstScalarExpr(expr, symbols);
    if(unwrapped.kind == ExprKind::Unary && !unwrapped.isPostfix && unwrapped.op == TokenType::Minus && unwrapped.lhs)
        return IsUnitFloatLiteral(*unwrapped.lhs, symbols);
    return (unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral)
        && (unwrapped.text == "-1.0f" || unwrapped.text == "-1.0" || unwrapped.text == "-1.f" || unwrapped.text == "-1.0F" || unwrapped.text == "-1.F");
}

[[nodiscard]] b8 IsHalfFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = UnwrapConstScalarExpr(expr, symbols);
    return (unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral)
        && (unwrapped.text == "0.5f" || unwrapped.text == "0.5" || unwrapped.text == ".5f" || unwrapped.text == ".5" || unwrapped.text == "0.5F" || unwrapped.text == ".5F");
}

[[nodiscard]] MStringView TryMatchKnownSimdFloatVectorConstant(const Span<const Expr* const> arguments, const b8 isDouble, const SymbolTable* const symbols)noexcept{
    if(arguments.size() == 1u && arguments[0]){
        const Expr& a0 = *arguments[0];
        if(a0.resolvedSimdType.empty() && a0.resolvedUnalignedType.empty()){
            if(IsUnitFloatLiteral(a0, symbols))
                return isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne";
            if(IsNegativeUnitFloatLiteral(a0, symbols))
                return isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne";
            if(IsHalfFloatLiteral(a0, symbols))
                return isDouble ? "::s_DoubleSIMDOneHalf" : "::s_SIMDOneHalf";
            return {};
        }
        if(symbols && a0.kind == ExprKind::Identifier){
            if(const Stmt* globalConst = symbols->findGlobalConstant(a0.text)){
                const Span<const Expr* const> constArgs = __hidden_adhd_simd_codegen_utils::ExtractGlobalConstCtorArgs(*globalConst);
                if(!constArgs.empty()){
                    b8 allZero = true;
                    for(const Expr* cArg : constArgs){
                        if(!cArg || !IsZeroScalarLiteral(*cArg, symbols)){
                            allZero = false;
                            break;
                        }
                    }
                    if(allZero)
                        return isDouble ? "::VectorDoubleZero()" : "::VectorZero()";
                    return TryMatchKnownSimdFloatVectorConstant(constArgs, isDouble, symbols);
                }
            }
        }
        return {};
    }
    if(arguments.size() != 3u && arguments.size() != 4u)
        return {};
    for(const Expr* arg : arguments){
        if(!arg)
            return {};
    }
    const b8 hasW = (arguments.size() == 4u);
    const b8 u0 = IsUnitFloatLiteral(*arguments[0], symbols);
    const b8 u1 = IsUnitFloatLiteral(*arguments[1], symbols);
    const b8 u2 = IsUnitFloatLiteral(*arguments[2], symbols);
    const b8 u3 = hasW && IsUnitFloatLiteral(*arguments[3], symbols);
    const b8 z0 = IsZeroScalarLiteral(*arguments[0], symbols);
    const b8 z1 = IsZeroScalarLiteral(*arguments[1], symbols);
    const b8 z2 = IsZeroScalarLiteral(*arguments[2], symbols);
    const b8 z3 = !hasW || IsZeroScalarLiteral(*arguments[3], symbols);

    if(u0 && u1 && u2 && u3)
        return isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne";
    if(u0 && u1 && u2 && z3)
        return isDouble ? "::s_DoubleSIMDOne3" : "::s_SIMDOne3";
    if(u0 && z1 && z2 && z3)
        return isDouble ? "::s_DoubleSIMDIdentityR0" : "::s_SIMDIdentityR0";
    if(z0 && u1 && z2 && z3)
        return isDouble ? "::s_DoubleSIMDIdentityR1" : "::s_SIMDIdentityR1";
    if(z0 && z1 && u2 && z3)
        return isDouble ? "::s_DoubleSIMDIdentityR2" : "::s_SIMDIdentityR2";
    if(hasW && z0 && z1 && z2 && u3)
        return isDouble ? "::s_DoubleSIMDIdentityR3" : "::s_SIMDIdentityR3";

    if(
        hasW
        && IsNegativeUnitFloatLiteral(*arguments[0], symbols)
        && IsNegativeUnitFloatLiteral(*arguments[1], symbols)
        && IsNegativeUnitFloatLiteral(*arguments[2], symbols)
        && IsNegativeUnitFloatLiteral(*arguments[3], symbols)
    ){
        return isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne";
    }
    if(
        hasW
        && IsHalfFloatLiteral(*arguments[0], symbols)
        && IsHalfFloatLiteral(*arguments[1], symbols)
        && IsHalfFloatLiteral(*arguments[2], symbols)
        && IsHalfFloatLiteral(*arguments[3], symbols)
    ){
        return isDouble ? "::s_DoubleSIMDOneHalf" : "::s_SIMDOneHalf";
    }
    return {};
}

[[nodiscard]] b8 IsIdentityMatrixCtorArgs(const Span<const Expr* const> arguments, const SymbolTable& symbols)noexcept{
    const usize count = arguments.size();
    if(count == 1u && arguments[0] && arguments[0]->kind == ExprKind::Identifier){
        if(const Stmt* globalConst = symbols.findGlobalConstant(arguments[0]->text)){
            const Span<const Expr* const> constArgs = __hidden_adhd_simd_codegen_utils::ExtractGlobalConstCtorArgs(*globalConst);
            if(!constArgs.empty())
                return IsIdentityMatrixCtorArgs(constArgs, symbols);
        }
        return false;
    }
    if(count == 3u || count == 4u){
        for(usize r = 0u; r < count; ++r){
            if(!arguments[r] || !__hidden_adhd_simd_codegen_utils::IsIdentityMatrixRowCtorExpr(*arguments[r], static_cast<u32>(r), symbols))
                return false;
        }
        return true;
    }
    if(count == 9u || count == 12u || count == 16u){
        const usize stride = (count == 9u) ? 4u : 5u;
        for(usize i = 0u; i < count; ++i){
            if(!arguments[i])
                return false;
            const b8 isDiag = (i % stride == 0u);
            if(isDiag){
                if(!IsUnitFloatLiteral(*arguments[i], &symbols))
                    return false;
            }
            else if(!IsZeroScalarLiteral(*arguments[i], &symbols)){
                return false;
            }
        }
        return true;
    }
    return false;
}

[[nodiscard]] b8 TryGetStorageVectorLoadInfo(const Expr& srcExpr, const b8 targetIsDouble, MStringView& outLoadFn, u32& outLanes)noexcept{
    if(!srcExpr.resolvedSimdType.empty() && SymbolTable::IsSimdStorageVectorType(srcExpr.resolvedSimdType, outLanes)){
        const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(srcExpr.resolvedSimdType);
        if(!vecInfo || vecInfo->isDoubleReg != targetIsDouble)
            return false;
        if(vecInfo->isMixed){
            outLoadFn = vecInfo->isDoubleReg ? "::LoadDoubleInt" : "::LoadFloatInt";
            outLanes = 3u;
            return true;
        }
        outLoadFn = vecInfo->loadCppFunc;
        return !outLoadFn.empty();
    }
    if(!srcExpr.resolvedUnalignedType.empty()){
        const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(srcExpr.resolvedUnalignedType);
        if(!unalignedInfo)
            return false;
        outLanes = unalignedInfo->lanes;
        if(unalignedInfo->isHalfFloat){
            outLoadFn = targetIsDouble ? "::LoadDouble" : "::LoadHalf";
            return true;
        }
        const b8 srcIsDouble = (unalignedInfo->laneScalarType == "f64" || unalignedInfo->laneScalarType == "i64" || unalignedInfo->laneScalarType == "u64");
        if(srcIsDouble != targetIsDouble)
            return false;
        const b8 srcIsInt = IsIntegerScalarType(unalignedInfo->laneScalarType);
        outLoadFn = srcIsDouble ? (srcIsInt ? "::LoadDoubleInt" : "::LoadDouble") : (srcIsInt ? "::LoadInt" : "::LoadFloat");
        return true;
    }
    return false;
}

[[nodiscard]] b8 TryGetFloatVectorSourceInfo(const Expr& srcExpr, const b8 targetIsDouble, MStringView& outRegSimdType, MStringView& outStorageLoadFn, u32& outEffectiveZeroedAboveLane)noexcept{
    outRegSimdType = {};
    outStorageLoadFn = {};
    outEffectiveZeroedAboveLane = 4u;
    const MStringView expectedRegType = targetIsDouble ? "VectorDouble" : "VectorSingle";
    if(srcExpr.resolvedSimdType == expectedRegType){
        outRegSimdType = expectedRegType;
        return true;
    }
    u32 storageLanes = 0u;
    if(!srcExpr.resolvedSimdType.empty() && SymbolTable::IsSimdStorageVectorType(srcExpr.resolvedSimdType, storageLanes)){
        const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(srcExpr.resolvedSimdType);
        if(!vecInfo || !vecInfo->isFloatReg || vecInfo->isIntReg || vecInfo->isMixed || vecInfo->isDoubleReg != targetIsDouble || vecInfo->loadCppFunc.empty())
            return false;
        outStorageLoadFn = vecInfo->loadCppFunc;
        outEffectiveZeroedAboveLane = storageLanes;
        return true;
    }
    if(!srcExpr.resolvedUnalignedType.empty()){
        const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(srcExpr.resolvedUnalignedType);
        if(!unalignedInfo || unalignedInfo->isHalfFloat || IsIntegerScalarType(unalignedInfo->laneScalarType))
            return false;
        const b8 srcIsDouble = (unalignedInfo->laneScalarType == "f64");
        if(srcIsDouble != targetIsDouble)
            return false;
        outStorageLoadFn = srcIsDouble ? "::LoadDouble" : "::LoadFloat";
        outEffectiveZeroedAboveLane = unalignedInfo->lanes;
        return true;
    }
    return false;
}

[[nodiscard]] b8 TryGetAnyFloatVectorSourceInfo(const Expr& srcExpr, b8& outIsDouble, MStringView& outRegSimdType, MStringView& outStorageLoadFn, u32& outEffectiveZeroedAboveLane)noexcept{
    if(TryGetFloatVectorSourceInfo(srcExpr, false, outRegSimdType, outStorageLoadFn, outEffectiveZeroedAboveLane)){
        outIsDouble = false;
        return true;
    }
    if(TryGetFloatVectorSourceInfo(srcExpr, true, outRegSimdType, outStorageLoadFn, outEffectiveZeroedAboveLane)){
        outIsDouble = true;
        return true;
    }
    return false;
}

[[nodiscard]] MStringView SimdRegisterCppType(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->regCppType.empty())
        return vecInfo->regCppType;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->regCppType;
    return "::SIMDVectorSingle";
}

[[nodiscard]] MStringView SimdLoadFunctionCpp(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->loadCppFunc.empty())
        return vecInfo->loadCppFunc;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->loadCppFunc;
    return "::LoadFloat";
}

[[nodiscard]] MStringView SimdStoreFunctionCpp(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->storeCppFunc.empty())
        return vecInfo->storeCppFunc;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->storeCppFunc;
    return "::StoreFloat";
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

[[nodiscard]] MStringView MapSimdUnaryFloatIntrinsic(const MStringView callee, const b8 isDouble)noexcept{
#define NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC(adhdName, floatIntrinsicPrefix, doubleIntrinsicPrefix) \
    if(callee == adhdName) return isDouble ? doubleIntrinsicPrefix : floatIntrinsicPrefix;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC
    return {};
}

void AppendSimdLaneCountIntrinsicPrefix(MString& out, const b8 isDouble, const u32 effLanes, const MStringView suffix){
    out.append(isDouble ? "::VectorDouble" : "::Vector");
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

[[nodiscard]] b8 IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept{
    if(!expr || expr->kind != ExprKind::Binary || expr->op != TokenType::Star || !expr->lhs || !expr->rhs)
        return false;
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(expr->resolvedSimdType);
    if(!vecInfo || !vecInfo->isFloatReg)
        return false;
    outMulA = expr->lhs;
    outMulB = expr->rhs;
    return true;
}

[[nodiscard]] const Expr* TryUnwrapSimdSplatScalarArg(const Expr& expr, const SymbolTable& symbols)noexcept{
    if(expr.kind != ExprKind::Call || !expr.lhs || expr.lhs->kind != ExprKind::Identifier || expr.arguments.size() != 1u || !expr.arguments[0])
        return nullptr;
    const MStringView baseCallee = symbols.resolveBaseTypeName(expr.lhs->text);
    if(!SymbolTable::IsSimdRegisterVectorType(baseCallee))
        return nullptr;
    const Expr* arg0 = expr.arguments[0];
    if(!arg0->resolvedSimdType.empty() || !arg0->resolvedUnalignedType.empty())
        return nullptr;
    return arg0;
}

[[nodiscard]] b8 IsZeroScalarLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = UnwrapConstScalarExpr(expr, symbols);
    if(unwrapped.text.empty())
        return false;
    if(unwrapped.kind == ExprKind::IntegerLiteral){
        b8 sawZero = false;
        const b8 isHex = unwrapped.text.size() >= 2u && unwrapped.text[0] == '0' && (unwrapped.text[1] == 'x' || unwrapped.text[1] == 'X');
        for(usize i = isHex ? 2u : 0u; i < unwrapped.text.size(); ++i){
            const char c = unwrapped.text[i];
            if(c == '0'){
                sawZero = true;
                continue;
            }
            if(c == 'u' || c == 'U' || c == 'l' || c == 'L' || (!isHex && (c == 'b' || c == 'B')))
                continue;
            return false;
        }
        return sawZero;
    }
    if(unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral){
        b8 sawZero = false;
        for(usize i = 0u; i < unwrapped.text.size(); ++i){
            const char c = unwrapped.text[i];
            if(c == 'e' || c == 'E')
                break;
            if(c == '0'){
                sawZero = true;
                continue;
            }
            if(c == '.' || c == 'f' || c == 'F')
                continue;
            return false;
        }
        return sawZero;
    }
    return false;
}

[[nodiscard]] b8 IsPureSimdAddressExpr(const Expr& expr)noexcept{
    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:
    case ExprKind::ThisMember:
        return !expr.isMutArg;
    case ExprKind::ScopeAccess:
        return !expr.isMutArg && (!expr.lhs || IsPureSimdAddressExpr(*expr.lhs));
    case ExprKind::MemberAccess:
        return !expr.isMutArg && expr.lhs && IsPureSimdAddressExpr(*expr.lhs);
    case ExprKind::Index:
        return !expr.isMutArg && expr.lhs && expr.rhs && IsPureSimdAddressExpr(*expr.lhs) && IsPureSimdAddressExpr(*expr.rhs);
    case ExprKind::Unary:
        return !expr.isMutArg && !expr.isPostfix
            && (expr.op == TokenType::Minus || expr.op == TokenType::Plus || expr.op == TokenType::Tilde || expr.op == TokenType::Bang)
            && expr.lhs && IsPureSimdAddressExpr(*expr.lhs);
    case ExprKind::Cast:
        return !expr.isMutArg && expr.targetType && expr.targetType->refMode == ParamPassingMode::None
            && expr.targetType->templateArgs.empty() && expr.lhs && IsPureSimdAddressExpr(*expr.lhs);
    default:
        return false;
    }
}

[[nodiscard]] b8 AreSamePureSimdExpr(const Expr& a, const Expr& b)noexcept{
    if(
        a.kind != b.kind
        || a.text != b.text
        || a.op != b.op
        || a.isPostfix != b.isPostfix
        || a.isSwizzle != b.isSwizzle
        || a.isPointerMemberAccess != b.isPointerMemberAccess
    ){
        return false;
    }
    if((a.targetType != nullptr) != (b.targetType != nullptr))
        return false;
    if(a.targetType && (a.targetType->name != b.targetType->name || a.targetType->refMode != b.targetType->refMode || a.targetType->templateArgs.size() != b.targetType->templateArgs.size()))
        return false;
    if(a.scopeChain.size() != b.scopeChain.size())
        return false;
    for(usize i = 0u; i < a.scopeChain.size(); ++i){
        if(a.scopeChain[i] != b.scopeChain[i])
            return false;
    }
    if((a.lhs != nullptr) != (b.lhs != nullptr) || (a.rhs != nullptr) != (b.rhs != nullptr))
        return false;
    if(a.lhs && !AreSamePureSimdExpr(*a.lhs, *b.lhs))
        return false;
    if(a.rhs && !AreSamePureSimdExpr(*a.rhs, *b.rhs))
        return false;
    return a.arguments.empty() && b.arguments.empty();
}

[[nodiscard]] b8 TryMatchSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept{
    if(!expr || expr->kind != ExprKind::MemberAccess || expr->isSwizzle || expr->isMutArg || !expr->lhs)
        return false;
    u32 lane = 0u;
    if(!IsSingleSwizzleLane(expr->text, lane))
        return false;
    const Expr* base = expr->lhs;
    while(base && base->kind == ExprKind::MemberAccess && base->isSwizzle && !base->isMutArg && base->lhs){
        if(lane >= base->text.size() || !TryMapSwizzleCharToLane(base->text[lane], lane))
            return false;
        base = base->lhs;
    }
    if(!base || !IsPureSimdAddressExpr(*base))
        return false;
    outBase = base;
    outLane = lane;
    return true;
}

[[nodiscard]] b8 TryMatchNegatedSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept{
    if(!expr || expr->kind != ExprKind::Unary || expr->isPostfix || expr->op != TokenType::Minus || !expr->lhs)
        return false;
    return TryMatchSingleLaneAccess(expr->lhs, outBase, outLane);
}

[[nodiscard]] b8 TryMatchCastSingleLaneAccess(const Expr* expr, const MStringView expectedScalarType, const SymbolTable& symbols, const Expr*& outBase, u32& outLane)noexcept{
    if(!expr || expr->kind != ExprKind::Cast || !expr->targetType || !expr->lhs)
        return false;
    if(expr->targetType->refMode != ParamPassingMode::None || !expr->targetType->templateArgs.empty())
        return false;
    if(symbols.resolveBaseTypeName(expr->targetType->name) != expectedScalarType)
        return false;
    return TryMatchSingleLaneAccess(expr->lhs, outBase, outLane);
}

[[nodiscard]] MStringView ResolveLocalVarDeclSimdType(const Stmt& stmt, const SymbolTable& symbols)noexcept{
    if(stmt.usesScratchArena || (stmt.varType && stmt.varType->refMode != ParamPassingMode::None))
        return {};
    if(stmt.varType){
        const MStringView resolved = symbols.resolveBaseTypeName(stmt.varType->name);
        if(IsRegisterPromotableSimdType(resolved))
            return resolved;
    }
    if(stmt.expr && IsRegisterPromotableSimdType(stmt.expr->resolvedSimdType))
        return stmt.expr->resolvedSimdType;
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
