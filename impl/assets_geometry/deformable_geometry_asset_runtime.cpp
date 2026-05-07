// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_geometry_asset.h"

#include "deformable_geometry_binary_payload.h"
#include "deformable_geometry_payload_logging.h"
#include "geometry_binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_auto_registration.h>
#include <global/binary.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_geometry_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateDeformableGeometryAssetCodec(){
    return MakeUnique<DeformableGeometryAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_DeformableGeometryAssetCodecAutoRegistrar(&CreateDeformableGeometryAssetCodec);

UniquePtr<Core::Assets::IAssetCodec> CreateDeformableDisplacementTextureAssetCodec(){
    return MakeUnique<DeformableDisplacementTextureAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_DeformableDisplacementTextureAssetCodecAutoRegistrar(
    &CreateDeformableDisplacementTextureAssetCodec
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void DeformableDisplacementTexture::setSize(const u32 width, const u32 height){
    m_width = width;
    m_height = height;
}

bool DeformableDisplacementTexture::validatePayload()const{
    const auto texturePathText = [this]() -> TString{
        return
            virtualPath()
                ? StringConvert(virtualPath().c_str())
                : TString(NWB_TEXT("<unnamed>"))
        ;
    };

    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(m_width == 0u || m_height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' dimensions are empty")
            , texturePathText()
        );
        return false;
    }
    if(m_width > Limit<u32>::s_Max / m_height){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' dimensions overflow")
            , texturePathText()
        );
        return false;
    }

    const usize requiredTexelCount = static_cast<usize>(m_width) * static_cast<usize>(m_height);
    if(m_texels.size() != requiredTexelCount){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' texel count {} does not match dimensions {}x{}")
            , texturePathText()
            , m_texels.size()
            , m_width
            , m_height
        );
        return false;
    }

    for(usize i = 0; i < m_texels.size(); ++i){
        const Float4U& texel = m_texels[i];
        if(!IsFinite(texel.x) || !IsFinite(texel.y) || !IsFinite(texel.z) || !IsFinite(texel.w)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' texel {} is not finite")
                , texturePathText()
                , i
            );
            return false;
        }
    }

    return true;
}

bool DeformableDisplacementTexture::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_width = 0u;
    m_height = 0u;
    m_texels.clear();

    usize cursor = 0u;
    DeformableGeometryBinaryPayload::DeformableDisplacementTextureHeaderBinary header;
    if(!ReadPOD(binary, cursor, header)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: malformed header"));
        return false;
    }

    if(header.magic != DeformableGeometryBinaryPayload::s_DeformableDisplacementTextureMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: invalid magic"));
        return false;
    }
    if(header.version != DeformableGeometryBinaryPayload::s_DeformableDisplacementTextureVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }
    const u32 width = header.width;
    const u32 height = header.height;
    const u64 texelCount = header.texelCount;
    if(width == 0u || height == 0u || width > Limit<u32>::s_Max / height){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: invalid dimensions"));
        return false;
    }
    if(texelCount != static_cast<u64>(width) * static_cast<u64>(height)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: texel count does not match dimensions"));
        return false;
    }

    m_width = width;
    m_height = height;
    if(!GeometryBinaryPayload::ReadVector(
        binary,
        cursor,
        texelCount,
        m_texels,
        NWB_TEXT("DeformableDisplacementTexture::loadBinary"),
        NWB_TEXT("texels")
    ))
        return false;
    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DeformableGeometry::validatePayload()const{
    const auto geometryPathText = [this]() -> TString{
        return
            virtualPath()
                ? StringConvert(virtualPath().c_str())
                : TString(NWB_TEXT("<unnamed>"))
        ;
    };

    const usize indexCount = m_indices.size();
    const u32 sourceTriangleCount = indexCount <= static_cast<usize>(Limit<u32>::s_Max)
        ? static_cast<u32>(indexCount / 3u)
        : 0u
    ;
    if(!ValidGeometryClass(m_geometryClass) || !GeometryClassUsesDeformableRuntime(m_geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' has invalid geometry class '{}'")
            , geometryPathText()
            , StringConvert(GeometryClassText(m_geometryClass))
        );
        return false;
    }

    const bool hasSkin = !m_skin.empty();
    if(GeometryClassUsesSkinning(m_geometryClass) != hasSkin){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' class '{}' does not match skin payload")
            , geometryPathText()
            , StringConvert(GeometryClassText(m_geometryClass))
        );
        return false;
    }
    if(!GeometryClassAllowsRuntimeDeform(m_geometryClass)){
        if(!m_sourceSamples.empty() || !m_editMaskPerTriangle.empty() || !m_morphs.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' class '{}' cannot carry surface edit or morph payload")
                , geometryPathText()
                , StringConvert(GeometryClassText(m_geometryClass))
            );
            return false;
        }
        if(m_displacement.mode != DeformableDisplacementMode::None){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' class '{}' cannot carry displacement payload")
                , geometryPathText()
                , StringConvert(GeometryClassText(m_geometryClass))
            );
            return false;
        }
    }
    const DeformableValidation::RuntimePayloadFailureInfo runtimePayloadFailure =
        DeformableValidation::FindRuntimePayloadFailure(
            m_restVertices,
            m_indices,
            sourceTriangleCount,
            m_skeletonJointCount,
            m_skin,
            m_inverseBindMatrices,
            m_sourceSamples,
            m_editMaskPerTriangle,
            m_morphs
        )
    ;
    if(runtimePayloadFailure.reason != DeformableValidation::RuntimePayloadFailure::None){
        DeformableValidation::LogRuntimePayloadFailure(
            NWB_TEXT("DeformableGeometry::validatePayload failed"),
            NWB_TEXT("geometry"),
            geometryPathText(),
            m_morphs,
            runtimePayloadFailure
        );
        return false;
    }

    if(!ValidDeformableDisplacementDescriptor(m_displacement)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' displacement descriptor is invalid")
            , geometryPathText()
        );
        return false;
    }
    if(
        !m_displacementTextureVirtualPathText.empty()
        && !DeformableGeometryBinaryPayload::StableTextMatchesName(
                m_displacementTextureVirtualPathText,
                m_displacement.texture.name()
            )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' displacement texture path text does not match asset ref")
            , geometryPathText()
        );
        return false;
    }
    for(const DeformableMorph& morph : m_morphs){
        if(!morph.nameText.empty() && !DeformableGeometryBinaryPayload::StableTextMatchesName(morph.nameText, morph.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph name text does not match morph name")
                , geometryPathText()
            );
            return false;
        }
    }

    return true;
}

bool DeformableGeometry::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_restVertices.clear();
    m_indices.clear();
    m_geometryClass = GeometryClass::Invalid;
    m_skin.clear();
    m_skeletonJointCount = 0u;
    m_inverseBindMatrices.clear();
    m_sourceSamples.clear();
    m_editMaskPerTriangle.clear();
    m_displacement = DeformableDisplacement{};
    m_displacementTextureVirtualPathText.clear();
    m_morphs.clear();

    usize cursor = 0;
    DeformableGeometryBinaryPayload::DeformableGeometryHeaderBinary header;
    if(!ReadPOD(binary, cursor, header)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed header"));
        return false;
    }

    if(header.magic != DeformableGeometryBinaryPayload::s_DeformableGeometryMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: invalid magic"));
        return false;
    }
    if(header.version != DeformableGeometryBinaryPayload::s_DeformableGeometryVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }
    const u32 geometryClass = header.geometryClass;
    const u64 vertexCount = header.restVertexCount;
    const u64 indexCount = header.indexCount;
    const u64 skinCount = header.skinCount;
    const u64 skeletonJointCount = header.skeletonJointCount;
    const u64 inverseBindMatrixCount = header.inverseBindMatrixCount;
    const u64 sourceSampleCount = header.sourceSampleCount;
    const u64 editMaskCount = header.editMaskCount;
    const u64 morphCount = header.morphCount;
    const u64 stringTableByteCount = header.stringTableByteCount;
    if(!ValidGeometryClass(geometryClass) || !GeometryClassUsesDeformableRuntime(geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: invalid geometry class"));
        return false;
    }
    if(
        vertexCount > static_cast<u64>(Limit<u32>::s_Max)
        || indexCount > static_cast<u64>(Limit<u32>::s_Max)
        || skinCount > static_cast<u64>(Limit<u32>::s_Max)
        || skeletonJointCount > static_cast<u64>(Limit<u32>::s_Max)
        || inverseBindMatrixCount > static_cast<u64>(Limit<u32>::s_Max)
        || sourceSampleCount > static_cast<u64>(Limit<u32>::s_Max)
        || editMaskCount > static_cast<u64>(Limit<u32>::s_Max)
        || morphCount > static_cast<u64>(Limit<u32>::s_Max)
        || stringTableByteCount > static_cast<u64>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: payload counts exceed u32 limits"));
        return false;
    }
    if(vertexCount == 0u || indexCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: rest/index payload is empty"));
        return false;
    }
    if((indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: index count is not a multiple of 3"));
        return false;
    }
    if(skinCount != 0u && skinCount != vertexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: skin count must be empty or match vertex count"));
        return false;
    }
    if(skinCount != 0u && skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: skeleton joint count is required when skin is present"));
        return false;
    }
    if(skeletonJointCount > DeformableGeometryBinaryPayload::s_DeformableSkeletonJointLimit){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: skeleton joint count exceeds skin stream limits"));
        return false;
    }
    if(inverseBindMatrixCount != 0u && inverseBindMatrixCount != skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: inverse bind matrix count must be empty or match skeleton joint count"));
        return false;
    }
    if(sourceSampleCount != 0u && sourceSampleCount != vertexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: source sample count must be empty or match vertex count"));
        return false;
    }
    const u64 triangleCount = indexCount / 3u;
    if(editMaskCount != 0u && editMaskCount != triangleCount){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: edit mask count must be empty or match triangle count"));
        return false;
    }
    if(GeometryClassUsesSkinning(geometryClass) != (skinCount != 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: geometry class does not match skin payload"));
        return false;
    }
    if(!GeometryClassAllowsRuntimeDeform(geometryClass) && (sourceSampleCount != 0u || editMaskCount != 0u || morphCount != 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: geometry class cannot carry surface edit or morph payload"));
        return false;
    }

    const tchar* const loadFailureContext = NWB_TEXT("DeformableGeometry::loadBinary");
    auto readVector = [&](const u64 count, auto& outValues, const tchar* label){
        return GeometryBinaryPayload::ReadVector(binary, cursor, count, outValues, loadFailureContext, label);
    };
    if(!readVector(vertexCount, m_restVertices, NWB_TEXT("rest vertices")))
        return false;
    if(!readVector(indexCount, m_indices, NWB_TEXT("indices")))
        return false;
    if(!readVector(skinCount, m_skin, NWB_TEXT("skin")))
        return false;
    m_geometryClass = geometryClass;
    m_skeletonJointCount = static_cast<u32>(skeletonJointCount);
    if(!readVector(inverseBindMatrixCount, m_inverseBindMatrices, NWB_TEXT("inverse bind matrices")))
        return false;
    if(!readVector(sourceSampleCount, m_sourceSamples, NWB_TEXT("source samples")))
        return false;
    if(!readVector(editMaskCount, m_editMaskPerTriangle, NWB_TEXT("edit masks")))
        return false;

    if(morphCount > 0u){
        constexpr usize minMorphHeaderSize = sizeof(DeformableGeometryBinaryPayload::DeformableMorphHeaderBinary);
        const usize remainingBytes = cursor <= binary.size() ? binary.size() - cursor : 0u;
        if(morphCount > static_cast<u64>(remainingBytes / minMorphHeaderSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed morph table"));
            return false;
        }
    }
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> morphNameOffsets{ Core::Alloc::ScratchAllocator<u32>(scratchArena) };
    morphNameOffsets.reserve(static_cast<usize>(morphCount));
    m_morphs.reserve(static_cast<usize>(morphCount));
    for(u64 morphIndex = 0; morphIndex < morphCount; ++morphIndex){
        DeformableGeometryBinaryPayload::DeformableMorphHeaderBinary morphHeader;
        if(!ReadPOD(binary, cursor, morphHeader)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed morph header"));
            return false;
        }
        if(morphHeader.reserved != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unsupported morph header reserved data"));
            return false;
        }
        if(morphHeader.deltaCount > static_cast<u64>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: morph delta count exceeds u32 limits"));
            return false;
        }

        DeformableMorph morph;
        if(!readVector(morphHeader.deltaCount, morph.deltas, NWB_TEXT("morph deltas")))
            return false;
        m_morphs.push_back(Move(morph));
        morphNameOffsets.push_back(morphHeader.nameOffset);
    }
    DeformableGeometryBinaryPayload::DeformableDisplacementBinary displacementBinary;
    if(!ReadPOD(binary, cursor, displacementBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed displacement descriptor"));
        return false;
    }
    if(displacementBinary.reserved != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unsupported displacement reserved data"));
        return false;
    }
    m_displacement = DeformableGeometryBinaryPayload::BuildDisplacement(displacementBinary);
    if(!GeometryClassAllowsRuntimeDeform(m_geometryClass) && m_displacement.mode != DeformableDisplacementMode::None){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: geometry class cannot carry displacement payload"));
        return false;
    }
    const bool requiresStringTable =
        morphCount != 0u
        || DeformableDisplacementModeUsesTexture(displacementBinary.mode)
    ;
    if(!requiresStringTable && stringTableByteCount != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unexpected string table"));
        return false;
    }
    if(requiresStringTable && stringTableByteCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: missing string table"));
        return false;
    }

    if(
        cursor > binary.size()
        || static_cast<usize>(stringTableByteCount) > binary.size() - cursor
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed string table"));
        return false;
    }
    const usize stringTableOffset = cursor;
    cursor += static_cast<usize>(stringTableByteCount);
    for(usize morphIndex = 0; morphIndex < m_morphs.size(); ++morphIndex){
        CompactString morphNameText;
        if(
            !::ReadStringTableText(
                binary,
                stringTableOffset,
                static_cast<usize>(stringTableByteCount),
                morphNameOffsets[morphIndex],
                morphNameText
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed morph name string"));
            return false;
        }
        m_morphs[morphIndex].name = Name(morphNameText.view());
        m_morphs[morphIndex].nameText = Move(morphNameText);
    }
    if(DeformableDisplacementModeUsesTexture(displacementBinary.mode)){
        CompactString texturePathText;
        if(
            !::ReadStringTableText(
                binary,
                stringTableOffset,
                static_cast<usize>(stringTableByteCount),
                displacementBinary.texturePathOffset,
                texturePathText
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed displacement texture path string"));
            return false;
        }
        m_displacement.texture.virtualPath = Name(texturePathText.view());
        m_displacementTextureVirtualPathText = Move(texturePathText);
    }
    else if(displacementBinary.texturePathOffset != Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unexpected displacement texture path"));
        return false;
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return validatePayload();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

