// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////1


#include "bind_private.h"


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
    const MaterialBindAttribute* attribute = findAttribute(__hidden_bind::s_DefaultAttribute);
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


bool BuildMaterialBindParameterKey(
    const AStringView instanceName,
    const AStringView fieldName,
    ACompactString& outKey
){
    outKey.clear();
    return outKey.assign(instanceName) && outKey.pushBack('.') && outKey.append(fieldName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 ComputeMaterialBindParameterKeyHash(const AStringView parameterKey){
    return UpdateFnv64TextCanonical(FNV64_OFFSET_BASIS, parameterKey);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMaterialBindSource(
    const Path& bindFilePath,
    MaterialBindEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry.reset();

    MaterialCookArena& arena = outEntry.source.get_allocator().arena();
    Metascript::Document doc(arena);
    if(!__hidden_bind::ParseMaterialBindDocument(bindFilePath, arena, doc))
        return false;

    return __hidden_bind::ParseMaterialBindSource(bindFilePath, doc, arena, outEntry, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMaterialBindTypedLayout(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialBindTypedLayout& outLayout,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_bind::BuildMaterialBindTypedLayoutImpl(bindEntry, contextName, outLayout, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FindOrBuildMaterialBindTypedLayout(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    const MaterialBindTypedLayout*& outLayout,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_bind::FindOrBuildMaterialBindTypedLayoutImpl(
        materialInterface,
        bindEntry,
        inOutCache,
        outLayout,
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
    outBlocks.assign(layout.typedLayoutBlocks.begin(), layout.typedLayoutBlocks.end());
    outFields.assign(layout.typedLayoutFields.begin(), layout.typedLayoutFields.end());
    outBlockBytes.assign(layout.typedBlockBytes.begin(), layout.typedBlockBytes.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyMaterialBindTypedLayoutParameters(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const MaterialBindParameterMap& parameters,
    Material::TypedBlockByteVector& inOutBlockBytes
){
    for(const auto& [parameterName, parameterValue] : parameters){
        if(!__hidden_bind::ApplyMaterialBindTypedLayoutParameterValue(
            layout,
            materialName,
            parameterName,
            parameterValue,
            inOutBlockBytes
        ))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

