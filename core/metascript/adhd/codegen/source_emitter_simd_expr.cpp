// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_expr{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EmitSimdMatrixBinaryRowInitSuffix(MString& out, const MStringView matRegType, const MStringView fnPrefix){
    out.append("; return ");
    out.append(matRegType.data(), matRegType.size());
    out.append("{ .v = { ");
    for(u32 i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        out.append(fnPrefix.data(), fnPrefix.size());
        out.append("__a.v[");
        AppendUnsignedDecimal(out, i);
        out.append("], __b.v[");
        AppendUnsignedDecimal(out, i);
        out.append("])");
    }
    out.append(" } }; }())");
}

void EmitSimdMatrixUnaryRowInitSuffix(MString& out, const MStringView matRegType, const MStringView fnPrefix, const MStringView fnSuffix){
    out.append("; return ");
    out.append(matRegType.data(), matRegType.size());
    out.append("{ .v = { ");
    for(u32 i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        out.append(fnPrefix.data(), fnPrefix.size());
        out.append("__m.v[");
        AppendUnsignedDecimal(out, i);
        out.push_back(']');
        out.append(fnSuffix.data(), fnSuffix.size());
    }
    out.append(" } }; }())");
}

void EmitSimdIntTwosComplementNegatePrefix(MString& out, const b8 isDouble){
    out.append(isDouble ? "::VectorDoubleAddInt(::VectorDoubleXorInt(" : "::VectorAddInt(::VectorXorInt(");
}

void EmitSimdIntTwosComplementNegateSuffix(MString& out, const b8 isDouble){
    out.append(isDouble ? ", ::VectorDoubleTrueInt()), ::VectorDoubleReplicateInt(1ull))" : ", ::VectorTrueInt()), ::VectorReplicateInt(1u))");
}

[[nodiscard]] const Expr* TryMatchSimdLengthSqVecArg(const Expr* expr, const b8 isDouble, const SymbolTable& symbols)noexcept{
    if(!expr)
        return nullptr;
    const Expr* candidate = expr->resolvedSimdType.empty() ? expr : TryUnwrapSimdSplatScalarArg(*expr, symbols);
    if(!candidate)
        return nullptr;
    if(
        candidate->kind == ExprKind::Call
        && candidate->isBuiltinCall
        && candidate->lhs
        && candidate->lhs->kind == ExprKind::Identifier
        && candidate->lhs->text == "lengthSq"
        && candidate->arguments.size() == 1u
        && candidate->arguments[0]
        && IsRegisterPromotableSimdType(candidate->arguments[0]->resolvedSimdType)
        && IsDoubleSimdType(candidate->arguments[0]->resolvedSimdType) == isDouble
    ){
        return candidate->arguments[0];
    }
    return nullptr;
}

[[nodiscard]] const Expr* TryMatchSimdLengthVecArg(const Expr* expr, const b8 isDouble, const SymbolTable& symbols)noexcept{
    if(!expr)
        return nullptr;
    const Expr* candidate = expr->resolvedSimdType.empty() ? expr : TryUnwrapSimdSplatScalarArg(*expr, symbols);
    if(!candidate)
        return nullptr;
    if(
        candidate->kind == ExprKind::Call
        && candidate->isBuiltinCall
        && candidate->lhs
        && candidate->lhs->kind == ExprKind::Identifier
        && candidate->arguments.size() == 1u
        && candidate->arguments[0]
    ){
        if(
            candidate->lhs->text == "length"
            && IsRegisterPromotableSimdType(candidate->arguments[0]->resolvedSimdType)
            && IsDoubleSimdType(candidate->arguments[0]->resolvedSimdType) == isDouble
        ){
            return candidate->arguments[0];
        }
        if(candidate->lhs->text == "sqrt")
            return TryMatchSimdLengthSqVecArg(candidate->arguments[0], isDouble, symbols);
    }
    return nullptr;
}

[[nodiscard]] b8 CanEmitScalarAsSimdVectorWithoutReplicate(const Expr& scalarExpr, const MStringView simdType, const SymbolTable& symbols)noexcept{
    if(!scalarExpr.resolvedSimdType.empty() || IsIntSimdType(simdType) || SymbolTable::IsSimdMatrixType(simdType))
        return false;
    const b8 isDouble = IsDoubleSimdType(simdType);
    const Expr* laneBase = nullptr;
    u32 laneIdx = 0u;
    if(TryMatchSingleLaneAccess(&scalarExpr, laneBase, laneIdx) && laneBase && laneBase->resolvedSimdType == simdType)
        return true;
    if(scalarExpr.kind == ExprKind::Call && scalarExpr.isBuiltinCall && scalarExpr.lhs && scalarExpr.lhs->kind == ExprKind::Identifier){
        const MStringView callee = scalarExpr.lhs->text;
        if(!MapSimdScalarReductionSuffix(callee).empty() && !scalarExpr.arguments.empty() && scalarExpr.arguments[0]){
            const MStringView argSimd = scalarExpr.arguments[0]->resolvedSimdType;
            if(IsRegisterPromotableSimdType(argSimd) && IsDoubleSimdType(argSimd) == isDouble)
                return true;
        }
        if(callee == "rsqrt" && scalarExpr.arguments.size() == 1u && TryMatchSimdLengthSqVecArg(scalarExpr.arguments[0], isDouble, symbols))
            return true;
    }
    if(scalarExpr.kind == ExprKind::Binary && scalarExpr.op == TokenType::Slash && scalarExpr.lhs && scalarExpr.rhs){
        if(IsUnitFloatLiteral(*scalarExpr.lhs, &symbols) && TryMatchSimdLengthVecArg(scalarExpr.rhs, isDouble, symbols))
            return true;
    }
    return false;
}

[[nodiscard]] b8 WouldSimdMulOperandRequireReplicateInFma(const Expr& operand, const MStringView simdType, const SymbolTable& symbols)noexcept{
    if(IsIntSimdType(simdType) || SymbolTable::IsSimdMatrixType(simdType))
        return false;
    const Expr* scalarExpr = operand.resolvedSimdType.empty() ? &operand : TryUnwrapSimdSplatScalarArg(operand, symbols);
    if(!scalarExpr)
        return false;
    if(
        IsZeroScalarLiteral(*scalarExpr, &symbols)
        || IsUnitFloatLiteral(*scalarExpr, &symbols)
        || IsNegativeUnitFloatLiteral(*scalarExpr, &symbols)
        || IsHalfFloatLiteral(*scalarExpr, &symbols)
    ){
        return false;
    }
    return !CanEmitScalarAsSimdVectorWithoutReplicate(*scalarExpr, simdType, symbols);
}

[[nodiscard]] b8 TryMatchSimdTransform3Arg(
    const Expr& vecExpr,
    const MStringView simdType,
    const SymbolTable& symbols,
    const Expr*& outBaseVec,
    MStringView& outBaseRegType,
    MStringView& outBaseLoadFn,
    u32& outTransformLanes,
    b8& outIsNormal
)noexcept{
    if(vecExpr.kind != ExprKind::Call || !vecExpr.lhs || vecExpr.lhs->kind != ExprKind::Identifier)
        return false;
    const MStringView baseCallee = symbols.resolveBaseTypeName(vecExpr.lhs->text);
    if(baseCallee != simdType)
        return false;
    const b8 isDouble = IsDoubleSimdType(simdType);
    if(vecExpr.arguments.size() == 2u && vecExpr.arguments[0] && vecExpr.arguments[1]){
        MStringView regType;
        MStringView loadFn;
        u32 zeroedAbove = 4u;
        if(
            TryGetFloatVectorSourceInfo(*vecExpr.arguments[0], isDouble, regType, loadFn, zeroedAbove)
            && zeroedAbove >= 3u
            && vecExpr.arguments[1]->resolvedSimdType.empty()
            && vecExpr.arguments[1]->resolvedUnalignedType.empty()
        ){
            if(IsUnitFloatLiteral(*vecExpr.arguments[1], &symbols)){
                outBaseVec = vecExpr.arguments[0];
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = false;
                return true;
            }
            if(IsZeroScalarLiteral(*vecExpr.arguments[1], &symbols)){
                outBaseVec = vecExpr.arguments[0];
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = true;
                return true;
            }
        }
        return false;
    }
    if((vecExpr.arguments.size() == 3u || vecExpr.arguments.size() == 4u) && vecExpr.arguments[0] && vecExpr.arguments[1] && vecExpr.arguments[2]){
        const Expr* baseVec = nullptr;
        for(u32 i = 0u; i < 2u; ++i){
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(!TryMatchSingleLaneAccess(vecExpr.arguments[i], laneBase, laneIdx) || laneIdx != i || !laneBase)
                return false;
            if(!baseVec)
                baseVec = laneBase;
            else if(!AreSamePureSimdExpr(*baseVec, *laneBase))
                return false;
        }
        MStringView regType;
        MStringView loadFn;
        u32 zeroedAbove = 4u;
        if(!baseVec || !TryGetFloatVectorSourceInfo(*baseVec, isDouble, regType, loadFn, zeroedAbove))
            return false;
        if(
            vecExpr.arguments.size() == 4u
            && vecExpr.arguments[3]
            && zeroedAbove >= 2u
            && vecExpr.arguments[2]->resolvedSimdType.empty()
            && vecExpr.arguments[2]->resolvedUnalignedType.empty()
            && vecExpr.arguments[3]->resolvedSimdType.empty()
            && vecExpr.arguments[3]->resolvedUnalignedType.empty()
            && IsZeroScalarLiteral(*vecExpr.arguments[2], &symbols)
            && IsUnitFloatLiteral(*vecExpr.arguments[3], &symbols)
        ){
            outBaseVec = baseVec;
            outBaseRegType = regType;
            outBaseLoadFn = loadFn;
            outTransformLanes = 2u;
            outIsNormal = false;
            return true;
        }
        const Expr* lane2Base = nullptr;
        u32 lane2Idx = 0u;
        if(
            zeroedAbove < 3u
            || !TryMatchSingleLaneAccess(vecExpr.arguments[2], lane2Base, lane2Idx)
            || lane2Idx != 2u
            || !lane2Base
            || !AreSamePureSimdExpr(*baseVec, *lane2Base)
        ){
            return false;
        }
        if(vecExpr.arguments.size() == 3u){
            outBaseVec = baseVec;
            outBaseRegType = regType;
            outBaseLoadFn = loadFn;
            outTransformLanes = 3u;
            outIsNormal = true;
            return true;
        }
        if(vecExpr.arguments[3] && vecExpr.arguments[3]->resolvedSimdType.empty() && vecExpr.arguments[3]->resolvedUnalignedType.empty()){
            if(IsUnitFloatLiteral(*vecExpr.arguments[3], &symbols)){
                outBaseVec = baseVec;
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = false;
                return true;
            }
            if(IsZeroScalarLiteral(*vecExpr.arguments[3], &symbols)){
                outBaseVec = baseVec;
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = true;
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] const Expr* TryUnwrapSimdNegatedVecExpr(const Expr& expr, const MStringView simdType, const SymbolTable& symbols)noexcept{
    if(expr.resolvedSimdType != simdType || IsIntSimdType(simdType) || SymbolTable::IsSimdMatrixType(simdType))
        return nullptr;
    if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs && expr.lhs->resolvedSimdType == simdType)
        return expr.lhs;
    if(expr.kind == ExprKind::Binary && expr.lhs && expr.rhs){
        if(expr.op == TokenType::Star){
            if(expr.lhs->resolvedSimdType == simdType){
                const Expr* rhsScalar = expr.rhs->resolvedSimdType.empty() ? expr.rhs : TryUnwrapSimdSplatScalarArg(*expr.rhs, symbols);
                if(rhsScalar && IsNegativeUnitFloatLiteral(*rhsScalar, &symbols))
                    return expr.lhs;
            }
            if(expr.rhs->resolvedSimdType == simdType){
                const Expr* lhsScalar = expr.lhs->resolvedSimdType.empty() ? expr.lhs : TryUnwrapSimdSplatScalarArg(*expr.lhs, symbols);
                if(lhsScalar && IsNegativeUnitFloatLiteral(*lhsScalar, &symbols))
                    return expr.rhs;
            }
        }
        else if(expr.op == TokenType::Slash && expr.lhs->resolvedSimdType == simdType){
            const Expr* rhsScalar = expr.rhs->resolvedSimdType.empty() ? expr.rhs : TryUnwrapSimdSplatScalarArg(*expr.rhs, symbols);
            if(rhsScalar && IsNegativeUnitFloatLiteral(*rhsScalar, &symbols))
                return expr.lhs;
        }
    }
    if(
        expr.kind == ExprKind::Call
        && expr.lhs
        && expr.lhs->kind == ExprKind::Identifier
        && expr.arguments.size() == 4u
        && symbols.resolveBaseTypeName(expr.lhs->text) == simdType
    ){
        const Expr* negBase = nullptr;
        for(u32 i = 0u; i < 4u; ++i){
            const Expr* curBase = nullptr;
            u32 curLane = 0u;
            if(!TryMatchNegatedSingleLaneAccess(expr.arguments[i], curBase, curLane) || !curBase || curBase->resolvedSimdType != simdType || curLane != i)
                return nullptr;
            if(!negBase)
                negBase = curBase;
            else if(!AreSamePureSimdExpr(*negBase, *curBase))
                return nullptr;
        }
        return negBase;
    }
    return nullptr;
}

[[nodiscard]] b8 TryMatchSimdNegatedMulExpr(
    const Expr* expr,
    const MStringView simdType,
    const Expr*& outA,
    const Expr*& outB,
    const SymbolTable& symbols
)noexcept{
    if(!expr)
        return false;
    if(const Expr* negInner = TryUnwrapSimdNegatedVecExpr(*expr, simdType, symbols)){
        if(IsSimdMulExpr(negInner, outA, outB, &symbols))
            return true;
    }
    const Expr* mulA = nullptr;
    const Expr* mulB = nullptr;
    if(!IsSimdMulExpr(expr, mulA, mulB, &symbols) || !mulA || !mulB)
        return false;
    const Expr* negA = TryUnwrapSimdNegatedVecExpr(*mulA, simdType, symbols);
    const Expr* negB = TryUnwrapSimdNegatedVecExpr(*mulB, simdType, symbols);
    if(negA && !negB){
        outA = negA;
        outB = mulB;
        return true;
    }
    if(!negA && negB){
        outA = mulA;
        outB = negB;
        return true;
    }
    return false;
}

[[nodiscard]] const Expr* TryGetSimdScalarOperand(const Expr& operand, const SymbolTable& symbols)noexcept{
    if(operand.resolvedSimdType.empty() && operand.resolvedUnalignedType.empty())
        return &operand;
    return TryUnwrapSimdSplatScalarArg(operand, symbols);
}

[[nodiscard]] const Expr* TryMatchSimdSaturateCallArg(
    const Expr& callExpr,
    const MStringView simdType,
    const b8 isDouble,
    const SymbolTable& symbols
)noexcept{
    if(callExpr.kind != ExprKind::Call || !callExpr.isBuiltinCall || !callExpr.lhs || callExpr.lhs->kind != ExprKind::Identifier)
        return nullptr;
    const MStringView callee = callExpr.lhs->text;
    if(callee == "clamp" && callExpr.arguments.size() == 3u && callExpr.arguments[0] && callExpr.arguments[1] && callExpr.arguments[2]){
        if(
            IsSimdZeroFloatVectorExpr(*callExpr.arguments[1], isDouble, &symbols)
            && IsSimdUnitFloatVectorExpr(*callExpr.arguments[2], isDouble, &symbols)
        ){
            return callExpr.arguments[0];
        }
        return nullptr;
    }
    const auto matchMinMaxConstArg = [&](const Expr& e, const MStringView expectedName, const b8 wantUnit)noexcept -> const Expr*{
        if(
            e.kind != ExprKind::Call
            || !e.isBuiltinCall
            || !e.lhs
            || e.lhs->kind != ExprKind::Identifier
            || e.lhs->text != expectedName
            || e.arguments.size() != 2u
            || !e.arguments[0]
            || !e.arguments[1]
        ){
            return nullptr;
        }
        const b8 arg1Matches = wantUnit
            ? IsSimdUnitFloatVectorExpr(*e.arguments[1], isDouble, &symbols)
            : IsSimdZeroFloatVectorExpr(*e.arguments[1], isDouble, &symbols);
        if(arg1Matches)
            return e.arguments[0];
        const b8 arg0Matches = wantUnit
            ? IsSimdUnitFloatVectorExpr(*e.arguments[0], isDouble, &symbols)
            : IsSimdZeroFloatVectorExpr(*e.arguments[0], isDouble, &symbols);
        if(arg0Matches)
            return e.arguments[1];
        return nullptr;
    };
    if(callee == "min"){
        if(const Expr* innerMax = matchMinMaxConstArg(callExpr, "min", true)){
            if(innerMax->resolvedSimdType == simdType)
                return matchMinMaxConstArg(*innerMax, "max", false);
        }
    }
    else if(callee == "max"){
        if(const Expr* innerMin = matchMinMaxConstArg(callExpr, "max", false)){
            if(innerMin->resolvedSimdType == simdType)
                return matchMinMaxConstArg(*innerMin, "min", true);
        }
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitSimdLaneGetExpr(MString& out, const Expr& vecExpr, const u32 lane, const Expr* dynamicIndexExpr)const{
    const MStringView lhsSimd = vecExpr.resolvedSimdType;
    const b8 isDouble = IsDoubleSimdType(lhsSimd);
    const b8 isInt = IsIntSimdType(lhsSimd);
    const b8 isSignedInt = IsSignedIntSimdType(lhsSimd);

    if(isSignedInt)
        out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
    if(isInt)
        out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
    else
        out.append(isDouble ? "::VectorDoubleGet" : "::VectorGet");
    if(dynamicIndexExpr){
        out.append("ByIndex(");
        emitSimdRegExpr(out, vecExpr, lhsSimd);
        out.append(", static_cast<usize>(");
        emitExpr(out, *dynamicIndexExpr);
        out.append("))");
    }
    else{
        const MStringView laneSuffix = SimdLaneCallSuffix(lane);
        out.append(laneSuffix.data(), laneSuffix.size());
        emitSimdRegExpr(out, vecExpr, lhsSimd);
        out.push_back(')');
    }
    if(isSignedInt)
        out.push_back(')');
}

b8 SourceEmitter::tryEmitSimdBinaryOpReg(MString& out, const TokenType::Enum binaryOp, const Expr& lhs, const Expr& rhs, const MStringView simdType)const{
    using namespace __hidden_adhd_simd_expr;

    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);
    const b8 isMatrix = SymbolTable::IsSimdMatrixType(simdType);

    const auto emitBinaryRegCall = [&](const MStringView fnPrefix, const Expr& a, const Expr& b){
        out.append(fnPrefix.data(), fnPrefix.size());
        emitSimdRegExpr(out, a, simdType);
        out.append(", ");
        emitSimdRegExpr(out, b, simdType);
        out.push_back(')');
    };

    const auto emitTernaryRegCall = [&](const MStringView fnPrefix, const Expr& a, const Expr& b, const Expr& c){
        out.append(fnPrefix.data(), fnPrefix.size());
        emitSimdRegExpr(out, a, simdType);
        out.append(", ");
        emitSimdRegExpr(out, b, simdType);
        out.append(", ");
        emitSimdRegExpr(out, c, simdType);
        out.push_back(')');
    };

    if(isMatrix){
        const MStringView matRegType = SimdRegisterCppType(simdType);
        if(binaryOp == TokenType::Plus || binaryOp == TokenType::Minus){
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __a = ");
            emitSimdRegExpr(out, lhs, simdType);
            out.append("; const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __b = ");
            emitSimdRegExpr(out, rhs, simdType);
            EmitSimdMatrixBinaryRowInitSuffix(
                out,
                matRegType,
                binaryOp == TokenType::Plus
                    ? (isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(")
                    : (isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(")
            );
            return true;
        }
        if(binaryOp == TokenType::Star){
            if(!lhs.resolvedSimdType.empty() && !rhs.resolvedSimdType.empty()){
                emitBinaryRegCall(isDouble ? "::MatrixDoubleMultiply(" : "::MatrixMultiply(", lhs, rhs);
                return true;
            }
            const Expr& matOperand = !lhs.resolvedSimdType.empty() ? lhs : rhs;
            const Expr& scalarOperand = !lhs.resolvedSimdType.empty() ? rhs : lhs;
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __m = ");
            emitSimdRegExpr(out, matOperand, simdType);
            out.append(isDouble ? "; const f64 __s = static_cast<f64>(" : "; const f32 __s = static_cast<f32>(");
            emitExpr(out, scalarOperand);
            out.push_back(')');
            EmitSimdMatrixUnaryRowInitSuffix(out, matRegType, isDouble ? "::VectorDoubleScale(" : "::VectorScale(", ", __s)");
            return true;
        }
        if(binaryOp == TokenType::Slash && rhs.resolvedSimdType.empty()){
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __m = ");
            emitSimdRegExpr(out, lhs, simdType);
            out.append(isDouble ? "; const ::SIMDVectorDouble __s = ::VectorDoubleReplicate(static_cast<f64>(" : "; const ::SIMDVectorSingle __s = ::VectorReplicate(static_cast<f32>(");
            emitExpr(out, rhs);
            out.append("))");
            EmitSimdMatrixUnaryRowInitSuffix(out, matRegType, isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(", ", __s)");
            return true;
        }
    }

    const Expr* mulA = nullptr;
    const Expr* mulB = nullptr;
    if(!isInt && (binaryOp == TokenType::Plus || binaryOp == TokenType::Minus || binaryOp == TokenType::Star || binaryOp == TokenType::Slash)){
        const Expr* lhsScalar = TryGetSimdScalarOperand(lhs, m_symbols);
        const Expr* rhsScalar = TryGetSimdScalarOperand(rhs, m_symbols);
        if(
            lhsScalar
            && rhsScalar
            && !CanEmitScalarAsSimdVectorWithoutReplicate(*lhsScalar, simdType, m_symbols)
            && !CanEmitScalarAsSimdVectorWithoutReplicate(*rhsScalar, simdType, m_symbols)
        ){
            const b8 skipForUnitOrNegUnit = (binaryOp == TokenType::Star && (IsUnitFloatLiteral(*lhsScalar, &m_symbols) || IsNegativeUnitFloatLiteral(*lhsScalar, &m_symbols) || IsUnitFloatLiteral(*rhsScalar, &m_symbols) || IsNegativeUnitFloatLiteral(*rhsScalar, &m_symbols)))
                || (binaryOp == TokenType::Slash && (IsUnitFloatLiteral(*rhsScalar, &m_symbols) || IsNegativeUnitFloatLiteral(*rhsScalar, &m_symbols)));
            if(!skipForUnitOrNegUnit){
                const MStringView opSym = SimdBinaryScalarOpSymbol(binaryOp);
                out.append(isDouble ? "::VectorDoubleReplicate((" : "::VectorReplicate((");
                emitExpr(out, *lhsScalar);
                out.append(opSym.data(), opSym.size());
                emitExpr(out, *rhsScalar);
                out.append("))");
                return true;
            }
        }
    }
    if(binaryOp == TokenType::Plus){
        if(!isInt){
            const Expr* negMulA = nullptr;
            const Expr* negMulB = nullptr;
            if(TryMatchSimdNegatedMulExpr(&lhs, simdType, negMulA, negMulB, m_symbols)){
                emitTernaryRegCall(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(", *negMulA, *negMulB, rhs);
                return true;
            }
            if(TryMatchSimdNegatedMulExpr(&rhs, simdType, negMulA, negMulB, m_symbols)){
                emitTernaryRegCall(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(", *negMulA, *negMulB, lhs);
                return true;
            }
            const Expr* rhsMulA = nullptr;
            const Expr* rhsMulB = nullptr;
            const b8 lhsIsMul = IsSimdMulExpr(&lhs, mulA, mulB, &m_symbols);
            const b8 rhsIsMul = IsSimdMulExpr(&rhs, rhsMulA, rhsMulB, &m_symbols);
            if(lhsIsMul){
                const Expr* negA = TryUnwrapSimdNegatedVecExpr(*mulA, simdType, m_symbols);
                const Expr* negB = TryUnwrapSimdNegatedVecExpr(*mulB, simdType, m_symbols);
                if(negA && negB){
                    mulA = negA;
                    mulB = negB;
                }
            }
            if(rhsIsMul){
                const Expr* negA = TryUnwrapSimdNegatedVecExpr(*rhsMulA, simdType, m_symbols);
                const Expr* negB = TryUnwrapSimdNegatedVecExpr(*rhsMulB, simdType, m_symbols);
                if(negA && negB){
                    rhsMulA = negA;
                    rhsMulB = negB;
                }
            }
            if(lhsIsMul){
                if(const Expr* negRhs = TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_symbols)){
                    emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(", *mulA, *mulB, *negRhs);
                    return true;
                }
            }
            if(rhsIsMul){
                if(const Expr* negLhs = TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_symbols)){
                    emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(", *rhsMulA, *rhsMulB, *negLhs);
                    return true;
                }
            }
            if(lhsIsMul && rhsIsMul){
                const b8 lhsNeedsRep = WouldSimdMulOperandRequireReplicateInFma(*mulA, simdType, m_symbols)
                    || WouldSimdMulOperandRequireReplicateInFma(*mulB, simdType, m_symbols);
                const b8 rhsNeedsRep = WouldSimdMulOperandRequireReplicateInFma(*rhsMulA, simdType, m_symbols)
                    || WouldSimdMulOperandRequireReplicateInFma(*rhsMulB, simdType, m_symbols);
                if(lhsNeedsRep && !rhsNeedsRep){
                    emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *rhsMulA, *rhsMulB, lhs);
                    return true;
                }
            }
            if(lhsIsMul){
                emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, rhs);
                return true;
            }
            if(rhsIsMul){
                emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *rhsMulA, *rhsMulB, lhs);
                return true;
            }
            if(const Expr* negRhs = TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_symbols)){
                emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", lhs, *negRhs);
                return true;
            }
            if(const Expr* negLhs = TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_symbols)){
                emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", rhs, *negLhs);
                return true;
            }
        }
        const MStringView addPrefix = isInt
            ? (isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(")
            : (isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(");
        emitBinaryRegCall(addPrefix, lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Minus){
        if(isInt){
            out.append(isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(");
            emitSimdRegExpr(out, lhs, simdType);
            out.append(", ");
            EmitSimdIntTwosComplementNegatePrefix(out, isDouble);
            emitSimdRegExpr(out, rhs, simdType);
            EmitSimdIntTwosComplementNegateSuffix(out, isDouble);
            out.push_back(')');
            return true;
        }
        if(TryMatchSimdNegatedMulExpr(&rhs, simdType, mulA, mulB, m_symbols)){
            emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, lhs);
            return true;
        }
        if(IsSimdMulExpr(&lhs, mulA, mulB, &m_symbols) && !TryMatchSimdNegatedMulExpr(&lhs, simdType, mulA, mulB, m_symbols)){
            const Expr* negA = TryUnwrapSimdNegatedVecExpr(*mulA, simdType, m_symbols);
            const Expr* negB = TryUnwrapSimdNegatedVecExpr(*mulB, simdType, m_symbols);
            if(negA && negB){
                mulA = negA;
                mulB = negB;
            }
            if(const Expr* negRhs = TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_symbols)){
                emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, *negRhs);
                return true;
            }
            emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(", *mulA, *mulB, rhs);
            return true;
        }
        if(IsSimdMulExpr(&rhs, mulA, mulB, &m_symbols)){
            const Expr* negA = TryUnwrapSimdNegatedVecExpr(*mulA, simdType, m_symbols);
            const Expr* negB = TryUnwrapSimdNegatedVecExpr(*mulB, simdType, m_symbols);
            if(negA && negB){
                mulA = negA;
                mulB = negB;
            }
            emitTernaryRegCall(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(", *mulA, *mulB, lhs);
            return true;
        }
        if(const Expr* negRhs = TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_symbols)){
            emitBinaryRegCall(isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(", lhs, *negRhs);
            return true;
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", lhs, rhs);
        return true;
    }
    if(isInt && IsSimdIntLaneFallbackBinaryOp(binaryOp)){
        const MStringView regType = SimdRegisterCppType(simdType);
        const MStringView laneType = IntSimdLaneType(simdType);
        const MStringView rawWordType = IntSimdRawWordType(isDouble);
        const MStringView opSym = SimdBinaryScalarOpSymbol(binaryOp);
        out.append("([&]()noexcept{ const ");
        out.append(regType.data(), regType.size());
        out.append(" __lhs = ");
        emitSimdRegExpr(out, lhs, simdType);
        out.append("; const ");
        out.append(regType.data(), regType.size());
        out.append(" __rhs = ");
        emitSimdRegExpr(out, rhs, simdType);
        out.append("; return ");
        out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
        for(u32 lane = 0u; lane < 4u; ++lane){
            if(lane > 0u)
                out.append(", ");
            out.append("static_cast<");
            out.append(rawWordType.data(), rawWordType.size());
            out.append(">(");
            AppendIntSimdLaneExtract(out, "__lhs", lane, isDouble, laneType);
            out.append(opSym.data(), opSym.size());
            AppendIntSimdLaneExtract(out, "__rhs", lane, isDouble, laneType);
            out.push_back(')');
        }
        out.append("); }())");
        return true;
    }
    if(binaryOp == TokenType::Star){
        if(!isInt){
            const Expr* negLhs = TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_symbols);
            const Expr* negRhs = TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_symbols);
            if(negLhs && negRhs)
                return tryEmitSimdBinaryOpReg(out, binaryOp, *negLhs, *negRhs, simdType);
            const Expr* rhsScalar = TryGetSimdScalarOperand(rhs, m_symbols);
            if((!lhs.resolvedSimdType.empty() || !lhs.resolvedUnalignedType.empty()) && rhsScalar){
                if(IsUnitFloatLiteral(*rhsScalar, &m_symbols)){
                    emitSimdRegExpr(out, lhs, simdType);
                    return true;
                }
                if(IsNegativeUnitFloatLiteral(*rhsScalar, &m_symbols)){
                    out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                    emitSimdRegExpr(out, lhs, simdType);
                    out.push_back(')');
                    return true;
                }
                if(!CanEmitScalarAsSimdVectorWithoutReplicate(*rhsScalar, simdType, m_symbols)){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    emitSimdRegExpr(out, lhs, simdType);
                    out.append(", ");
                    emitExpr(out, *rhsScalar);
                    out.push_back(')');
                    return true;
                }
            }
            const Expr* lhsScalar = TryGetSimdScalarOperand(lhs, m_symbols);
            if(lhsScalar && (!rhs.resolvedSimdType.empty() || !rhs.resolvedUnalignedType.empty())){
                if(IsUnitFloatLiteral(*lhsScalar, &m_symbols)){
                    emitSimdRegExpr(out, rhs, simdType);
                    return true;
                }
                if(IsNegativeUnitFloatLiteral(*lhsScalar, &m_symbols)){
                    out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                    emitSimdRegExpr(out, rhs, simdType);
                    out.push_back(')');
                    return true;
                }
                if(!CanEmitScalarAsSimdVectorWithoutReplicate(*lhsScalar, simdType, m_symbols)){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    emitSimdRegExpr(out, rhs, simdType);
                    out.append(", ");
                    emitExpr(out, *lhsScalar);
                    out.push_back(')');
                    return true;
                }
            }
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply(", lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Slash){
        if(!isInt){
            const Expr* negLhs = TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_symbols);
            const Expr* negRhs = TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_symbols);
            if(negLhs && negRhs)
                return tryEmitSimdBinaryOpReg(out, binaryOp, *negLhs, *negRhs, simdType);
        }
        const Expr* rhsScalar = TryGetSimdScalarOperand(rhs, m_symbols);
        if(!isInt && (!lhs.resolvedSimdType.empty() || !lhs.resolvedUnalignedType.empty()) && rhsScalar){
            if(IsUnitFloatLiteral(*rhsScalar, &m_symbols)){
                emitSimdRegExpr(out, lhs, simdType);
                return true;
            }
            if(IsNegativeUnitFloatLiteral(*rhsScalar, &m_symbols)){
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                emitSimdRegExpr(out, lhs, simdType);
                out.push_back(')');
                return true;
            }
        }
        const Expr* lhsScalar = TryGetSimdScalarOperand(lhs, m_symbols);
        if(!isInt && lhsScalar && IsUnitFloatLiteral(*lhsScalar, &m_symbols) && (!rhs.resolvedSimdType.empty() || !rhs.resolvedUnalignedType.empty())){
            if(const Expr* lenVec = TryMatchSimdLengthVecArg(&rhs, isDouble, m_symbols)){
                const u32 effLanes = InferEffectiveSimdLanes(*lenVec, &m_symbols);
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "ReciprocalLength");
                emitSimdRegExpr(out, *lenVec, lenVec->resolvedSimdType);
                out.push_back(')');
                return true;
            }
            if(
                rhs.kind == ExprKind::Call
                && rhs.isBuiltinCall
                && rhs.lhs
                && rhs.lhs->kind == ExprKind::Identifier
                && rhs.lhs->text == "sqrt"
                && rhs.arguments.size() == 1u
                && rhs.arguments[0]
                && rhs.arguments[0]->resolvedSimdType == simdType
            ){
                out.append(isDouble ? "::VectorDoubleReciprocalSqrt(" : "::VectorReciprocalSqrt(");
                emitSimdRegExpr(out, *rhs.arguments[0], simdType);
                out.push_back(')');
                return true;
            }
            out.append(isDouble ? "::VectorDoubleReciprocal(" : "::VectorReciprocal(");
            emitSimdRegExpr(out, rhs, simdType);
            out.push_back(')');
            return true;
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(", lhs, rhs);
        return true;
    }
    if(const MStringView bitwisePrefix = MapSimdBitwiseBinaryIntrinsic(binaryOp, isDouble); !bitwisePrefix.empty()){
        emitBinaryRegCall(bitwisePrefix, lhs, rhs);
        return true;
    }
    return false;
}

void SourceEmitter::emitSimdRegExpr(MString& out, const Expr& expr, const MStringView expectedSimdType)const{
    using namespace __hidden_adhd_simd_expr;

    const MStringView simdType = !expr.resolvedSimdType.empty() ? expr.resolvedSimdType : expectedSimdType;
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);
    const b8 isMatrix = SymbolTable::IsSimdMatrixType(simdType);

    if(expr.resolvedSimdType.empty() && !expectedSimdType.empty()){
        MStringView storageLoadFn;
        u32 storageLanes = 0u;
        if(TryGetStorageVectorLoadInfo(expr, isDouble, storageLoadFn, storageLanes)){
            out.append(storageLoadFn.data(), storageLoadFn.size());
            out.push_back('(');
            emitExpr(out, expr);
            out.push_back(')');
            return;
        }
        if(!isInt && !isMatrix){
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(TryMatchSingleLaneAccess(&expr, laneBase, laneIdx) && laneBase && laneBase->resolvedSimdType == expectedSimdType){
                const MStringView splatPrefix = SimdSplatIntrinsic(laneIdx, isDouble);
                out.append(splatPrefix.data(), splatPrefix.size());
                emitSimdRegExpr(out, *laneBase, expectedSimdType);
                out.push_back(')');
                return;
            }
            if(expr.kind == ExprKind::Call && expr.isBuiltinCall && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
                const MStringView callee = expr.lhs->text;
                const MStringView scalarReductionSuffix = MapSimdScalarReductionSuffix(callee);
                if(!scalarReductionSuffix.empty() && !expr.arguments.empty() && expr.arguments[0] && IsRegisterPromotableSimdType(expr.arguments[0]->resolvedSimdType)){
                    const MStringView argSimd = expr.arguments[0]->resolvedSimdType;
                    if(IsDoubleSimdType(argSimd) == isDouble){
                        u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0], &m_symbols);
                        if(expr.arguments.size() == 2u && expr.arguments[1]){
                            const u32 effLanes1 = InferEffectiveSimdLanes(*expr.arguments[1], &m_symbols);
                            if(effLanes1 < effLanes)
                                effLanes = effLanes1;
                        }
                        if(callee == "dot" && effLanes == 4u && !IsIntSimdType(argSimd) && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                            const Expr* sumArg = IsSimdUnitFloatVectorExpr(*expr.arguments[1], isDouble, &m_symbols)
                                ? expr.arguments[0]
                                : (IsSimdUnitFloatVectorExpr(*expr.arguments[0], isDouble, &m_symbols) ? expr.arguments[1] : nullptr);
                            if(sumArg){
                                out.append(isDouble ? "::VectorDoubleSum(" : "::VectorSum(");
                                emitSimdRegExpr(out, *sumArg, argSimd);
                                out.push_back(')');
                                return;
                            }
                        }
                        AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, scalarReductionSuffix);
                        for(usize i = 0u; i < expr.arguments.size(); ++i){
                            if(i > 0u)
                                out.append(", ");
                            if(expr.arguments[i])
                                emitSimdRegExpr(out, *expr.arguments[i], argSimd);
                        }
                        out.push_back(')');
                        return;
                    }
                }
                if(callee == "rsqrt" && expr.arguments.size() == 1u){
                    if(const Expr* lenSqVec = TryMatchSimdLengthSqVecArg(expr.arguments[0], isDouble, m_symbols)){
                        const u32 effLanes = InferEffectiveSimdLanes(*lenSqVec, &m_symbols);
                        AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "ReciprocalLength");
                        emitSimdRegExpr(out, *lenSqVec, lenSqVec->resolvedSimdType);
                        out.push_back(')');
                        return;
                    }
                }
            }
            if(expr.kind == ExprKind::Binary && expr.op == TokenType::Slash && expr.lhs && expr.rhs && IsUnitFloatLiteral(*expr.lhs, &m_symbols)){
                if(const Expr* lenVec = TryMatchSimdLengthVecArg(expr.rhs, isDouble, m_symbols)){
                    const u32 effLanes = InferEffectiveSimdLanes(*lenVec, &m_symbols);
                    AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "ReciprocalLength");
                    emitSimdRegExpr(out, *lenVec, lenVec->resolvedSimdType);
                    out.push_back(')');
                    return;
                }
            }
            if(IsZeroScalarLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
                return;
            }
            if(IsUnitFloatLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne");
                return;
            }
            if(IsNegativeUnitFloatLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne");
                return;
            }
            if(IsHalfFloatLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::s_DoubleSIMDOneHalf" : "::s_SIMDOneHalf");
                return;
            }
        }
        if(isInt){
            if(IsZeroScalarLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
                return;
            }
            out.append(isDouble ? "::VectorDoubleReplicateInt(static_cast<u64>(" : "::VectorReplicateInt(static_cast<u32>(");
            emitExpr(out, expr);
            out.append("))");
        }
        else{
            out.append(isDouble ? "::VectorDoubleReplicate(" : "::VectorReplicate(");
            emitExpr(out, expr);
            out.push_back(')');
        }
        return;
    }

    switch(expr.kind){
    case ExprKind::MemberAccess:{
        if(expr.isSwizzle && expr.lhs){
            const u32 len = static_cast<u32>(expr.text.size());
            Array<u32, 4> lanes{
                len > 0u ? MapSwizzleCharToLane(expr.text[0]) : 0u,
                len > 1u ? MapSwizzleCharToLane(expr.text[1]) : 1u,
                len > 2u ? MapSwizzleCharToLane(expr.text[2]) : 2u,
                len > 3u ? MapSwizzleCharToLane(expr.text[3]) : 3u,
            };
            const Expr* curBase = expr.lhs;
            while(curBase && curBase->kind == ExprKind::MemberAccess && curBase->isSwizzle && curBase->lhs && curBase->lhs->resolvedSimdType == simdType){
                const u32 parentLen = static_cast<u32>(curBase->text.size());
                const Array<u32, 4> parentLanes{
                    parentLen > 0u ? MapSwizzleCharToLane(curBase->text[0]) : 0u,
                    parentLen > 1u ? MapSwizzleCharToLane(curBase->text[1]) : 1u,
                    parentLen > 2u ? MapSwizzleCharToLane(curBase->text[2]) : 2u,
                    parentLen > 3u ? MapSwizzleCharToLane(curBase->text[3]) : 3u,
                };
                for(u32 i = 0u; i < 4u; ++i)
                    lanes[i] = parentLanes[lanes[i] < 4u ? lanes[i] : 3u];
                curBase = curBase->lhs;
            }
            if(lanes[0] == 0u && lanes[1] == 1u && lanes[2] == 2u && lanes[3] == 3u){
                emitSimdRegExpr(out, *curBase, simdType);
                return;
            }
            if(!isInt && lanes[0] == lanes[1] && lanes[1] == lanes[2] && lanes[2] == lanes[3]){
                const MStringView splatPrefix = SimdSplatIntrinsic(lanes[0], isDouble);
                out.append(splatPrefix.data(), splatPrefix.size());
                emitSimdRegExpr(out, *curBase, curBase->resolvedSimdType);
                out.push_back(')');
                return;
            }
            out.append(isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
            AppendUnsignedDecimal(out, lanes[0]);
            out.append("u, ");
            AppendUnsignedDecimal(out, lanes[1]);
            out.append("u, ");
            AppendUnsignedDecimal(out, lanes[2]);
            out.append("u, ");
            AppendUnsignedDecimal(out, lanes[3]);
            out.append("u>(");
            emitSimdRegExpr(out, *curBase, curBase->resolvedSimdType);
            out.push_back(')');
            return;
        }
        break;
    }
    case ExprKind::Unary:{
        if(expr.lhs){
            if(expr.op == TokenType::Minus){
                if(isMatrix){
                    const MStringView matRegType = SimdRegisterCppType(simdType);
                    out.append("([&]()noexcept{ const ");
                    out.append(matRegType.data(), matRegType.size());
                    out.append(" __m = ");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    EmitSimdMatrixUnaryRowInitSuffix(out, matRegType, isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(", ")");
                    return;
                }
                if(isInt){
                    EmitSimdIntTwosComplementNegatePrefix(out, isDouble);
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    EmitSimdIntTwosComplementNegateSuffix(out, isDouble);
                    return;
                }
                if(const Expr* innerVec = TryUnwrapSimdNegatedVecExpr(*expr.lhs, simdType, m_symbols)){
                    emitSimdRegExpr(out, *innerVec, simdType);
                    return;
                }
                if(const Expr* splatScalar = TryUnwrapSimdSplatScalarArg(*expr.lhs, m_symbols)){
                    if(IsZeroScalarLiteral(*splatScalar, &m_symbols)){
                        out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
                        return;
                    }
                    if(IsUnitFloatLiteral(*splatScalar, &m_symbols)){
                        out.append(isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne");
                        return;
                    }
                    if(IsNegativeUnitFloatLiteral(*splatScalar, &m_symbols)){
                        out.append(isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne");
                        return;
                    }
                    if(!CanEmitScalarAsSimdVectorWithoutReplicate(*splatScalar, simdType, m_symbols)){
                        out.append(isDouble ? "::VectorDoubleReplicate(-(" : "::VectorReplicate(-(");
                        emitExpr(out, *splatScalar);
                        out.append("))");
                        return;
                    }
                }
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Tilde){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(isDouble ? ", ::VectorDoubleTrueInt())" : ", ::VectorTrueInt())");
                return;
            }
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.lhs && expr.rhs && tryEmitSimdBinaryOpReg(out, expr.op, *expr.lhs, *expr.rhs, simdType))
            return;
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr.lhs->text;
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(callee);
            u32 ctorLanes = 0u;
            if(SymbolTable::IsSimdVectorType(baseCallee, ctorLanes) || SymbolTable::IsSimdMatrixType(baseCallee)){
                emitSimdVectorCtorReg(out, simdType, expr.arguments);
                return;
            }
            if(!expr.isBuiltinCall)
                break;

            b8 isIntFallbackCallee = false;
#define NWB_ADHD_EXPORT_SIMD_INT_FALLBACK(adhdName) \
            if(callee == adhdName) isIntFallbackCallee = true;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_INT_FALLBACK
            if(isInt && isIntFallbackCallee){
                const MStringView regType = SimdRegisterCppType(simdType);
                const MStringView laneType = IntSimdLaneType(simdType);
                const MStringView rawWordType = IntSimdRawWordType(isDouble);
                const MStringView scalarFn = MapBuiltinMathFunction(callee);
                out.append("([&]()noexcept{ ");
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    out.append("const ");
                    out.append(regType.data(), regType.size());
                    out.append(" __a");
                    AppendUnsignedDecimal(out, i);
                    out.append(" = ");
                    if(expr.arguments[i])
                        emitSimdRegExpr(out, *expr.arguments[i], simdType);
                    out.append("; ");
                }
                out.append("return ");
                out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
                for(u32 lane = 0u; lane < 4u; ++lane){
                    if(lane > 0u)
                        out.append(", ");
                    out.append("static_cast<");
                    out.append(rawWordType.data(), rawWordType.size());
                    out.append(">(");
                    out.append(scalarFn.data(), scalarFn.size());
                    out.push_back('(');
                    for(usize i = 0u; i < expr.arguments.size(); ++i){
                        if(i > 0u)
                            out.append(", ");
                        MString argVarName(m_arena);
                        argVarName.append("__a");
                        AppendUnsignedDecimal(argVarName, i);
                        AppendIntSimdLaneExtract(out, MStringView(argVarName.data(), argVarName.size()), lane, isDouble, laneType);
                    }
                    out.append("))");
                }
                out.append("); }())");
                return;
            }

            if(!isInt && callee == "rsqrt" && expr.arguments.size() == 1u){
                if(const Expr* lenSqVec = TryMatchSimdLengthSqVecArg(expr.arguments[0], isDouble, m_symbols)){
                    const u32 effLanes = InferEffectiveSimdLanes(*lenSqVec, &m_symbols);
                    AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "ReciprocalLength");
                    emitSimdRegExpr(out, *lenSqVec, lenSqVec->resolvedSimdType);
                    out.push_back(')');
                    return;
                }
            }

            auto emitSimdCallRegArgsAndClose = [&](const Span<Expr* const> callArgs){
                for(usize i = 0u; i < callArgs.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(callArgs[i])
                        emitSimdRegExpr(out, *callArgs[i], simdType);
                }
                out.push_back(')');
            };

            const MStringView registerCall = MapSimdRegisterCall(callee);
            if(!registerCall.empty()){
                out.append(registerCall.data(), registerCall.size());
                out.push_back('(');
                emitSimdCallRegArgsAndClose(expr.arguments);
                return;
            }
            if(!isInt && !isMatrix){
                if(const Expr* satArg = TryMatchSimdSaturateCallArg(expr, simdType, isDouble, m_symbols)){
                    out.append(isDouble ? "::VectorDoubleSaturate(" : "::VectorSaturate(");
                    emitSimdRegExpr(out, *satArg, simdType);
                    out.push_back(')');
                    return;
                }
            }
            const MStringView unaryIntrinsic = MapSimdUnaryFloatIntrinsic(callee, isDouble);
            if(!unaryIntrinsic.empty()){
                out.append(unaryIntrinsic.data(), unaryIntrinsic.size());
                emitSimdCallRegArgsAndClose(expr.arguments);
                return;
            }
            if(callee == "lerp" && expr.arguments.size() == 3u && expr.arguments[0] && expr.arguments[1] && expr.arguments[2]){
                const Expr* scalarT = expr.arguments[2]->resolvedSimdType.empty()
                    ? expr.arguments[2]
                    : TryUnwrapSimdSplatScalarArg(*expr.arguments[2], m_symbols);
                const b8 vectorT = !scalarT || CanEmitScalarAsSimdVectorWithoutReplicate(*scalarT, simdType, m_symbols);
                if(vectorT)
                    out.append(isDouble ? "::VectorDoubleLerpV(" : "::VectorLerpV(");
                else
                    out.append(isDouble ? "::VectorDoubleLerp(" : "::VectorLerp(");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.arguments[1], simdType);
                out.append(", ");
                if(vectorT)
                    emitSimdRegExpr(out, *expr.arguments[2], simdType);
                else
                    emitExpr(out, *scalarT);
                out.push_back(')');
                return;
            }
            MStringView laneVectorSuffix;
            u32 maxVectorLanes = 4u;
#define NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC(adhdName, laneIntrinsicSuffix, maxLanes) \
            if(callee == adhdName){ laneVectorSuffix = laneIntrinsicSuffix; maxVectorLanes = maxLanes; }
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC
            if(!laneVectorSuffix.empty() && !expr.arguments.empty() && expr.arguments[0]){
                const u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0], &m_symbols);
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes > maxVectorLanes ? maxVectorLanes : effLanes, laneVectorSuffix);
                emitSimdCallRegArgsAndClose(expr.arguments);
                return;
            }
            if(callee == "mul" && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const MStringView lhsSimd = expr.arguments[0]->resolvedSimdType;
                const MStringView rhsSimd = expr.arguments[1]->resolvedSimdType;
                const b8 lhsIsMat = SymbolTable::IsSimdMatrixType(lhsSimd);
                const b8 rhsIsMat = SymbolTable::IsSimdMatrixType(rhsSimd);
                if(lhsIsMat && rhsIsMat){
                    out.append(isDouble ? "::MatrixDoubleMultiply(" : "::MatrixMultiply(");
                    emitSimdRegExpr(out, *expr.arguments[0], lhsSimd);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.arguments[1], rhsSimd);
                    out.push_back(')');
                    return;
                }
                const Expr& vecExpr = lhsIsMat ? *expr.arguments[1] : *expr.arguments[0];
                const Expr& matExpr = lhsIsMat ? *expr.arguments[0] : *expr.arguments[1];
                const Expr* transformBaseVec = nullptr;
                MStringView transformRegType;
                MStringView transformLoadFn;
                u32 transformLanes = 3u;
                b8 isNormalTransform = false;
                if(!isInt && TryMatchSimdTransform3Arg(vecExpr, vecExpr.resolvedSimdType, m_symbols, transformBaseVec, transformRegType, transformLoadFn, transformLanes, isNormalTransform) && transformBaseVec){
                    if(transformLanes == 2u)
                        out.append(isDouble ? "::VectorDouble2Transform(" : "::Vector2Transform(");
                    else if(isNormalTransform)
                        out.append(isDouble ? "::VectorDouble3TransformNormal(" : "::Vector3TransformNormal(");
                    else
                        out.append(isDouble ? "::VectorDouble3Transform(" : "::Vector3Transform(");
                    if(!transformRegType.empty()){
                        emitSimdRegExpr(out, *transformBaseVec, transformRegType);
                    }
                    else{
                        out.append(transformLoadFn.data(), transformLoadFn.size());
                        out.push_back('(');
                        emitExpr(out, *transformBaseVec);
                        out.push_back(')');
                    }
                    out.append(", ");
                    emitSimdRegExpr(out, matExpr, matExpr.resolvedSimdType);
                    out.push_back(')');
                    return;
                }
                const u32 effLanes = InferEffectiveSimdLanes(vecExpr);
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "Transform");
                emitSimdRegExpr(out, vecExpr, vecExpr.resolvedSimdType);
                out.append(", ");
                emitSimdRegExpr(out, matExpr, matExpr.resolvedSimdType);
                out.push_back(')');
                return;
            }
            if(callee == "transpose" && !expr.arguments.empty() && expr.arguments[0]){
                const Expr& arg0 = *expr.arguments[0];
                if(
                    arg0.kind == ExprKind::Call && arg0.lhs && arg0.lhs->kind == ExprKind::Identifier
                    && arg0.isBuiltinCall && arg0.lhs->text == "mul" && arg0.arguments.size() == 2u && arg0.arguments[0] && arg0.arguments[1]
                    && SymbolTable::IsSimdMatrixType(arg0.arguments[0]->resolvedSimdType)
                    && SymbolTable::IsSimdMatrixType(arg0.arguments[1]->resolvedSimdType)
                ){
                    out.append(isDouble ? "::MatrixDoubleMultiplyTranspose(" : "::MatrixMultiplyTranspose(");
                    emitSimdRegExpr(out, *arg0.arguments[0], arg0.arguments[0]->resolvedSimdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *arg0.arguments[1], arg0.arguments[1]->resolvedSimdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::MatrixDoubleTranspose(" : "::MatrixTranspose(");
                emitSimdRegExpr(out, arg0, simdType);
                out.push_back(')');
                return;
            }
        }
        break;
    }
    default:
        break;
    }

    if(SymbolTable::IsSimdRegisterType(expr.resolvedSimdType)){
        emitExpr(out, expr);
        return;
    }

    const MStringView loadFn = SimdLoadFunctionCpp(simdType);
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    emitExpr(out, expr);
    out.push_back(')');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
