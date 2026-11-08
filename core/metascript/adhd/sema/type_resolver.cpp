// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_resolver.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TypeRef* TypeResolver::extractUnderlyingFuncType(TypeRef* type)const noexcept{
    for(u32 depth = 0u; type && depth < 16u; ++depth){
        if(type->isFuncType)
            return type;
        if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(type->name)){
            if(aliasDecl->aliasedType){
                type = aliasDecl->aliasedType;
                continue;
            }
        }
        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(type->name); genericInfo && genericInfo->paramKind != GenericParamKind::TwoType && !type->templateArgs.empty() && type->templateArgs[0].type){
            type = type->templateArgs[0].type;
            continue;
        }
        break;
    }
    return nullptr;
}

const TypeRef* TypeResolver::unwrapOptionalFieldType(const TypeRef* const fieldType, b8& outIsOptional, MStringView& outBaseTypeName)const noexcept{
    outIsOptional = false;
    outBaseTypeName = {};
    if(!fieldType)
        return nullptr;
    const TypeRef* resolvedFieldType = m_symbols.unwrapTypeAlias(fieldType);
    const MStringView outerName = resolvedFieldType ? resolvedFieldType->name : fieldType->name;
    outIsOptional = (
        IsOptionalContainerType(outerName)
        && resolvedFieldType
        && !resolvedFieldType->templateArgs.empty()
        && resolvedFieldType->templateArgs[0].type
    );
    const TypeRef* effectiveTypeRef = outIsOptional
        ? m_symbols.unwrapTypeAlias(resolvedFieldType->templateArgs[0].type)
        : resolvedFieldType;
    outBaseTypeName = effectiveTypeRef
        ? m_symbols.resolveBaseTypeName(effectiveTypeRef->name)
        : (outIsOptional ? m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name) : outerName);
    return effectiveTypeRef;
}

const TypeRef* TypeResolver::deduceTemplateParamTypeRef(const TypeRef* const paramType, const TypeRef* const argType, const MStringView tpName, const u32 depth)const noexcept{
    if(!paramType || !argType || depth > 8u)
        return nullptr;
    if(paramType->name == tpName && paramType->templateArgs.empty())
        return argType;
    const TypeRef* up = m_symbols.unwrapTypeAlias(paramType);
    const TypeRef* ua = m_symbols.unwrapTypeAlias(argType);
    if(!up || !ua)
        return nullptr;
    if(up->name == tpName && up->templateArgs.empty())
        return ua;
    if(up->name == ua->name && up->templateArgs.size() == ua->templateArgs.size()){
        for(usize k = 0u; k < up->templateArgs.size(); ++k){
            if(const TypeRef* found = deduceTemplateParamTypeRef(up->templateArgs[k].type, ua->templateArgs[k].type, tpName, depth + 1u))
                return found;
        }
    }
    else if(DoesImplicitConversionPropagateFirstTypeArg(ua->name, up->name) && up->templateArgs.size() == 1u && !ua->templateArgs.empty()){
        if(const TypeRef* found = deduceTemplateParamTypeRef(up->templateArgs[0].type, ua->templateArgs[0].type, tpName, depth + 1u))
            return found;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

