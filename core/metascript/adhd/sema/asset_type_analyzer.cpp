// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_type_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

b8 AssetTypeAnalyzer::tryResolveSubsystemFieldAssetType(const FieldDecl& field, MStringView& outAssetTypeName, b8& outIsVector)const noexcept{
    outAssetTypeName = {};
    outIsVector = false;
    if(!field.type)
        return false;

    const TypeRef* resolved = m_symbols.unwrapTypeAlias(field.type);
    if(!resolved || resolved->refMode != ParamPassingMode::None)
        return false;

    const TypeRef* assetRefType = resolved;
    if(resolved->name == "Vector" && resolved->templateArgs.size() == 1u && resolved->templateArgs[0].type){
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

