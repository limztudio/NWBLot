// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_type_analyzer.h"
#include "type_resolver.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const TypeRef* AssetTypeAnalyzer::findImplementedAssetInterface(const ClassDecl& cls)const noexcept{
    if(cls.assetTypeName.empty())
        return nullptr;
    for(const TypeRef* ifaceRef : cls.implementedInterfaces){
        if(!ifaceRef)
            continue;
        const InterfaceDecl* ifaceDecl = m_symbols.findInterface(m_symbols.resolveBaseTypeName(ifaceRef->name));
        if(ifaceDecl && !ifaceDecl->assetTypeName.empty())
            return ifaceRef;
    }
    return nullptr;
}

const MethodDecl* AssetTypeAnalyzer::findAssetLoadBinaryMethod(const ClassDecl& cls, const b8 requireLocalDefinition)const noexcept{
    const AssetCodecContractInfo& codec = ContractSemantics::s_AssetCodec;
    for(const MethodDecl* method : cls.methods){
        if(
            method
            && (!requireLocalDefinition || (!method->isImported && method->hasBody))
            && !method->isConstructor
            && !method->isDestructor
            && !method->isOperator
            && FindSpecialMethodSemantics(method->name) == SpecialMethodKind::AssetLoadBinary
            && method->params.size() == codec.hookParamCount
        )
            return method;
    }
    return nullptr;
}

b8 AssetTypeAnalyzer::isAssetDependencyType(const TypeRef* const type, const u32 depth)const noexcept{
    if(!type || depth > 8u)
        return false;
    const TypeRef* resolved = m_symbols.unwrapTypeAlias(type);
    if(!resolved)
        return false;
    const MStringView tname = resolved->name;
    if(IsAssetHandleType(tname))
        return true;
    if(const ClassDecl* cls = m_symbols.findClass(tname)){
        for(const FieldDecl* f : cls->orderedFields){
            if(!f || !IsFieldReflected(*f) || !f->type)
                continue;
            const TypeRef* ft = m_symbols.substituteTypeParams(f->type, cls->templateParams, resolved);
            if(isAssetDependencyType(ft, depth + 1u))
                return true;
        }
    }
    if((IsOptionalContainerType(tname) || IsReflectedLinearContainerType(tname)) && !resolved->templateArgs.empty() && resolved->templateArgs[0].type)
        return isAssetDependencyType(resolved->templateArgs[0].type, depth + 1u);
    if(IsReflectedMapContainerType(tname) && resolved->templateArgs.size() >= 2u && resolved->templateArgs[1].type)
        return isAssetDependencyType(resolved->templateArgs[1].type, depth + 1u);
    return false;
}

b8 AssetTypeAnalyzer::classifyReflectedAssetDependencyField(const FieldDecl& field, ReflectedAssetDependencyField& outField)const noexcept{
    outField = {};
    if(!IsFieldReflected(field) || !field.type || !isAssetDependencyType(field.type))
        return false;

    outField.effectiveType = TypeResolver(m_symbols).unwrapOptionalFieldType(field.type, outField.isOptional, outField.effectiveTypeName);
    if(IsAssetHandleType(outField.effectiveTypeName)){
        outField.kind = AssetDependencyFieldKind::AssetHandle;
        return true;
    }
    if(m_symbols.findClass(outField.effectiveTypeName)){
        outField.kind = AssetDependencyFieldKind::Class;
        return true;
    }
    if(IsReflectedLinearContainerType(outField.effectiveTypeName) && outField.effectiveType && !outField.effectiveType->templateArgs.empty() && outField.effectiveType->templateArgs[0].type){
        outField.elementType = m_symbols.unwrapTypeAlias(outField.effectiveType->templateArgs[0].type);
        outField.elementTypeName = m_symbols.resolveBaseTypeName(outField.effectiveType->templateArgs[0].type->name);
        if(IsAssetHandleType(outField.elementTypeName) || m_symbols.findClass(outField.elementTypeName)){
            outField.kind = AssetDependencyFieldKind::LinearContainer;
            return true;
        }
    }
    else if(IsReflectedMapContainerType(outField.effectiveTypeName) && outField.effectiveType && outField.effectiveType->templateArgs.size() >= 2u && outField.effectiveType->templateArgs[1].type){
        outField.elementType = m_symbols.unwrapTypeAlias(outField.effectiveType->templateArgs[1].type);
        outField.elementTypeName = m_symbols.resolveBaseTypeName(outField.effectiveType->templateArgs[1].type->name);
        if(IsAssetHandleType(outField.elementTypeName) || m_symbols.findClass(outField.elementTypeName)){
            outField.kind = AssetDependencyFieldKind::MapContainer;
            return true;
        }
    }
    return false;
}

b8 AssetTypeAnalyzer::tryResolveSubsystemFieldAssetType(const FieldDecl& field, MStringView& outAssetTypeName, b8& outIsVector)const noexcept{
    outAssetTypeName = {};
    outIsVector = false;
    if(!field.type)
        return false;

    const TypeRef* resolved = m_symbols.unwrapTypeAlias(field.type);
    if(!resolved || resolved->refMode != ParamPassingMode::None)
        return false;

    const TypeRef* assetRefType = resolved;
    if(IsSubsystemVectorContainerType(resolved->name) && resolved->templateArgs.size() == 1u && resolved->templateArgs[0].type){
        outIsVector = true;
        assetRefType = m_symbols.unwrapTypeAlias(resolved->templateArgs[0].type);
    }
    if(assetRefType && IsAssetHandleType(assetRefType->name) && assetRefType->templateArgs.size() == 1u && assetRefType->templateArgs[0].type){
        if(const TypeRef* targetType = m_symbols.unwrapTypeAlias(assetRefType->templateArgs[0].type))
            outAssetTypeName = targetType->name;
    }
    return !outAssetTypeName.empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

