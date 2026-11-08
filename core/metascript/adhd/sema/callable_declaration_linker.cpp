// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "callable_declaration_linker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


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
            if(tIdx < ool->ownerTemplateParamDecls.size()){
                const TemplateParamDecl& clsTp = cls->templateParams[tIdx];
                const TemplateParamDecl& oolOwnerTp = ool->ownerTemplateParamDecls[tIdx];
                if(
                    (clsTp.constValueType != nullptr) != (oolOwnerTp.constValueType != nullptr)
                    || (clsTp.constValueType && oolOwnerTp.constValueType && !m_comparator.areEquivalent(clsTp.constValueType, oolOwnerTp.constValueType))
                    || clsTp.interfaceBounds.size() != oolOwnerTp.interfaceBounds.size()
                ){
                    ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method owner class template parameter constraints do not match class declaration");
                    ok = false;
                    ownerTemplateNamesMatch = false;
                    break;
                }
                for(usize bIdx = 0u; bIdx < clsTp.interfaceBounds.size(); ++bIdx){
                    if(!m_comparator.areEquivalent(clsTp.interfaceBounds[bIdx], oolOwnerTp.interfaceBounds[bIdx])){
                        ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method owner class template parameter constraints do not match class declaration");
                        ok = false;
                        ownerTemplateNamesMatch = false;
                        break;
                    }
                }
                if(!ownerTemplateNamesMatch)
                    break;
            }
        }
        if(!ownerTemplateNamesMatch)
            continue;

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
        b8 methodTemplateParamsMatch = true;
        for(usize tIdx = 0u; tIdx < matched->templateParams.size(); ++tIdx){
            const TemplateParamDecl& declTp = matched->templateParams[tIdx];
            const TemplateParamDecl& oolTp = ool->templateParams[tIdx];
            if(declTp.name != oolTp.name){
                ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method template parameter name does not match class declaration");
                ok = false;
                methodTemplateParamsMatch = false;
                break;
            }
            if(
                (declTp.constValueType != nullptr) != (oolTp.constValueType != nullptr)
                || (declTp.constValueType && oolTp.constValueType && !m_comparator.areEquivalent(declTp.constValueType, oolTp.constValueType))
                || declTp.interfaceBounds.size() != oolTp.interfaceBounds.size()
            ){
                ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method template parameter constraints do not match class declaration");
                ok = false;
                methodTemplateParamsMatch = false;
                break;
            }
            for(usize bIdx = 0u; bIdx < declTp.interfaceBounds.size(); ++bIdx){
                if(!m_comparator.areEquivalent(declTp.interfaceBounds[bIdx], oolTp.interfaceBounds[bIdx])){
                    ReportAdhdDiagnostic(m_arena, errors, ool->span, "out-of-line method template parameter constraints do not match class declaration");
                    ok = false;
                    methodTemplateParamsMatch = false;
                    break;
                }
            }
            if(!methodTemplateParamsMatch)
                break;
        }
        if(!methodTemplateParamsMatch)
            continue;

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
            b8 fnTemplateParamsMatch = true;
            for(usize tIdx = 0u; tIdx < prev->templateParams.size(); ++tIdx){
                const TemplateParamDecl& prevTp = prev->templateParams[tIdx];
                const TemplateParamDecl& curTp = fn->templateParams[tIdx];
                if(
                    prevTp.name != curTp.name
                    || (prevTp.constValueType != nullptr) != (curTp.constValueType != nullptr)
                    || (prevTp.constValueType && curTp.constValueType && !m_comparator.areEquivalent(prevTp.constValueType, curTp.constValueType))
                    || prevTp.interfaceBounds.size() != curTp.interfaceBounds.size()
                ){
                    ReportAdhdDiagnostic(m_arena, errors, fn->span, "free function template parameter list does not match previous declaration");
                    ok = false;
                    fnTemplateParamsMatch = false;
                    break;
                }
                for(usize bIdx = 0u; bIdx < prevTp.interfaceBounds.size(); ++bIdx){
                    if(!m_comparator.areEquivalent(prevTp.interfaceBounds[bIdx], curTp.interfaceBounds[bIdx])){
                        ReportAdhdDiagnostic(m_arena, errors, fn->span, "free function template parameter constraints do not match previous declaration");
                        ok = false;
                        fnTemplateParamsMatch = false;
                        break;
                    }
                }
                if(!fnTemplateParamsMatch)
                    break;
            }
            if(!fnTemplateParamsMatch)
                continue;
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

