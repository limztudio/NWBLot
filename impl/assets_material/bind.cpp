// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind.h"
#include "bind_private.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MaterialBindStruct* MaterialBindEntry::findStruct(const AStringView typeName)const{
    for(const MaterialBindStruct& bindStruct : structs){
        if(AStringView(bindStruct.name) == typeName)
            return &bindStruct;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MaterialBindAttribute* MaterialBindField::findAttribute(const AStringView attributeName)const{
    for(const MaterialBindAttribute& attribute : attributes){
        if(AStringView(attribute.name) == attributeName)
            return &attribute;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AStringView MaterialBindField::defaultArgument()const{
    const MaterialBindAttribute* attribute = findAttribute(MaterialBindDetail::s_DefaultAttribute);
    return (attribute && attribute->arguments.size() == 1u) ? AStringView(attribute->arguments[0u]) : AStringView();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MaterialBindField* MaterialBindStruct::findField(const AStringView fieldName)const{
    for(const MaterialBindField& field : fields){
        if(AStringView(field.name) == fieldName)
            return &field;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MaterialBindAttribute* MaterialBindStruct::findAttribute(const AStringView attributeName)const{
    for(const MaterialBindAttribute& attribute : attributes){
        if(AStringView(attribute.name) == attributeName)
            return &attribute;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MaterialBindInstance* MaterialBindEntry::findInstance(const AStringView instanceName)const{
    for(const MaterialBindInstance& instance : instances){
        if(AStringView(instance.name) == instanceName)
            return &instance;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialBindTypedLayout::reset(){
    bindEntry = nullptr;
    layoutHash = 0u;
    typedLayoutBlocks.clear();
    typedLayoutFields.clear();
    typedBlockBytes.clear();
    blockLookup.clear();
    parameterLookup.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialBindTypedLayoutCache::reserve(const usize count){
    entries.reserve(count);
    lookup.reserve(count);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ACompactString> BuildMaterialBindParameterKey(const AStringView instanceName, const AStringView fieldName){
    ACompactString key;
    if(!key.assign(instanceName) || !key.pushBack('.') || !key.append(fieldName))
        return MakeUnexpected(Failure{});
    return key;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 ComputeMaterialBindParameterKeyHash(const AStringView parameterKey){
    return UpdateFnv64TextCanonical(s_Fnv64OffsetBasis, parameterKey);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindEntry> ParseMaterialBindSource(
    const Path& bindFilePath,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto doc = MaterialBindDetail::ParseMaterialBindDocument(bindFilePath, arena);
    if(!doc)
        return MakeUnexpected(Failure{});

    return MaterialBindDetail::ParseMaterialBindSource(bindFilePath, *doc, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindTypedLayout> BuildMaterialBindTypedLayout(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialBindDetail::BuildMaterialBindTypedLayoutImpl(bindEntry, contextName, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<const MaterialBindTypedLayout*> FindOrBuildMaterialBindTypedLayout(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialBindDetail::FindOrBuildMaterialBindTypedLayoutImpl(
        materialInterface,
        bindEntry,
        inOutCache,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CopyMaterialBindTypedLayoutDefaults(
    const MaterialBindTypedLayout& layout,
    u64& outLayoutHash,
    Material::TypedLayoutBlockVector& outBlocks,
    Material::TypedLayoutFieldVector& outFields,
    Material::TypedBlockByteVector& outBlockBytes
){
    outLayoutHash = layout.layoutHash;
    outBlocks.reserve(layout.typedLayoutBlocks.size());
    outBlocks.assign(layout.typedLayoutBlocks.begin(), layout.typedLayoutBlocks.end());
    outFields.reserve(layout.typedLayoutFields.size());
    outFields.assign(layout.typedLayoutFields.begin(), layout.typedLayoutFields.end());
    outBlockBytes.reserve(layout.typedBlockBytes.size());
    outBlockBytes.assign(layout.typedBlockBytes.begin(), layout.typedBlockBytes.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyMaterialBindTypedLayoutParameters(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const MaterialBindParameterMap& parameters,
    Material::TypedBlockByteVector& inOutBlockBytes,
    Material::ResourceReferenceVector& outResourceReferences
){
    outResourceReferences.clear();
    for(const auto& [parameterName, parameterValue] : parameters){
        if(!MaterialBindDetail::ApplyMaterialBindTypedLayoutParameterValue(
            layout,
            materialName,
            parameterName,
            parameterValue,
            inOutBlockBytes,
            outResourceReferences
        ))
            return false;
    }

    Sort(outResourceReferences.begin(), outResourceReferences.end(), [](const MaterialResourceReference& lhs, const MaterialResourceReference& rhs){
        return lhs.constantByteOffset < rhs.constantByteOffset;
    });

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

