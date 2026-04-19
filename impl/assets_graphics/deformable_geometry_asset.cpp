// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_geometry_asset.h"

#include "deformable_geometry_validation.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_auto_registration.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_DeformableGeometryMagic = 0x44474F31u; // DGO1
static constexpr u32 s_DeformableGeometryVersion = 2u;


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
        if(!DeformableValidation::ValidRestVertex(vertex)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} contains non-finite data"),
                geometryPathText,
                i
            );
            return false;
        }
        if(!DeformableValidation::ValidRestVertexFrameBasis(vertex)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' rest vertex {} has a degenerate normal/tangent frame"),
                geometryPathText,
                i
            );
            return false;
        }
        if(!DeformableValidation::ValidRestVertexFrame(vertex)){
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

        if(!DeformableValidation::ValidTriangle(m_restVertices, a, b, c)){
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
        if(!DeformableValidation::ValidSkinInfluence(m_skin[i])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: '{}' skin weights for vertex {} are invalid"),
                geometryPathText,
                i
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
        if(!DeformableValidation::ValidSourceSample(sample, static_cast<u32>(triangleCount))){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' source sample {} is invalid"),
                geometryPathText,
                i
            );
            return false;
        }
    }

    if(!ValidDeformableDisplacementDescriptor(m_displacement)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableGeometry::validatePayload failed: geometry '{}' displacement descriptor is invalid"),
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
            if(!DeformableValidation::ValidMorphDelta(delta, m_restVertices.size())){
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
    if(version != __hidden_assets::s_DeformableGeometryVersion){
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
    if(!ReadPOD(binary, cursor, m_displacement)){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableGeometry::loadBinary failed: malformed displacement descriptor"));
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

