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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Material::loadBinary(const Span<const u8> binary){
    if(!checkVirtualPath(__hidden_assets_material_runtime::s_MaterialLoadBinaryContext))
        return false;

    usize cursor = 0u;
    Material candidate(m_shaderVariant.get_allocator().arena(), virtualPath());
    if(!ReadMaterialBinaryPreamble(binary, cursor, candidate.m_shaderVariant, candidate.m_materialInterface))
        return false;
    if(!ReadMaterialTypedLayout(
        binary,
        cursor,
        candidate.m_typedLayoutHash,
        candidate.m_typedLayoutBlocks,
        candidate.m_typedLayoutFields,
        candidate.m_typedBlockBytes,
        candidate.m_resourceReferences
    ))
        return false;
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

    if(!ReadMaterialBinaryFooter(
        binary,
        cursor,
        candidate.m_transparent,
        candidate.m_twoSided,
        candidate.m_refractive,
        candidate.m_shadingModelId,
        candidate.m_surfaceDispatchId,
        candidate.m_avboitAccumulatePixelShader,
        candidate.m_avboitOccupancyPixelShader,
        candidate.m_avboitExtinctionPixelShader
    ))
        return false;
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
    AssignMaterialTypedLayoutPayload(layoutHash, blocks, fields, blockBytes, m_typedLayoutHash, m_typedLayoutBlocks, m_typedLayoutFields, m_typedBlockBytes);
}

void Material::setResourceReferences(const Span<const MaterialResourceReference> resourceReferences){
    AssignMaterialResourceReferencesPayload(resourceReferences, m_resourceReferences);
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
