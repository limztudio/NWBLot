// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include <core/metascript/adhd/ast/builtin_type_semantics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SourceEmitter::tryEmitScalarFloatCall(MString& out, const Expr& expr)const{
    if(!expr.isBuiltinCall || !expr.lhs || expr.lhs->kind != ExprKind::Identifier)
        return false;
    const MStringView name = expr.lhs->text;
    const b8 lerp = name == "Lerp";
    const b8 reciprocalRoot = name == "Rsqrt";
    if(!lerp && !reciprocalRoot)
        return false;
    const usize count = lerp ? 3u : 1u;
    if(expr.arguments.size() != count)
        return false;
    for(const Expr* argument : expr.arguments){
        if(!argument || !BuiltinTypeSemantics::IsScalarType(argument->resolvedScalarType))
            return false;
        if(!lerp && expr.callTemplateArgs.empty() && argument->resolvedScalarType != expr.resolvedScalarType)
            return false;
    }
    const b8 isDouble = expr.resolvedScalarType == "f64";
    const MStringView prefix = isDouble ? "::VectorDouble" : "::Vector";
    const MStringView scalar = isDouble ? "::f64" : "::f32";
    const auto append = [&](const MStringView text){ out.append(text.data(), text.size()); };
    const auto replicate = [&](const MStringView value){
        append(prefix);
        out.append("Replicate(static_cast<");
        append(scalar);
        out.append(">(");
        append(value);
        out.append("))");
    };
    if(lerp){
        const MStringView first = expr.arguments[0]->resolvedScalarType;
        const MStringView second = expr.arguments[1]->resolvedScalarType;
        const b8 doubleDifference = first == "f64" || second == "f64";
        const b8 floatDifference = doubleDifference || first == "f32" || second == "f32";
        const MStringView differencePrefix = doubleDifference ? "::VectorDouble" : "::Vector";
        const MStringView differenceScalar = doubleDifference ? "::f64" : "::f32";
        out.append("([](const auto __a, const auto __b, const auto __t)noexcept{ const auto __difference = ");
        if(floatDifference){
            append(differencePrefix);
            out.append("Subtract(");
            for(const MStringView value : { MStringView("__b"), MStringView("__a") }){
                if(value == "__a")
                    out.append(", ");
                append(differencePrefix);
                out.append("Replicate(static_cast<");
                append(differenceScalar);
                out.append(">(");
                append(value);
                out.append("))");
            }
            out.push_back(')');
        }
        else
            out.append("(__b - __a)");
        out.append("; return ");
        append(prefix);
        out.append("GetX(");
        append(prefix);
        out.append("Add(");
        replicate("__a");
        out.append(", ");
        append(prefix);
        out.append("Multiply(");
        if(floatDifference && doubleDifference == isDouble)
            out.append("__difference");
        else{
            append(prefix);
            out.append("Replicate(static_cast<");
            append(scalar);
            out.append(">(");
            if(floatDifference){
                append(differencePrefix);
                out.append("GetX(__difference)");
            }
            else
                out.append("__difference");
            out.append("))");
        }
        out.append(", ");
        replicate("__t");
        out.append("))); }(");
    }
    else{
        out.append("([](const ");
        append(scalar);
        out.append(" __a)noexcept{ return ");
        append(prefix);
        out.append("GetX(");
        append(prefix);
        out.append("Divide(");
        replicate("1");
        out.append(", ");
        replicate("::Sqrt(__a)");
        out.append(")); }(");
    }
    for(usize i = 0u; i < count; ++i){
        if(i)
            out.append(", ");
        emitExpr(out, *expr.arguments[i]);
    }
    out.append("))");
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

