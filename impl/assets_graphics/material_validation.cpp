// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_validation.h"

#include <core/graphics/shader_stage_names.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsVolumeCookDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateMaterialVariant(
    ShaderCook& shaderCook,
    const MaterialCookEntry& materialEntry,
    const PreparedShaderEntry& preparedShaderEntry,
    const Name& stageName,
    ScratchArena& scratchArena
){
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' has empty shader_variant")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    const AStringView requestedVariant(materialEntry.shaderVariant.data(), materialEntry.shaderVariant.size());

    ShaderCook::CookArena& arena = materialEntry.shaderVariant.get_allocator().arena();
    const CookString contextLabel = StringFormat(arena, "{} [{}]", materialEntry.virtualPath.c_str(), stageName.c_str());
    return shaderCook.validateVariantSignature(contextLabel, requestedVariant, preparedShaderEntry.entry.defineValues, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateMaterials(
    ShaderCook& shaderCook,
    const PreparedShaderVector& preparedEntries,
    const ShaderCook::CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    HashMap<
        PreparedShaderKey,
        const PreparedShaderEntry*,
        PreparedShaderKeyHasher,
        EqualTo<PreparedShaderKey>,
        ScratchArena
    > preparedShaderLookup(
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        scratchArena
    );
    preparedShaderLookup.reserve(preparedEntries.size());
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        const PreparedShaderKey shaderKey{
            ToName(preparedEntry.entry.name),
            ToName(preparedEntry.entry.archiveStage.view())
        };
        if(!preparedShaderLookup.emplace(shaderKey, &preparedEntry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: duplicate prepared shader key '{}' stage '{}'")
                , StringConvert(preparedEntry.entry.name)
                , StringConvert(preparedEntry.entry.archiveStage.c_str())
            );
            return false;
        }
    }

    for(const MaterialCookEntry& materialEntry : materialEntries){
        bool hasShaderStage = false;
        bool hasMeshShader = false;

        for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
            hasShaderStage = true;
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            const PreparedShaderKey shaderLookupKey{ shaderAsset.name(), stageName };
            const auto foundShader = preparedShaderLookup.find(shaderLookupKey);
            if(foundShader == preparedShaderLookup.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown shader '{}' for stage '{}'")
                    , StringConvert(materialEntry.virtualPath.c_str())
                    , StringConvert(shaderAsset.name().c_str())
                    , StringConvert(stageName.c_str())
                );
                return false;
            }

            if(!__hidden_material_validation::ValidateMaterialVariant(shaderCook, materialEntry, *foundShader.value(), stageName, scratchArena))
                return false;

            if(shaderType == Core::ShaderType::MeshStage){
                hasMeshShader = true;
                const Name shaderMaterialInterface = foundShader.value()->materialTypedBindingInterface;
                const CookString& shaderMaterialInterfacePath = foundShader.value()->materialTypedBindingInterfacePath;
                if(!shaderMaterialInterface){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' declares interface '{}' but mesh shader '{}' "
                        "does not include a generated material bind")
                        , StringConvert(materialEntry.virtualPath.c_str())
                        , StringConvert(materialEntry.materialInterface.c_str())
                        , StringConvert(shaderAsset.name().c_str())
                    );
                    return false;
                }
                if(shaderMaterialInterface != materialEntry.materialInterface){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' declares interface '{}' but mesh shader '{}' "
                        "includes generated material bind interface '{}'")
                        , StringConvert(materialEntry.virtualPath.c_str())
                        , StringConvert(materialEntry.materialInterface.c_str())
                        , StringConvert(shaderAsset.name().c_str())
                        , StringConvert(shaderMaterialInterfacePath)
                    );
                    return false;
                }
            }
        }

        if(!hasShaderStage){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' has no shader stages")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!hasMeshShader){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' declares interface '{}' but has no mesh shader stage")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

