// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_geometry_asset.h"

#include "deformable_geometry_validation.h"
#include "geometry_binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_auto_registration.h>
#include <global/binary.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_geometry_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_DeformableGeometryMagic = 0x44474F31u; // DGO1
static constexpr u32 s_DeformableGeometryVersion = 8u;
static constexpr u32 s_DeformableDisplacementTextureMagic = 0x44445431u; // DDT1
static constexpr u32 s_DeformableDisplacementTextureVersion = 1u;
static constexpr u32 s_DeformableSkeletonJointLimit = static_cast<u32>(Limit<u16>::s_Max) + 1u;
#if defined(NWB_COOK)
static constexpr usize s_DeformableGeometryHeaderBytes =
    sizeof(u32) + // magic
    sizeof(u32) + // version
    sizeof(u32) + // geometry class
    sizeof(u64) + // rest vertex count
    sizeof(u64) + // index count
    sizeof(u64) + // skin count
    sizeof(u64) + // skeleton joint count
    sizeof(u64) + // inverse bind matrix count
    sizeof(u64) + // source sample count
    sizeof(u64) + // edit mask count
    sizeof(u64) + // morph count
    sizeof(u64)   // string table byte count
;
#endif

struct DeformableMorphHeaderBinary{
    u32 nameOffset = Limit<u32>::s_Max;
    u32 reserved = 0;
    u64 deltaCount = 0;
};
static_assert(IsStandardLayout_V<DeformableMorphHeaderBinary>, "DeformableMorphHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableMorphHeaderBinary>, "DeformableMorphHeaderBinary must stay binary-serializable");

struct DeformableDisplacementBinary{
    u32 texturePathOffset = Limit<u32>::s_Max;
    u32 reserved = 0;
    u32 mode = DeformableDisplacementMode::None;
    f32 amplitude = 0.0f;
    f32 bias = 0.0f;
    Float2U uvScale = Float2U(1.0f, 1.0f);
    Float2U uvOffset = Float2U(0.0f, 0.0f);
};
static_assert(IsStandardLayout_V<DeformableDisplacementBinary>, "DeformableDisplacementBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableDisplacementBinary>, "DeformableDisplacementBinary must stay binary-serializable");

[[nodiscard]] bool StableTextMatchesName(const CompactString& text, const Name& name){
    return !text.empty() && Name(text.view()) == name;
}

template<typename StringTable>
[[nodiscard]] bool BuildDisplacementBinary(
    const DeformableDisplacement& displacement,
    const CompactString& texturePathText,
    StringTable& stringTable,
    DeformableDisplacementBinary& outBinary){
    outBinary = DeformableDisplacementBinary{};
    if(DeformableDisplacementModeUsesTexture(displacement.mode)){
        if(!StableTextMatchesName(texturePathText, displacement.texture.name()))
            return false;
        if(!::AppendStringTableText(stringTable, texturePathText.view(), outBinary.texturePathOffset))
            return false;
    }

    outBinary.mode = displacement.mode;
    outBinary.amplitude = displacement.amplitude;
    outBinary.bias = displacement.bias;
    outBinary.uvScale = displacement.uvScale;
    outBinary.uvOffset = displacement.uvOffset;
    return true;
}

[[nodiscard]] DeformableDisplacement BuildDisplacement(const DeformableDisplacementBinary& binary){
    DeformableDisplacement displacement;
    displacement.mode = binary.mode;
    displacement.amplitude = binary.amplitude;
    displacement.bias = binary.bias;
    displacement.uvScale = binary.uvScale;
    displacement.uvOffset = binary.uvOffset;
    return displacement;
}


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


void LogGeometryMorphPayloadFailure(
    const TString& geometryPathText,
    const Vector<DeformableMorph>& morphs,
    const DeformableValidation::MorphPayloadFailureInfo& failure){
    const TString morphNameText = DeformableValidation::MorphPayloadFailureMorphNameText(morphs, failure);

    switch(failure.reason){
    case DeformableValidation::MorphPayloadFailure::MorphCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph count exceeds u32 limits")
            , geometryPathText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::EmptyMorph:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph {} is unnamed or empty")
            , geometryPathText
            , failure.morphIndex
        );
        break;
    case DeformableValidation::MorphPayloadFailure::DuplicateMorphName:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' contains duplicate morph '{}'")
            , geometryPathText
            , morphNameText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::MorphDeltaCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' delta count exceeds u32 limits")
            , geometryPathText
            , morphNameText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::InvalidMorphDelta:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' delta {} is invalid")
            , geometryPathText
            , morphNameText
            , failure.deltaIndex
        );
        break;
    case DeformableValidation::MorphPayloadFailure::DuplicateMorphDeltaVertex:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' has duplicate vertex {}")
            , geometryPathText
            , morphNameText
            , failure.vertexId
        );
        break;
    case DeformableValidation::MorphPayloadFailure::None:
        break;
    }
}

void LogGeometryRuntimePayloadFailure(
    const TString& geometryPathText,
    const Vector<DeformableMorph>& morphs,
    const DeformableValidation::RuntimePayloadFailureInfo& failure){
    switch(failure.reason){
    case DeformableValidation::RuntimePayloadFailure::IncompleteRestIndexPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' has incomplete rest/index payload")
            , geometryPathText
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::VertexIndexCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' exceeds u32 vertex/index count limits")
            , geometryPathText
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::IndexCountNotTriangleList:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' index count {} is not a multiple of 3")
            , geometryPathText
            , failure.count
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::InvalidRestVertex:
        if(failure.restVertexFailure == DeformableValidation::RestVertexPayloadFailure::NonFiniteData){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} contains non-finite data")
                , geometryPathText
                , failure.vertexIndex
            );
        }
        else if(failure.restVertexFailure == DeformableValidation::RestVertexPayloadFailure::DegenerateFrame){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} has a degenerate normal/tangent frame")
                , geometryPathText
                , failure.vertexIndex
            );
        }
        else if(failure.restVertexFailure == DeformableValidation::RestVertexPayloadFailure::InvalidFrame){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} has an invalid normal/tangent frame")
                , geometryPathText
                , failure.vertexIndex
            );
        }
        break;
    case DeformableValidation::RuntimePayloadFailure::IndexOutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' index {} exceeds {} vertices")
            , geometryPathText
            , failure.vertexId
            , failure.count
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::DegenerateTriangle:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' triangle {} is degenerate")
            , geometryPathText
            , failure.indexBase / 3u
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::ZeroAreaTriangle:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' triangle {} has zero area")
            , geometryPathText
            , failure.indexBase / 3u
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::SkinCountMismatch:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' skin count {} does not match vertex count {}")
            , geometryPathText
            , failure.count
            , failure.expectedCount
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::SkinMissingSkeleton:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' has skin but no skeleton joint count")
            , geometryPathText
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::SkeletonJointCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' skeleton joint count {} exceeds skin stream limits")
            , geometryPathText
            , failure.count
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::InvalidInverseBindMatrices:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' inverse bind matrices must be empty or match a valid skeleton")
            , geometryPathText
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::InvalidSkinInfluence:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' skin weights for vertex {} are invalid")
            , geometryPathText
            , failure.vertexIndex
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::SkinJointOutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' skin joint {} for vertex {} exceeds skeleton joint count {}")
            , geometryPathText
            , failure.failedJoint
            , failure.vertexIndex
            , failure.count
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::SourceSampleCountMismatch:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' source samples {} do not match vertices {}")
            , geometryPathText
            , failure.count
            , failure.expectedCount
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::InvalidSourceSample:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' source sample {} is invalid")
            , geometryPathText
            , failure.vertexIndex
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::EditMaskCountMismatch:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' edit mask count {} does not match triangle count {}")
            , geometryPathText
            , failure.count
            , failure.expectedCount
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::InvalidEditMask:
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' edit mask {} is invalid")
            , geometryPathText
            , failure.indexBase / 3u
        );
        break;
    case DeformableValidation::RuntimePayloadFailure::MorphPayload:
        LogGeometryMorphPayloadFailure(geometryPathText, morphs, failure.morphFailure);
        break;
    case DeformableValidation::RuntimePayloadFailure::None:
        break;
    }
}


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
    u32 magic = 0u;
    u32 version = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u64 texelCount = 0u;
    if(
        !ReadPOD(binary, cursor, magic)
        || !ReadPOD(binary, cursor, version)
        || !ReadPOD(binary, cursor, width)
        || !ReadPOD(binary, cursor, height)
        || !ReadPOD(binary, cursor, texelCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: malformed header"));
        return false;
    }

    if(magic != __hidden_deformable_geometry_asset::s_DeformableDisplacementTextureMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: invalid magic"));
        return false;
    }
    if(version != __hidden_deformable_geometry_asset::s_DeformableDisplacementTextureVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::loadBinary failed: unsupported version {}"), version);
        return false;
    }
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
        __hidden_deformable_geometry_asset::LogGeometryRuntimePayloadFailure(
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
        && !__hidden_deformable_geometry_asset::StableTextMatchesName(
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
        if(!morph.nameText.empty() && !__hidden_deformable_geometry_asset::StableTextMatchesName(morph.nameText, morph.name)){
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
    u32 magic = 0;
    u32 version = 0;
    u32 geometryClass = GeometryClass::Invalid;
    u64 vertexCount = 0;
    u64 indexCount = 0;
    u64 skinCount = 0;
    u64 skeletonJointCount = 0;
    u64 inverseBindMatrixCount = 0;
    u64 sourceSampleCount = 0;
    u64 editMaskCount = 0;
    u64 morphCount = 0;
    u64 stringTableByteCount = 0;
    if(
        !ReadPOD(binary, cursor, magic)
        || !ReadPOD(binary, cursor, version)
        || !ReadPOD(binary, cursor, geometryClass)
        || !ReadPOD(binary, cursor, vertexCount)
        || !ReadPOD(binary, cursor, indexCount)
        || !ReadPOD(binary, cursor, skinCount)
        || !ReadPOD(binary, cursor, skeletonJointCount)
        || !ReadPOD(binary, cursor, inverseBindMatrixCount)
        || !ReadPOD(binary, cursor, sourceSampleCount)
        || !ReadPOD(binary, cursor, editMaskCount)
        || !ReadPOD(binary, cursor, morphCount)
        || !ReadPOD(binary, cursor, stringTableByteCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed header"));
        return false;
    }

    if(magic != __hidden_deformable_geometry_asset::s_DeformableGeometryMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: invalid magic"));
        return false;
    }
    if(version != __hidden_deformable_geometry_asset::s_DeformableGeometryVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unsupported version {}"), version);
        return false;
    }
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
    if(skeletonJointCount > __hidden_deformable_geometry_asset::s_DeformableSkeletonJointLimit){
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
        constexpr usize minMorphHeaderBytes = sizeof(__hidden_deformable_geometry_asset::DeformableMorphHeaderBinary);
        const usize remainingBytes = cursor <= binary.size() ? binary.size() - cursor : 0u;
        if(morphCount > static_cast<u64>(remainingBytes / minMorphHeaderBytes)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed morph table"));
            return false;
        }
    }
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> morphNameOffsets{ Core::Alloc::ScratchAllocator<u32>(scratchArena) };
    morphNameOffsets.reserve(static_cast<usize>(morphCount));
    m_morphs.reserve(static_cast<usize>(morphCount));
    for(u64 morphIndex = 0; morphIndex < morphCount; ++morphIndex){
        __hidden_deformable_geometry_asset::DeformableMorphHeaderBinary morphHeader;
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
    __hidden_deformable_geometry_asset::DeformableDisplacementBinary displacementBinary;
    if(!ReadPOD(binary, cursor, displacementBinary)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed displacement descriptor"));
        return false;
    }
    if(displacementBinary.reserved != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unsupported displacement reserved data"));
        return false;
    }
    m_displacement = __hidden_deformable_geometry_asset::BuildDisplacement(displacementBinary);
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

bool DeformableGeometryAssetCodec::deserialize(
    const Name& virtualPath,
    const Core::Assets::AssetBytes& binary,
    UniquePtr<Core::Assets::IAsset>& outAsset
)const{
    return Core::Assets::DeserializeTypedAsset<DeformableGeometry>(virtualPath, binary, outAsset);
}

bool DeformableDisplacementTextureAssetCodec::deserialize(
    const Name& virtualPath,
    const Core::Assets::AssetBytes& binary,
    UniquePtr<Core::Assets::IAsset>& outAsset
)const{
    return Core::Assets::DeserializeTypedAsset<DeformableDisplacementTexture>(virtualPath, binary, outAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DeformableGeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(DeformableGeometry::s_AssetTypeText)
        );
        return false;
    }

    const DeformableGeometry& geometry = static_cast<const DeformableGeometry&>(asset);
    if(!geometry.validatePayload())
        return false;

    usize reserveBytes = __hidden_deformable_geometry_asset::s_DeformableGeometryHeaderBytes;
    bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, geometry.restVertices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.indices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.skin())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.inverseBindMatrices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.sourceSamples())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.editMaskPerTriangle())
    ;
    for(const DeformableMorph& morph : geometry.morphs()){
        canReserve = canReserve
            && AddBinaryReserveBytes(
                reserveBytes,
                sizeof(__hidden_deformable_geometry_asset::DeformableMorphHeaderBinary)
            )
            && AddBinaryVectorReserveBytes(reserveBytes, morph.deltas)
        ;
    }
    canReserve = canReserve
        && AddBinaryReserveBytes(reserveBytes, sizeof(__hidden_deformable_geometry_asset::DeformableDisplacementBinary))
    ;

    usize stringTableReserveBytes = 0u;
    bool canReserveStringTable = true;
    for(const DeformableMorph& morph : geometry.morphs())
        canReserveStringTable = canReserveStringTable && ::AddStringTableTextReserveBytes(stringTableReserveBytes, morph.nameText);
    if(DeformableDisplacementModeUsesTexture(geometry.displacement().mode)){
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

    AppendPOD(outBinary, __hidden_deformable_geometry_asset::s_DeformableGeometryMagic);
    AppendPOD(outBinary, __hidden_deformable_geometry_asset::s_DeformableGeometryVersion);
    AppendPOD(outBinary, geometry.geometryClass());
    AppendPOD(outBinary, static_cast<u64>(geometry.restVertices().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.indices().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.skin().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.skeletonJointCount()));
    AppendPOD(outBinary, static_cast<u64>(geometry.inverseBindMatrices().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.sourceSamples().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.editMaskPerTriangle().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.morphs().size()));
    const usize stringTableByteCountOffset = outBinary.size();
    AppendPOD(outBinary, static_cast<u64>(0u));

    const tchar* const serializeFailureContext = NWB_TEXT("DeformableGeometryAssetCodec::serialize");
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

    for(const DeformableMorph& morph : geometry.morphs()){
        __hidden_deformable_geometry_asset::DeformableMorphHeaderBinary morphHeader;
        morphHeader.deltaCount = static_cast<u64>(morph.deltas.size());
        if(
            !__hidden_deformable_geometry_asset::StableTextMatchesName(morph.nameText, morph.name)
            || !::AppendStringTableText(
                stringTable,
                morph.nameText.view(),
                morphHeader.nameOffset
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: morph '{}' is missing stable name text")
                , StringConvert(morph.name.c_str())
            );
            return false;
        }
        AppendPOD(outBinary, morphHeader);
        if(!appendVector(morph.deltas, NWB_TEXT("morph deltas")))
            return false;
    }
    __hidden_deformable_geometry_asset::DeformableDisplacementBinary displacementBinary;
    if(
        !__hidden_deformable_geometry_asset::BuildDisplacementBinary(
            geometry.displacement(),
            geometry.displacementTextureVirtualPathText(),
            stringTable,
            displacementBinary
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: displacement texture is missing stable virtual path text"));
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

bool DeformableDisplacementTextureAssetCodec::serialize(
    const Core::Assets::IAsset& asset,
    Core::Assets::AssetBytes& outBinary)const
{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTextureAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(DeformableDisplacementTexture::s_AssetTypeText)
        );
        return false;
    }

    const DeformableDisplacementTexture& texture = static_cast<const DeformableDisplacementTexture&>(asset);
    if(!texture.validatePayload())
        return false;

    usize reserveBytes = sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u64);
    if(!AddBinaryVectorReserveBytes(reserveBytes, texture.texels())){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTextureAssetCodec::serialize failed: payload size overflows"));
        return false;
    }

    outBinary.clear();
    outBinary.reserve(reserveBytes);
    AppendPOD(outBinary, __hidden_deformable_geometry_asset::s_DeformableDisplacementTextureMagic);
    AppendPOD(outBinary, __hidden_deformable_geometry_asset::s_DeformableDisplacementTextureVersion);
    AppendPOD(outBinary, texture.width());
    AppendPOD(outBinary, texture.height());
    AppendPOD(outBinary, static_cast<u64>(texture.texels().size()));
    return GeometryBinaryPayload::AppendVector(
        outBinary,
        texture.texels(),
        NWB_TEXT("DeformableDisplacementTextureAssetCodec::serialize"),
        NWB_TEXT("texels")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

