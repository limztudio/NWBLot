// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_geometry_asset.h"

#include "deformable_geometry_validation.h"

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
static constexpr u32 s_DeformableGeometryVersion = 7u;
static constexpr u32 s_DeformableDisplacementTextureMagic = 0x44445431u; // DDT1
static constexpr u32 s_DeformableDisplacementTextureVersion = 1u;
static constexpr u32 s_DeformableSkeletonJointLimit = static_cast<u32>(Limit<u16>::s_Max) + 1u;
#if defined(NWB_COOK)
static constexpr usize s_DeformableGeometryHeaderBytes =
    sizeof(u32) + // magic
    sizeof(u32) + // version
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
    DeformableDisplacementBinary& outBinary)
{
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


template<typename T>
[[nodiscard]] bool ComputePayloadBytes(const u64 count, usize& outBytes, const tchar* label){
    outBytes = 0;
    if(count > static_cast<u64>(Limit<usize>::s_Max / sizeof(T))){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry: '{}' payload byte size overflows"), label);
        return false;
    }

    outBytes = static_cast<usize>(count) * sizeof(T);
    return true;
}

#if defined(NWB_COOK)
[[nodiscard]] static bool AddReserveBytes(usize& inOutBytes, const usize additionalBytes){
    if(additionalBytes > Limit<usize>::s_Max - inOutBytes)
        return false;

    inOutBytes += additionalBytes;
    return true;
}

template<typename T>
[[nodiscard]] static bool AddVectorReserveBytes(usize& inOutBytes, const Vector<T>& values){
    if(values.size() > Limit<usize>::s_Max / sizeof(T))
        return false;

    return AddReserveBytes(inOutBytes, values.size() * sizeof(T));
}
#endif

template<typename T>
static void ResizeVectorPayload(Vector<T>& outValues, const usize count){
    outValues.resize(count);
}

static void ResizeVectorPayload(Vector<DeformableJointMatrix>& outValues, const usize count){
    outValues.resize(count, DeformableJointMatrix{});
}

template<typename T>
[[nodiscard]] bool ReadVectorPayload(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    Vector<T>& outValues,
    const tchar* label
){
    usize payloadBytes = 0;
    if(!ComputePayloadBytes<T>(count, payloadBytes, label))
        return false;
    if(inOutCursor > binary.size() || payloadBytes > binary.size() - inOutCursor){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed '{}' payload"), label);
        return false;
    }

    ResizeVectorPayload(outValues, static_cast<usize>(count));
    if(payloadBytes > 0)
        NWB_MEMCPY(outValues.data(), payloadBytes, binary.data() + inOutCursor, payloadBytes);
    inOutCursor += payloadBytes;
    return true;
}

template<typename T, typename Allocator>
[[nodiscard]] bool AppendVectorPayload(Core::Assets::AssetBytes& outBinary, const Vector<T, Allocator>& values, const tchar* label){
    static_assert(IsTriviallyCopyable_V<T>, "AppendVectorPayload requires trivially-copyable payload elements");
    if(values.size() > Limit<usize>::s_Max / sizeof(T)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: '{}' payload byte size overflows"), label);
        return false;
    }

    const usize beginOffset = outBinary.size();
    const usize payloadBytes = values.size() * sizeof(T);
    if(payloadBytes > Limit<usize>::s_Max - beginOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry serialize failed: '{}' payload overflows"), label);
        return false;
    }

    if(payloadBytes > 0){
        outBinary.resize(beginOffset + payloadBytes);
        NWB_MEMCPY(
            outBinary.data() + beginOffset,
            payloadBytes,
            reinterpret_cast<const u8*>(values.data()),
            payloadBytes
        );
    }
    return true;
}

void LogGeometryMorphPayloadFailure(
    const TString& geometryPathText,
    const Vector<DeformableMorph>& morphs,
    const DeformableValidation::MorphPayloadFailureInfo& failure)
{
    const TString morphNameText = DeformableValidation::MorphPayloadFailureMorphNameText(morphs, failure);

    switch(failure.reason){
    case DeformableValidation::MorphPayloadFailure::MorphCountLimit:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph count exceeds u32 limits"),
            geometryPathText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::EmptyMorph:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph {} is unnamed or empty"),
            geometryPathText,
            failure.morphIndex
        );
        break;
    case DeformableValidation::MorphPayloadFailure::DuplicateMorphName:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' contains duplicate morph '{}'"),
            geometryPathText,
            morphNameText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::MorphDeltaCountLimit:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' delta count exceeds u32 limits"),
            geometryPathText,
            morphNameText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::InvalidMorphDelta:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' delta {} is invalid"),
            geometryPathText,
            morphNameText,
            failure.deltaIndex
        );
        break;
    case DeformableValidation::MorphPayloadFailure::DuplicateMorphDeltaVertex:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' has duplicate vertex {}"),
            geometryPathText,
            morphNameText,
            failure.vertexId
        );
        break;
    case DeformableValidation::MorphPayloadFailure::None:
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
        return virtualPath()
            ? StringConvert(virtualPath().c_str())
            : TString(NWB_TEXT("<unnamed>"))
        ;
    };

    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(m_width == 0u || m_height == 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' dimensions are empty"),
            texturePathText()
        );
        return false;
    }
    if(m_width > Limit<u32>::s_Max / m_height){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' dimensions overflow"),
            texturePathText()
        );
        return false;
    }

    const usize requiredTexelCount = static_cast<usize>(m_width) * static_cast<usize>(m_height);
    if(m_texels.size() != requiredTexelCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' texel count {} does not match dimensions {}x{}"),
            texturePathText(),
            m_texels.size(),
            m_width,
            m_height
        );
        return false;
    }

    for(usize i = 0; i < m_texels.size(); ++i){
        const Float4U& texel = m_texels[i];
        if(!IsFinite(texel.x) || !IsFinite(texel.y) || !IsFinite(texel.z) || !IsFinite(texel.w)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableDisplacementTexture::validatePayload failed: texture '{}' texel {} is not finite"),
                texturePathText(),
                i
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
    if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, texelCount, m_texels, NWB_TEXT("texels")))
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
        return virtualPath()
            ? StringConvert(virtualPath().c_str())
            : TString(NWB_TEXT("<unnamed>"))
        ;
    };

    const usize vertexCount = m_restVertices.size();
    const usize indexCount = m_indices.size();
    if(m_restVertices.empty() || m_indices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' has incomplete rest/index payload"),
            geometryPathText()
        );
        return false;
    }
    if(
        vertexCount > static_cast<usize>(Limit<u32>::s_Max)
        || indexCount > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' exceeds u32 vertex/index count limits"),
            geometryPathText()
        );
        return false;
    }
    if((indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' index count {} is not a multiple of 3"),
            geometryPathText(),
            indexCount
        );
        return false;
    }

    for(usize i = 0; i < vertexCount; ++i){
        const DeformableVertexRest& vertex = m_restVertices[i];
        const DeformableValidation::RestVertexPayloadFailure::Enum restVertexFailure =
            DeformableValidation::FindRestVertexPayloadFailure(vertex)
        ;
        if(restVertexFailure == DeformableValidation::RestVertexPayloadFailure::NonFiniteData){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} contains non-finite data"),
                geometryPathText(),
                i
            );
            return false;
        }
        if(restVertexFailure == DeformableValidation::RestVertexPayloadFailure::DegenerateFrame){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} has a degenerate normal/tangent frame"),
                geometryPathText(),
                i
            );
            return false;
        }
        if(restVertexFailure == DeformableValidation::RestVertexPayloadFailure::InvalidFrame){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} has an invalid normal/tangent frame"),
                geometryPathText(),
                i
            );
            return false;
        }
    }

    for(usize indexBase = 0; indexBase < indexCount; indexBase += 3u){
        const u32 a = m_indices[indexBase + 0u];
        const u32 b = m_indices[indexBase + 1u];
        const u32 c = m_indices[indexBase + 2u];
        if(a >= vertexCount || b >= vertexCount || c >= vertexCount){
            u32 invalidIndex = a;
            if(a < vertexCount)
                invalidIndex = b >= vertexCount ? b : c;
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' index {} exceeds {} vertices"),
                geometryPathText(),
                invalidIndex,
                vertexCount
            );
            return false;
        }
        if(a == b || a == c || b == c){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' triangle {} is degenerate"),
                geometryPathText(),
                indexBase / 3u
            );
            return false;
        }

        if(!DeformableValidation::ValidTriangleArea(m_restVertices, a, b, c)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' triangle {} has zero area"),
                geometryPathText(),
                indexBase / 3u
            );
            return false;
        }
    }

    if(!m_skin.empty() && m_skin.size() != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' skin count {} does not match vertex count {}"),
            geometryPathText(),
            m_skin.size(),
            vertexCount
        );
        return false;
    }
    if(!m_skin.empty() && m_skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' has skin but no skeleton joint count"),
            geometryPathText()
        );
        return false;
    }
    if(m_skeletonJointCount > __hidden_deformable_geometry_asset::s_DeformableSkeletonJointLimit){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' skeleton joint count {} exceeds skin stream limits"),
            geometryPathText(),
            m_skeletonJointCount
        );
        return false;
    }
    if(!DeformableValidation::ValidInverseBindMatrices(m_inverseBindMatrices, m_skeletonJointCount)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' inverse bind matrices must be empty or match a valid skeleton"),
            geometryPathText()
        );
        return false;
    }
    for(usize i = 0; i < m_skin.size(); ++i){
        if(!DeformableValidation::ValidSkinInfluence(m_skin[i])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' skin weights for vertex {} are invalid"),
                geometryPathText(),
                i
            );
            return false;
        }
        for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
            const u32 joint = static_cast<u32>(m_skin[i].joint[influenceIndex]);
            if(joint >= m_skeletonJointCount){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' skin joint {} for vertex {} exceeds skeleton joint count {}"),
                    geometryPathText(),
                    joint,
                    i,
                    m_skeletonJointCount
                );
                return false;
            }
        }
    }

    const usize triangleCount = indexCount / 3u;
    if(!m_sourceSamples.empty() && m_sourceSamples.size() != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' source samples {} do not match vertices {}"),
            geometryPathText(),
            m_sourceSamples.size(),
            vertexCount
        );
        return false;
    }
    for(usize i = 0; i < m_sourceSamples.size(); ++i){
        const SourceSample& sample = m_sourceSamples[i];
        if(!DeformableValidation::ValidSourceSample(sample, static_cast<u32>(triangleCount))){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' source sample {} is invalid"),
                geometryPathText(),
                i
            );
            return false;
        }
    }

    if(!m_editMaskPerTriangle.empty() && m_editMaskPerTriangle.size() != triangleCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' edit mask count {} does not match triangle count {}"),
            geometryPathText(),
            m_editMaskPerTriangle.size(),
            triangleCount
        );
        return false;
    }
    for(usize i = 0; i < m_editMaskPerTriangle.size(); ++i){
        if(!ValidDeformableEditMaskFlags(m_editMaskPerTriangle[i])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' edit mask {} is invalid"),
                geometryPathText(),
                i
            );
            return false;
        }
    }

    if(!ValidDeformableDisplacementDescriptor(m_displacement)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' displacement descriptor is invalid"),
            geometryPathText()
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
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' displacement texture path text does not match asset ref"),
            geometryPathText()
        );
        return false;
    }

    const DeformableValidation::MorphPayloadFailureInfo morphFailure =
        DeformableValidation::FindMorphPayloadFailure(m_morphs, vertexCount)
    ;
    if(morphFailure.reason != DeformableValidation::MorphPayloadFailure::None){
        __hidden_deformable_geometry_asset::LogGeometryMorphPayloadFailure(geometryPathText(), m_morphs, morphFailure);
        return false;
    }
    for(const DeformableMorph& morph : m_morphs){
        if(!morph.nameText.empty() && !__hidden_deformable_geometry_asset::StableTextMatchesName(morph.nameText, morph.name)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph name text does not match morph name"),
                geometryPathText()
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
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::loadBinary failed: inverse bind matrix count must be empty or match skeleton joint count")
        );
        return false;
    }
    if(sourceSampleCount != 0u && sourceSampleCount != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::loadBinary failed: source sample count must be empty or match vertex count")
        );
        return false;
    }
    const u64 triangleCount = indexCount / 3u;
    if(editMaskCount != 0u && editMaskCount != triangleCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::loadBinary failed: edit mask count must be empty or match triangle count")
        );
        return false;
    }

    if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, vertexCount, m_restVertices, NWB_TEXT("rest vertices")))
        return false;
    if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, indexCount, m_indices, NWB_TEXT("indices")))
        return false;
    if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, skinCount, m_skin, NWB_TEXT("skin")))
        return false;
    m_skeletonJointCount = static_cast<u32>(skeletonJointCount);
    if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, inverseBindMatrixCount, m_inverseBindMatrices, NWB_TEXT("inverse bind matrices")))
        return false;
    if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, sourceSampleCount, m_sourceSamples, NWB_TEXT("source samples")))
        return false;
    if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, editMaskCount, m_editMaskPerTriangle, NWB_TEXT("edit masks")))
        return false;

    if(morphCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: morph count exceeds addressable size"));
        return false;
    }
    if(morphCount > 0u){
        constexpr usize minMorphHeaderBytes = sizeof(__hidden_deformable_geometry_asset::DeformableMorphHeaderBinary);
        const usize remainingBytes = cursor <= binary.size() ? binary.size() - cursor : 0u;
        if(morphCount > static_cast<u64>(remainingBytes / minMorphHeaderBytes)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed morph table"));
            return false;
        }
    }
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> morphNameOffsets{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
    };
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
        if(!__hidden_deformable_geometry_asset::ReadVectorPayload(binary, cursor, morphHeader.deltaCount, morph.deltas, NWB_TEXT("morph deltas")))
            return false;
        morphNameOffsets.push_back(morphHeader.nameOffset);
        m_morphs.push_back(Move(morph));
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
    auto asset = MakeUnique<DeformableGeometry>(virtualPath);
    if(!asset->loadBinary(binary))
        return false;

    outAsset = Move(asset);
    return true;
}

bool DeformableDisplacementTextureAssetCodec::deserialize(
    const Name& virtualPath,
    const Core::Assets::AssetBytes& binary,
    UniquePtr<Core::Assets::IAsset>& outAsset
)const{
    auto asset = MakeUnique<DeformableDisplacementTexture>(virtualPath);
    if(!asset->loadBinary(binary))
        return false;

    outAsset = Move(asset);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DeformableGeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'"),
            StringConvert(asset.assetType().c_str()),
            StringConvert(DeformableGeometry::s_AssetTypeText)
        );
        return false;
    }

    const DeformableGeometry& geometry = static_cast<const DeformableGeometry&>(asset);
    if(!geometry.validatePayload())
        return false;

    usize reserveBytes = __hidden_deformable_geometry_asset::s_DeformableGeometryHeaderBytes;
    bool canReserve = __hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, geometry.restVertices())
        && __hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, geometry.indices())
        && __hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, geometry.skin())
        && __hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, geometry.inverseBindMatrices())
        && __hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, geometry.sourceSamples())
        && __hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, geometry.editMaskPerTriangle())
    ;
    for(const DeformableMorph& morph : geometry.morphs()){
        canReserve = canReserve
            && __hidden_deformable_geometry_asset::AddReserveBytes(
                reserveBytes,
                sizeof(__hidden_deformable_geometry_asset::DeformableMorphHeaderBinary)
            )
            && __hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, morph.deltas)
        ;
    }
    canReserve = canReserve
        && __hidden_deformable_geometry_asset::AddReserveBytes(reserveBytes, sizeof(__hidden_deformable_geometry_asset::DeformableDisplacementBinary))
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
        canReserve = __hidden_deformable_geometry_asset::AddReserveBytes(reserveBytes, stringTableReserveBytes);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> stringTable{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    if(canReserveStringTable)
        stringTable.reserve(stringTableReserveBytes);

    AppendPOD(outBinary, __hidden_deformable_geometry_asset::s_DeformableGeometryMagic);
    AppendPOD(outBinary, __hidden_deformable_geometry_asset::s_DeformableGeometryVersion);
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

    if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, geometry.restVertices(), NWB_TEXT("rest vertices")))
        return false;
    if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, geometry.indices(), NWB_TEXT("indices")))
        return false;
    if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, geometry.skin(), NWB_TEXT("skin")))
        return false;
    if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, geometry.inverseBindMatrices(), NWB_TEXT("inverse bind matrices")))
        return false;
    if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, geometry.sourceSamples(), NWB_TEXT("source samples")))
        return false;
    if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, geometry.editMaskPerTriangle(), NWB_TEXT("edit masks")))
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
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: morph '{}' is missing stable name text"),
                StringConvert(morph.name.c_str())
            );
            return false;
        }
        AppendPOD(outBinary, morphHeader);
        if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, morph.deltas, NWB_TEXT("morph deltas")))
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
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: displacement texture is missing stable virtual path text")
        );
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
    if(!__hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, stringTable, NWB_TEXT("string table")))
        return false;

    return true;
}

bool DeformableDisplacementTextureAssetCodec::serialize(
    const Core::Assets::IAsset& asset,
    Core::Assets::AssetBytes& outBinary)const
{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableDisplacementTextureAssetCodec::serialize failed: invalid asset type '{}', expected '{}'"),
            StringConvert(asset.assetType().c_str()),
            StringConvert(DeformableDisplacementTexture::s_AssetTypeText)
        );
        return false;
    }

    const DeformableDisplacementTexture& texture = static_cast<const DeformableDisplacementTexture&>(asset);
    if(!texture.validatePayload())
        return false;

    usize reserveBytes = sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u64);
    if(!__hidden_deformable_geometry_asset::AddVectorReserveBytes(reserveBytes, texture.texels())){
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
    return __hidden_deformable_geometry_asset::AppendVectorPayload(outBinary, texture.texels(), NWB_TEXT("texels"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

