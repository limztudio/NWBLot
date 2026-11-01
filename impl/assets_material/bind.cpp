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


AStringView MaterialBindField::defaultArgument()const noexcept{
    const MaterialBindAttribute* attribute = findAttribute(s_MaterialDefaultAttribute);
    return (attribute && attribute->arguments.size() == 1u) ? AStringView(attribute->arguments[0u]) : AStringView();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialBindTypedLayoutCache::reserve(const usize count){
    entries.reserve(count);
    lookup.reserve(count);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ACompactString> BuildMaterialBindParameterKey(const AStringView instanceName, const AStringView fieldName)noexcept{
    ACompactString key;
    if(!key.assign(instanceName) || !key.pushBack('.') || !key.append(fieldName))
        return MakeUnexpected(Failure{});
    return key;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CopyMaterialBindTypedLayoutDefaults(const MaterialBindTypedLayout& layout, u64& outLayoutHash, Material::TypedLayoutBlockVector& outBlocks, Material::TypedLayoutFieldVector& outFields, Material::TypedBlockByteVector& outBlockBytes){
    outLayoutHash = layout.layoutHash;
    outBlocks.reserve(layout.typedLayoutBlocks.size());
    outBlocks.assign(layout.typedLayoutBlocks.begin(), layout.typedLayoutBlocks.end());
    outFields.reserve(layout.typedLayoutFields.size());
    outFields.assign(layout.typedLayoutFields.begin(), layout.typedLayoutFields.end());
    outBlockBytes.reserve(layout.typedBlockBytes.size());
    outBlockBytes.assign(layout.typedBlockBytes.begin(), layout.typedBlockBytes.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ApplyMaterialBindTypedLayoutParameters(const MaterialBindTypedLayout& layout, const Name& materialName, const MaterialBindParameterMap& parameters, Material::TypedBlockByteVector& inOutBlockBytes, Material::ResourceReferenceVector& outResourceReferences){
    outResourceReferences.clear();
    for(const auto& [parameterName, parameterValue] : parameters){
        if(!MaterialBindDetail::ApplyMaterialBindTypedLayoutParameterValue(layout, materialName, parameterName, parameterValue, outResourceReferences, inOutBlockBytes))
            return false;
    }

    Sort(outResourceReferences.begin(), outResourceReferences.end(), [](const MaterialResourceReference& lhs, const MaterialResourceReference& rhs)noexcept{
        return lhs.constantByteOffset < rhs.constantByteOffset;
    });

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

