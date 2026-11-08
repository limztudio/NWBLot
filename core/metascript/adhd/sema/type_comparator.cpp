// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_comparator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeComparator::areEquivalentWithSubst(const TypeRef* actual, const TypeRef* pattern, const Span<const TemplateParamDecl> ifaceParams, const Span<const TypeArg> ifaceArgs)const noexcept{
    actual = m_symbols.unwrapTypeAlias(actual);
    pattern = m_symbols.unwrapTypeAlias(pattern);
    if(actual == pattern)
        return true;
    if(!actual || !pattern)
        return IsVoidTypeRef(actual) && IsVoidTypeRef(pattern);

    if(!ifaceParams.empty() && !ifaceArgs.empty() && pattern->templateArgs.empty() && !pattern->isFuncType){
        const usize substCount = ifaceParams.size() < ifaceArgs.size() ? ifaceParams.size() : ifaceArgs.size();
        for(usize k = 0u; k < substCount; ++k){
            if(!ifaceParams[k].constValueType && ifaceParams[k].name == pattern->name && ifaceArgs[k].type){
                const TypeRef* subst = m_symbols.unwrapTypeAlias(ifaceArgs[k].type);
                if(!subst)
                    return false;
                if(pattern->refMode == ParamPassingMode::None || subst->refMode == pattern->refMode)
                    return areEquivalent(actual, subst);
                if(actual->refMode != pattern->refMode)
                    return false;
                TypeRef actualNormalized(*const_cast<TypeRef*>(actual));
                actualNormalized.refMode = subst->refMode;
                return areEquivalent(&actualNormalized, subst);
            }
        }
    }

    if(actual->name != pattern->name || actual->refMode != pattern->refMode || actual->isAuto != pattern->isAuto || actual->isFuncType != pattern->isFuncType || actual->templateArgs.size() != pattern->templateArgs.size())
        return false;
    for(usize i = 0u; i < actual->templateArgs.size(); ++i){
        const TypeArg& ta = actual->templateArgs[i];
        const TypeArg& tb = pattern->templateArgs[i];
        if(!AreConstantTemplateArgsEquivalent(ta.constantText, tb.constantText, m_symbols) || !areEquivalentWithSubst(ta.type, tb.type, ifaceParams, ifaceArgs))
            return false;
    }
    if(actual->isFuncType){
        if(!areEquivalentWithSubst(actual->funcReturnType, pattern->funcReturnType, ifaceParams, ifaceArgs) || actual->funcParams.size() != pattern->funcParams.size())
            return false;
        for(usize i = 0u; i < actual->funcParams.size(); ++i){
            if(actual->funcParams[i].mode != pattern->funcParams[i].mode || !areEquivalentWithSubst(actual->funcParams[i].type, pattern->funcParams[i].type, ifaceParams, ifaceArgs))
                return false;
        }
    }
    return true;
}

b8 TypeComparator::areTemplateArgListsEqual(const Span<const TypeArg> a, const Span<const TypeArg> b)const noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].type && b[i].type){
            if(!areEquivalent(a[i].type, b[i].type))
                return false;
        }
        else{
            if(!AreConstantTemplateArgsEquivalent(a[i].constantOrTypeName(), b[i].constantOrTypeName(), m_symbols))
                return false;
        }
    }
    return true;
}

b8 TypeComparator::doParamListsMatch(const Span<const ParamDecl> a, const Span<const ParamDecl> b)const noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].mode != b[i].mode || !areEquivalent(a[i].type, b[i].type))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

