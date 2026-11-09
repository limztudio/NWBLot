// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_ref_encode.h>
#include <impl/assets_mesh/meshlet_ref_validation.h>
#include <impl/assets_mesh/skin_asset.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_AssetLoadContext = NWB_TEXT("MeshSkinningRuntimeCache");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 BuildRuntimeZippedPayload(const Mesh& mesh, const Span<const SkinInfluence4> skinStream, Core::Alloc::ScratchArena& scratchArena, MeshSkinningRuntimeInstance& instance){
    Vector<MeshletPositionStreamRef, Core::Alloc::ScratchArena> runtimePositionRefs{scratchArena};
    Vector<MeshletAttributeStreamRef, Core::Alloc::ScratchArena> runtimeAttributeRefs{scratchArena};
    instance.meshletPositionRefDeltas.clear();
    instance.meshletAttributeRefDeltas.clear();
    instance.attributeSkins.clear();
    if(!TryDecodeRuntimeZippedMeshletStreams(
        mesh.meshlets(),
        mesh.meshletPositionRefDeltas(),
        mesh.meshletAttributeRefDeltas(),
        mesh.positionStream(),
        mesh.normalStream(),
        mesh.tangentStream(),
        mesh.uv0Stream().size(),
        mesh.colorStream().size(),
        skinStream.size(),
        instance.restPositions,
        instance.restNormals,
        instance.restTangents,
        instance.meshlets,
        runtimePositionRefs,
        runtimeAttributeRefs
    ))
        return false;

    if(!ResolveRuntimeZippedAttributeSkins(
        instance.meshlets,
        runtimePositionRefs,
        instance.meshletLocalVertexRefs,
        runtimeAttributeRefs.size(),
        instance.attributeSkins
    ))
        return false;

    if(!EncodeMeshletRefDeltas(runtimePositionRefs, runtimeAttributeRefs, true, [&](const usize meshletIndex, const TStringView reason){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime meshlet {} {}"), meshletIndex, reason);
        return false;
    }, instance.meshlets, instance.meshletPositionRefDeltas, instance.meshletAttributeRefDeltas
    ))
        return false;

    instance.meshletPositionRefCount = static_cast<u32>(runtimePositionRefs.size());
    instance.meshletAttributeRefCount = static_cast<u32>(runtimeAttributeRefs.size());
    return BuildRuntimeMeshLocalBounds(instance.sourceName, instance.restPositions, instance.localBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const Mesh& MeshSkinningRuntimeCache::MeshSkinningSource::mesh()const noexcept{
    const auto asset = MakeNotNull(Core::Assets::CastAsset<Mesh>(meshAsset.get()));
    return *asset;
}

const Skin& MeshSkinningRuntimeCache::MeshSkinningSource::skin()const noexcept{
    const auto asset = MakeNotNull(Core::Assets::CastAsset<Skin>(skinAsset.get()));
    return *asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshSkinningRuntimeCache::prepareResources(Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena){
    auto skinningBindingView = world.view<SkinnedMeshBindingComponent>();
    const usize rendererCandidateCount = skinningBindingView.candidateCount();
    if(rendererCandidateCount == 0u){
        clear();
        return;
    }

    m_instances.reserve(rendererCandidateCount);
    m_handleToEntity.reserve(rendererCandidateCount);
    m_sources.reserve(rendererCandidateCount);

    const b8 pruneStaleInstances = !m_instances.empty();
    skinningBindingView.each([&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
        if(!ensureRuntimeMesh(entity, scratchArena, component))
            component.runtimeMesh.reset();
    });

    if(!pruneStaleInstances)
        return;

    for(auto it = m_instances.begin(); it != m_instances.end();){
        const Core::ECS::EntityID entity = it->first;
        if(world.tryGetComponent<SkinnedMeshBindingComponent>(entity)){
            ++it;
            continue;
        }

        it = eraseInstance(it);
    }
}

void MeshSkinningRuntimeCache::clear()noexcept{
    m_handleToEntity.clear();
    m_instances.clear();
    m_sources.clear();
}

const MeshSkinningRuntimeInstance* MeshSkinningRuntimeCache::findInstance(const RuntimeMeshHandle handle)const noexcept{
    const Expected<Core::ECS::EntityID> foundEntity = TryFindRuntimeMeshEntityByHandle(m_handleToEntity, handle.value);
    if(!foundEntity)
        return nullptr;

    const auto foundInstance = m_instances.find(*foundEntity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

Expected<RuntimeMeshHandle> MeshSkinningRuntimeCache::allocateHandle(){
    const Expected<u64> handleValue = TryAllocateRuntimeMeshHandleValue(m_handleToEntity, m_nextHandleValue);
    if(!handleValue)
        return MakeUnexpected(Failure{});

    RuntimeMeshHandle handle;
    handle.value = *handleValue;
    return handle;
}

void MeshSkinningRuntimeCache::releaseRuntimeMesh(const Core::ECS::EntityID entity)noexcept{
    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return;

    eraseInstance(foundInstance);
}

MeshSkinningRuntimeCache::InstanceMap::iterator MeshSkinningRuntimeCache::eraseInstance(const InstanceMap::iterator it)noexcept{
    const MeshSkinningRuntimeInstance& instance = it.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.sourceName);
    return m_instances.erase(it);
}

void MeshSkinningRuntimeCache::releaseSource(const Name& sourceName)noexcept{
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    MeshSkinningSource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(foundSource);
}

void MeshSkinningRuntimeCache::eraseUnusedSource(const Name& sourceName)noexcept{
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(foundSource);
}

b8 MeshSkinningRuntimeCache::ensureRuntimeMesh(Core::ECS::EntityID entity, Core::Alloc::ScratchArena& scratchArena, SkinnedMeshBindingComponent& component){
    const Name& sourceName = component.skin.name();
    if(!component.skin.valid()){
        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
        return false;
    }

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance != m_instances.end()){
        MeshSkinningRuntimeInstance& instance = foundInstance.value();
        if(instance.sourceName == sourceName){
            component.runtimeMesh = instance.handle;
            if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
                if(!uploadRuntimeMeshBuffers(instance, scratchArena)){
                    releaseRuntimeMesh(entity);
                    component.runtimeMesh.reset();
                    return false;
                }
                instance.dirtyFlags &= ~RuntimeMeshDirtyFlag::All;
            }
            return instance.valid();
        }

        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
    }

    const auto sourceResult = ensureSourceLoaded(component.skin);
    if(!sourceResult)
        return false;
    MeshSkinningSource& source = **sourceResult;
    const Mesh& mesh = source.mesh();
    const Skin& skin = source.skin();

    MeshSkinningRuntimeInstance instance(m_arena);
    instance.entity = entity;
    const Expected<RuntimeMeshHandle> handleResult = allocateHandle();
    if(!handleResult){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.handle = *handleResult;
    instance.sourceName = sourceName;
    instance.uv0 = mesh.uv0Stream();
    instance.colors = mesh.colorStream();
    instance.skin = skin.influences();
    instance.skeletonJointCount = static_cast<u32>(skin.inverseBindMatrices().size());
    instance.inverseBindMatrices = skin.inverseBindMatrices();
    instance.meshletBounds = mesh.meshletBounds();
    instance.meshletLocalVertexRefs = mesh.meshletLocalVertexRefs();
    instance.meshletPrimitiveIndices = mesh.meshletPrimitiveIndices();
    instance.solidTriangleWords = mesh.solidTriangleWords();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;
    if(!__hidden_runtime_cache_source::BuildRuntimeZippedPayload(mesh, skin.influences(), scratchArena, instance)){
        eraseUnusedSource(sourceName);
        return false;
    }

    if(!uploadRuntimeMeshBuffers(instance, scratchArena)){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.dirtyFlags &= ~RuntimeMeshDirtyFlag::All;

    if(!instance.valid()){
        eraseUnusedSource(sourceName);
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: prepared runtime mesh is incomplete"));
        return false;
    }

    const RuntimeMeshHandle handle = instance.handle;
    auto [storedInstance, instanceInserted] = m_instances.try_emplace(entity, Move(instance));
    if(!instanceInserted){
        eraseUnusedSource(sourceName);
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime instance identity was already registered"));
        return false;
    }
    ScopeExit discardInstance([&]()noexcept{ m_instances.erase(storedInstance); });
    const auto [storedEntity, handleInserted] = m_handleToEntity.emplace(handle.value, entity);
    if(!handleInserted){
        m_instances.erase(storedInstance);
        discardInstance.release();
        eraseUnusedSource(sourceName);
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh handle was already registered"));
        return false;
    }

    ++source.referenceCount;
    component.runtimeMesh = handle;
    discardInstance.release();
    return true;
}

Expected<NotNull<MeshSkinningRuntimeCache::MeshSkinningSource*>> MeshSkinningRuntimeCache::ensureSourceLoaded(const Core::Assets::AssetRef<Skin>& skinAsset){
    NWB_ASSERT(skinAsset.valid());

    const Name& sourceName = skinAsset.name();
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end()){
        MeshSkinningSource& cachedSource = foundSource.value();
        return MakeNotNull(&cachedSource);
    }

    auto loadedSkinAsset = m_assetManager.loadTypedSync<Skin>(skinAsset.name(), __hidden_runtime_cache_source::s_AssetLoadContext);
    if(!loadedSkinAsset)
        return MakeUnexpected(Failure{});

    auto loadedMeshAsset = m_assetManager.loadTypedSync<Mesh>((*loadedSkinAsset)->mesh().name(), __hidden_runtime_cache_source::s_AssetLoadContext);
    if(!loadedMeshAsset)
        return MakeUnexpected(Failure{});

    MeshSkinningSource source;
    source.meshAsset = Move(*loadedMeshAsset);
    source.skinAsset = Move(*loadedSkinAsset);

    auto [stored, inserted] = m_sources.try_emplace(sourceName, Move(source));
    if(!inserted){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: source identity was already registered"));
        return MakeUnexpected(Failure{});
    }
    MeshSkinningSource& storedSource = stored.value();
    return MakeNotNull(&storedSource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

