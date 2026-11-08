// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_private.h"

#include <impl/assets_material/layout_validation.adhd.h>
#include <impl/assets_material/material_binary_writer.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using OptionalAvboitPixelShaderSetter = void(Material::*)(const Core::Assets::AssetRef<PixelShader>&);

template<OptionalAvboitPixelShaderSetter Setter>
static b8 SetOptionalAvboitPixelShader(const MaterialCookEntry& materialEntry, const AStringView shaderNameText, const AStringView passLabel, Material& outMaterial){
    static_assert(Setter != nullptr);
    Core::Assets::AssetRef<PixelShader> shaderRef;
    if(!TryResolveOptionalMaterialAvboitPixelShader(AStringView(materialEntry.virtualPath), shaderNameText, passLabel, shaderRef))
        return false;
    if(shaderRef.valid())
        (outMaterial.*Setter)(shaderRef);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Material> BuildMaterialAsset(const MaterialCookEntry& materialEntry, Core::Assets::AssetArena& arena){
    if(!ValidateMaterialCookEntryRequiredFields(
        AStringView(materialEntry.virtualPath),
        AStringView(materialEntry.materialInterface),
        materialEntry.typedLayoutHash,
        AStringView(materialEntry.shaderVariant)
    ))
        return MakeUnexpected(Failure{});

    const auto virtualPath = Name::FromText(AStringView(materialEntry.virtualPath));
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: virtual path contains an embedded null"));
        return MakeUnexpected(Failure{});
    }
    Material asset(arena, *virtualPath);
    asset.setShaderVariant(materialEntry.shaderVariant);
    const auto materialInterface = Name::FromText(AStringView(materialEntry.materialInterface));
    if(!materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface contains an embedded null"));
        return MakeUnexpected(Failure{});
    }
    asset.setMaterialInterface(*materialInterface);
    asset.setShadingModelId(materialEntry.shadingModelId);
    asset.setSurfaceDispatchId(materialEntry.surfaceDispatchId);
    if(!SetOptionalAvboitPixelShader<&Material::setAvboitAccumulatePixelShader>(materialEntry, materialEntry.avboitAccumulatePixelShaderName, s_MaterialAvboitAccumulatePassLabel, asset))
        return MakeUnexpected(Failure{});
    if(!SetOptionalAvboitPixelShader<&Material::setAvboitOccupancyPixelShader>(materialEntry, materialEntry.avboitOccupancyPixelShaderName, s_MaterialAvboitOccupancyPassLabel, asset))
        return MakeUnexpected(Failure{});
    if(!SetOptionalAvboitPixelShader<&Material::setAvboitExtinctionPixelShader>(materialEntry, materialEntry.avboitExtinctionPixelShaderName, s_MaterialAvboitExtinctionPassLabel, asset))
        return MakeUnexpected(Failure{});
    asset.setTransparent(materialEntry.transparent);
    asset.setTwoSided(materialEntry.twoSided);
    asset.setRefractive(materialEntry.refractive);
    if(!HasValidMaterialAvboitPixelShaderContract(asset.transparent(), asset.avboitAccumulatePixelShader(), asset.avboitOccupancyPixelShader(), asset.avboitExtinctionPixelShader())){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' AVBOIT pixel shaders must be present if and only if it is transparent"), ConvertString(AStringView(materialEntry.virtualPath)));
        return MakeUnexpected(Failure{});
    }
    asset.setTypedLayout(materialEntry.typedLayoutHash, materialEntry.typedLayoutBlocks, materialEntry.typedLayoutFields, materialEntry.typedBlockBytes);
    asset.setResourceReferences(materialEntry.resourceReferences);

    for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
        if(!asset.setShaderForStage(shaderType, shaderAsset)){
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: invalid shader stage '{}' for '{}'"), ConvertString(stageName.resolvedText()), ConvertString(AStringView(materialEntry.virtualPath)));
            return MakeUnexpected(Failure{});
        }
    }

    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    static constexpr TStringView s_SerializeFailureContext = NWB_TEXT("MaterialAssetCodec::serialize");
    if(!checkSerializeAssetType(asset, s_SerializeFailureContext))
        return false;

    const Material& material = static_cast<const Material&>(asset);
    if(!material.validatePayload(s_SerializeFailureContext, s_MaterialSerializeContextText))
        return false;
    return MaterialBinaryWriter().serialize(
        AStringView(material.shaderVariant()),
        material.materialInterface(),
        material.typedLayoutHash(),
        material.typedLayoutBlocks(),
        material.typedLayoutFields(),
        material.typedBlockBytes(),
        material.resourceReferences(),
        material.stageShaders(),
        material.stageShaderCount(),
        material.transparent(),
        material.twoSided(),
        material.refractive(),
        material.shadingModelId(),
        material.surfaceDispatchId(),
        material.avboitAccumulatePixelShader(),
        material.avboitOccupancyPixelShader(),
        material.avboitExtinctionPixelShader(),
        outBinary
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
