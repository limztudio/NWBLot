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


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_MaterialAssetCodecAutoRegistrar, MaterialAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateMaterialTypedLayout(
    const u64 layoutHash,
    const Material::TypedLayoutBlockVector& blocks,
    const Material::TypedLayoutFieldVector& fields,
    const Material::TypedBlockByteVector& blockBytes,
    const TStringView failureContext
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

            const auto expectedFieldOffset = AlignMaterialLayoutFieldOffset(expectedOffset, field.fieldType);
            if(!expectedFieldOffset){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} alignment overflows")
                    , failureContext
                    , fieldIndex
                );
                return false;
            }
            if(field.offset != *expectedFieldOffset){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} has misaligned offset")
                    , failureContext
                    , fieldIndex
                );
                return false;
            }

            const u32 fieldByteSize = MaterialLayoutFieldByteSize(field.fieldType);
            if(fieldByteSize == 0u || *expectedFieldOffset > Limit<u32>::s_Max - fieldByteSize){
                NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout field {} byte size overflows"), failureContext, fieldIndex);
                return false;
            }
            expectedOffset = *expectedFieldOffset + fieldByteSize;
        }

        const auto expectedBlockByteSize = AlignMaterialLayoutBlockByteSize(expectedOffset);
        if(!expectedBlockByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed layout block {} byte size overflows")
                , failureContext
                , blockIndex
            );
            return false;
        }
        if(*expectedBlockByteSize != block.byteSize){
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

    const auto expectedBlockByteSize = MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(blocks);
    if(!expectedBlockByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed block byte size overflows"), failureContext);
        return false;
    }
    if(blockBytes.size() != *expectedBlockByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: typed block byte count does not match typed layout"), failureContext);
        return false;
    }

    return true;
}

static Expected<u64> ReadMaterialTypedLayout(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    Material::TypedLayoutBlockVector& outBlocks,
    Material::TypedLayoutFieldVector& outFields,
    Material::TypedBlockByteVector& outBlockBytes,
    Material::ResourceReferenceVector& outResourceReferences
){
    outBlocks.clear();
    outFields.clear();
    outBlockBytes.clear();
    outResourceReferences.clear();

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

    if(
        inOutCursor > binary.size()
        || blockCount > (binary.size() - inOutCursor) / MaterialBinaryPayload::s_TypedLayoutBlockBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed layout block count exceeds available data"));
        return MakeUnexpected(Failure{});
    }
    outBlocks.reserve(blockCount);
    for(u32 i = 0u; i < blockCount; ++i){
        MaterialBinaryPayload::MaterialTypedLayoutBlockBinary blockBinary;
        const auto blockBinaryResult = ReadPOD<MaterialBinaryPayload::MaterialTypedLayoutBlockBinary>(binary, inOutCursor);
        if(!blockBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed layout block at index {}"), i);
            return MakeUnexpected(Failure{});
        }
        blockBinary = *blockBinaryResult;

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
        return MakeUnexpected(Failure{});
    }
    outFields.reserve(fieldCount);
    for(u32 i = 0u; i < fieldCount; ++i){
        MaterialBinaryPayload::MaterialTypedLayoutFieldBinary fieldBinary;
        const auto fieldBinaryResult = ReadPOD<MaterialBinaryPayload::MaterialTypedLayoutFieldBinary>(binary, inOutCursor);
        if(!fieldBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed typed layout field at index {}"), i);
            return MakeUnexpected(Failure{});
        }
        fieldBinary = *fieldBinaryResult;

        MaterialTypedLayoutField field;
        field.fieldName = Name(fieldBinary.fieldNameHash);
        field.fieldType = static_cast<MaterialLayoutFieldType::Enum>(fieldBinary.fieldType);
        field.offset = fieldBinary.offset;
        field.defaultValue = fieldBinary.defaultValue;
        outFields.push_back(field);
    }

    u32 blockByteCount = 0u;
    const auto blockByteCountResult = ReadPOD<u32>(binary, inOutCursor);
    if(!blockByteCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing typed block byte count"));
        return MakeUnexpected(Failure{});
    }
    blockByteCount = *blockByteCountResult;
    if(inOutCursor > binary.size() || blockByteCount > binary.size() - inOutCursor){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: typed block byte count exceeds available data"));
        return MakeUnexpected(Failure{});
    }

    outBlockBytes.resize(blockByteCount);
    if(!BinaryDetail::ReadBytes(binary, inOutCursor, outBlockBytes.data(), blockByteCount)){
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
    if(
        inOutCursor > binary.size()
        || resourceReferenceCount > (binary.size() - inOutCursor) / MaterialBinaryPayload::s_ResourceReferenceBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material resource reference count exceeds available data"));
        return MakeUnexpected(Failure{});
    }
    outResourceReferences.reserve(resourceReferenceCount);
    for(u32 i = 0u; i < resourceReferenceCount; ++i){
        MaterialBinaryPayload::MaterialResourceReferenceBinary resourceReferenceBinary;
        const auto resourceReferenceBinaryResult = ReadPOD<MaterialBinaryPayload::MaterialResourceReferenceBinary>(binary, inOutCursor);
        if(!resourceReferenceBinaryResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: malformed material resource reference at index {}"), i);
            return MakeUnexpected(Failure{});
        }
        resourceReferenceBinary = *resourceReferenceBinaryResult;
        const MaterialResourceKind::Enum resourceKind =
            static_cast<MaterialResourceKind::Enum>(resourceReferenceBinary.resourceKind);
        const Name resourceName(resourceReferenceBinary.resourceNameHash);
        if(!IsValidSerializedMaterialResourceReference(resourceKind, resourceName)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material resource reference at index {} has an invalid asset identity"), i);
            return MakeUnexpected(Failure{});
        }

        MaterialResourceReference resourceReference;
        resourceReference.blockName = Name(resourceReferenceBinary.blockNameHash);
        resourceReference.fieldName = Name(resourceReferenceBinary.fieldNameHash);
        resourceReference.resourceKind = resourceKind;
        resourceReference.constantByteOffset = resourceReferenceBinary.constantByteOffset;
        if(!AssignMaterialResourceReferenceAsset(resourceReference, resourceKind, resourceName))
            return MakeUnexpected(Failure{});
        outResourceReferences.push_back(resourceReference);
    }

    if(!ValidateMaterialTypedLayout(layoutHash, outBlocks, outFields, outBlockBytes, NWB_TEXT("Material::loadBinary")))
        return MakeUnexpected(Failure{});
    if(!MaterialBinaryPayload::ValidateMaterialResourceReferences(outBlocks, outFields, outResourceReferences)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material resource references do not match typed layout"));
        return MakeUnexpected(Failure{});
    }

    return layoutHash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Material::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!checkVirtualPath(NWB_TEXT("Material::loadBinary")))
        return false;

    m_shaderVariant.clear();
    m_materialInterface = s_NameNone;
    m_shadingModelId = 0u;
    m_surfaceDispatchId = 0u;
    m_typedLayoutHash = 0u;
    m_typedLayoutBlocks.clear();
    m_typedLayoutFields.clear();
    m_typedBlockBytes.clear();
    m_resourceReferences.clear();
    clearStageShaders();
    m_avboitAccumulatePixelShader.reset();
    m_avboitOccupancyPixelShader.reset();
    m_avboitExtinctionPixelShader.reset();
    m_transparent = false;
    m_twoSided = false;
    m_refractive = false;

    usize cursor = 0;
    u32 magic = 0;
    const auto magicResult = ReadPOD<u32>(binary, cursor);
    if(!magicResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing magic"));
        return false;
    }
    magic = *magicResult;
    if(magic != MaterialBinaryPayload::s_MaterialMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid magic"));
        return false;
    }

    const auto shaderVariant = ReadString(binary, cursor);
    if(!shaderVariant){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader variant"));
        return false;
    }
    m_shaderVariant.assign(shaderVariant->data(), shaderVariant->size());
    if(m_shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: shader variant is empty"));
        return false;
    }

    NameHash materialInterfaceHash = {};
    const auto materialInterfaceHashResult = ReadPOD<NameHash>(binary, cursor);
    if(!materialInterfaceHashResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material interface"));
        return false;
    }
    materialInterfaceHash = *materialInterfaceHashResult;
    m_materialInterface = Name(materialInterfaceHash);
    if(!m_materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material interface is required"));
        return false;
    }

    const auto layoutHash = __hidden_runtime::ReadMaterialTypedLayout(
        binary, cursor, m_typedLayoutBlocks, m_typedLayoutFields, m_typedBlockBytes, m_resourceReferences
    );
    if(!layoutHash)
        return false;
    m_typedLayoutHash = *layoutHash;
    if(m_typedLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: interface material is missing typed layout data"));
        return false;
    }

    u32 shaderCount = 0;
    const auto shaderCountResult = ReadPOD<u32>(binary, cursor);
    if(!shaderCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shader count"));
        return false;
    }
    shaderCount = *shaderCountResult;
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

    for(u32 i = 0u; i < shaderCount; ++i){
        Core::ShaderType::Enum shaderType = Core::ShaderType::Invalid;
        NameHash shaderNameHash = {};
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
    const auto materialFlagsResult = ReadPOD<u32>(binary, cursor);
    if(!materialFlagsResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing material flags"));
        return false;
    }
    materialFlags = *materialFlagsResult;
    if((materialFlags & ~MaterialBinaryPayload::MaterialFlag::All) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: material flags contain unsupported bits {}"), materialFlags);
        return false;
    }
    m_transparent = (materialFlags & MaterialBinaryPayload::MaterialFlag::Transparent) != 0u;
    m_twoSided = (materialFlags & MaterialBinaryPayload::MaterialFlag::TwoSided) != 0u;
    m_refractive = (materialFlags & MaterialBinaryPayload::MaterialFlag::Refractive) != 0u;

    const auto m_shadingModelIdResult = ReadPOD<u32>(binary, cursor);
    if(!m_shadingModelIdResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing shading model id"));
        return false;
    }
    m_shadingModelId = *m_shadingModelIdResult;

    const auto m_surfaceDispatchIdResult = ReadPOD<u32>(binary, cursor);
    if(!m_surfaceDispatchIdResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing surface dispatch id"));
        return false;
    }
    m_surfaceDispatchId = *m_surfaceDispatchIdResult;

    // Optional per-material AVBOIT pixel shaders (transparent materials only): accumulate, occupancy, extinction, each a presence flag plus shader name hash.
    const auto readOptionalAvboitPixelShader = [&](const TStringView passLabel) -> Expected<Core::Assets::AssetRef<PixelShader>>{
        u32 hasShader = 0u;
        const auto hasShaderResult = ReadPOD<u32>(binary, cursor);
        if(!hasShaderResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing AVBOIT {} pixel shader presence flag"), passLabel);
            return MakeUnexpected(Failure{});
        }
        hasShader = *hasShaderResult;
        if(hasShader > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: invalid AVBOIT {} pixel shader presence flag {}"), passLabel, hasShader);
            return MakeUnexpected(Failure{});
        }
        if(hasShader == 1u){
            NameHash shaderNameHash = {};
            const auto shaderNameHashResult = ReadPOD<NameHash>(binary, cursor);
            if(!shaderNameHashResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: missing AVBOIT {} pixel shader name"), passLabel);
                return MakeUnexpected(Failure{});
            }
            shaderNameHash = *shaderNameHashResult;
            Core::Assets::AssetRef<PixelShader> shaderRef;
            shaderRef.virtualPath = Name(shaderNameHash);
            if(!shaderRef.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: AVBOIT {} pixel shader name is empty"), passLabel);
                return MakeUnexpected(Failure{});
            }
            return shaderRef;
        }
        return Core::Assets::AssetRef<PixelShader>{};
    };
    const auto accumulateShader = readOptionalAvboitPixelShader(NWB_TEXT("accumulate"));
    if(!accumulateShader)
        return false;
    m_avboitAccumulatePixelShader = *accumulateShader;
    const auto occupancyShader = readOptionalAvboitPixelShader(NWB_TEXT("occupancy"));
    if(!occupancyShader)
        return false;
    m_avboitOccupancyPixelShader = *occupancyShader;
    const auto extinctionShader = readOptionalAvboitPixelShader(NWB_TEXT("extinction"));
    if(!extinctionShader)
        return false;
    m_avboitExtinctionPixelShader = *extinctionShader;
    if(!HasValidMaterialAvboitPixelShaderContract(
        m_transparent,
        m_avboitAccumulatePixelShader,
        m_avboitOccupancyPixelShader,
        m_avboitExtinctionPixelShader
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: AVBOIT pixel shaders must be present if and only if the material is transparent"));
        return false;
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return true;
}

void Material::clearStageShaders()noexcept{
    for(Core::Assets::AssetRef<IShader>& shaderAsset : m_stageShaders)
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
    m_typedLayoutBlocks.reserve(blocks.size());
    m_typedLayoutBlocks.assign(blocks.begin(), blocks.end());
    m_typedLayoutFields.reserve(fields.size());
    m_typedLayoutFields.assign(fields.begin(), fields.end());
    m_typedBlockBytes.reserve(blockBytes.size());
    m_typedBlockBytes.assign(blockBytes.begin(), blockBytes.end());
}

void Material::setResourceReferences(const ResourceReferenceVector& resourceReferences){
    m_resourceReferences.reserve(resourceReferences.size());
    m_resourceReferences.assign(resourceReferences.begin(), resourceReferences.end());
}

bool Material::setShaderForStage(const Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<IShader>& shaderAsset)noexcept{
    if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid())
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

