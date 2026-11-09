// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/operator_semantics.h>
#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "source_emitter.h"
#include "simd_binary_emitter.h"
#include "simd_expression_analysis.h"
#include "simd_storage_emitter.h"
#include "simd_constructor_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_stmt{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const Expr* TryExtractDirectTransposeMatSource(const Stmt& declStmt)noexcept{
    if(!declStmt.isConst || declStmt.name.empty())
        return nullptr;
    const Expr* initExpr = nullptr;
    if(!declStmt.isCtorInit && declStmt.expr)
        initExpr = declStmt.expr;
    else if(declStmt.isCtorInit && declStmt.arguments.size() == 1u && declStmt.arguments[0])
        initExpr = declStmt.arguments[0];
    if(
        !initExpr
        || initExpr->kind != ExprKind::Call
        || !initExpr->isBuiltinCall
        || !initExpr->lhs
        || initExpr->lhs->kind != ExprKind::Identifier
        || initExpr->lhs->text != "Transpose"
        || initExpr->arguments.size() != 1u
        || !initExpr->arguments[0]
    ){
        return nullptr;
    }
    const Expr* srcMat = initExpr->arguments[0];
    if(srcMat->kind != ExprKind::Identifier || srcMat->isMutArg || !SymbolTable::IsSimdRegisterMatrixType(srcMat->resolvedSimdType))
        return nullptr;
    return srcMat;
}

[[nodiscard]] b8 ExprReferencesIdentifier(const Expr* expr, const MStringView name)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Identifier && expr->text == name)
        return true;
    if(ExprReferencesIdentifier(expr->lhs, name) || ExprReferencesIdentifier(expr->rhs, name))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprReferencesIdentifier(arg, name))
            return true;
    }
    return false;
}

[[nodiscard]] b8 CanElideConstTransposeVarDeclInStmts(
    const MStringView transposedName,
    const MStringView srcMatName,
    const Span<const Stmt* const> statements,
    const SymbolTable& symbols,
    const Span<const Stmt* const> localVarDecls,
    u32& inOutValidUseCount
)noexcept{
    const auto checkExpr = [&](auto& self, const Expr* expr)noexcept -> b8{
        if(!expr)
            return true;
        if(expr->kind == ExprKind::Lambda)
            return !ExprReferencesIdentifier(expr, transposedName) && !ExprReferencesIdentifier(expr, srcMatName);
        if(
            expr->kind == ExprKind::Call
            && expr->isBuiltinCall
            && expr->lhs
            && expr->lhs->kind == ExprKind::Identifier
            && expr->lhs->text == "Mul"
            && expr->arguments.size() == 2u
            && expr->arguments[0]
            && expr->arguments[1]
        ){
            const Expr* a0 = expr->arguments[0];
            const Expr* a1 = expr->arguments[1];
            const b8 a0IsTrans = (a0->kind == ExprKind::Identifier && a0->text == transposedName && !a0->isMutArg);
            const b8 a1IsTrans = (a1->kind == ExprKind::Identifier && a1->text == transposedName && !a1->isMutArg);
            if(a0IsTrans != a1IsTrans){
                const Expr* vecArg = a0IsTrans ? a1 : a0;
                u32 rowIdx = 0u;
                if(SimdConstantAnalysis::TryMatchIdentityRowIndex(rowIdx, *vecArg, symbols, localVarDecls) && !ExprReferencesIdentifier(vecArg, transposedName)){
                    ++inOutValidUseCount;
                    return self(self, vecArg);
                }
                return false;
            }
        }
        if(expr->kind == ExprKind::Identifier){
            if(expr->text == transposedName)
                return false;
            if(expr->text == srcMatName && expr->isMutArg)
                return false;
        }
        if(expr->kind == ExprKind::Binary && OperatorSemantics::IsCompoundOrSimpleAssignment(expr->op) && ExprReferencesIdentifier(expr->lhs, srcMatName))
            return false;
        if(!self(self, expr->lhs) || !self(self, expr->rhs))
            return false;
        for(const Expr* arg : expr->arguments){
            if(!self(self, arg))
                return false;
        }
        return true;
    };

    for(const Stmt* s : statements){
        if(!s)
            continue;
        if(s->kind == StmtKind::NativeBlock)
            return false;
        if(!s->name.empty() && (s->name == transposedName || s->name == srcMatName))
            return false;
        if(s->initStmt && !s->initStmt->name.empty() && (s->initStmt->name == transposedName || s->initStmt->name == srcMatName))
            return false;
        if(!checkExpr(checkExpr, s->expr) || !checkExpr(checkExpr, s->condition) || !checkExpr(checkExpr, s->stepExpr))
            return false;
        for(const Expr* arg : s->arguments){
            if(!checkExpr(checkExpr, arg))
                return false;
        }
        if(s->initStmt && !checkExpr(checkExpr, s->initStmt->expr))
            return false;
        if(!CanElideConstTransposeVarDeclInStmts(transposedName, srcMatName, s->statements, symbols, localVarDecls, inOutValidUseCount))
            return false;
        if(!CanElideConstTransposeVarDeclInStmts(transposedName, srcMatName, s->elseStatements, symbols, localVarDecls, inOutValidUseCount))
            return false;
        for(const SwitchCase& sc : s->switchCases){
            if(!checkExpr(checkExpr, sc.matchExpr))
                return false;
            if(!CanElideConstTransposeVarDeclInStmts(transposedName, srcMatName, sc.statements, symbols, localVarDecls, inOutValidUseCount))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
        if(SymbolTable::IsSimdRegisterMatrixType(varSimdType)){
            if(const Expr* transposeSrcMat = __hidden_adhd_simd_stmt::TryExtractDirectTransposeMatSource(stmt)){
                u32 validUseCount = 0u;
                if(__hidden_adhd_simd_stmt::CanElideConstTransposeVarDeclInStmts(stmt.name, transposeSrcMat->text, m_remainingBlockStatements, m_symbols, activeLocalVarDecls(), validUseCount) && validUseCount > 0u)
                    return true;
            }
        }
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

