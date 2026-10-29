// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_binding_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_codegen_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr const char* s_IntLaneGettersSingle[4] = {
    "::VectorGetIntX(",
    "::VectorGetIntY(",
    "::VectorGetIntZ(",
    "::VectorGetIntW("
};

static constexpr const char* s_IntLaneGettersDouble[4] = {
    "::VectorDoubleGetIntX(",
    "::VectorDoubleGetIntY(",
    "::VectorDoubleGetIntZ(",
    "::VectorDoubleGetIntW("
};

[[nodiscard]] constexpr b8 IsFloatSimdVectorType(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(isFloatReg){ \
        if(simdType == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 TryMapSwizzleCharToLane(const char c, u32& outLane)noexcept{
    switch(c){
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex) \
    case xyzwChar: \
    case rgbaChar: \
        outLane = laneIndex; \
        return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsRegisterPromotableSimdType(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(isFloatReg || isIntReg){ \
        if(simdType == adhdName) return true; \
    }
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(simdType == adhdName) return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return false;
}

[[nodiscard]] b8 IsDoubleSimdType(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(isDoubleReg && (isFloatReg || isIntReg)){ \
        if(simdType == adhdName) return true; \
    }
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(isDoubleReg){ \
        if(simdType == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return false;
}

[[nodiscard]] b8 IsIntSimdType(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(isIntReg){ \
        if(simdType == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return false;
}

[[nodiscard]] b8 IsSignedIntSimdType(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(isIntReg && isSignedInt){ \
        if(simdType == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return false;
}

[[nodiscard]] MStringView IntSimdLaneType(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(isIntReg){ \
        if(simdType == adhdName) return xyzScalarType; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return "u64";
}

[[nodiscard]] MStringView IntSimdRawWordType(const b8 isDouble)noexcept{
    return isDouble ? "u64" : "u32";
}

void AppendIntSimdLaneExtract(
    MString& out,
    const MStringView varName,
    const u32 lane,
    const b8 isDouble,
    const MStringView laneType
){
    const char* const getter = isDouble
        ? __hidden_adhd_simd_codegen_utils::s_IntLaneGettersDouble[lane]
        : __hidden_adhd_simd_codegen_utils::s_IntLaneGettersSingle[lane];
    out.append("static_cast<");
    out.append(laneType.data(), laneType.size());
    out.append(">(");
    out.append(getter);
    out.append(varName.data(), varName.size());
    out.append("))");
}

[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept{
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle && expr.text.size() >= 2u && expr.text.size() <= 4u)
        return static_cast<u32>(expr.text.size());
    u32 lanes = 4u;
    if(SymbolTable::isSimdVectorType(expr.resolvedSimdType, lanes))
        return lanes;
    return 4u;
}

[[nodiscard]] b8 IsUnitFloatLiteral(const Expr& expr)noexcept{
    if(expr.kind != ExprKind::FloatLiteral && expr.kind != ExprKind::DoubleLiteral)
        return false;
    return expr.text == "1.0f" || expr.text == "1.0" || expr.text == "1.f";
}

[[nodiscard]] MStringView SimdRegisterCppType(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(!MStringView(regCppType).empty()){ \
        if(simdType == adhdName) return regCppType; \
    }
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(simdType == adhdName) return regCppType;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return "::SIMDVectorSingle";
}

[[nodiscard]] MStringView SimdLoadFunctionCpp(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(!MStringView(loadCppFunc).empty()){ \
        if(simdType == adhdName) return loadCppFunc; \
    }
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(simdType == adhdName) return loadCppFunc;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return "::LoadFloat";
}

[[nodiscard]] MStringView SimdStoreFunctionCpp(const MStringView simdType)noexcept{
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if constexpr(!MStringView(storeCppFunc).empty()){ \
        if(simdType == adhdName) return storeCppFunc; \
    }
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, rows, cols, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(simdType == adhdName) return storeCppFunc;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return "::StoreFloat";
}

[[nodiscard]] MStringView MapBuiltinMathFunction(const MStringView name)noexcept{
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule) \
    if(name == adhdName) return cppExportName;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION
    return {};
}

[[nodiscard]] MStringView MapSimdUnaryFloatIntrinsic(const MStringView callee, const b8 isDouble)noexcept{
#define NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC(adhdName, floatIntrinsicPrefix, doubleIntrinsicPrefix) \
    if(callee == adhdName) return isDouble ? doubleIntrinsicPrefix : floatIntrinsicPrefix;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC
    return {};
}

void AppendSimdLaneCountIntrinsicPrefix(
    MString& out,
    const b8 isDouble,
    const u32 effLanes,
    const MStringView suffix
){
    out.append(isDouble ? "::VectorDouble" : "::Vector");
    out.push_back(effLanes == 2u ? '2' : (effLanes == 3u ? '3' : '4'));
    out.append(suffix.data(), suffix.size());
    out.push_back('(');
}

[[nodiscard]] b8 IsSimdAssignmentOp(const TokenType::Enum op)noexcept{
    return IsCompoundOrSimpleAssignment(op);
}

[[nodiscard]] TokenType::Enum MapSimdAssignToBinaryOp(const TokenType::Enum op)noexcept{
    switch(op){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, floatIntrinsic, doubleIntrinsic, int128Intrinsic, int256Intrinsic) \
    case assignToken: return binaryToken;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return op;
    }
}

[[nodiscard]] MStringView SimdBinaryScalarOpSymbol(const TokenType::Enum binaryOp)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, floatIntrinsic, doubleIntrinsic, int128Intrinsic, int256Intrinsic) \
    case binaryToken: return paddedBinarySymbol;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return {};
    }
}

[[nodiscard]] MStringView SimdCompoundScalarOpSymbol(const TokenType::Enum op)noexcept{
    return SimdBinaryScalarOpSymbol(MapSimdAssignToBinaryOp(op));
}

[[nodiscard]] b8 IsSingleSwizzleLane(const MStringView text, u32& outLane)noexcept{
    return text.size() == 1u && __hidden_adhd_simd_codegen_utils::TryMapSwizzleCharToLane(text[0], outLane);
}

[[nodiscard]] u32 MapSwizzleCharToLane(const char c)noexcept{
    u32 lane = 3u;
    (void)__hidden_adhd_simd_codegen_utils::TryMapSwizzleCharToLane(c, lane);
    return lane;
}

[[nodiscard]] b8 IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept{
    if(!expr || expr->kind != ExprKind::Binary || expr->op != TokenType::Star || !expr->lhs || !expr->rhs)
        return false;
    if(expr->resolvedSimdType.empty() || !__hidden_adhd_simd_codegen_utils::IsFloatSimdVectorType(expr->resolvedSimdType))
        return false;
    outMulA = expr->lhs;
    outMulB = expr->rhs;
    return true;
}

[[nodiscard]] b8 ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept{
    if(!expr)
        return false;

    if(expr->kind == ExprKind::MemberAccess && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
        u32 lane = 0u;
        if(expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane))
            return true;
    }

    if(expr->kind == ExprKind::Index && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Unary && IsRegisterPromotableSimdType(expr->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Binary && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        if(expr->op != TokenType::Equal)
            return true;
        if(ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }

    if(expr->kind == ExprKind::Binary && IsSimdAssignmentOp(expr->op) && expr->lhs){
        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr->lhs->text, lane))
        {
            return true;
        }
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType))
        {
            return true;
        }
    }

    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr->lhs->text;
        const MStringView baseCallee = symbols.resolveBaseTypeName(callee);
        if(symbols.isSimdType(baseCallee) && IsRegisterPromotableSimdType(expr->resolvedSimdType))
            return true;
        if(!MapBuiltinMathFunction(callee).empty()){
            if(IsRegisterPromotableSimdType(expr->resolvedSimdType))
                return true;
            for(const Expr* arg : expr->arguments){
                if(arg && IsRegisterPromotableSimdType(arg->resolvedSimdType))
                    return true;
            }
        }
    }

    if(ExprContainsSimdMath(expr->lhs, symbols) || ExprContainsSimdMath(expr->rhs, symbols))
        return true;

    for(const Expr* arg : expr->arguments){
        if(ExprContainsSimdMath(arg, symbols))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
