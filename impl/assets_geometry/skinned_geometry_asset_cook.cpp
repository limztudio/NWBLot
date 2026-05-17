// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_asset.h"

#include "skinned_geometry_binary_payload.h"
#include "geometry_binary_payload.h"

#include <core/alloc/scratch.h>
#include <global/binary.h>
#include <core/common/log.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedGeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(SkinnedGeometry::s_AssetTypeText)
        );
        return false;
    }

    const SkinnedGeometry& geometry = static_cast<const SkinnedGeometry&>(asset);
    if(!geometry.validatePayload())
        return false;

    usize reserveBytes = sizeof(SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary);
    bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, geometry.restVertices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.indices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.skin())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.inverseBindMatrices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.sourceSamples())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.editMaskPerTriangle())
    ;
    for(const SkinnedGeometryMorph& morph : geometry.morphs()){
        canReserve = canReserve
            && AddBinaryReserveBytes(
                reserveBytes,
                sizeof(SkinnedGeometryBinaryPayload::SkinnedGeometryMorphHeaderBinary)
            )
            && AddBinaryVectorReserveBytes(reserveBytes, morph.deltas)
        ;
    }
    canReserve = canReserve
        && AddBinaryReserveBytes(reserveBytes, sizeof(SkinnedGeometryBinaryPayload::SkinnedGeometryDisplacementBinary))
    ;

    usize stringTableReserveBytes = 0u;
    bool canReserveStringTable = true;
    for(const SkinnedGeometryMorph& morph : geometry.morphs())
        canReserveStringTable = canReserveStringTable && ::AddStringTableTextReserveBytes(stringTableReserveBytes, morph.nameText);
    if(SkinnedGeometryDisplacementModeUsesTexture(geometry.displacement().mode)){
        canReserveStringTable = canReserveStringTable
            && ::AddStringTableTextReserveBytes(stringTableReserveBytes, geometry.displacementTextureVirtualPathText())
        ;
    }
    if(canReserve && canReserveStringTable)
        canReserve = AddBinaryReserveBytes(reserveBytes, stringTableReserveBytes);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> stringTable{ Core::Alloc::ScratchAllocator<u8>(scratchArena) };
    if(canReserveStringTable)
        stringTable.reserve(stringTableReserveBytes);

    SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary header;
    header.geometryClass = geometry.geometryClass();
    header.restVertexCount = static_cast<u64>(geometry.restVertices().size());
    header.indexCount = static_cast<u64>(geometry.indices().size());
    header.skinCount = static_cast<u64>(geometry.skin().size());
    header.skeletonJointCount = static_cast<u64>(geometry.skeletonJointCount());
    header.inverseBindMatrixCount = static_cast<u64>(geometry.inverseBindMatrices().size());
    header.sourceSampleCount = static_cast<u64>(geometry.sourceSamples().size());
    header.editMaskCount = static_cast<u64>(geometry.editMaskPerTriangle().size());
    header.morphCount = static_cast<u64>(geometry.morphs().size());
    const usize headerOffset = outBinary.size();
    AppendPOD(outBinary, header);
    const usize stringTableByteCountOffset =
        headerOffset + offsetof(SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary, stringTableByteCount);

    const tchar* const serializeFailureContext = NWB_TEXT("SkinnedGeometryAssetCodec::serialize");
    auto appendVector = [&](const auto& values, const tchar* label){
        return GeometryBinaryPayload::AppendVector(outBinary, values, serializeFailureContext, label);
    };
    if(!appendVector(geometry.restVertices(), NWB_TEXT("rest vertices")))
        return false;
    if(!appendVector(geometry.indices(), NWB_TEXT("indices")))
        return false;
    if(!appendVector(geometry.skin(), NWB_TEXT("skin")))
        return false;
    if(!appendVector(geometry.inverseBindMatrices(), NWB_TEXT("inverse bind matrices")))
        return false;
    if(!appendVector(geometry.sourceSamples(), NWB_TEXT("source samples")))
        return false;
    if(!appendVector(geometry.editMaskPerTriangle(), NWB_TEXT("edit masks")))
        return false;

    for(const SkinnedGeometryMorph& morph : geometry.morphs()){
        SkinnedGeometryBinaryPayload::SkinnedGeometryMorphHeaderBinary morphHeader;
        morphHeader.deltaCount = static_cast<u64>(morph.deltas.size());
        if(
            !SkinnedGeometryBinaryPayload::StableTextMatchesName(morph.nameText, morph.name)
            || !::AppendStringTableText(
                stringTable,
                morph.nameText.view(),
                morphHeader.nameOffset
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryAssetCodec::serialize failed: morph '{}' is missing stable name text")
                , StringConvert(morph.name.c_str())
            );
            return false;
        }
        AppendPOD(outBinary, morphHeader);
        if(!appendVector(morph.deltas, NWB_TEXT("morph deltas")))
            return false;
    }
    SkinnedGeometryBinaryPayload::SkinnedGeometryDisplacementBinary displacementBinary;
    if(
        !SkinnedGeometryBinaryPayload::BuildDisplacementBinary(
            geometry.displacement(),
            geometry.displacementTextureVirtualPathText(),
            stringTable,
            displacementBinary
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryAssetCodec::serialize failed: displacement texture is missing stable virtual path text"));
        return false;
    }
    AppendPOD(outBinary, displacementBinary);

    const u64 stringTableByteCount = static_cast<u64>(stringTable.size());
    NWB_MEMCPY(
        outBinary.data() + stringTableByteCountOffset,
        sizeof(stringTableByteCount),
        &stringTableByteCount,
        sizeof(stringTableByteCount)
    );
    if(!appendVector(stringTable, NWB_TEXT("string table")))
        return false;

    return true;
}

bool SkinnedGeometryDisplacementTextureAssetCodec::serialize(
    const Core::Assets::IAsset& asset,
    Core::Assets::AssetBytes& outBinary
)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryDisplacementTextureAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(SkinnedGeometryDisplacementTexture::s_AssetTypeText)
        );
        return false;
    }

    const SkinnedGeometryDisplacementTexture& texture = static_cast<const SkinnedGeometryDisplacementTexture&>(asset);
    if(!texture.validatePayload())
        return false;

    usize reserveBytes = sizeof(SkinnedGeometryBinaryPayload::SkinnedGeometryDisplacementTextureHeaderBinary);
    if(!AddBinaryVectorReserveBytes(reserveBytes, texture.texels())){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryDisplacementTextureAssetCodec::serialize failed: payload size overflows"));
        return false;
    }

    outBinary.clear();
    outBinary.reserve(reserveBytes);
    SkinnedGeometryBinaryPayload::SkinnedGeometryDisplacementTextureHeaderBinary header;
    header.width = texture.width();
    header.height = texture.height();
    header.texelCount = static_cast<u64>(texture.texels().size());
    AppendPOD(outBinary, header);
    return GeometryBinaryPayload::AppendVector(
        outBinary,
        texture.texels(),
        NWB_TEXT("SkinnedGeometryDisplacementTextureAssetCodec::serialize"),
        NWB_TEXT("texels")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

