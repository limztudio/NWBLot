// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/common/log.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCodecAutoRegistrar s_MaterialAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<MaterialAssetCodec>);


static bool ValidateMaterialTypedLayout(
    const u64 layoutHash,
    const Material::TypedLayoutBlockVector& blocks,
    const Material::TypedLayoutFieldVector& fields,
    const Material::TypedBlockByteVector& blockBytes,
    const tchar* failureContext
){
    if(blocks.empty() && fields.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout is empty"), failureContext);
        return false;
    }
    if(layoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout has zero hash"), failureContext);
        return false;
    }
    if(blocks.empty() || fields.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout blocks and fields must both be present"), failureContext);
        return false;
    }

    u32 nextFieldBegin = 0u;
    for(usize blockIndex = 0u; blockIndex < blocks.size(); ++blockIndex){
        const MaterialTypedLayoutBlock& block = blocks[blockIndex];
        if(!block.blockName){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout block {} has empty name"), failureContext, blockIndex);
            return false;
        }
        if(!IsValidMaterialBlockClass(block.blockClass)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout block {} has invalid class {}")
                , failureContext
                , blockIndex
                , static_cast<u32>(block.blockClass)
            );
            return false;
        }
        if(block.fieldBegin != nextFieldBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout block {} has non-contiguous field range")
                , failureContext
                , blockIndex
            );
            return false;
        }
        if(block.fieldCount == 0u || block.fieldBegin > fields.size() || block.fieldCount > fields.size() - block.fieldBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout block {} field range exceeds field count")
                , failureContext
                , blockIndex
            );
            return false;
        }

        u32 expectedOffset = 0u;
        for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
            const usize fieldIndex = static_cast<usize>(block.fieldBegin) + fieldOffset;
            const MaterialTypedLayoutField& field = fields[fieldIndex];
            if(!field.fieldName){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} has empty name"), failureContext, fieldIndex);
                return false;
            }
            if(!IsValidMaterialLayoutFieldType(field.fieldType)){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} has invalid type {}")
                    , failureContext
                    , fieldIndex
                    , static_cast<u32>(field.fieldType)
                );
                return false;
            }

            u32 expectedFieldOffset = 0u;
            if(!AlignMaterialLayoutFieldOffset(expectedOffset, field.fieldType, expectedFieldOffset)){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} alignment overflows")
                    , failureContext
                    , fieldIndex
                );
                return false;
            }
            if(field.offset != expectedFieldOffset){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} has misaligned offset")
                    , failureContext
                    , fieldIndex
                );
                return false;
            }

            const u32 fieldByteSize = MaterialLayoutFieldByteSize(field.fieldType);
            if(fieldByteSize == 0u || expectedFieldOffset > Limit<u32>::s_Max - fieldByteSize){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} byte size overflows"), failureContext, fieldIndex);
                return false;
            }
            expectedOffset = expectedFieldOffset + fieldByteSize;
        }

        u32 expectedBlockByteSize = 0u;
        if(!AlignMaterialLayoutBlockByteSize(expectedOffset, expectedBlockByteSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout block {} byte size overflows")
                , failureContext
                , blockIndex
            );
            return false;
        }
        if(expectedBlockByteSize != block.byteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout block {} byte size does not match its fields")
                , failureContext
                , blockIndex
            );
            return false;
        }

        nextFieldBegin += block.fieldCount;
    }

    if(nextFieldBegin != fields.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout has unowned fields"), failureContext);
        return false;
    }

    const u64 computedHash = MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(blocks, fields);
    if(computedHash != layoutHash){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout hash mismatch"), failureContext);
        return false;
    }

    usize expectedBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(blocks, expectedBlockByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed block byte size overflows"), failureContext);
        return false;
    }
    if(blockBytes.size() != expectedBlockByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed block byte count does not match typed layout"), failureContext);
        return false;
    }

    return true;
}

static bool ReadMaterialTypedLayout(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    u64& outLayoutHash,
    Material::TypedLayoutBlockVector& outBlocks,
    Material::TypedLayoutFieldVector& outFields,
    Material::TypedBlockByteVector& outBlockBytes
){
    outLayoutHash = 0u;
    outBlocks.clear();
    outFields.clear();
    outBlockBytes.clear();

    u32 blockCount = 0u;
    u32 fieldCount = 0u;
    if(
        !ReadPOD(binary, inOutCursor, outLayoutHash)
        || !ReadPOD(binary, inOutCursor, blockCount)
        || !ReadPOD(binary, inOutCursor, fieldCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing typed layout header"));
        return false;
    }

    if(
        inOutCursor > binary.size()
        || blockCount > (binary.size() - inOutCursor) / MaterialBinaryPayload::s_TypedLayoutBlockBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed layout block count exceeds available data"));
        return false;
    }
    outBlocks.reserve(blockCount);
    for(u32 i = 0u; i < blockCount; ++i){
        MaterialBinaryPayload::MaterialTypedLayoutBlockBinary blockBinary;
        if(!ReadPOD(binary, inOutCursor, blockBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed layout block at index {}"), i);
            return false;
        }

        MaterialTypedLayoutBlock block;
        block.blockName = Name(blockBinary.blockNameHash);
        block.blockClass = static_cast<MaterialBlockClass::Enum>(blockBinary.blockClass);
        block.fieldBegin = blockBinary.fieldBegin;
        block.fieldCount = blockBinary.fieldCount;
        block.byteSize = blockBinary.byteSize;
        outBlocks.push_back(block);
    }

    if(
        inOutCursor > binary.size()
        || fieldCount > (binary.size() - inOutCursor) / MaterialBinaryPayload::s_TypedLayoutFieldBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed layout field count exceeds available data"));
        return false;
    }
    outFields.reserve(fieldCount);
    for(u32 i = 0u; i < fieldCount; ++i){
        MaterialBinaryPayload::MaterialTypedLayoutFieldBinary fieldBinary;
        if(!ReadPOD(binary, inOutCursor, fieldBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed layout field at index {}"), i);
            return false;
        }

        MaterialTypedLayoutField field;
        field.fieldName = Name(fieldBinary.fieldNameHash);
        field.fieldType = static_cast<MaterialLayoutFieldType::Enum>(fieldBinary.fieldType);
        field.offset = fieldBinary.offset;
        field.defaultValue = fieldBinary.defaultValue;
        outFields.push_back(field);
    }

    u32 blockByteCount = 0u;
    if(!ReadPOD(binary, inOutCursor, blockByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing typed block byte count"));
        return false;
    }
    if(inOutCursor > binary.size() || blockByteCount > binary.size() - inOutCursor){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed block byte count exceeds available data"));
        return false;
    }

    outBlockBytes.resize(blockByteCount);
    if(!BinaryDetail::ReadBytes(binary, inOutCursor, outBlockBytes.data(), blockByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed block bytes"));
        return false;
    }

    return ValidateMaterialTypedLayout(outLayoutHash, outBlocks, outFields, outBlockBytes, NWB_TEXT("Material::loadBinary"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Material::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_shaderVariant.clear();
    m_materialInterface = NAME_NONE;
    m_shadingModelId = 0u;
    m_shadowTransmittanceModelId = 0u;
    m_typedLayoutHash = 0u;
    m_typedLayoutBlocks.clear();
    m_typedLayoutFields.clear();
    m_typedBlockBytes.clear();
    clearStageShaders();
    m_avboitAccumulatePixelShader.reset();
    m_avboitOccupancyPixelShader.reset();
    m_avboitExtinctionPixelShader.reset();
    m_transparent = false;
    m_twoSided = false;
    m_refractive = false;

    usize cursor = 0;
    u32 magic = 0;
    if(!ReadPOD(binary, cursor, magic)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing magic"));
        return false;
    }
    if(magic != MaterialBinaryPayload::s_MaterialMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid magic"));
        return false;
    }

    if(!ReadString(binary, cursor, m_shaderVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader variant"));
        return false;
    }
    if(m_shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader variant is empty"));
        return false;
    }

    NameHash materialInterfaceHash = {};
    if(!ReadPOD(binary, cursor, materialInterfaceHash)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material interface"));
        return false;
    }
    m_materialInterface = Name(materialInterfaceHash);
    if(!m_materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material interface is required"));
        return false;
    }

    if(!__hidden_runtime::ReadMaterialTypedLayout(
        binary,
        cursor,
        m_typedLayoutHash,
        m_typedLayoutBlocks,
        m_typedLayoutFields,
        m_typedBlockBytes
    ))
        return false;
    if(m_typedLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: interface material is missing typed layout data"));
        return false;
    }

    u32 shaderCount = 0;
    if(!ReadPOD(binary, cursor, shaderCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader count"));
        return false;
    }
    if(shaderCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material has no shader stages"));
        return false;
    }
    if(shaderCount > static_cast<u32>(Core::ShaderType::Count)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader count exceeds supported shader stage count"));
        return false;
    }
    if(cursor > binary.size() || shaderCount > (binary.size() - cursor) / MaterialBinaryPayload::s_ShaderEntryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader count exceeds available data"));
        return false;
    }

    for(u32 i = 0; i < shaderCount; ++i){
        Core::ShaderType::Enum shaderType = Core::ShaderType::Invalid;
        NameHash shaderNameHash = {};
        if(!ReadPOD(binary, cursor, shaderType) || !ReadPOD(binary, cursor, shaderNameHash)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed shader stage at index {}"), i);
            return false;
        }

        const Name shaderName(shaderNameHash);
        Core::Assets::AssetRef<Shader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader stage entries must not be empty"));
            return false;
        }

        const usize shaderIndex = Core::ShaderType::ToIndex(shaderType);
        if(m_stageShaders[shaderIndex].valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: duplicate shader stage index {}"), shaderIndex);
            return false;
        }

        m_stageShaders[shaderIndex] = shaderAsset;
        ++m_stageShaderCount;
    }

    u32 materialFlags = 0u;
    if(!ReadPOD(binary, cursor, materialFlags)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material flags"));
        return false;
    }
    if((materialFlags & ~MaterialBinaryPayload::MaterialFlag::All) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material flags contain unsupported bits {}"), materialFlags);
        return false;
    }
    m_transparent = (materialFlags & MaterialBinaryPayload::MaterialFlag::Transparent) != 0u;
    m_twoSided = (materialFlags & MaterialBinaryPayload::MaterialFlag::TwoSided) != 0u;
    m_refractive = (materialFlags & MaterialBinaryPayload::MaterialFlag::Refractive) != 0u;

    if(!ReadPOD(binary, cursor, m_shadingModelId)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shading model id"));
        return false;
    }

    if(!ReadPOD(binary, cursor, m_shadowTransmittanceModelId)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shadow transmittance model id"));
        return false;
    }

    // Optional per-material AVBOIT pass pixel shaders (present only for surface-authored transparent materials):
    // accumulate, then occupancy, then extinction -- each a presence flag followed by the shader name hash,
    // mirroring how a stage shader is stored. All three carry the material's SAME shader-decided surface.renderCoverage.
    const auto readOptionalAvboitPixelShader = [&](const tchar* passLabel, Core::Assets::AssetRef<Shader>& outShaderRef) -> bool{
        u32 hasShader = 0u;
        if(!ReadPOD(binary, cursor, hasShader)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing AVBOIT {} pixel shader presence flag"), passLabel);
            return false;
        }
        if(hasShader > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid AVBOIT {} pixel shader presence flag {}"), passLabel, hasShader);
            return false;
        }
        if(hasShader == 1u){
            NameHash shaderNameHash = {};
            if(!ReadPOD(binary, cursor, shaderNameHash)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing AVBOIT {} pixel shader name"), passLabel);
                return false;
            }
            Core::Assets::AssetRef<Shader> shaderRef;
            shaderRef.virtualPath = Name(shaderNameHash);
            if(!shaderRef.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: AVBOIT {} pixel shader name is empty"), passLabel);
                return false;
            }
            outShaderRef = shaderRef;
        }
        return true;
    };
    if(!readOptionalAvboitPixelShader(NWB_TEXT("accumulate"), m_avboitAccumulatePixelShader))
        return false;
    if(!readOptionalAvboitPixelShader(NWB_TEXT("occupancy"), m_avboitOccupancyPixelShader))
        return false;
    if(!readOptionalAvboitPixelShader(NWB_TEXT("extinction"), m_avboitExtinctionPixelShader))
        return false;

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return true;
}

void Material::clearStageShaders(){
    for(Core::Assets::AssetRef<Shader>& shaderAsset : m_stageShaders)
        shaderAsset.reset();
    m_stageShaderCount = 0;
}

void Material::setTypedLayout(
    const u64 layoutHash,
    const TypedLayoutBlockVector& blocks,
    const TypedLayoutFieldVector& fields,
    const TypedBlockByteVector& blockBytes
){
    m_typedLayoutHash = layoutHash;
    m_typedLayoutBlocks.assign(blocks.begin(), blocks.end());
    m_typedLayoutFields.assign(fields.begin(), fields.end());
    m_typedBlockBytes.assign(blockBytes.begin(), blockBytes.end());
}

bool Material::setShaderForStage(const Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<Shader>& shaderAsset){
    if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid())
        return false;

    Core::Assets::AssetRef<Shader>& storedShader = m_stageShaders[Core::ShaderType::ToIndex(shaderType)];
    if(!storedShader.valid())
        ++m_stageShaderCount;

    storedShader = shaderAsset;
    return true;
}

bool Material::findShaderForStage(const Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const{
    outShaderAsset.reset();
    if(!Core::ShaderType::IsValid(shaderType))
        return false;

    outShaderAsset = m_stageShaders[Core::ShaderType::ToIndex(shaderType)];
    return outShaderAsset.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

