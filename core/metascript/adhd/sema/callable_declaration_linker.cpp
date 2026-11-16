// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "callable_declaration_linker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_callable_declaration_linker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TemplateParamListsMatch(
    MetaArena& arena,
    MVector<ParseError>& errors,
    const TypeComparator& comparator,
    const Span<const TemplateParamDecl> expectedParams,
    const Span<const TemplateParamDecl> actualParams,
    const SourceSpan errorSpan,
    const MStringView nameOrConstMismatchMsg,
    const MStringView constraintMismatchMsg,
    const b8 reportNameSeparately,
    const MStringView nameMismatchMsg
){
    const usize count = expectedParams.size() < actualParams.size() ? expectedParams.size() : actualParams.size();
    for(usize tIdx = 0u; tIdx < count; ++tIdx){
        const TemplateParamDecl& expectedTp = expectedParams[tIdx];
        const TemplateParamDecl& actualTp = actualParams[tIdx];
        if(reportNameSeparately && expectedTp.name != actualTp.name){
            ReportAdhdDiagnostic(arena, errors, errorSpan, nameMismatchMsg);
            return false;
        }
        if(!comparator.doTemplateParamHeaderMatch(expectedTp, actualTp, !reportNameSeparately)){
            ReportAdhdDiagnostic(arena, errors, errorSpan, nameOrConstMismatchMsg);
            return false;
        }
        if(!comparator.doTemplateParamBoundsMatch(expectedTp, actualTp)){
            ReportAdhdDiagnostic(arena, errors, errorSpan, constraintMismatchMsg);
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CallableDeclarationLinker::link(TranslationUnit& unit, MVector<ParseError>& errors)const{
    b8 ok = true;

    for(MethodDecl* ool : unit.outOfLineMethods){
        ClassDecl* cls = m_symbols.findClass(ool->ownerClassName);
        if(!cls){
            ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method references unknown class");
            ok = false;
            continue;
        }

        if(cls->templateParams.size() != ool->ownerTemplateParams.size()){
            ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method owner class template parameter count does not match class declaration");
            ok = false;
            continue;
        }
        b8 ownerTemplateNamesMatch = true;
        for(usize tIdx = 0u; tIdx < cls->templateParams.size(); ++tIdx){
            if(cls->templateParams[tIdx].name != ool->ownerTemplateParams[tIdx]){
                ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method owner class template parameter name does not match class declaration");
                ok = false;
                ownerTemplateNamesMatch = false;
                break;
            }
        }
        if(!ownerTemplateNamesMatch)
            continue;
        if(!__hidden_adhd_callable_declaration_linker::TemplateParamListsMatch(
            m_arena,
            errors,
            m_comparator,
            cls->templateParams,
            ool->ownerTemplateParamDecls,
            ool->span,
            "out-of-line method owner class template parameter constraints do not match class declaration",
            "out-of-line method owner class template parameter constraints do not match class declaration",
            false,
            {}
        )){
            ok = false;
            continue;
        }

        MethodDecl* matched = nullptr;
        MethodDecl* paramMatched = nullptr;
        for(MethodDecl* m : cls->methods){
            if(m->isConstructor == ool->isConstructor && m->isDestructor == ool->isDestructor && m->isOperator == ool->isOperator && m->name == ool->name && m->params.size() == ool->params.size()){
                if(!matched || m->templateParams.size() == ool->templateParams.size())
                    matched = m;
                if(m->templateParams.size() == ool->templateParams.size() && m_comparator.doParamListsMatch(m->params, ool->params)){
                    paramMatched = m;
                    if(m->isConst == ool->isConst)
                        break;
                }
            }
        }
        if(paramMatched)
            matched = paramMatched;

        if(!matched){
            ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method is not declared in class");
            ok = false;
            continue;
        }

        if(matched->templateParams.size() != ool->templateParams.size()){
            ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method template parameter count does not match class declaration");
            ok = false;
            continue;
        }
        if(!__hidden_adhd_callable_declaration_linker::TemplateParamListsMatch(
            m_arena,
            errors,
            m_comparator,
            matched->templateParams,
            ool->templateParams,
            ool->span,
            "out-of-line method template parameter constraints do not match class declaration",
            "out-of-line method template parameter constraints do not match class declaration",
            true,
            "out-of-line method template parameter name does not match class declaration"
        )){
            ok = false;
            continue;
        }

        if(matched->isConst != ool->isConst){
            ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method 'const' qualifier does not match class declaration");
            ok = false;
            continue;
        }

        if(ool->isOverride && !matched->isOverride){
            ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method cannot specify 'override' when class declaration is not marked 'override'");
            ok = false;
            continue;
        }

        if(!matched->isConstructor && !matched->isDestructor){
            if(!m_comparator.areEquivalent(matched->returnType, ool->returnType)){
                ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method return type does not match class declaration");
                ok = false;
                continue;
            }
        }

        b8 paramsMatch = true;
        for(usize pIdx = 0u; pIdx < matched->params.size(); ++pIdx){
            const ParamDecl& declP = matched->params[pIdx];
            const ParamDecl& oolP = ool->params[pIdx];
            if(declP.mode != oolP.mode || !m_comparator.areEquivalent(declP.type, oolP.type)){
                ReportAdhdDiagnostic(m_arena, errors, oolP.span, "out-of-line parameter 'in'/'mut' mode or type does not match class declaration");
                ok = false;
                paramsMatch = false;
                break;
            }
        }
        if(!paramsMatch)
            continue;

        if(matched->hasBody){
            ReportAdhdDiagnostic(m_arena, errors, ool->span, "method already has an inline body in class declaration");
            ok = false;
            continue;
        }

        if(!ool->templateParams.empty())
            matched->templateParams = ool->templateParams;
        matched->params = ool->params;
        matched->hasBody = true;
        matched->isOutOfLine = true;
        matched->body = ool->body;
        if(ool->isReflect){
            matched->isReflect = true;
            cls->isReflect = true;
        }
        if(!ool->isImported)
            matched->isImported = false;
    }

    for(usize i = 0u; i < unit.freeFunctions.size(); ++i){
        MethodDecl* fn = unit.freeFunctions[i];
        if(!fn)
            continue;
        for(usize j = 0u; j < i; ++j){
            MethodDecl* prev = unit.freeFunctions[j];
            if(!prev || prev->name != fn->name || prev->templateParams.size() != fn->templateParams.size())
                continue;
            if(!m_comparator.doParamListsMatch(prev->params, fn->params))
                continue;
            if(!__hidden_adhd_callable_declaration_linker::TemplateParamListsMatch(
                m_arena,
                errors,
                m_comparator,
                prev->templateParams,
                fn->templateParams,
                fn->span,
                "free function template parameter list does not match previous declaration",
                "free function template parameter constraints do not match previous declaration",
                false,
                {}
            )){
                ok = false;
                continue;
            }
            if(!m_comparator.areEquivalent(prev->returnType, fn->returnType)){
                ReportAdhdDiagnostic(m_arena, errors, fn->span, "conflicting return type for free function declaration");
                ok = false;
                continue;
            }
            if(prev->hasBody && fn->hasBody){
                ReportAdhdDiagnostic(m_arena, errors, fn->span, "duplicate free function definition with identical signature");
                ok = false;
                continue;
            }
            if(!prev->hasBody && fn->hasBody){
                if(!prev->isImported){
                    prev->params = fn->params;
                    prev->hasBody = true;
                    prev->isOutOfLine = true;
                    prev->body = fn->body;
                    fn->isImported = true;
                    fn->hasBody = false;
                }
                else{
                    fn->isOutOfLine = true;
                    prev->hasBody = true;
                }
            }
            else if(prev->hasBody && !fn->hasBody){
                fn->isImported = true;
            }
            else if(!prev->hasBody && !fn->hasBody){
                fn->isImported = true;
            }
        }
    }

    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

