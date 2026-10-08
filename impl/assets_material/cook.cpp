// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookEntry> ParseMaterialCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::ParseMaterialMeta(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        arena,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateMaterialCookInterfaces(
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::ValidateMaterialCookInterfaces(materialBindEntries, materialEntries, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookString> BuildMaterialBindIncludeSource(
    MaterialCookArena& arena,
    const MaterialBindEntry& entry,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::BuildMaterialBindIncludeSourceImpl(arena, entry, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> EmitMaterialBindIncludes(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::EmitMaterialBindIncludes(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialBindEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindDependency> ResolveMaterialBindDependencyInterface(
    MaterialCookArena& arena,
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const MaterialCookVector<Path>& dependencies,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::ResolveMaterialBindDependencyInterface(
        arena,
        shaderName,
        materialBindIncludeRoot,
        dependencies,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using OptionalAvboitPixelShaderSetter = void(Material::*)(const Core::Assets::AssetRef<PixelShader>&);

static bool SetOptionalAvboitPixelShader(
    const MaterialCookEntry& materialEntry,
    const AStringView shaderNameText,
    const AStringView passLabel,
    const OptionalAvboitPixelShaderSetter setter,
    Material& outMaterial
){
    if(shaderNameText.empty())
        return true;

    const Name shaderName = ToName(shaderNameText);
    if(!shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has an invalid AVBOIT {} pixel shader name")
            , StringConvert(AStringView(materialEntry.virtualPath))
            , StringConvert(passLabel)
        );
        return false;
    }

    Core::Assets::AssetRef<PixelShader> shaderRef;
    shaderRef.virtualPath = shaderName;
    (outMaterial.*setter)(shaderRef);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Material> BuildMaterialAsset(const MaterialCookEntry& materialEntry, Core::Assets::AssetArena& arena){
    if(materialEntry.materialInterface.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing required material interface")
            , StringConvert(AStringView(materialEntry.virtualPath))
        );
        return MakeUnexpected(Failure{});
    }
    if(materialEntry.typedLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface material '{}' is missing typed layout data")
            , StringConvert(AStringView(materialEntry.virtualPath))
        );
        return MakeUnexpected(Failure{});
    }
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has empty shader variant")
            , StringConvert(AStringView(materialEntry.virtualPath))
        );
        return MakeUnexpected(Failure{});
    }

    Material asset(arena, Name(AStringView(materialEntry.virtualPath)));
    asset.setShaderVariant(materialEntry.shaderVariant);
    asset.setMaterialInterface(Name(AStringView(materialEntry.materialInterface)));
    asset.setShadingModelId(materialEntry.shadingModelId);
    asset.setSurfaceDispatchId(materialEntry.surfaceDispatchId);
    if(!SetOptionalAvboitPixelShader(
        materialEntry,
        materialEntry.avboitAccumulatePixelShaderName,
        AStringView("accumulate"),
        &Material::setAvboitAccumulatePixelShader,
        asset
    ))
        return MakeUnexpected(Failure{});
    if(!SetOptionalAvboitPixelShader(
        materialEntry,
        materialEntry.avboitOccupancyPixelShaderName,
        AStringView("occupancy"),
        &Material::setAvboitOccupancyPixelShader,
        asset
    ))
        return MakeUnexpected(Failure{});
    if(!SetOptionalAvboitPixelShader(
        materialEntry,
        materialEntry.avboitExtinctionPixelShaderName,
        AStringView("extinction"),
        &Material::setAvboitExtinctionPixelShader,
        asset
    ))
        return MakeUnexpected(Failure{});
    asset.setTransparent(materialEntry.transparent);
    asset.setTwoSided(materialEntry.twoSided);
    asset.setRefractive(materialEntry.refractive);
    if(!HasValidMaterialAvboitPixelShaderContract(
        asset.transparent(),
        asset.avboitAccumulatePixelShader(),
        asset.avboitOccupancyPixelShader(),
        asset.avboitExtinctionPixelShader()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' AVBOIT pixel shaders must be present if and only if it is transparent")
            , StringConvert(AStringView(materialEntry.virtualPath))
        );
        return MakeUnexpected(Failure{});
    }
    asset.setTypedLayout(
        materialEntry.typedLayoutHash,
        materialEntry.typedLayoutBlocks,
        materialEntry.typedLayoutFields,
        materialEntry.typedBlockBytes
    );
    asset.setResourceReferences(materialEntry.resourceReferences);

    for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
        if(!asset.setShaderForStage(shaderType, shaderAsset)){
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: invalid shader stage '{}' for '{}'")
                , StringConvert(stageName.resolvedText())
                , StringConvert(AStringView(materialEntry.virtualPath))
            );
            return MakeUnexpected(Failure{});
        }
    }

    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssignMaterialShadingModelIds(
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::AssignMaterialShadingModelIdsImpl(materialEntries, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> EmitDeferredBxdfDispatchModule(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::EmitDeferredBxdfDispatchModuleImpl(
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> EmitShadowSurfaceDispatchModule(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::EmitShadowSurfaceDispatchModuleImpl(
        cacheDirectory,
        configurationSafeName,
        materialBindEntries,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::EmitMaterialPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        sharedMeshShaderName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitAccumulatePixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::EmitMaterialAvboitAccumulatePixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitOccupancyPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::EmitMaterialAvboitOccupancyPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitExtinctionPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return MaterialCookDetail::EmitMaterialAvboitExtinctionPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("MaterialAssetCodec::serialize")))
        return false;

    const Material& material = static_cast<const Material&>(asset);
    if(!material.checkVirtualPath(NWB_TEXT("MaterialAssetCodec::serialize")))
        return false;
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
    if(!HasValidMaterialAvboitPixelShaderContract(
        material.transparent(),
        material.avboitAccumulatePixelShader(),
        material.avboitOccupancyPixelShader(),
        material.avboitExtinctionPixelShader()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: AVBOIT pixel shaders must be present if and only if the material is transparent"));
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
    if(material.resourceReferences().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material resource reference count exceeds u32 range"));
        return false;
    }
    if(MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(
        material.typedLayoutBlocks(),
        material.typedLayoutFields()
    ) != material.typedLayoutHash()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed layout hash mismatch"));
        return false;
    }
    const auto expectedTypedBlockByteSize = MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(material.typedLayoutBlocks());
    if(!expectedTypedBlockByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    if(*expectedTypedBlockByteSize != material.typedBlockBytes().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    if(!MaterialBinaryPayload::ValidateMaterialResourceReferences(
        material.typedLayoutBlocks(),
        material.typedLayoutFields(),
        material.resourceReferences()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material resource references do not match typed layout"));
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
        && AddBinaryRepeatedReserveBytes(
            reserveBytes,
            material.resourceReferences().size(),
            MaterialBinaryPayload::s_ResourceReferenceBytes
        )
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaderCount(), MaterialBinaryPayload::s_ShaderEntryBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // material flags
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // shading model id
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // surface dispatch id
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
    AppendPOD(outBinary, static_cast<u32>(material.resourceReferences().size()));
    for(const MaterialResourceReference& resourceReference : material.resourceReferences()){
        const Name& resourceName =
            resourceReference.resourceKind == MaterialResourceKind::SampledImage2D
            ? resourceReference.textureAsset.name()
            : resourceReference.samplerAsset.name()
        ;
        MaterialBinaryPayload::MaterialResourceReferenceBinary resourceReferenceBinary;
        resourceReferenceBinary.blockNameHash = resourceReference.blockName.hash();
        resourceReferenceBinary.fieldNameHash = resourceReference.fieldName.hash();
        resourceReferenceBinary.resourceNameHash = resourceName.hash();
        resourceReferenceBinary.resourceKind = static_cast<u32>(resourceReference.resourceKind);
        resourceReferenceBinary.constantByteOffset = resourceReference.constantByteOffset;
        AppendPOD(outBinary, resourceReferenceBinary);
    }
    AppendPOD(outBinary, material.stageShaderCount());

    const Material::StageShaderArray& stageShaders = material.stageShaders();
    for(usize shaderIndex = 0u; shaderIndex < stageShaders.size(); ++shaderIndex){
        const Core::Assets::AssetRef<IShader>& shaderAsset = stageShaders[shaderIndex];
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
    AppendPOD(outBinary, material.surfaceDispatchId());

    // Optional per-material AVBOIT pixel shaders (accumulate, occupancy, extinction): each a presence flag plus
    // shader name hash, present only for surface-authored transparent materials and read back in this order.
    const auto appendOptionalAvboitPixelShader = [&outBinary](const Core::Assets::AssetRef<PixelShader>& shaderRef){
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

