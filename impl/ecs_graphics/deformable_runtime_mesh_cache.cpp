// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_runtime_mesh_cache.h"

#include <core/alloc/scratch.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_runtime_mesh_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_KnownDirtyFlags = RuntimeMeshDirtyFlag::All;
static constexpr RuntimeMeshDirtyFlags s_GpuUploadHandledDirtyFlags =
    RuntimeMeshDirtyFlag::TopologyDirty | RuntimeMeshDirtyFlag::AttributesDirty | RuntimeMeshDirtyFlag::GpuUploadDirty
;
static constexpr f32 s_BarycentricSumEpsilon = 0.001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;
static constexpr f32 s_RestFrameLengthSquaredEpsilon = 0.000001f;
static constexpr f32 s_RestFrameUnitLengthSquaredEpsilon = 0.01f;
static constexpr f32 s_RestFrameOrthogonalityEpsilon = 0.01f;
static constexpr f32 s_TangentHandednessEpsilon = 0.000001f;
static constexpr f32 s_TangentHandednessUnitEpsilon = 0.001f;
static constexpr f32 s_TriangleAreaLengthSquaredEpsilon = 0.000000000001f;

[[nodiscard]] RuntimeMeshDirtyFlags SanitizeDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    return static_cast<RuntimeMeshDirtyFlags>(dirtyFlags & s_KnownDirtyFlags);
}

[[nodiscard]] RuntimeMeshDirtyFlags ExpandDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    RuntimeMeshDirtyFlags expanded = SanitizeDirtyFlags(dirtyFlags);
    if((expanded & (RuntimeMeshDirtyFlag::TopologyDirty | RuntimeMeshDirtyFlag::AttributesDirty)) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(
            expanded | RuntimeMeshDirtyFlag::GpuUploadDirty | RuntimeMeshDirtyFlag::DeformerInputDirty
        );
    }
    else if((expanded & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(expanded | RuntimeMeshDirtyFlag::DeformerInputDirty);
    }
    return expanded;
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

[[nodiscard]] bool IsFiniteRestVertex(const DeformableVertexRest& vertex){
    return IsFiniteFloat3(vertex.position)
        && IsFiniteFloat3(vertex.normal)
        && IsFiniteFloat4(vertex.tangent)
        && IsFiniteFloat2(vertex.uv0)
        && IsFiniteFloat4(vertex.color0)
    ;
}

[[nodiscard]] f32 AbsF32(const f32 value){
    return value < 0.0f ? -value : value;
}

[[nodiscard]] f32 LengthSquared3(const f32 x, const f32 y, const f32 z){
    return (x * x) + (y * y) + (z * z);
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

[[nodiscard]] bool NearlyOne(const f32 value, const f32 epsilon){
    return AbsF32(value - 1.0f) <= epsilon;
}

[[nodiscard]] bool NearlySignedOne(const f32 value){
    return AbsF32(AbsF32(value) - 1.0f) <= s_TangentHandednessUnitEpsilon;
}

[[nodiscard]] bool NearlyUnitLengthSquared(const f32 value){
    return AbsF32(value - 1.0f) <= s_RestFrameUnitLengthSquaredEpsilon;
}

[[nodiscard]] bool ValidRestVertexFrame(const DeformableVertexRest& vertex){
    if(!IsFiniteRestVertex(vertex))
        return false;

    const f32 normalLengthSquared = LengthSquared3(vertex.normal.x, vertex.normal.y, vertex.normal.z);
    const f32 tangentLengthSquared = LengthSquared3(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
    const Float3Data tangentVector(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
    const f32 tangentHandedness = AbsF32(vertex.tangent.w);
    const Float3Data frameCross = Cross3(vertex.normal, tangentVector);
    const f32 frameCrossLengthSquared = LengthSquared3(frameCross.x, frameCross.y, frameCross.z);
    if(normalLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentHandedness <= s_TangentHandednessEpsilon
        || !NearlySignedOne(vertex.tangent.w)
        || frameCrossLengthSquared <= s_RestFrameLengthSquaredEpsilon
    )
        return false;

    const f32 frameDot = Dot3(vertex.normal, tangentVector);
    return NearlyUnitLengthSquared(normalLengthSquared)
        && NearlyUnitLengthSquared(tangentLengthSquared)
        && AbsF32(frameDot) <= s_RestFrameOrthogonalityEpsilon
    ;
}

[[nodiscard]] bool ValidBarycentric(const f32 (&bary)[3]){
    const f32 barySum = bary[0] + bary[1] + bary[2];
    return IsFinite(bary[0])
        && IsFinite(bary[1])
        && IsFinite(bary[2])
        && bary[0] >= 0.0f
        && bary[1] >= 0.0f
        && bary[2] >= 0.0f
        && NearlyOne(barySum, s_BarycentricSumEpsilon)
    ;
}

[[nodiscard]] bool ValidSourceSample(const SourceSample& sample, const u32 sourceTriangleCount){
    return sourceTriangleCount != 0u && sample.sourceTri < sourceTriangleCount && ValidBarycentric(sample.bary);
}

[[nodiscard]] bool ValidSkinInfluence(const SkinInfluence4& skin){
    f32 weightSum = 0.0f;
    for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
        const f32 weight = skin.weight[influenceIndex];
        if(!IsFinite(weight) || weight < 0.0f)
            return false;

        weightSum += weight;
        if(!IsFinite(weightSum))
            return false;
    }
    return NearlyOne(weightSum, s_SkinWeightSumEpsilon);
}

[[nodiscard]] bool ValidMorphDelta(const DeformableMorphDelta& delta, const usize vertexCount){
    return delta.vertexId < vertexCount
        && IsFiniteFloat3(delta.deltaPosition)
        && IsFiniteFloat3(delta.deltaNormal)
        && IsFiniteFloat4(delta.deltaTangent)
    ;
}

[[nodiscard]] bool ValidateRuntimeMeshUploadPayload(const DeformableRuntimeMeshInstance& instance){
    const TString sourceText = instance.source.name()
        ? StringConvert(instance.source.name().c_str())
        : TString(NWB_TEXT("<unnamed>"))
    ;

    if(instance.restVertices.empty() || instance.indices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has incomplete rest/index payload"),
            sourceText
        );
        return false;
    }
    if(instance.restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || instance.indices.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' exceeds u32 vertex/index count limits"),
            sourceText
        );
        return false;
    }
    if((instance.indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' index count {} is not a multiple of 3"),
            sourceText,
            instance.indices.size()
        );
        return false;
    }
    if(!ValidDeformableDisplacementDescriptor(instance.displacement)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has an invalid displacement descriptor"),
            sourceText
        );
        return false;
    }

    for(usize vertexIndex = 0; vertexIndex < instance.restVertices.size(); ++vertexIndex){
        if(!ValidRestVertexFrame(instance.restVertices[vertexIndex])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' rest vertex {} has invalid data or frame"),
                sourceText,
                vertexIndex
            );
            return false;
        }
    }

    for(const u32 index : instance.indices){
        if(index >= instance.restVertices.size()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' index {} exceeds {} vertices"),
                sourceText,
                index,
                instance.restVertices.size()
            );
            return false;
        }
    }
    for(usize indexBase = 0; indexBase < instance.indices.size(); indexBase += 3u){
        const u32 a = instance.indices[indexBase + 0u];
        const u32 b = instance.indices[indexBase + 1u];
        const u32 c = instance.indices[indexBase + 2u];
        if(a == b || a == c || b == c){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' triangle {} is degenerate"),
                sourceText,
                indexBase / 3u
            );
            return false;
        }

        const Float3Data ab = Subtract3(instance.restVertices[b].position, instance.restVertices[a].position);
        const Float3Data ac = Subtract3(instance.restVertices[c].position, instance.restVertices[a].position);
        const Float3Data areaCross = Cross3(ab, ac);
        const f32 areaLengthSquared = LengthSquared3(areaCross.x, areaCross.y, areaCross.z);
        if(areaLengthSquared <= s_TriangleAreaLengthSquaredEpsilon){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' triangle {} has zero area"),
                sourceText,
                indexBase / 3u
            );
            return false;
        }
    }

    if(!instance.skin.empty() && instance.skin.size() != instance.restVertices.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' skin count {} does not match vertex count {}"),
            sourceText,
            instance.skin.size(),
            instance.restVertices.size()
        );
        return false;
    }
    for(usize vertexIndex = 0; vertexIndex < instance.skin.size(); ++vertexIndex){
        if(!ValidSkinInfluence(instance.skin[vertexIndex])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' skin influence {} is invalid"),
                sourceText,
                vertexIndex
            );
            return false;
        }
    }

    if(!instance.sourceSamples.empty() && instance.sourceSamples.size() != instance.restVertices.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' source sample count {} does not match vertex count {}"),
            sourceText,
            instance.sourceSamples.size(),
            instance.restVertices.size()
        );
        return false;
    }
    for(usize vertexIndex = 0; vertexIndex < instance.sourceSamples.size(); ++vertexIndex){
        if(!ValidSourceSample(instance.sourceSamples[vertexIndex], instance.sourceTriangleCount)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' source sample {} is invalid"),
                sourceText,
                vertexIndex
            );
            return false;
        }
    }

    if(instance.morphs.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph count exceeds u32 limits"),
            sourceText
        );
        return false;
    }
    if(instance.morphs.empty())
        return true;

    Core::Alloc::ScratchArena<> scratchArena;
    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, Core::Alloc::ScratchAllocator<NameHash>> seenMorphNames(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        Core::Alloc::ScratchAllocator<NameHash>(scratchArena)
    );
    seenMorphNames.reserve(instance.morphs.size());
    for(const DeformableMorph& morph : instance.morphs){
        if(!morph.name || morph.deltas.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has an unnamed or empty morph"),
                sourceText
            );
            return false;
        }
        if(!seenMorphNames.insert(morph.name.hash()).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' contains duplicate morph '{}'"),
                sourceText,
                StringConvert(morph.name.c_str())
            );
            return false;
        }
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph '{}' delta count exceeds u32 limits"),
                sourceText,
                StringConvert(morph.name.c_str())
            );
            return false;
        }

        HashSet<u32, Hasher<u32>, EqualTo<u32>, Core::Alloc::ScratchAllocator<u32>> seenDeltaVertices(
            0,
            Hasher<u32>(),
            EqualTo<u32>(),
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        );
        seenDeltaVertices.reserve(morph.deltas.size());
        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!ValidMorphDelta(delta, instance.restVertices.size())){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph '{}' contains an invalid delta"),
                    sourceText,
                    StringConvert(morph.name.c_str())
                );
                return false;
            }
            if(!seenDeltaVertices.insert(delta.vertexId).second){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph '{}' has duplicate vertex {}"),
                    sourceText,
                    StringConvert(morph.name.c_str()),
                    delta.vertexId
                );
                return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const DeformableGeometry* DeformableRuntimeMeshCache::DeformableGeometrySource::geometry()const{
    if(!asset || asset->assetType() != DeformableGeometry::AssetTypeName())
        return nullptr;
    return static_cast<const DeformableGeometry*>(asset.get());
}


DeformableRuntimeMeshCache::DeformableRuntimeMeshCache(Core::Graphics& graphics, Core::Assets::AssetManager& assetManager)
    : m_graphics(graphics)
    , m_assetManager(assetManager)
{}


void DeformableRuntimeMeshCache::update(Core::ECS::World& world){
    Core::Alloc::ScratchArena<> scratchArena;
    HashSet<
        Core::ECS::EntityID,
        Hasher<Core::ECS::EntityID>,
        EqualTo<Core::ECS::EntityID>,
        Core::Alloc::ScratchAllocator<Core::ECS::EntityID>
    > activeEntities(
        0,
        Hasher<Core::ECS::EntityID>(),
        EqualTo<Core::ECS::EntityID>(),
        Core::Alloc::ScratchAllocator<Core::ECS::EntityID>(scratchArena)
    );
    activeEntities.reserve(world.entityCount());

    world.view<DeformableRendererComponent>().each(
        [&](Core::ECS::EntityID entity, DeformableRendererComponent& component){
            activeEntities.insert(entity);
            if(!ensureRuntimeMesh(entity, component))
                component.runtimeMesh.reset();
        }
    );

    Vector<
        Core::ECS::EntityID,
        Core::Alloc::ScratchAllocator<Core::ECS::EntityID>
    > staleEntities{Core::Alloc::ScratchAllocator<Core::ECS::EntityID>(scratchArena)};
    staleEntities.reserve(m_instances.size());
    for(const auto& [entity, instance] : m_instances){
        (void)instance;
        if(activeEntities.find(entity) == activeEntities.end())
            staleEntities.push_back(entity);
    }
    for(const Core::ECS::EntityID entity : staleEntities)
        releaseRuntimeMesh(entity);
}

RuntimeMeshHandle DeformableRuntimeMeshCache::handleForEntity(const Core::ECS::EntityID entity)const{
    const auto found = m_instances.find(entity);
    if(found == m_instances.end())
        return RuntimeMeshHandle{};
    return found.value().handle;
}

DeformableRuntimeMeshInstance* DeformableRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle){
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end())
        return nullptr;
    const auto foundInstance = m_instances.find(foundEntity.value());
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

const DeformableRuntimeMeshInstance* DeformableRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle)const{
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end())
        return nullptr;
    const auto foundInstance = m_instances.find(foundEntity.value());
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

u32 DeformableRuntimeMeshCache::editRevision(const RuntimeMeshHandle handle)const{
    const DeformableRuntimeMeshInstance* instance = findInstance(handle);
    return instance ? instance->editRevision : 0u;
}

bool DeformableRuntimeMeshCache::bumpEditRevision(const RuntimeMeshHandle handle, const RuntimeMeshDirtyFlags dirtyFlags){
    if(!handle.valid())
        return false;

    DeformableRuntimeMeshInstance* instance = findInstance(handle);
    if(!instance)
        return false;
    const RuntimeMeshDirtyFlags expandedDirtyFlags =
        __hidden_deformable_runtime_mesh_cache::ExpandDirtyFlags(dirtyFlags)
    ;
    if(expandedDirtyFlags == RuntimeMeshDirtyFlag::None){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' revision bump requires dirty flags"),
            instance->handle.value
        );
        return false;
    }
    if(instance->editRevision == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' edit revision overflowed"),
            instance->handle.value
        );
        return false;
    }

    ++instance->editRevision;
    instance->dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance->dirtyFlags | expandedDirtyFlags
    );
    return true;
}

bool DeformableRuntimeMeshCache::ensureRuntimeMesh(Core::ECS::EntityID entity, DeformableRendererComponent& component){
    const Name sourceName = component.deformableGeometry.name();
    if(!sourceName){
        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
        return false;
    }

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance != m_instances.end()){
        DeformableRuntimeMeshInstance& instance = foundInstance.value();
        if(instance.source.name() == sourceName){
            component.runtimeMesh = instance.handle;
            if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
                if(!uploadRuntimeMeshBuffers(instance)){
                    releaseRuntimeMesh(entity);
                    component.runtimeMesh.reset();
                    return false;
                }
                instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
                    instance.dirtyFlags & ~__hidden_deformable_runtime_mesh_cache::s_GpuUploadHandledDirtyFlags
                );
            }
            return instance.valid();
        }

        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
    }

    DeformableGeometrySource* source = nullptr;
    if(!ensureSourceLoaded(component.deformableGeometry, source))
        return false;
    const DeformableGeometry* geometry = source ? source->geometry() : nullptr;
    if(!geometry)
        return false;

    DeformableRuntimeMeshInstance instance;
    instance.entity = entity;
    instance.handle = allocateHandle();
    if(!instance.handle.valid()){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.source = component.deformableGeometry;
    instance.restVertices = geometry->restVertices();
    instance.indices = geometry->indices();
    instance.sourceTriangleCount = static_cast<u32>(geometry->indices().size() / 3u);
    instance.skin = geometry->skin();
    instance.sourceSamples = geometry->sourceSamples();
    instance.displacement = geometry->displacement();
    instance.morphs = geometry->morphs();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;

    if(!uploadRuntimeMeshBuffers(instance)){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~__hidden_deformable_runtime_mesh_cache::s_GpuUploadHandledDirtyFlags
    );

    ++source->referenceCount;
    const RuntimeMeshHandle handle = instance.handle;
    auto [it, inserted] = m_instances.emplace(entity, DeformableRuntimeMeshInstance{});
    (void)inserted;
    it.value() = Move(instance);
    m_handleToEntity.emplace(handle.value, entity);
    component.runtimeMesh = handle;
    return it.value().valid();
}

bool DeformableRuntimeMeshCache::ensureSourceLoaded(
    const Core::Assets::AssetRef<DeformableGeometry>& sourceAsset,
    DeformableGeometrySource*& outSource)
{
    outSource = nullptr;

    const Name sourceName = sourceAsset.name();
    if(!sourceName){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformableRuntimeMeshCache: deformable renderer source asset is empty"));
        return false;
    }

    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end()){
        outSource = &foundSource.value();
        return outSource->geometry() != nullptr;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(DeformableGeometry::AssetTypeName(), sourceName, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: failed to load deformable geometry '{}'"),
            StringConvert(sourceName.c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != DeformableGeometry::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: asset '{}' is not deformable geometry"),
            StringConvert(sourceName.c_str())
        );
        return false;
    }

    DeformableGeometrySource source;
    source.sourceName = sourceName;
    source.asset = Move(loadedAsset);

    auto [it, inserted] = m_sources.emplace(sourceName, DeformableGeometrySource{});
    (void)inserted;
    it.value() = Move(source);
    outSource = &it.value();
    return outSource->geometry() != nullptr;
}

bool DeformableRuntimeMeshCache::uploadRuntimeMeshBuffers(DeformableRuntimeMeshInstance& instance){
    if(!__hidden_deformable_runtime_mesh_cache::ValidateRuntimeMeshUploadPayload(instance))
        return false;

    usize restVertexBytes = 0;
    usize indexBytes = 0;
    if(!computePayloadBytes(
        instance,
        instance.restVertices.size(),
        sizeof(DeformableVertexRest),
        restVertexBytes,
        "rest vertices"
    ))
        return false;
    if(!computePayloadBytes(
        instance,
        instance.indices.size(),
        sizeof(u32),
        indexBytes,
        "indices"
    ))
        return false;

    const Name restVertexBufferName = deriveRuntimeBufferName(instance, AStringView("rest_vb"));
    const Name indexBufferName = deriveRuntimeBufferName(instance, AStringView("index"));
    const Name deformedVertexBufferName = deriveRuntimeBufferName(instance, AStringView("deformed_vb"));
    if(!restVertexBufferName || !indexBufferName || !deformedVertexBufferName)
        return false;

    Core::Graphics::BufferSetupDesc restVertexSetup;
    restVertexSetup.bufferDesc
        .setByteSize(static_cast<u64>(restVertexBytes))
        .setStructStride(sizeof(DeformableVertexRest))
        .setDebugName(restVertexBufferName)
    ;
    restVertexSetup.data = instance.restVertices.data();
    restVertexSetup.dataSize = restVertexBytes;
    instance.restVertexBuffer = m_graphics.setupBuffer(restVertexSetup);
    if(!instance.restVertexBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: failed to create rest vertex buffer for '{}'"),
            StringConvert(instance.source.name().c_str())
        );
        return false;
    }

    Core::Graphics::BufferSetupDesc indexSetup;
    indexSetup.bufferDesc
        .setByteSize(static_cast<u64>(indexBytes))
        .setStructStride(sizeof(u32))
        .setDebugName(indexBufferName)
    ;
    indexSetup.data = instance.indices.data();
    indexSetup.dataSize = indexBytes;
    instance.indexBuffer = m_graphics.setupBuffer(indexSetup);
    if(!instance.indexBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: failed to create index buffer for '{}'"),
            StringConvert(instance.source.name().c_str())
        );
        return false;
    }

    Core::Graphics::BufferSetupDesc deformedVertexSetup;
    deformedVertexSetup.bufferDesc
        .setByteSize(static_cast<u64>(restVertexBytes))
        .setStructStride(sizeof(DeformableVertexRest))
        .setCanHaveUAVs(true)
        .setIsVertexBuffer(true)
        .setDebugName(deformedVertexBufferName)
    ;
    deformedVertexSetup.data = instance.restVertices.data();
    deformedVertexSetup.dataSize = restVertexBytes;
    instance.deformedVertexBuffer = m_graphics.setupBuffer(deformedVertexSetup);
    if(!instance.deformedVertexBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: failed to create deformed vertex buffer for '{}'"),
            StringConvert(instance.source.name().c_str())
        );
        return false;
    }

    return true;
}

RuntimeMeshHandle DeformableRuntimeMeshCache::allocateHandle(){
    while(m_nextHandleValue != 0u){
        RuntimeMeshHandle handle;
        handle.value = m_nextHandleValue++;
        if(m_handleToEntity.find(handle.value) == m_handleToEntity.end())
            return handle;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh handle space exhausted"));
    return RuntimeMeshHandle{};
}

void DeformableRuntimeMeshCache::releaseRuntimeMesh(const Core::ECS::EntityID entity){
    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return;

    const DeformableRuntimeMeshInstance& instance = foundInstance.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.source.name());
    m_instances.erase(entity);
}

void DeformableRuntimeMeshCache::releaseSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    DeformableGeometrySource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(sourceName);
}

void DeformableRuntimeMeshCache::eraseUnusedSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(sourceName);
}

Name DeformableRuntimeMeshCache::deriveRuntimeBufferName(const DeformableRuntimeMeshInstance& instance, const AStringView suffix)const{
    const AString derivedSuffix = StringFormat(
        ":runtime_{}_revision_{}_{}",
        instance.entity.id,
        instance.editRevision,
        suffix
    );
    return DeriveName(instance.source.name(), derivedSuffix);
}

bool DeformableRuntimeMeshCache::computePayloadBytes(
    const DeformableRuntimeMeshInstance& instance,
    const usize count,
    const usize stride,
    usize& outBytes,
    const char* label)const
{
    outBytes = 0;
    if(stride == 0u || count > Limit<usize>::s_Max / stride){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' '{}' payload byte size overflows"),
            StringConvert(instance.source.name().c_str()),
            StringConvert(AString(label))
        );
        return false;
    }

    outBytes = count * stride;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

