// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include <core/metascript/adhd/ast/builtin_type_semantics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SourceEmitter::tryEmitScalarFloatExpr(MString& out, const Expr& expr)const{
    if(!m_emittingScalarSimd || (expr.resolvedScalarType != "f32" && expr.resolvedScalarType != "f64") || !expr.lhs || !expr.resolvedSimdType.empty() || !expr.resolvedUnalignedType.empty())
        return false;
    if(expr.kind == ExprKind::Call)
        return tryEmitScalarFloatCall(out, expr);
    const b8 unary = expr.kind == ExprKind::Unary;
    const b8 increment = unary && (expr.op == TokenType::PlusPlus || expr.op == TokenType::MinusMinus);
    const b8 assignment = expr.kind == ExprKind::Binary && (expr.op == TokenType::PlusEqual || expr.op == TokenType::MinusEqual || expr.op == TokenType::StarEqual || expr.op == TokenType::SlashEqual);
    MStringView operation;
    if(unary && expr.op == TokenType::Minus)
        operation = "Negate";
    else if((expr.kind == ExprKind::Binary && (expr.op == TokenType::Plus || expr.op == TokenType::PlusEqual)) || (increment && expr.op == TokenType::PlusPlus))
        operation = "Add";
    else if((expr.kind == ExprKind::Binary && (expr.op == TokenType::Minus || expr.op == TokenType::MinusEqual)) || (increment && expr.op == TokenType::MinusMinus))
        operation = "Subtract";
    else if(expr.kind == ExprKind::Binary && (expr.op == TokenType::Star || expr.op == TokenType::StarEqual))
        operation = "Multiply";
    else if(expr.kind == ExprKind::Binary && (expr.op == TokenType::Slash || expr.op == TokenType::SlashEqual))
        operation = "Divide";
    else
        return false;
    if(!BuiltinTypeSemantics::IsScalarType(expr.lhs->resolvedScalarType) || (!unary && (!expr.rhs || !BuiltinTypeSemantics::IsScalarType(expr.rhs->resolvedScalarType))))
        return false;
    const b8 isDouble = expr.lhs->resolvedScalarType == "f64" || (!unary && expr.rhs->resolvedScalarType == "f64");
    const MStringView prefix = isDouble ? "::VectorDouble" : "::Vector";
    const MStringView scalarType = isDouble ? "::f64" : "::f32";
    if(assignment){
        // Evaluate the right side before asking for the target, outside generated names.
        out.append("([](auto __target, const auto __rhs)noexcept -> decltype(auto){ auto& __lhs = __target(); __lhs = ");
    }
    else if(increment){
        out.append("([](auto& __lhs)noexcept -> decltype(auto){ ");
        if(expr.isPostfix)
            out.append("const auto __old = __lhs; ");
        out.append("__lhs = ");
    }
    out.append(prefix.data(), prefix.size());
    out.append("GetX(");
    out.append(prefix.data(), prefix.size());
    out.append(operation.data(), operation.size());
    out.push_back('(');
    const auto emitOperand = [&](const Expr* value, const MStringView replacement){
        out.append(prefix.data(), prefix.size());
        out.append("Replicate(static_cast<");
        out.append(scalarType.data(), scalarType.size());
        out.append(">(");
        if(replacement.empty())
            emitExpr(out, *value);
        else
            out.append(replacement.data(), replacement.size());
        out.append("))");
    };
    emitOperand(expr.lhs, assignment || increment ? MStringView("__lhs") : MStringView{});
    if(operation != "Negate"){
        out.append(", ");
        emitOperand(expr.rhs, assignment ? MStringView("__rhs") : (increment ? MStringView("1") : MStringView{}));
    }
    out.append("))");
    if(assignment){
        out.append("; return (__lhs); }([&]()noexcept -> decltype(auto){ return (");
        emitExpr(out, *expr.lhs);
        out.append("); }, ");
        emitExpr(out, *expr.rhs);
        out.append("))");
    }
    else if(increment){
        out.append(expr.isPostfix ? "; return __old; }(" : "; return (__lhs); }(");
        emitExpr(out, *expr.lhs);
        out.append("))");
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

