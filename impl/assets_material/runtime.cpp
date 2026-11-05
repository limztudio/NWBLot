// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/common/log.h>
#include <core/assets/auto_registration.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_material_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_MaterialLoadBinaryContext = NWB_TEXT("Material::loadBinary");

NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_MaterialAssetCodecAutoRegistrar, MaterialAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialTypedLayoutPayload final{
    u64 layoutHash = 0u;
    Material::TypedLayoutBlockVector blocks;
    Material::TypedLayoutFieldVector fields;
    Material::TypedBlockByteVector blockBytes;
    Material::ResourceReferenceVector resourceReferences;


public:
    explicit MaterialTypedLayoutPayload(Core::Assets::AssetArena& arena)noexcept
        : blocks(arena)
        , fields(arena)
        , blockBytes(arena)
        , resourceReferences(arena)
    {}
};

static Expected<MaterialTypedLayoutPayload> ReadMaterialTypedLayout(const Span<const u8> binary, Core::Assets::AssetArena& arena, usize& inOutCursor){
    MaterialTypedLayoutPayload payload(arena);

    const auto layoutHashResult = ReadPOD<u64>(binary, inOutCursor);
    if(!layoutHashResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing typed layout header"));
        return MakeUnexpected(Failure{});
    }
    const u64 layoutHash = *layoutHashResult;
    const auto blockCountResult = ReadPOD<u32>(binary, inOutCursor);
    if(!blockCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing typed layout header"));
        return MakeUnexpected(Failure{});
    }
    const u32 blockCount = *blockCountResult;
    const auto fieldCountResult = ReadPOD<u32>(binary, inOutCursor);
    if(!fieldCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing typed layout header"));
        return MakeUnexpected(Failure{});
    }
    const u32 fieldCount = *fieldCountResult;

    // Successful reads keep the cursor inside the binary.
    if(blockCount > (binary.size() - inOutCursor) / s_MaterialTypedLayoutBlockBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed layout block count exceeds available data"));
        return MakeUnexpected(Failure{});
    }
    payload.blocks.reserve(blockCount);
    for(u32 i = 0u; i < blockCount; ++i){
        const auto blockBinaryResult = ReadPOD<MaterialTypedLayoutBlockBinary>(binary, inOutCursor);
        if(!blockBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed layout block at index {}"), i);
            return MakeUnexpected(Failure{});
        }
        payload.blocks.push_back(UnpackMaterialTypedLayoutBlockBinary(*blockBinaryResult));
    }

    if(fieldCount > (binary.size() - inOutCursor) / s_MaterialTypedLayoutFieldBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed layout field count exceeds available data"));
        return MakeUnexpected(Failure{});
    }
    payload.fields.reserve(fieldCount);
    for(u32 i = 0u; i < fieldCount; ++i){
        const auto fieldBinaryResult = ReadPOD<MaterialTypedLayoutFieldBinary>(binary, inOutCursor);
        if(!fieldBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed layout field at index {}"), i);
            return MakeUnexpected(Failure{});
        }
        payload.fields.push_back(UnpackMaterialTypedLayoutFieldBinary(*fieldBinaryResult));
    }

    u32 blockByteCount = 0u;
    const auto blockByteCountResult = ReadPOD<u32>(binary, inOutCursor);
    if(!blockByteCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing typed block byte count"));
        return MakeUnexpected(Failure{});
    }
    blockByteCount = *blockByteCountResult;
    if(blockByteCount > binary.size() - inOutCursor){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed block byte count exceeds available data"));
        return MakeUnexpected(Failure{});
    }

    payload.blockBytes.resize(blockByteCount);
    if(!ReadBytes(binary, blockByteCount, inOutCursor, payload.blockBytes.data())){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed block bytes"));
        return MakeUnexpected(Failure{});
    }

    u32 resourceReferenceCount = 0u;
    const auto resourceReferenceCountResult = ReadPOD<u32>(binary, inOutCursor);
    if(!resourceReferenceCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material resource reference count"));
        return MakeUnexpected(Failure{});
    }
    resourceReferenceCount = *resourceReferenceCountResult;
    if(resourceReferenceCount > (binary.size() - inOutCursor) / s_MaterialResourceReferenceBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material resource reference count exceeds available data"));
        return MakeUnexpected(Failure{});
    }
    payload.resourceReferences.reserve(resourceReferenceCount);
    for(u32 i = 0u; i < resourceReferenceCount; ++i){
        const auto resourceReferenceBinaryResult = ReadPOD<MaterialResourceReferenceBinary>(binary, inOutCursor);
        if(!resourceReferenceBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed material resource reference at index {}"), i);
            return MakeUnexpected(Failure{});
        }
        const Expected<MaterialResourceReference> resourceReference = TryUnpackMaterialResourceReferenceBinary(*resourceReferenceBinaryResult);
        if(!resourceReference){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material resource reference at index {} has an invalid asset identity"), i);
            return MakeUnexpected(Failure{});
        }
        payload.resourceReferences.push_back(*resourceReference);
    }

    if(!LogAndValidateMaterialTypedLayout(layoutHash, payload.blocks, payload.fields, payload.blockBytes, s_MaterialLoadBinaryContextText))
        return MakeUnexpected(Failure{});
    if(!ValidateMaterialResourceReferences(payload.blocks, payload.fields, payload.resourceReferences)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material resource references do not match typed layout"));
        return MakeUnexpected(Failure{});
    }

    payload.layoutHash = layoutHash;
    return payload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Material::loadBinary(const Span<const u8> binary){
    if(!checkVirtualPath(__hidden_assets_material_runtime::s_MaterialLoadBinaryContext))
        return false;

    usize cursor = 0;
    u32 magic = 0;
    const auto magicResult = ReadPOD<u32>(binary, cursor);
    if(!magicResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing magic"));
        return false;
    }
    magic = *magicResult;
    if(magic != s_MaterialBinaryMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid magic"));
        return false;
    }

    Material candidate(m_shaderVariant.get_allocator().arena(), virtualPath());
    const auto shaderVariant = ReadString(binary, cursor);
    if(!shaderVariant){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader variant"));
        return false;
    }
    candidate.m_shaderVariant.assign(shaderVariant->data(), shaderVariant->size());
    if(candidate.m_shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader variant is empty"));
        return false;
    }

    NameHash materialInterfaceHash;
    const auto materialInterfaceHashResult = ReadPOD<NameHash>(binary, cursor);
    if(!materialInterfaceHashResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material interface"));
        return false;
    }
    materialInterfaceHash = *materialInterfaceHashResult;
    candidate.m_materialInterface = Name(materialInterfaceHash);
    if(!candidate.m_materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material interface is required"));
        return false;
    }

    auto typedLayout = __hidden_assets_material_runtime::ReadMaterialTypedLayout(binary, m_shaderVariant.get_allocator().arena(), cursor);
    if(!typedLayout)
        return false;
    candidate.m_typedLayoutHash = typedLayout->layoutHash;
    candidate.m_typedLayoutBlocks = Move(typedLayout->blocks);
    candidate.m_typedLayoutFields = Move(typedLayout->fields);
    candidate.m_typedBlockBytes = Move(typedLayout->blockBytes);
    candidate.m_resourceReferences = Move(typedLayout->resourceReferences);
    NWB_ASSERT(candidate.m_typedLayoutHash != 0u);

    u32 shaderCount = 0;
    const auto shaderCountResult = ReadPOD<u32>(binary, cursor);
    if(!shaderCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader count"));
        return false;
    }
    shaderCount = *shaderCountResult;
    if(!ValidateMaterialBinaryShaderCount(shaderCount, static_cast<u32>(Core::ShaderType::Count), binary.size() - cursor, s_MaterialShaderEntryBytes))
        return false;

    for(u32 i = 0u; i < shaderCount; ++i){
        Core::ShaderType::Enum shaderType = Core::ShaderType::Invalid;
        NameHash shaderNameHash;
        const auto shaderTypeResult = ReadPOD<Core::ShaderType::Enum>(binary, cursor);
        if(!shaderTypeResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed shader stage at index {}"), i);
            return false;
        }
        shaderType = *shaderTypeResult;
        const auto shaderNameHashResult = ReadPOD<NameHash>(binary, cursor);
        if(!shaderNameHashResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed shader stage at index {}"), i);
            return false;
        }
        shaderNameHash = *shaderNameHashResult;

        const Name shaderName(shaderNameHash);
        Core::Assets::AssetRef<IShader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader stage entries must not be empty"));
            return false;
        }
        if(!IsSupportedMaterialShaderStage(shaderType)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader stage {} is not a material stage; only mesh and pixel are allowed"), static_cast<u32>(shaderType));
            return false;
        }

        const usize shaderIndex = Core::ShaderType::ToIndex(shaderType);
        if(candidate.m_stageShaders[shaderIndex].valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: duplicate shader stage index {}"), shaderIndex);
            return false;
        }

        candidate.m_stageShaders[shaderIndex] = shaderAsset;
        ++candidate.m_stageShaderCount;
    }

    u32 materialFlags = 0u;
    const auto materialFlagsResult = ReadPOD<u32>(binary, cursor);
    if(!materialFlagsResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material flags"));
        return false;
    }
    materialFlags = *materialFlagsResult;
    if(!ValidateMaterialBinaryFlags(materialFlags, candidate.m_transparent, candidate.m_twoSided, candidate.m_refractive))
        return false;

    const auto shadingModelIdResult = ReadPOD<u32>(binary, cursor);
    if(!shadingModelIdResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shading model id"));
        return false;
    }
    candidate.m_shadingModelId = *shadingModelIdResult;

    const auto surfaceDispatchIdResult = ReadPOD<u32>(binary, cursor);
    if(!surfaceDispatchIdResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing surface dispatch id"));
        return false;
    }
    candidate.m_surfaceDispatchId = *surfaceDispatchIdResult;

    // Per-material AVBOIT shaders (transparent only): accumulate, occupancy, extinction as flag plus hash.
    const auto readOptionalAvboitPixelShader = [&](const AStringView passLabel) -> Expected<Core::Assets::AssetRef<PixelShader>>{
        u32 hasShader = 0u;
        const auto hasShaderResult = ReadPOD<u32>(binary, cursor);
        if(!hasShaderResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing AVBOIT {} pixel shader presence flag"), ConvertString(passLabel));
            return MakeUnexpected(Failure{});
        }
        hasShader = *hasShaderResult;
        if(!ValidateMaterialBinaryAvboitPresenceFlag(passLabel, hasShader))
            return MakeUnexpected(Failure{});
        if(hasShader == 1u){
            NameHash shaderNameHash;
            const auto shaderNameHashResult = ReadPOD<NameHash>(binary, cursor);
            if(!shaderNameHashResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing AVBOIT {} pixel shader name"), ConvertString(passLabel));
                return MakeUnexpected(Failure{});
            }
            shaderNameHash = *shaderNameHashResult;
            Core::Assets::AssetRef<PixelShader> shaderRef;
            shaderRef.virtualPath = Name(shaderNameHash);
            if(!ValidateMaterialBinaryAvboitShaderRef(passLabel, shaderRef.valid()))
                return MakeUnexpected(Failure{});
            return shaderRef;
        }
        return Core::Assets::AssetRef<PixelShader>{};
    };
    const auto accumulateShader = readOptionalAvboitPixelShader(s_MaterialAvboitAccumulatePassLabel);
    if(!accumulateShader)
        return false;
    candidate.m_avboitAccumulatePixelShader = *accumulateShader;
    const auto occupancyShader = readOptionalAvboitPixelShader(s_MaterialAvboitOccupancyPassLabel);
    if(!occupancyShader)
        return false;
    candidate.m_avboitOccupancyPixelShader = *occupancyShader;
    const auto extinctionShader = readOptionalAvboitPixelShader(s_MaterialAvboitExtinctionPassLabel);
    if(!extinctionShader)
        return false;
    candidate.m_avboitExtinctionPixelShader = *extinctionShader;
    if(!candidate.validatePayload(__hidden_assets_material_runtime::s_MaterialLoadBinaryContext, s_MaterialLoadBinaryContextText))
        return false;

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: trailing bytes detected"));
        return false;
    }

    *this = Move(candidate);
    return true;
}

b8 Material::validatePayload(const TStringView failureContext, const AStringView failureContextText)const{
    if(!checkVirtualPath(failureContext))
        return false;
    return ValidateMaterialPayloadData(
        m_stageShaderCount,
        m_materialInterface,
        AStringView(m_shaderVariant),
        m_transparent,
        m_avboitAccumulatePixelShader,
        m_avboitOccupancyPixelShader,
        m_avboitExtinctionPixelShader,
        m_typedLayoutHash,
        m_typedLayoutBlocks,
        m_typedLayoutFields,
        m_typedBlockBytes,
        m_resourceReferences,
        failureContextText
    );
}

void Material::setTypedLayout(const u64 layoutHash, const Span<const MaterialTypedLayoutBlock> blocks, const Span<const MaterialTypedLayoutField> fields, const Span<const u8> blockBytes){
    m_typedLayoutHash = layoutHash;
    m_typedLayoutBlocks.reserve(blocks.size());
    m_typedLayoutBlocks.assign(blocks.begin(), blocks.end());
    m_typedLayoutFields.reserve(fields.size());
    m_typedLayoutFields.assign(fields.begin(), fields.end());
    m_typedBlockBytes.reserve(blockBytes.size());
    m_typedBlockBytes.assign(blockBytes.begin(), blockBytes.end());
}

void Material::setResourceReferences(const Span<const MaterialResourceReference> resourceReferences){
    m_resourceReferences.reserve(resourceReferences.size());
    m_resourceReferences.assign(resourceReferences.begin(), resourceReferences.end());
}

b8 Material::setShaderForStage(const Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<IShader>& shaderAsset)noexcept{
    if(!IsSupportedMaterialShaderStage(shaderType) || !shaderAsset.valid())
        return false;

    Core::Assets::AssetRef<IShader>& storedShader = m_stageShaders[Core::ShaderType::ToIndex(shaderType)];
    if(!storedShader.valid())
        ++m_stageShaderCount;

    storedShader = shaderAsset;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
