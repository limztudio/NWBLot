// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitExpr(MString& out, const Expr& expr)const{
    if(expr.adaptToTChar && expr.kind != ExprKind::StringLiteral){
        out.append("::ConvertString(");
        Expr unwrapped = expr;
        unwrapped.adaptToTChar = false;
        emitExpr(out, unwrapped);
        out.push_back(')');
        return;
    }

    if(expr.moveAtLastUse){
        out.append("::Move(");
        out.append(expr.text.data(), expr.text.size());
        out.push_back(')');
        return;
    }

    if(IsRegisterPromotableSimdType(expr.resolvedSimdType)){
        const b8 isDirectSimdRegOp = (expr.kind == ExprKind::MemberAccess && expr.isSwizzle)
            || expr.kind == ExprKind::Unary
            || (expr.kind == ExprKind::Binary && expr.op != TokenType::Equal)
            || (expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier
                && !MapBuiltinMathFunction(expr.lhs->text).empty());
        if(isDirectSimdRegOp){
            const MStringView simdType = expr.resolvedSimdType;
            const MStringView storeFn = SimdStoreFunctionCpp(simdType);
            TypeRef tmpType(m_arena);
            tmpType.name = simdType;

            out.append("([&]()noexcept{ ");
            m_typeEmitter.emitTypeCpp(out, tmpType);
            out.append(" __adhd_tmp{}; ");
            out.append(storeFn.data(), storeFn.size());
            out.push_back('(');
            emitSimdRegExpr(out, expr, simdType);
            out.append(", __adhd_tmp); return __adhd_tmp; }())");
            return;
        }
    }

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:{
        if(expr.derefNotNull){
            out.append("(*");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else if(expr.text == "s_InvalidEntityId"){
            out.append("::NWB::Core::ECS::s_InvalidEntityId");
        }
        else{
            out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::StringLiteral:{
        if(expr.adaptToTChar){
            out.append("NWB_TEXT(\"");
            out.append(expr.text.data(), expr.text.size());
            out.append("\")");
        }
        else{
            out.push_back('"');
            out.append(expr.text.data(), expr.text.size());
            out.push_back('"');
        }
        break;
    }
    case ExprKind::ThisMember:{
        if(expr.derefNotNull){
            out.append("(*this->");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            out.append("this->");
            out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::MemberAccess:{
        u32 lane = 0u;
        if(!expr.isMutArg && expr.lhs && IsRegisterPromotableSimdType(expr.lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr.text, lane))
        {
            if(findSimdBindingForExpr(expr.lhs) || ExprContainsSimdMath(expr.lhs, m_symbols)){
                const MStringView lhsSimd = expr.lhs->resolvedSimdType;
                const b8 isDouble = IsDoubleSimdType(lhsSimd);
                const b8 isInt = IsIntSimdType(lhsSimd);
                const b8 isSignedInt = IsSignedIntSimdType(lhsSimd);
                static constexpr const char* s_LaneSuffix[4] = { "X(", "Y(", "Z(", "W(" };

                if(isSignedInt)
                    out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
                else
                    out.append(isDouble ? "::VectorDoubleGet" : "::VectorGet");
                out.append(s_LaneSuffix[lane]);
                emitSimdRegExpr(out, *expr.lhs, lhsSimd);
                out.push_back(')');
                if(isSignedInt)
                    out.push_back(')');
                break;
            }
        }
        if(expr.derefNotNull)
            out.append("(*(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isPointerMemberAccess)
            out.append("->");
        else
            out.push_back('.');
        out.append(expr.text.data(), expr.text.size());
        if(expr.derefNotNull)
            out.append("))");
        break;
    }
    case ExprKind::ScopeAccess:{
        for(usize i = 0u; i < expr.scopeChain.size(); ++i){
            if(i > 0u)
                out.append("::");
            out.append(expr.scopeChain[i].data(), expr.scopeChain[i].size());
        }
        break;
    }
    case ExprKind::Index:{
        u32 vecLanes = 0u;
        if(!expr.isMutArg && expr.lhs && expr.rhs
            && IsRegisterPromotableSimdType(expr.lhs->resolvedSimdType)
            && m_symbols.isSimdVectorType(expr.lhs->resolvedSimdType, vecLanes))
        {
            if(findSimdBindingForExpr(expr.lhs) || ExprContainsSimdMath(expr.lhs, m_symbols)){
                const MStringView lhsSimd = expr.lhs->resolvedSimdType;
                const b8 isDouble = IsDoubleSimdType(lhsSimd);
                const b8 isInt = IsIntSimdType(lhsSimd);
                const b8 isSignedInt = IsSignedIntSimdType(lhsSimd);

                if(isSignedInt)
                    out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleGetIntByIndex(" : "::VectorGetIntByIndex(");
                else
                    out.append(isDouble ? "::VectorDoubleGetByIndex(" : "::VectorGetByIndex(");
                emitSimdRegExpr(out, *expr.lhs, lhsSimd);
                out.append(", static_cast<usize>(");
                emitExpr(out, *expr.rhs);
                out.append("))");
                if(isSignedInt)
                    out.push_back(')');
                break;
            }
        }
        if(expr.derefNotNull)
            out.append("(*(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isVectorRawIndex)
            out.append(".raw[");
        else
            out.push_back('[');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(']');
        if(expr.derefNotNull)
            out.append("))");
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr.lhs->text;
            if((callee == "dot" || callee == "length" || callee == "lengthSq")
                && !expr.arguments.empty() && expr.arguments[0]
                && IsRegisterPromotableSimdType(expr.arguments[0]->resolvedSimdType))
            {
                const MStringView argSimd = expr.arguments[0]->resolvedSimdType;
                const b8 isDouble = IsDoubleSimdType(argSimd);
                const u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0]);
                out.append(isDouble ? "::VectorDoubleGetX(" : "::VectorGetX(");
                if(callee == "dot"){
                    AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "Dot");
                    emitSimdRegExpr(out, *expr.arguments[0], argSimd);
                    out.append(", ");
                    if(expr.arguments.size() > 1u && expr.arguments[1])
                        emitSimdRegExpr(out, *expr.arguments[1], argSimd);
                    out.append("))");
                }
                else{
                    AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, callee == "length" ? "Length" : "LengthSq");
                    emitSimdRegExpr(out, *expr.arguments[0], argSimd);
                    out.append("))");
                }
                break;
            }
            if(callee == "lerp" && expr.arguments.size() == 3u
                && expr.arguments[0] && expr.arguments[1] && expr.arguments[2]
                && expr.resolvedSimdType.empty())
            {
                out.append("((");
                emitExpr(out, *expr.arguments[0]);
                out.append(") + ((");
                emitExpr(out, *expr.arguments[1]);
                out.append(") - (");
                emitExpr(out, *expr.arguments[0]);
                out.append(")) * (");
                emitExpr(out, *expr.arguments[2]);
                out.append("))");
                break;
            }
            if(callee == "rsqrt" && expr.arguments.size() == 1u
                && expr.arguments[0] && expr.resolvedSimdType.empty())
            {
                out.append("(static_cast<decltype(");
                emitExpr(out, *expr.arguments[0]);
                out.append(")>(1) / ::Sqrt(");
                emitExpr(out, *expr.arguments[0]);
                out.append("))");
                break;
            }
        }

        if(expr.derefNotNull)
            out.append("(*");
        if(expr.lhs){
            if(expr.lhs->kind == ExprKind::Identifier){
                const MStringView mappedMath = MapBuiltinMathFunction(expr.lhs->text);
                if(!mappedMath.empty()){
                    out.append(mappedMath.data(), mappedMath.size());
                }
                else{
                    TypeRef maybeType(m_arena);
                    maybeType.name = expr.lhs->text;
                    m_typeEmitter.emitTypeCpp(out, maybeType);
                }
            }
            else{
                emitExpr(out, *expr.lhs);
            }
        }
        if(!expr.callTemplateArgs.empty())
            m_typeEmitter.emitTypeArgListCpp(out, expr.callTemplateArgs);
        const MStringView baseCallee = (expr.lhs && expr.lhs->kind == ExprKind::Identifier)
            ? m_symbols.resolveBaseTypeName(expr.lhs->text)
            : MStringView{};
        if(!baseCallee.empty() && m_symbols.isSimdMatrixType(baseCallee)){
            if(expr.arguments.empty()){
                out.append("{}");
            }
            else{
                out.append("{ .raw = { ");
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(expr.arguments[i])
                        emitExpr(out, *expr.arguments[i]);
                }
                out.append(" } }");
            }
            if(expr.derefNotNull)
                out.push_back(')');
            break;
        }
        out.push_back('(');
        u32 ctorVecLanes = 0u;
        const b8 isScalarSplatCtor = !baseCallee.empty()
            && (m_symbols.isSimdVectorType(baseCallee, ctorVecLanes) || m_symbols.isUnalignedVectorType(baseCallee, ctorVecLanes))
            && expr.arguments.size() == 1u && expr.arguments[0]
            && expr.arguments[0]->resolvedSimdType.empty();
        if(isScalarSplatCtor){
            for(u32 lane = 0u; lane < ctorVecLanes; ++lane){
                if(lane > 0u)
                    out.append(", ");
                emitExpr(out, *expr.arguments[0]);
            }
        }
        else{
            for(usize i = 0u; i < expr.arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(expr.arguments[i])
                    emitExpr(out, *expr.arguments[i]);
            }
            if(expr.injectClassArena){
                if(!expr.arguments.empty())
                    out.append(", ");
                out.append("s_ClassArena");
            }
            else if(expr.injectScratchArena){
                if(!expr.arguments.empty())
                    out.append(", ");
                out.append("__adhd_scratch");
            }
        }
        out.push_back(')');
        if(expr.derefNotNull)
            out.push_back(')');
        break;
    }
    case ExprKind::Unary:{
        if(expr.isPostfix){
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(expr.text.data(), expr.text.size());
        }
        else{
            out.append(expr.text.data(), expr.text.size());
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.isNotNullRebind){
            out.push_back('(');
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(" = ::MakeNotNull(&(");
            if(expr.rhs)
                emitExpr(out, *expr.rhs);
            out.append(")))");
            break;
        }
        out.push_back('(');
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(' ');
        out.append(expr.text.data(), expr.text.size());
        out.push_back(' ');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Cast:{
        out.append("static_cast<");
        if(expr.targetType)
            m_typeEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::BitCast:{
        out.append("::BitCast<");
        if(expr.targetType)
            m_typeEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Lambda:{
        out.push_back('[');
        for(usize i = 0u; i < expr.lambdaCaptures.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const LambdaCapture& cap = expr.lambdaCaptures[i];
            if(cap.name == "this"){
                out.append("this");
            }
            else if(cap.mode == ParamPassingMode::Mut){
                out.push_back('&');
                out.append(cap.name.data(), cap.name.size());
            }
            else{
                out.append(cap.name.data(), cap.name.size());
            }
        }
        out.append("](");
        for(usize i = 0u; i < expr.lambdaParams.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const ParamDecl& p = expr.lambdaParams[i];
            if(p.type)
                m_typeEmitter.emitParamTypeCpp(out, p.mode, *p.type);
            out.push_back(' ');
            out.append(p.name.data(), p.name.size());
        }
        out.append(")noexcept -> ");
        if(expr.lambdaReturnType)
            m_typeEmitter.emitTypeCpp(out, *expr.lambdaReturnType);
        else
            out.append("void");
        out.append("{\r\n");
        if(expr.lambdaUsesScratchArena){
            emitIndent(out, 2u);
            out.append("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"lambda\"));\r\n");
        }
        if(expr.lambdaUsesTaskScope)
            emitTaskScopePrologue(out, 2u);
        emitScopedSimdBody(out, expr.lambdaBody, 2u);
        out.append("    }");
        break;
    }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

