// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using OptionalAvboitPixelShaderSetter = void(Material::*)(const Core::Assets::AssetRef<PixelShader>&);

template<OptionalAvboitPixelShaderSetter Setter>
static b8 SetOptionalAvboitPixelShader(const MaterialCookEntry& materialEntry, const AStringView shaderNameText, const AStringView passLabel, Material& outMaterial){
    static_assert(Setter != nullptr);
    if(shaderNameText.empty())
        return true;

    const auto shaderName = ToName(shaderNameText);
    if(!shaderName || !*shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has an invalid AVBOIT {} pixel shader name"), ConvertString(AStringView(materialEntry.virtualPath)), ConvertString(passLabel));
        return false;
    }

    Core::Assets::AssetRef<PixelShader> shaderRef;
    shaderRef.virtualPath = **shaderName;
    (outMaterial.*Setter)(shaderRef);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Material> BuildMaterialAsset(const MaterialCookEntry& materialEntry, Core::Assets::AssetArena& arena){
    if(materialEntry.materialInterface.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing required material interface"), ConvertString(AStringView(materialEntry.virtualPath)));
        return MakeUnexpected(Failure{});
    }
    if(materialEntry.typedLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface material '{}' is missing typed layout data"), ConvertString(AStringView(materialEntry.virtualPath)));
        return MakeUnexpected(Failure{});
    }
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has empty shader variant"), ConvertString(AStringView(materialEntry.virtualPath)));
        return MakeUnexpected(Failure{});
    }

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
    if(!material.validatePayload(s_SerializeFailureContext))
        return false;
    usize reserveBytes = sizeof(u32);
    b8 canReserve = AddBinaryStringReserveBytes(reserveBytes, AStringView(material.shaderVariant()))
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u64))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.typedLayoutBlocks().size(), s_MaterialTypedLayoutBlockBytes)
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.typedLayoutFields().size(), s_MaterialTypedLayoutFieldBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, material.typedBlockBytes().size())
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.resourceReferences().size(), s_MaterialResourceReferenceBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaderCount(), s_MaterialShaderEntryBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash))
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    AppendPOD(outBinary, s_MaterialBinaryMagic);
    if(!AppendString(outBinary, AStringView(material.shaderVariant()))){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is too long"));
        return false;
    }
    AppendPOD(outBinary, material.materialInterface().hash());
    AppendPOD(outBinary, material.typedLayoutHash());
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutBlocks().size()));
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutFields().size()));
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks())
        AppendPOD(outBinary, PackMaterialTypedLayoutBlockBinary(block));
    for(const MaterialTypedLayoutField& field : material.typedLayoutFields())
        AppendPOD(outBinary, PackMaterialTypedLayoutFieldBinary(field));
    AppendPOD(outBinary, static_cast<u32>(material.typedBlockBytes().size()));
    AppendBytesNoReserveUnchecked(outBinary, material.typedBlockBytes().data(), material.typedBlockBytes().size());
    AppendPOD(outBinary, static_cast<u32>(material.resourceReferences().size()));
    for(const MaterialResourceReference& resourceReference : material.resourceReferences())
        AppendPOD(outBinary, PackMaterialResourceReferenceBinary(resourceReference));
    AppendPOD(outBinary, material.stageShaderCount());

    const Material::StageShaderArray& stageShaders = material.stageShaders();
    for(usize shaderIndex = 0u; shaderIndex < stageShaders.size(); ++shaderIndex){
        const Core::Assets::AssetRef<IShader>& shaderAsset = stageShaders[shaderIndex];
        if(!shaderAsset.valid())
            continue;

        const Core::ShaderType::Enum shaderType = static_cast<Core::ShaderType::Enum>(shaderIndex);
        NWB_ASSERT(IsSupportedMaterialShaderStage(shaderType));

        AppendPOD(outBinary, shaderType);
        AppendPOD(outBinary, shaderAsset.name().hash());
    }

    AppendPOD(outBinary, PackMaterialFlags(material.transparent(), material.twoSided(), material.refractive()));
    AppendPOD(outBinary, material.shadingModelId());
    AppendPOD(outBinary, material.surfaceDispatchId());

    // Optional AVBOIT pixel shaders (accumulate, occupancy, extinction) serialize as a presence flag plus shader name hash.
    const auto appendOptionalAvboitPixelShader = [&outBinary](const Core::Assets::AssetRef<PixelShader>& shaderRef){
        if(shaderRef.valid()){
            AppendPOD(outBinary, static_cast<u32>(1u));
            AppendPOD(outBinary, shaderRef.name().hash());
        }
        else
            AppendPOD(outBinary, static_cast<u32>(0u));
    };
    appendOptionalAvboitPixelShader(material.avboitAccumulatePixelShader());
    appendOptionalAvboitPixelShader(material.avboitOccupancyPixelShader());
    appendOptionalAvboitPixelShader(material.avboitExtinctionPixelShader());

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

