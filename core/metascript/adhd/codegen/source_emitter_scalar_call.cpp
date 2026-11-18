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
    const SpecialFunctionKind::Enum specialFn = FindSpecialFunctionKind(expr.lhs->text);
    const b8 lerp = specialFn == SpecialFunctionKind::Lerp;
    const b8 reciprocalRoot = specialFn == SpecialFunctionKind::Rsqrt;
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
    const b8 isDouble = expr.resolvedScalarType == BuiltinTypeSemantics::Float64ScalarTypeName();
    const MStringView prefix = CodegenSymbols::SimdPrefix(isDouble);
    const MStringView scalar = CodegenSymbols::SimdScalarCppType(isDouble);
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
        const MStringView promotedDiff = BuiltinTypeSemantics::PromoteFloatScalarTypes(first, second);
        const b8 doubleDifference = promotedDiff == BuiltinTypeSemantics::Float64ScalarTypeName();
        const b8 floatDifference = !promotedDiff.empty();
        const MStringView differencePrefix = CodegenSymbols::SimdPrefix(doubleDifference);
        const MStringView differenceScalar = CodegenSymbols::SimdScalarCppType(doubleDifference);
        out.append("([](const auto __a, const auto __b, const auto __t)").append(lambdaExceptionSpec()).append("{ const auto __difference = ");
        if(floatDifference){
            append(differencePrefix);
            out.append("Subtract(");
            for(const MStringView value : { CodegenSymbols::s_ScalarParamB, CodegenSymbols::s_ScalarParamA }){
                if(value == CodegenSymbols::s_ScalarParamA)
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
        replicate(CodegenSymbols::s_ScalarParamA);
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
        out.append(" __a)").append(lambdaExceptionSpec()).append("{ return ");
        append(prefix);
        out.append("GetX(");
        append(prefix);
        out.append("Divide(");
        replicate(CodegenSymbols::s_ScalarUnitOne);
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

