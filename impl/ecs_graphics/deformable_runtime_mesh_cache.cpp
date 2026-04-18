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

[[nodiscard]] RuntimeMeshDirtyFlags SanitizeDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    return static_cast<RuntimeMeshDirtyFlags>(dirtyFlags & s_KnownDirtyFlags);
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
    const RuntimeMeshDirtyFlags sanitizedDirtyFlags =
        __hidden_deformable_runtime_mesh_cache::SanitizeDirtyFlags(dirtyFlags)
    ;
    if(sanitizedDirtyFlags == RuntimeMeshDirtyFlag::None){
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
        instance->dirtyFlags | sanitizedDirtyFlags
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
                    instance.dirtyFlags & ~RuntimeMeshDirtyFlag::GpuUploadDirty
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
    instance.skin = geometry->skin();
    instance.sourceSamples = geometry->sourceSamples();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;

    if(!uploadRuntimeMeshBuffers(instance)){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::GpuUploadDirty
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
    if(instance.restVertices.empty() || instance.indices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has incomplete rest/index payload"),
            StringConvert(instance.source.name().c_str())
        );
        return false;
    }

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

