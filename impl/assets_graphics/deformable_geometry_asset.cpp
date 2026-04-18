// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_geometry_asset.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_auto_registration.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_DeformableGeometryMagic = 0x44474F31u; // DGO1
static constexpr u32 s_DeformableGeometryVersionV1 = 1u;
static constexpr u32 s_DeformableGeometryVersion = 2u;
static constexpr f32 s_BarycentricSumEpsilon = 0.001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;
static constexpr f32 s_RestFrameLengthSquaredEpsilon = 0.000001f;
static constexpr f32 s_RestFrameUnitLengthSquaredEpsilon = 0.01f;
static constexpr f32 s_RestFrameOrthogonalityEpsilon = 0.01f;
static constexpr f32 s_TangentHandednessEpsilon = 0.000001f;
static constexpr f32 s_TangentHandednessUnitEpsilon = 0.001f;
static constexpr f32 s_TriangleAreaLengthSquaredEpsilon = 0.000000000001f;


UniquePtr<Core::Assets::IAssetCodec> CreateDeformableGeometryAssetCodec(){
    return MakeUnique<DeformableGeometryAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_DeformableGeometryAssetCodecAutoRegistrar(&CreateDeformableGeometryAssetCodec);


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

    outValues.resize(static_cast<usize>(count));
    if(payloadBytes > 0)
        NWB_MEMCPY(outValues.data(), payloadBytes, binary.data() + inOutCursor, payloadBytes);
    inOutCursor += payloadBytes;
    return true;
}

template<typename T>
[[nodiscard]] bool AppendVectorPayload(
    Core::Assets::AssetBytes& outBinary,
    const Vector<T>& values,
    const tchar* label
){
    if(values.size() > Limit<usize>::s_Max / sizeof(T)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometryAssetCodec::serialize failed: '{}' payload byte size overflows"), label);
        return false;
    }

    const usize payloadBytes = values.size() * sizeof(T);
    if(payloadBytes > Limit<usize>::s_Max - outBinary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry serialize failed: '{}' payload overflows"), label);
        return false;
    }

    const usize begin = outBinary.size();
    outBinary.resize(begin + payloadBytes);
    if(payloadBytes > 0)
        NWB_MEMCPY(outBinary.data() + begin, payloadBytes, values.data(), payloadBytes);
    return true;
}

[[nodiscard]] bool IsFiniteFloat2(const Float2Data& value){
    return IsFinite(value.x) && IsFinite(value.y);
}

[[nodiscard]] bool IsFiniteFloat3(const Float3Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool IsFiniteFloat4(const Float4Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

[[nodiscard]] f32 LengthSquared3(const f32 x, const f32 y, const f32 z){
    return (x * x) + (y * y) + (z * z);
}

[[nodiscard]] f32 AbsF32(const f32 value){
    return value < 0.f ? -value : value;
}

[[nodiscard]] f32 Dot3(const Float3Data& lhs, const Float3Data& rhs){
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

[[nodiscard]] Float3Data Subtract3(const Float3Data& lhs, const Float3Data& rhs){
    return Float3Data(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
}

[[nodiscard]] Float3Data Cross3(const Float3Data& lhs, const Float3Data& rhs){
    return Float3Data(
        (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.z * rhs.x) - (lhs.x * rhs.z),
        (lhs.x * rhs.y) - (lhs.y * rhs.x)
    );
}

[[nodiscard]] bool NearlyOne(const f32 value){
    const f32 difference = value > 1.f ? value - 1.f : 1.f - value;
    return difference <= s_BarycentricSumEpsilon;
}

[[nodiscard]] bool NearlySignedOne(const f32 value){
    const f32 magnitude = value < 0.f ? -value : value;
    const f32 difference = magnitude > 1.f ? magnitude - 1.f : 1.f - magnitude;
    return difference <= s_TangentHandednessUnitEpsilon;
}

[[nodiscard]] bool NearlyUnitLengthSquared(const f32 value){
    const f32 difference = value > 1.f ? value - 1.f : 1.f - value;
    return difference <= s_RestFrameUnitLengthSquaredEpsilon;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DeformableGeometry::validatePayload()const{
    const TString geometryPathText = virtualPath()
        ? StringConvert(virtualPath().c_str())
        : TString(NWB_TEXT("<unnamed>"))
    ;

    if(m_restVertices.empty() || m_indices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' has incomplete rest/index payload"),
            geometryPathText
        );
        return false;
    }
    if(m_restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || m_indices.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' exceeds u32 vertex/index count limits"),
            geometryPathText
        );
        return false;
    }
    if((m_indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' index count {} is not a multiple of 3"),
            geometryPathText,
            m_indices.size()
        );
        return false;
    }

    for(usize i = 0; i < m_restVertices.size(); ++i){
        const DeformableVertexRest& vertex = m_restVertices[i];
        if(!__hidden_assets::IsFiniteFloat3(vertex.position)
            || !__hidden_assets::IsFiniteFloat3(vertex.normal)
            || !__hidden_assets::IsFiniteFloat4(vertex.tangent)
            || !__hidden_assets::IsFiniteFloat2(vertex.uv0)
            || !__hidden_assets::IsFiniteFloat4(vertex.color0)
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} contains non-finite data"),
                geometryPathText,
                i
            );
            return false;
        }
        const f32 normalLengthSquared = __hidden_assets::LengthSquared3(vertex.normal.x, vertex.normal.y, vertex.normal.z);
        const f32 tangentLengthSquared = __hidden_assets::LengthSquared3(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
        const Float3Data tangentVector(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
        const f32 tangentHandedness = vertex.tangent.w < 0.f ? -vertex.tangent.w : vertex.tangent.w;
        const Float3Data frameCross = __hidden_assets::Cross3(
            vertex.normal,
            tangentVector
        );
        const f32 frameCrossLengthSquared = __hidden_assets::LengthSquared3(frameCross.x, frameCross.y, frameCross.z);
        if(normalLengthSquared <= __hidden_assets::s_RestFrameLengthSquaredEpsilon
            || tangentLengthSquared <= __hidden_assets::s_RestFrameLengthSquaredEpsilon
            || tangentHandedness <= __hidden_assets::s_TangentHandednessEpsilon
            || !__hidden_assets::NearlySignedOne(vertex.tangent.w)
            || frameCrossLengthSquared <= __hidden_assets::s_RestFrameLengthSquaredEpsilon
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} has a degenerate normal/tangent frame"),
                geometryPathText,
                i
            );
            return false;
        }
        const f32 frameDot = __hidden_assets::Dot3(vertex.normal, tangentVector);
        if(!__hidden_assets::NearlyUnitLengthSquared(normalLengthSquared)
            || !__hidden_assets::NearlyUnitLengthSquared(tangentLengthSquared)
            || __hidden_assets::AbsF32(frameDot) > __hidden_assets::s_RestFrameOrthogonalityEpsilon
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} has an invalid normal/tangent frame"),
                geometryPathText,
                i
            );
            return false;
        }
    }

    for(const u32 index : m_indices){
        if(index >= m_restVertices.size()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' index {} exceeds {} vertices"),
                geometryPathText,
                index,
                m_restVertices.size()
            );
            return false;
        }
    }
    for(usize indexBase = 0; indexBase < m_indices.size(); indexBase += 3u){
        const u32 a = m_indices[indexBase + 0u];
        const u32 b = m_indices[indexBase + 1u];
        const u32 c = m_indices[indexBase + 2u];
        if(a == b || a == c || b == c){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' triangle {} is degenerate"),
                geometryPathText,
                indexBase / 3u
            );
            return false;
        }

        const Float3Data ab = __hidden_assets::Subtract3(m_restVertices[b].position, m_restVertices[a].position);
        const Float3Data ac = __hidden_assets::Subtract3(m_restVertices[c].position, m_restVertices[a].position);
        const Float3Data areaCross = __hidden_assets::Cross3(ab, ac);
        const f32 areaLengthSquared = __hidden_assets::LengthSquared3(areaCross.x, areaCross.y, areaCross.z);
        if(areaLengthSquared <= __hidden_assets::s_TriangleAreaLengthSquaredEpsilon){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' triangle {} has zero area"),
                geometryPathText,
                indexBase / 3u
            );
            return false;
        }
    }

    if(!m_skin.empty() && m_skin.size() != m_restVertices.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' skin count {} does not match vertex count {}"),
            geometryPathText,
            m_skin.size(),
            m_restVertices.size()
        );
        return false;
    }
    for(usize i = 0; i < m_skin.size(); ++i){
        const SkinInfluence4& skin = m_skin[i];
        f32 weightSum = 0.f;
        for(u32 weightIndex = 0; weightIndex < 4u; ++weightIndex){
            if(!IsFinite(skin.weight[weightIndex]) || skin.weight[weightIndex] < 0.f){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' skin weight {} for vertex {} is invalid"),
                    geometryPathText,
                    weightIndex,
                    i
                );
                return false;
            }
            weightSum += skin.weight[weightIndex];
        }
        const f32 weightDifference = weightSum > 1.f ? weightSum - 1.f : 1.f - weightSum;
        if(weightDifference > __hidden_assets::s_SkinWeightSumEpsilon){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' skin weights for vertex {} sum to {}"),
                geometryPathText,
                i,
                weightSum
            );
            return false;
        }
    }

    const usize triangleCount = m_indices.size() / 3u;
    if(!m_sourceSamples.empty() && m_sourceSamples.size() != m_restVertices.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' source samples {} do not match vertices {}"),
            geometryPathText,
            m_sourceSamples.size(),
            m_restVertices.size()
        );
        return false;
    }
    for(usize i = 0; i < m_sourceSamples.size(); ++i){
        const SourceSample& sample = m_sourceSamples[i];
        const f32 barySum = sample.bary[0] + sample.bary[1] + sample.bary[2];
        if(sample.sourceTri >= triangleCount
            || !IsFinite(sample.bary[0])
            || !IsFinite(sample.bary[1])
            || !IsFinite(sample.bary[2])
            || sample.bary[0] < 0.f
            || sample.bary[1] < 0.f
            || sample.bary[2] < 0.f
            || !__hidden_assets::NearlyOne(barySum)
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' source sample {} is invalid"),
                geometryPathText,
                i
            );
            return false;
        }
    }

    if(m_displacement.mode != DeformableDisplacementMode::None
        && m_displacement.mode != DeformableDisplacementMode::ScalarUvRamp
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' has unsupported displacement mode {}"),
            geometryPathText,
            m_displacement.mode
        );
        return false;
    }
    if(m_displacement.padding0 != 0u || m_displacement.padding1 != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' displacement padding is not zeroed"),
            geometryPathText
        );
        return false;
    }
    if(m_displacement.mode == DeformableDisplacementMode::None){
        if(!IsFinite(m_displacement.amplitude) || m_displacement.amplitude != 0.0f){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' inactive displacement must have zero amplitude"),
                geometryPathText
            );
            return false;
        }
    }
    else if(!IsFinite(m_displacement.amplitude)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' displacement amplitude is invalid"),
            geometryPathText
        );
        return false;
    }

    if(m_morphs.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph count exceeds u32 limits"),
            geometryPathText
        );
        return false;
    }
    if(m_morphs.empty())
        return true;

    Core::Alloc::ScratchArena<> scratchArena;
    HashSet<
        NameHash,
        Hasher<NameHash>,
        EqualTo<NameHash>,
        Core::Alloc::ScratchAllocator<NameHash>
    > seenMorphNames(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        Core::Alloc::ScratchAllocator<NameHash>(scratchArena)
    );
    seenMorphNames.reserve(m_morphs.size());
    for(usize morphIndex = 0; morphIndex < m_morphs.size(); ++morphIndex){
        const DeformableMorph& morph = m_morphs[morphIndex];
        if(!morph.name || morph.deltas.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph {} is unnamed or empty"),
                geometryPathText,
                morphIndex
            );
            return false;
        }
        if(!seenMorphNames.insert(morph.name.hash()).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' contains duplicate morph '{}'"),
                geometryPathText,
                StringConvert(morph.name.c_str())
            );
            return false;
        }
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' delta count exceeds u32 limits"),
                geometryPathText,
                StringConvert(morph.name.c_str())
            );
            return false;
        }

        HashSet<
            u32,
            Hasher<u32>,
            EqualTo<u32>,
            Core::Alloc::ScratchAllocator<u32>
        > seenDeltaVertices(
            0,
            Hasher<u32>(),
            EqualTo<u32>(),
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        );
        seenDeltaVertices.reserve(morph.deltas.size());
        for(const DeformableMorphDelta& delta : morph.deltas){
            if(delta.vertexId >= m_restVertices.size()
                || !__hidden_assets::IsFiniteFloat3(delta.deltaPosition)
                || !__hidden_assets::IsFiniteFloat3(delta.deltaNormal)
                || !__hidden_assets::IsFiniteFloat4(delta.deltaTangent)
            ){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' contains an invalid delta"),
                    geometryPathText,
                    StringConvert(morph.name.c_str())
                );
                return false;
            }

            if(!seenDeltaVertices.insert(delta.vertexId).second){
                NWB_LOGGER_ERROR(
                    NWB_TEXT(
                        "DeformableGeometry::validatePayload failed: geometry '{}' morph '{}' has duplicate vertex {}"
                    ),
                    geometryPathText,
                    StringConvert(morph.name.c_str()),
                    delta.vertexId
                );
                return false;
            }
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
    m_sourceSamples.clear();
    m_displacement = DeformableDisplacement{};
    m_morphs.clear();

    usize cursor = 0;
    u32 magic = 0;
    u32 version = 0;
    u64 vertexCount = 0;
    u64 indexCount = 0;
    u64 skinCount = 0;
    u64 sourceSampleCount = 0;
    u64 morphCount = 0;
    if(!ReadPOD(binary, cursor, magic)
        || !ReadPOD(binary, cursor, version)
        || !ReadPOD(binary, cursor, vertexCount)
        || !ReadPOD(binary, cursor, indexCount)
        || !ReadPOD(binary, cursor, skinCount)
        || !ReadPOD(binary, cursor, sourceSampleCount)
        || !ReadPOD(binary, cursor, morphCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed header"));
        return false;
    }

    if(magic != __hidden_assets::s_DeformableGeometryMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: invalid magic"));
        return false;
    }
    if(version != __hidden_assets::s_DeformableGeometryVersionV1 && version != __hidden_assets::s_DeformableGeometryVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: unsupported version {}"), version);
        return false;
    }
    if(vertexCount > static_cast<u64>(Limit<u32>::s_Max)
        || indexCount > static_cast<u64>(Limit<u32>::s_Max)
        || skinCount > static_cast<u64>(Limit<u32>::s_Max)
        || sourceSampleCount > static_cast<u64>(Limit<u32>::s_Max)
        || morphCount > static_cast<u64>(Limit<u32>::s_Max)
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
    if(sourceSampleCount != 0u && sourceSampleCount != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::loadBinary failed: source sample count must be empty or match vertex count")
        );
        return false;
    }

    if(!__hidden_assets::ReadVectorPayload(binary, cursor, vertexCount, m_restVertices, NWB_TEXT("rest vertices")))
        return false;
    if(!__hidden_assets::ReadVectorPayload(binary, cursor, indexCount, m_indices, NWB_TEXT("indices")))
        return false;
    if(!__hidden_assets::ReadVectorPayload(binary, cursor, skinCount, m_skin, NWB_TEXT("skin")))
        return false;
    if(!__hidden_assets::ReadVectorPayload(binary, cursor, sourceSampleCount, m_sourceSamples, NWB_TEXT("source samples")))
        return false;

    if(morphCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: morph count exceeds addressable size"));
        return false;
    }
    if(morphCount > 0u){
        constexpr usize minMorphHeaderBytes = sizeof(NameHash) + sizeof(u64);
        const usize remainingBytes = cursor <= binary.size() ? binary.size() - cursor : 0u;
        if(morphCount > static_cast<u64>(remainingBytes / minMorphHeaderBytes)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed morph table"));
            return false;
        }
    }
    m_morphs.reserve(static_cast<usize>(morphCount));
    for(u64 morphIndex = 0; morphIndex < morphCount; ++morphIndex){
        NameHash morphNameHash = {};
        u64 deltaCount = 0;
        if(!ReadPOD(binary, cursor, morphNameHash) || !ReadPOD(binary, cursor, deltaCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed morph header"));
            return false;
        }
        if(deltaCount > static_cast<u64>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: morph delta count exceeds u32 limits"));
            return false;
        }

        DeformableMorph morph;
        morph.name = Name(morphNameHash);
        if(!__hidden_assets::ReadVectorPayload(binary, cursor, deltaCount, morph.deltas, NWB_TEXT("morph deltas")))
            return false;
        m_morphs.push_back(Move(morph));
    }
    if(version >= __hidden_assets::s_DeformableGeometryVersion){
        if(!ReadPOD(binary, cursor, m_displacement)){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed displacement descriptor"));
            return false;
        }
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

    outBinary.clear();
    AppendPOD(outBinary, __hidden_assets::s_DeformableGeometryMagic);
    AppendPOD(outBinary, __hidden_assets::s_DeformableGeometryVersion);
    AppendPOD(outBinary, static_cast<u64>(geometry.restVertices().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.indices().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.skin().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.sourceSamples().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.morphs().size()));

    if(!__hidden_assets::AppendVectorPayload(outBinary, geometry.restVertices(), NWB_TEXT("rest vertices")))
        return false;
    if(!__hidden_assets::AppendVectorPayload(outBinary, geometry.indices(), NWB_TEXT("indices")))
        return false;
    if(!__hidden_assets::AppendVectorPayload(outBinary, geometry.skin(), NWB_TEXT("skin")))
        return false;
    if(!__hidden_assets::AppendVectorPayload(outBinary, geometry.sourceSamples(), NWB_TEXT("source samples")))
        return false;

    for(const DeformableMorph& morph : geometry.morphs()){
        AppendPOD(outBinary, morph.name.hash());
        AppendPOD(outBinary, static_cast<u64>(morph.deltas.size()));
        if(!__hidden_assets::AppendVectorPayload(outBinary, morph.deltas, NWB_TEXT("morph deltas")))
            return false;
    }
    AppendPOD(outBinary, geometry.displacement());

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

