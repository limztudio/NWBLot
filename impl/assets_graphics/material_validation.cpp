// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook_plan.h"

#include <core/graphics/shader_stage_names.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ValidateMaterialVariant(const MaterialCookEntry& materialEntry, const PreparedShaderEntry& preparedShaderEntry, const Name& stageName, Core::Assets::ScratchArena& scratchArena){
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' has empty shader_variant"), ConvertString(AStringView(materialEntry.virtualPath)));
        return false;
    }
    const AStringView requestedVariant(materialEntry.shaderVariant.data(), materialEntry.shaderVariant.size());

    const Core::Assets::ScratchString contextLabel = StringFormat(scratchArena, "{} [{}]", AStringView(materialEntry.virtualPath), stageName.resolvedText());
    return ShaderCook::ValidateVariantSignature(contextLabel, requestedVariant, preparedShaderEntry.entry.defineValues, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateMaterials(const Span<const PreparedShaderEntry> preparedEntries, const Span<const MaterialCookEntry> materialEntries, Core::Assets::ScratchArena& scratchArena){
    HashMap<ShaderStageKey, NotNull<const PreparedShaderEntry*>, Core::Assets::ScratchArena, Hasher<ShaderStageKey>, EqualTo<ShaderStageKey>> preparedShaderLookup(0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), scratchArena);
    preparedShaderLookup.reserve(preparedEntries.size());
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        const auto shaderName = ToName(preparedEntry.entry.name);
        const auto stageName = ToName(preparedEntry.entry.archiveStage);
        if(!shaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: prepared shader name has invalid text (failure {})"), static_cast<u32>(shaderName.error()));
            return false;
        }
        if(!stageName){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: prepared shader stage has invalid text for entry '{}' (failure {})"), ConvertString(preparedEntry.entry.name), static_cast<u32>(stageName.error()));
            return false;
        }
        const ShaderStageKey shaderKey(*shaderName ? **shaderName : s_NameNone, *stageName ? **stageName : s_NameNone);
        if(!preparedShaderLookup.emplace(shaderKey, MakeNotNull(&preparedEntry)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: duplicate prepared shader key '{}' stage '{}'"), ConvertString(preparedEntry.entry.name), ConvertString(preparedEntry.entry.archiveStage));
            return false;
        }
    }

    for(const MaterialCookEntry& materialEntry : materialEntries){
        b8 hasShaderStage = false;
        b8 hasInterfaceStage = false;

        for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
            hasShaderStage = true;
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            const ShaderStageKey shaderLookupKey(shaderAsset.name(), stageName);
            const auto foundShader = preparedShaderLookup.find(shaderLookupKey);
            if(foundShader == preparedShaderLookup.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown shader '{}' for stage '{}'"), ConvertString(AStringView(materialEntry.virtualPath)), ConvertString(shaderAsset.name().resolvedText()), ConvertString(stageName.resolvedText()));
                return false;
            }

            if(!__hidden_material_validation::ValidateMaterialVariant(materialEntry, *foundShader.value(), stageName, scratchArena))
                return false;

            // Only the pixel surface hook requires the material interface; the shared mesh stage has none.
            if(shaderType == Core::ShaderType::PixelStage){
                hasInterfaceStage = true;
                const Name shaderMaterialInterface = foundShader.value()->materialTypedBindingInterface;
                const Core::Assets::CookString& shaderMaterialInterfacePath = foundShader.value()->materialTypedBindingInterfacePath;
                if(shaderMaterialInterface){
                    const auto materialInterface = Name::FromText(AStringView(materialEntry.materialInterface));
                    if(!materialInterface){
                        NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' interface has invalid text (failure {})"), ConvertString(AStringView(materialEntry.virtualPath)), static_cast<u32>(materialInterface.error()));
                        return false;
                    }
                    if(!ValidateMaterialCookPixelShaderInterface(
                        AStringView(materialEntry.virtualPath),
                        AStringView(materialEntry.materialInterface),
                        shaderAsset.name(),
                        shaderMaterialInterface,
                        AStringView(shaderMaterialInterfacePath),
                        *materialInterface
                    ))
                        return false;
                }
                else if(!ValidateMaterialCookPixelShaderInterface(
                    AStringView(materialEntry.virtualPath),
                    AStringView(materialEntry.materialInterface),
                    shaderAsset.name(),
                    s_NameNone,
                    AStringView(),
                    s_NameNone
                ))
                    return false;
            }
        }

        if(!ValidateMaterialCookStagePresence(
            AStringView(materialEntry.virtualPath),
            AStringView(materialEntry.materialInterface),
            hasShaderStage,
            hasInterfaceStage
        ))
            return false;
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

