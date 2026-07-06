// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMaterialCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::ParseMaterialMeta(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        outEntry,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateMaterialCookInterfaces(
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::ValidateMaterialCookInterfaces(materialBindEntries, materialEntries, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMaterialBindIncludeSource(
    MaterialCookArena& arena,
    const MaterialBindEntry& entry,
    MaterialCookString& outSource,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::BuildMaterialBindIncludeSourceImpl(arena, entry, outSource, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialBindIncludes(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialBindIncludes(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialBindEntries,
        outIncludeRoot,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const MaterialCookVector<Path>& dependencies,
    MaterialCookString& outInterfacePath,
    Name& outInterfaceName,
    bool& outDependsOnMaterialBind,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::ResolveMaterialBindDependencyInterface(
        shaderName,
        materialBindIncludeRoot,
        dependencies,
        outInterfacePath,
        outInterfaceName,
        outDependsOnMaterialBind,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using OptionalAvboitPixelShaderSetter = void(Material::*)(const Core::Assets::AssetRef<Shader>&);

static bool SetOptionalAvboitPixelShader(
    const MaterialCookEntry& materialEntry,
    const MaterialCookString& shaderNameText,
    const AStringView passLabel,
    const OptionalAvboitPixelShaderSetter setter,
    Material& outMaterial
){
    if(shaderNameText.empty())
        return true;

    const Name shaderName = ToName(AStringView(shaderNameText));
    if(!shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has an invalid AVBOIT {} pixel shader name")
            , StringConvert(materialEntry.virtualPath.c_str())
            , StringConvert(passLabel)
        );
        return false;
    }

    Core::Assets::AssetRef<Shader> shaderRef;
    shaderRef.virtualPath = shaderName;
    (outMaterial.*setter)(shaderRef);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMaterialAsset(const MaterialCookEntry& materialEntry, Material& outMaterial){
    Core::Assets::AssetArena& arena = materialEntry.shaderVariant.get_allocator().arena();
    if(materialEntry.materialInterface.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing required material interface")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    if(materialEntry.typedLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface material '{}' is missing typed layout data")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has empty shader variant")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }

    outMaterial = Material(arena, Name(AStringView(materialEntry.virtualPath)));
    outMaterial.setShaderVariant(materialEntry.shaderVariant);
    outMaterial.setMaterialInterface(Name(AStringView(materialEntry.materialInterface)));
    outMaterial.setShadingModelId(materialEntry.shadingModelId);
    outMaterial.setShadowTransmittanceModelId(materialEntry.shadowTransmittanceModelId);
    if(!SetOptionalAvboitPixelShader(
        materialEntry,
        materialEntry.avboitAccumulatePixelShaderName,
        AStringView("accumulate"),
        &Material::setAvboitAccumulatePixelShader,
        outMaterial
    ))
        return false;
    if(!SetOptionalAvboitPixelShader(
        materialEntry,
        materialEntry.avboitOccupancyPixelShaderName,
        AStringView("occupancy"),
        &Material::setAvboitOccupancyPixelShader,
        outMaterial
    ))
        return false;
    if(!SetOptionalAvboitPixelShader(
        materialEntry,
        materialEntry.avboitExtinctionPixelShaderName,
        AStringView("extinction"),
        &Material::setAvboitExtinctionPixelShader,
        outMaterial
    ))
        return false;
    outMaterial.setTransparent(materialEntry.transparent);
    outMaterial.setTwoSided(materialEntry.twoSided);
    outMaterial.setRefractive(materialEntry.refractive);
    outMaterial.setTypedLayout(
        materialEntry.typedLayoutHash,
        materialEntry.typedLayoutBlocks,
        materialEntry.typedLayoutFields,
        materialEntry.typedBlockBytes
    );

    for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
        if(!outMaterial.setShaderForStage(shaderType, shaderAsset)){
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: invalid shader stage '{}' for '{}'")
                , StringConvert(stageName.c_str())
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssignMaterialShadingModelIds(
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::AssignMaterialShadingModelIdsImpl(materialEntries, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitDeferredBxdfDispatchModule(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitDeferredBxdfDispatchModuleImpl(
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outIncludeRoot,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitShadowTransmittanceDispatchModule(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitShadowTransmittanceDispatchModuleImpl(
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outIncludeRoot,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        sharedMeshShaderName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialAvboitAccumulatePixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialAvboitAccumulatePixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialAvboitOccupancyPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialAvboitOccupancyPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialAvboitExtinctionPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialAvboitExtinctionPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Material::s_AssetTypeText)
        );
        return false;
    }

    const Material& material = static_cast<const Material&>(asset);
    if(!material.virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: virtual path is empty"));
        return false;
    }
    if(material.stageShaderCount() == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material has no shader stages"));
        return false;
    }
    if(!material.materialInterface()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material interface is required"));
        return false;
    }
    if(material.shaderVariant().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is empty"));
        return false;
    }
    if(material.typedLayoutHash() == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: interface material is missing typed layout data"));
        return false;
    }
    if(material.typedLayoutBlocks().size() > Limit<u32>::s_Max || material.typedLayoutFields().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed layout count exceeds u32 range"));
        return false;
    }
    if(material.typedBlockBytes().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block byte count exceeds u32 range"));
        return false;
    }
    if(MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(
        material.typedLayoutBlocks(),
        material.typedLayoutFields()
    ) != material.typedLayoutHash()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed layout hash mismatch"));
        return false;
    }
    usize expectedTypedBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(
        material.typedLayoutBlocks(),
        expectedTypedBlockByteSize
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    if(expectedTypedBlockByteSize != material.typedBlockBytes().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    usize reserveBytes = sizeof(u32); // magic
    bool canReserve = AddBinaryStringReserveBytes(reserveBytes, AStringView(material.shaderVariant()))
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u64))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(
            reserveBytes,
            material.typedLayoutBlocks().size(),
            MaterialBinaryPayload::s_TypedLayoutBlockBytes
        )
        && AddBinaryRepeatedReserveBytes(
            reserveBytes,
            material.typedLayoutFields().size(),
            MaterialBinaryPayload::s_TypedLayoutFieldBytes
        )
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, material.typedBlockBytes().size())
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaderCount(), MaterialBinaryPayload::s_ShaderEntryBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // material flags
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // shading model id
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // shadow transmittance model id
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // AVBOIT accumulate pixel shader presence flag
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash)) // optional AVBOIT accumulate pixel shader name
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // AVBOIT occupancy pixel shader presence flag
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash)) // optional AVBOIT occupancy pixel shader name
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // AVBOIT extinction pixel shader presence flag
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash)) // optional AVBOIT extinction pixel shader name
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    AppendPOD(outBinary, MaterialBinaryPayload::s_MaterialMagic);
    if(!AppendString(outBinary, AStringView(material.shaderVariant()))){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is too long"));
        return false;
    }
    AppendPOD(outBinary, material.materialInterface().hash());
    AppendPOD(outBinary, material.typedLayoutHash());
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutBlocks().size()));
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutFields().size()));
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        MaterialBinaryPayload::MaterialTypedLayoutBlockBinary blockBinary;
        blockBinary.blockNameHash = block.blockName.hash();
        blockBinary.blockClass = static_cast<u32>(block.blockClass);
        blockBinary.fieldBegin = block.fieldBegin;
        blockBinary.fieldCount = block.fieldCount;
        blockBinary.byteSize = block.byteSize;
        AppendPOD(outBinary, blockBinary);
    }
    for(const MaterialTypedLayoutField& field : material.typedLayoutFields()){
        MaterialBinaryPayload::MaterialTypedLayoutFieldBinary fieldBinary;
        fieldBinary.fieldNameHash = field.fieldName.hash();
        fieldBinary.fieldType = static_cast<u32>(field.fieldType);
        fieldBinary.offset = field.offset;
        fieldBinary.defaultValue = field.defaultValue;
        AppendPOD(outBinary, fieldBinary);
    }
    AppendPOD(outBinary, static_cast<u32>(material.typedBlockBytes().size()));
    BinaryDetail::AppendBytesNoReserveUnchecked(
        outBinary,
        material.typedBlockBytes().data(),
        material.typedBlockBytes().size()
    );
    AppendPOD(outBinary, material.stageShaderCount());

    const Material::StageShaderArray& stageShaders = material.stageShaders();
    for(usize shaderIndex = 0; shaderIndex < stageShaders.size(); ++shaderIndex){
        const Core::Assets::AssetRef<Shader>& shaderAsset = stageShaders[shaderIndex];
        if(!shaderAsset.valid())
            continue;

        const Core::ShaderType::Enum shaderType = static_cast<Core::ShaderType::Enum>(shaderIndex);
        if(!Core::ShaderType::IsValid(shaderType)){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader stage index {} is invalid"), shaderIndex);
            return false;
        }

        AppendPOD(outBinary, shaderType);
        AppendPOD(outBinary, shaderAsset.name().hash());
    }

    u32 materialFlags = 0u;
    if(material.transparent())
        materialFlags |= MaterialBinaryPayload::MaterialFlag::Transparent;
    if(material.twoSided())
        materialFlags |= MaterialBinaryPayload::MaterialFlag::TwoSided;
    if(material.refractive())
        materialFlags |= MaterialBinaryPayload::MaterialFlag::Refractive;
    AppendPOD(outBinary, materialFlags);
    AppendPOD(outBinary, material.shadingModelId());
    AppendPOD(outBinary, material.shadowTransmittanceModelId());

    // Optional per-material AVBOIT pass pixel shaders: accumulate, then occupancy, then extinction -- each a
    // presence flag + the shader name hash, mirroring a stage-shader entry. Present only for a surface-authored
    // transparent material; loadBinary reads them back in this order. All three carry the material's SAME
    // shader-decided surface.renderCoverage, so the renderer can bind a per-material PS for every AVBOIT pass.
    const auto appendOptionalAvboitPixelShader = [&outBinary](const Core::Assets::AssetRef<Shader>& shaderRef){
        if(shaderRef.valid()){
            AppendPOD(outBinary, static_cast<u32>(1u));
            AppendPOD(outBinary, shaderRef.name().hash());
        }
        else{
            AppendPOD(outBinary, static_cast<u32>(0u));
        }
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

