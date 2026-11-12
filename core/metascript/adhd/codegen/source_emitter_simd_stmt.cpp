// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include <core/metascript/adhd/ast/operator_semantics.h>
#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "simd_binary_emitter.h"
#include "simd_expression_analysis.h"
#include "simd_storage_emitter.h"
#include "simd_constructor_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SourceEmitter::isDirectSimdRegExpr(const Expr& expr)const noexcept{
    if(!IsRegisterPromotableSimdType(expr.resolvedSimdType))
        return false;
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle)
        return true;
    if(expr.kind == ExprKind::Unary)
        return true;
    if(expr.kind == ExprKind::Binary && expr.op != TokenType::Equal)
        return true;
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        if(expr.isBuiltinCall)
            return true;
        const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
        if(SymbolTable::IsSimdRegisterType(baseCallee))
            return true;
    }
    return false;
}

b8 SourceEmitter::tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.usesScratchArena || (stmt.varType && m_symbols.resolveRefMode(stmt.varType) != ParamPassingMode::None))
        return false;
    const MStringView varSimdType = SimdExpressionAnalysis::ResolveLocalVarDeclSimdType(stmt, m_symbols);
    if(SymbolTable::IsSimdRegisterType(varSimdType)){
        const MStringView regType = SimdRegisterCppType(varSimdType);
        emitIndent(out, indent);
        if(stmt.isConst)
            out.append("const ");
        out.append(regType.data(), regType.size());
        out.push_back(' ');
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = ");
        if(stmt.expr && !stmt.isCtorInit)
            emitSimdRegExpr(out, *stmt.expr, varSimdType);
        else
            SimdConstructorEmitter(*this, out, varSimdType, stmt.arguments).emit();
        out.append(";\r\n");
        return true;
    }
    const MStringView baseVarType = stmt.varType ? m_symbols.resolveBaseTypeName(stmt.varType->name) : MStringView{};
    if(!baseVarType.empty() && (SymbolTable::IsSimdStorageType(baseVarType) || SymbolTable::IsUnalignedVectorType(baseVarType))){
        Span<const Expr* const> storeArgs;
        if(stmt.isCtorInit && !stmt.arguments.empty()){
            storeArgs = stmt.arguments;
        }
        else if(stmt.expr && stmt.expr->kind == ExprKind::Call && stmt.expr->lhs && stmt.expr->lhs->kind == ExprKind::Identifier && m_symbols.resolveBaseTypeName(stmt.expr->lhs->text) == baseVarType && !stmt.expr->arguments.empty()){
            storeArgs = stmt.expr->arguments;
        }
        if(!storeArgs.empty()){
            MString storeCallStr(m_arena);
            if(SimdStorageEmitter(*this).tryEmit(storeCallStr, baseVarType, storeArgs, stmt.name)){
                emitIndent(out, indent);
                m_typeEmitter.emitTypeCpp(out, *stmt.varType);
                out.push_back(' ');
                out.append(stmt.name.data(), stmt.name.size());
                out.append("{};\r\n");
                emitIndent(out, indent);
                out.append(storeCallStr.data(), storeCallStr.size());
                out.append(";\r\n");
                return true;
            }
        }
    }
    return false;
}

b8 SourceEmitter::tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.expr && stmt.expr->kind == ExprKind::Binary && OperatorSemantics::IsCompoundOrSimpleAssignment(stmt.expr->op) && stmt.expr->lhs && stmt.expr->rhs){
        const Expr& lhs = *stmt.expr->lhs;
        const Expr& rhs = *stmt.expr->rhs;

        if(IsRegisterPromotableSimdType(lhs.resolvedSimdType)){
            const MStringView simdType = lhs.resolvedSimdType;
            emitIndent(out, indent);
            emitExpr(out, lhs);
            out.append(" = ");
            if(stmt.expr->op == TokenType::Equal || !SimdBinaryEmitter(*this).tryEmit(out, MapSimdAssignToBinaryOp(stmt.expr->op), lhs, rhs, simdType))
                emitSimdRegExpr(out, rhs, simdType);
            out.append(";\r\n");
            return true;
        }

        if(stmt.expr->op == TokenType::Equal && rhs.kind == ExprKind::Call && rhs.lhs && rhs.lhs->kind == ExprKind::Identifier && !rhs.arguments.empty()){
            const MStringView rhsBaseCallee = m_symbols.resolveBaseTypeName(rhs.lhs->text);
            if(SymbolTable::IsSimdStorageType(rhsBaseCallee) || SymbolTable::IsUnalignedVectorType(rhsBaseCallee)){
                MString lhsExprText(m_arena);
                emitExpr(lhsExprText, lhs);
                MString storeCallStr(m_arena);
                if(SimdStorageEmitter(*this).tryEmit(storeCallStr, rhsBaseCallee, rhs.arguments, MStringView(lhsExprText.data(), lhsExprText.size()))){
                    emitIndent(out, indent);
                    out.append(storeCallStr.data(), storeCallStr.size());
                    out.append(";\r\n");
                    return true;
                }
            }
        }

        u32 lane = 0u;
        const b8 isStaticLaneAssign = (
            lhs.kind == ExprKind::MemberAccess
            && lhs.lhs
            && SymbolTable::IsSimdRegisterVectorType(lhs.lhs->resolvedSimdType)
            && IsSingleSwizzleLane(lhs.text, lane)
        ) || (
            lhs.kind == ExprKind::Index
            && lhs.lhs
            && lhs.rhs
            && SymbolTable::IsSimdRegisterVectorType(lhs.lhs->resolvedSimdType)
            && SimdExpressionAnalysis::TryMatchConstLaneIndex(*lhs.rhs, lane)
        );
        const b8 isDynamicIndexAssign = !isStaticLaneAssign
            && lhs.kind == ExprKind::Index
            && lhs.lhs
            && lhs.rhs
            && SymbolTable::IsSimdRegisterVectorType(lhs.lhs->resolvedSimdType);
        if(isStaticLaneAssign || isDynamicIndexAssign){
            const MStringView simdType = lhs.lhs->resolvedSimdType;
            const b8 isDouble = IsDoubleSimdType(simdType);
            const b8 isInt = IsIntSimdType(simdType);

            emitIndent(out, indent);
            emitExpr(out, *lhs.lhs);
            out.append(" = ");
            if(isInt)
                out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
            else
                out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
            if(isDynamicIndexAssign){
                out.append("ByIndex(");
            }
            else{
                const MStringView laneSuffix = SimdLaneCallSuffix(lane);
                out.append(laneSuffix.data(), laneSuffix.size());
            }
            emitExpr(out, *lhs.lhs);
            out.append(", ");
            if(isInt)
                out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
            if(stmt.expr->op == TokenType::Equal){
                emitExpr(out, rhs);
            }
            else{
                const MStringView opSym = SimdBinaryScalarOpSymbol(MapSimdAssignToBinaryOp(stmt.expr->op));
                out.push_back('(');
                emitExpr(out, lhs);
                out.append(opSym.data(), opSym.size());
                out.push_back('(');
                emitExpr(out, rhs);
                out.append("))");
            }
            if(isInt)
                out.push_back(')');
            if(isDynamicIndexAssign){
                out.append(", static_cast<usize>(");
                emitExpr(out, *lhs.rhs);
                out.append("));\r\n");
            }
            else{
                out.append(");\r\n");
            }
            return true;
        }
    }
    return false;
}

b8 SourceEmitter::tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.wrapReturnNotNull)
        return false;
    if(stmt.expr && !stmt.expr->wrapInSimdResult && stmt.expr->kind == ExprKind::Call && stmt.expr->lhs && stmt.expr->lhs->kind == ExprKind::Identifier && !stmt.expr->arguments.empty()){
        const MStringView baseCallee = m_symbols.resolveBaseTypeName(stmt.expr->lhs->text);
        if(SymbolTable::IsSimdStorageType(baseCallee) || SymbolTable::IsUnalignedVectorType(baseCallee)){
            MString storeCallStr(m_arena);
            if(SimdStorageEmitter(*this).tryEmit(storeCallStr, baseCallee, stmt.expr->arguments, "__adhd_simd_ret")){
                TypeRef retType(m_arena);
                retType.name = stmt.expr->lhs->text;
                emitIndent(out, indent);
                m_typeEmitter.emitTypeCpp(out, retType);
                out.append(" __adhd_simd_ret{};\r\n");
                emitIndent(out, indent);
                out.append(storeCallStr.data(), storeCallStr.size());
                out.append(";\r\n");
                emitIndent(out, indent);
                out.append("return __adhd_simd_ret;\r\n");
                return true;
            }
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

