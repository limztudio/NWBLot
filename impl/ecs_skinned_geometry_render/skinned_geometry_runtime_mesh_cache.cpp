// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_runtime_mesh_cache.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_manager.h>
#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <impl/assets_geometry/skinned_geometry_asset.h>
#include <impl/assets_geometry/skinned_geometry_payload_logging.h>
#include <impl/ecs_skinned_geometry/skinned_geometry_runtime_names.h>
#include <impl/ecs_skinned_geometry/skinned_geometry_runtime_validation.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_runtime_mesh_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr RuntimeMeshDirtyFlags s_KnownDirtyFlags = RuntimeMeshDirtyFlag::All;
// uploadRuntimeMeshBuffers seeds the deformed draw buffer from the rest pose,
// so the upload also satisfies the static skinned geometry input update.
static constexpr RuntimeMeshDirtyFlags s_GpuUploadHandledDirtyFlags =
    RuntimeMeshDirtyFlag::TopologyDirty
    | RuntimeMeshDirtyFlag::AttributesDirty
    | RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
    | RuntimeMeshDirtyFlag::GpuUploadDirty
;

[[nodiscard]] RuntimeMeshDirtyFlags SanitizeDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    return static_cast<RuntimeMeshDirtyFlags>(dirtyFlags & s_KnownDirtyFlags);
}

[[nodiscard]] RuntimeMeshDirtyFlags ExpandDirtyFlags(const RuntimeMeshDirtyFlags dirtyFlags){
    RuntimeMeshDirtyFlags expanded = SanitizeDirtyFlags(dirtyFlags);
    if((expanded & (RuntimeMeshDirtyFlag::TopologyDirty | RuntimeMeshDirtyFlag::AttributesDirty)) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(
            expanded | RuntimeMeshDirtyFlag::GpuUploadDirty | RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
        );
    }
    else if((expanded & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
        expanded = static_cast<RuntimeMeshDirtyFlags>(expanded | RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty);
    }
    return expanded;
}

[[nodiscard]] bool ValidateRuntimeMeshUploadPayload(const SkinnedGeometryRuntimeMeshInstance& instance){
    const auto sourceText = [&instance]() -> TString{
        return
            instance.source.name()
                ? StringConvert(instance.source.name().c_str())
                : TString(NWB_TEXT("<unnamed>"))
        ;
    };

    if(!GeometryClassUsesSkinnedGeometryRuntime(instance.geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' has invalid geometry class")
            , sourceText()
        );
        return false;
    }
    if(!ValidSkinnedGeometryDisplacementDescriptor(instance.displacement)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' has an invalid displacement descriptor")
            , sourceText()
        );
        return false;
    }

    const bool hasSkin = !instance.skin.empty();
    if(!GeometryClassMatchesSkinPayload(instance.geometryClass, hasSkin)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' class '{}' does not match skin payload")
            , sourceText()
            , StringConvert(GeometryClassText(instance.geometryClass))
        );
        return false;
    }
    const bool hasSkinnedGeometryPayload = !instance.sourceSamples.empty() || !instance.editMaskPerTriangle.empty() || !instance.morphs.empty();
    if(!GeometryClassAcceptsSkinnedGeometryPayload(instance.geometryClass, hasSkinnedGeometryPayload)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' class '{}' cannot carry skinned geometry runtime or morph payload")
            , sourceText()
            , StringConvert(GeometryClassText(instance.geometryClass))
        );
        return false;
    }
    if(!GeometryClassAcceptsSkinnedGeometryPayload(instance.geometryClass, instance.displacement.mode != SkinnedGeometryDisplacementMode::None)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' class '{}' cannot carry displacement payload")
            , sourceText()
            , StringConvert(GeometryClassText(instance.geometryClass))
        );
        return false;
    }
    const SkinnedGeometryValidation::RuntimePayloadFailureInfo runtimePayloadFailure =
        SkinnedGeometryRuntime::FindRuntimeMeshPayloadFailure(instance)
    ;
    if(runtimePayloadFailure.reason != SkinnedGeometryValidation::RuntimePayloadFailure::None){
        SkinnedGeometryValidation::LogRuntimePayloadFailure(
            NWB_TEXT("SkinnedGeometryRuntimeMeshCache"),
            NWB_TEXT("runtime mesh"),
            sourceText(),
            instance.morphs,
            runtimePayloadFailure
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const SkinnedGeometry* SkinnedGeometryRuntimeMeshCache::SkinnedGeometrySource::geometry()const{
    if(!asset || asset->assetType() != SkinnedGeometry::AssetTypeName())
        return nullptr;
    return checked_cast<const SkinnedGeometry*>(asset.get());
}


SkinnedGeometryRuntimeMeshCache::SkinnedGeometryRuntimeMeshCache(Core::Alloc::CustomArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager)
    : m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_sources(0, Hasher<Name>(), EqualTo<Name>(), SourceMapAllocator(arena))
    , m_instances(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), InstanceMapAllocator(arena))
    , m_handleToEntity(0, Hasher<u64>(), EqualTo<u64>(), HandleMapAllocator(arena))
{}


void SkinnedGeometryRuntimeMeshCache::update(Core::ECS::World& world){
    auto skinnedGeometryView = world.view<SkinnedGeometryComponent>();
    const usize rendererCandidateCount = skinnedGeometryView.candidateCount();
    if(rendererCandidateCount == 0u){
        m_handleToEntity.clear();
        m_instances.clear();
        m_sources.clear();
        return;
    }

    m_instances.reserve(rendererCandidateCount);
    m_handleToEntity.reserve(rendererCandidateCount);
    m_sources.reserve(rendererCandidateCount);

    const bool pruneStaleInstances = !m_instances.empty();
    skinnedGeometryView.each(
        [&](Core::ECS::EntityID entity, SkinnedGeometryComponent& component){
            if(!ensureRuntimeMesh(entity, component))
                component.runtimeMesh.reset();
        }
    );

    if(!pruneStaleInstances)
        return;

    for(auto it = m_instances.begin(); it != m_instances.end();){
        const Core::ECS::EntityID entity = it->first;
        if(world.tryGetComponent<SkinnedGeometryComponent>(entity)){
            ++it;
            continue;
        }

        const SkinnedGeometryRuntimeMeshInstance& instance = it.value();
        m_handleToEntity.erase(instance.handle.value);
        releaseSource(instance.source.name());
        it = m_instances.erase(it);
    }
}

RuntimeMeshHandle SkinnedGeometryRuntimeMeshCache::handleForEntity(const Core::ECS::EntityID entity)const{
    const auto found = m_instances.find(entity);
    if(found == m_instances.end())
        return RuntimeMeshHandle{};
    return found.value().handle;
}

SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle){
    return findInstanceByEntity(entityForHandle(handle));
}

const SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstance(const RuntimeMeshHandle handle)const{
    return findInstanceByEntity(entityForHandle(handle));
}

u32 SkinnedGeometryRuntimeMeshCache::editRevision(const RuntimeMeshHandle handle)const{
    const SkinnedGeometryRuntimeMeshInstance* instance = findInstance(handle);
    return instance ? instance->editRevision : 0u;
}

bool SkinnedGeometryRuntimeMeshCache::bumpEditRevision(const RuntimeMeshHandle handle, const RuntimeMeshDirtyFlags dirtyFlags){
    if(!handle.valid())
        return false;

    SkinnedGeometryRuntimeMeshInstance* instance = findInstance(handle);
    if(!instance)
        return false;
    const RuntimeMeshDirtyFlags expandedDirtyFlags =
        __hidden_skinned_geometry_runtime_mesh_cache::ExpandDirtyFlags(dirtyFlags)
    ;
    if(expandedDirtyFlags == RuntimeMeshDirtyFlag::None){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' revision bump requires dirty flags")
            , instance->handle.value
        );
        return false;
    }
    if(instance->editRevision == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' edit revision overflowed")
            , instance->handle.value
        );
        return false;
    }

    ++instance->editRevision;
    instance->dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance->dirtyFlags | expandedDirtyFlags
    );
    return true;
}

bool SkinnedGeometryRuntimeMeshCache::ensureRuntimeMesh(Core::ECS::EntityID entity, SkinnedGeometryComponent& component){
    const Name sourceName = component.skinnedGeometry.name();
    if(!sourceName){
        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
        return false;
    }

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance != m_instances.end()){
        SkinnedGeometryRuntimeMeshInstance& instance = foundInstance.value();
        if(instance.source.name() == sourceName){
            component.runtimeMesh = instance.handle;
            if((instance.dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u){
                if(!uploadRuntimeMeshBuffers(instance)){
                    releaseRuntimeMesh(entity);
                    component.runtimeMesh.reset();
                    return false;
                }
                instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
                    instance.dirtyFlags & ~__hidden_skinned_geometry_runtime_mesh_cache::s_GpuUploadHandledDirtyFlags
                );
            }
            return instance.valid();
        }

        releaseRuntimeMesh(entity);
        component.runtimeMesh.reset();
    }

    SkinnedGeometrySource* source = nullptr;
    if(!ensureSourceLoaded(component.skinnedGeometry, source))
        return false;
    const SkinnedGeometry* geometry = source ? source->geometry() : nullptr;
    if(!geometry)
        return false;

    SkinnedGeometryRuntimeMeshInstance instance;
    instance.entity = entity;
    instance.handle = allocateHandle();
    if(!instance.handle.valid()){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.source = component.skinnedGeometry;
    instance.geometryClass = geometry->geometryClass();
    instance.restVertices = geometry->restVertices();
    instance.indices = geometry->indices();
    instance.sourceTriangleCount = static_cast<u32>(geometry->indices().size() / 3u);
    instance.skeletonJointCount = geometry->skeletonJointCount();
    instance.skin = geometry->skin();
    instance.inverseBindMatrices = geometry->inverseBindMatrices();
    instance.sourceSamples = geometry->sourceSamples();
    instance.editMaskPerTriangle = geometry->editMaskPerTriangle();
    instance.displacement = geometry->displacement();
    instance.morphs = geometry->morphs();
    instance.dirtyFlags = RuntimeMeshDirtyFlag::All;

    if(!uploadRuntimeMeshBuffers(instance)){
        eraseUnusedSource(sourceName);
        return false;
    }
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~__hidden_skinned_geometry_runtime_mesh_cache::s_GpuUploadHandledDirtyFlags
    );

    ++source->referenceCount;
    const RuntimeMeshHandle handle = instance.handle;
    auto result = m_instances.try_emplace(entity, Move(instance));
    auto it = result.first;
    m_handleToEntity.emplace(handle.value, entity);
    component.runtimeMesh = handle;
    return it.value().valid();
}

bool SkinnedGeometryRuntimeMeshCache::ensureSourceLoaded(const Core::Assets::AssetRef<SkinnedGeometry>& sourceAsset, SkinnedGeometrySource*& outSource){
    outSource = nullptr;

    const Name sourceName = sourceAsset.name();
    if(!sourceName){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: skinned geometry renderer source asset is empty"));
        return false;
    }

    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end()){
        outSource = &foundSource.value();
        return outSource->geometry() != nullptr;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(SkinnedGeometry::AssetTypeName(), sourceName, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: failed to load skinned geometry '{}'")
            , StringConvert(sourceName.c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != SkinnedGeometry::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: asset '{}' is not skinned geometry")
            , StringConvert(sourceName.c_str())
        );
        return false;
    }

    SkinnedGeometrySource source;
    source.sourceName = sourceName;
    source.asset = Move(loadedAsset);

    auto result = m_sources.try_emplace(sourceName, Move(source));
    auto it = result.first;
    outSource = &it.value();
    return outSource->geometry() != nullptr;
}

bool SkinnedGeometryRuntimeMeshCache::uploadRuntimeMeshBuffers(SkinnedGeometryRuntimeMeshInstance& instance){
    if(!__hidden_skinned_geometry_runtime_mesh_cache::ValidateRuntimeMeshUploadPayload(instance))
        return false;

    usize restVertexBytes = 0;
    usize indexBytes = 0;
    if(!computePayloadBytes(
        instance,
        instance.restVertices.size(),
        sizeof(SkinnedGeometryVertex),
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
        .setStructStride(sizeof(SkinnedGeometryVertex))
        .setDebugName(restVertexBufferName)
    ;
    restVertexSetup.data = instance.restVertices.data();
    restVertexSetup.dataSize = restVertexBytes;
    instance.restVertexBuffer = m_graphics.setupBuffer(restVertexSetup);
    if(!instance.restVertexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: failed to create rest vertex buffer for '{}'")
            , StringConvert(instance.source.name().c_str())
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
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: failed to create index buffer for '{}'")
            , StringConvert(instance.source.name().c_str())
        );
        return false;
    }

    Core::Graphics::BufferSetupDesc deformedVertexSetup;
    deformedVertexSetup.bufferDesc
        .setByteSize(static_cast<u64>(restVertexBytes))
        .setStructStride(sizeof(SkinnedGeometryVertex))
        .setCanHaveUAVs(true)
        .setIsVertexBuffer(true)
        .setDebugName(deformedVertexBufferName)
    ;
    deformedVertexSetup.data = instance.restVertices.data();
    deformedVertexSetup.dataSize = restVertexBytes;
    instance.deformedVertexBuffer = m_graphics.setupBuffer(deformedVertexSetup);
    if(!instance.deformedVertexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: failed to create deformed vertex buffer for '{}'")
            , StringConvert(instance.source.name().c_str())
        );
        return false;
    }

    return true;
}

RuntimeMeshHandle SkinnedGeometryRuntimeMeshCache::allocateHandle(){
    while(m_nextHandleValue != 0u){
        RuntimeMeshHandle handle;
        handle.value = m_nextHandleValue;
        ++m_nextHandleValue;
        if(m_handleToEntity.find(handle.value) == m_handleToEntity.end())
            return handle;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh handle space exhausted"));
    return RuntimeMeshHandle{};
}

void SkinnedGeometryRuntimeMeshCache::releaseRuntimeMesh(const Core::ECS::EntityID entity){
    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return;

    const SkinnedGeometryRuntimeMeshInstance& instance = foundInstance.value();
    m_handleToEntity.erase(instance.handle.value);
    releaseSource(instance.source.name());
    m_instances.erase(foundInstance);
}

void SkinnedGeometryRuntimeMeshCache::releaseSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    SkinnedGeometrySource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(foundSource);
}

void SkinnedGeometryRuntimeMeshCache::eraseUnusedSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(foundSource);
}

Core::ECS::EntityID SkinnedGeometryRuntimeMeshCache::entityForHandle(const RuntimeMeshHandle handle)const{
    const auto foundEntity = m_handleToEntity.find(handle.value);
    if(foundEntity == m_handleToEntity.end())
        return Core::ECS::ENTITY_ID_INVALID;
    return foundEntity.value();
}

SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstanceByEntity(const Core::ECS::EntityID entity){
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

const SkinnedGeometryRuntimeMeshInstance* SkinnedGeometryRuntimeMeshCache::findInstanceByEntity(const Core::ECS::EntityID entity)const{
    if(!entity.valid())
        return nullptr;

    const auto foundInstance = m_instances.find(entity);
    if(foundInstance == m_instances.end())
        return nullptr;
    return &foundInstance.value();
}

Name SkinnedGeometryRuntimeMeshCache::deriveRuntimeBufferName(const SkinnedGeometryRuntimeMeshInstance& instance, const AStringView suffix)const{
    return DeriveRuntimeResourceName(instance.source.name(), instance.entity.id, instance.editRevision, suffix);
}

bool SkinnedGeometryRuntimeMeshCache::computePayloadBytes(
    const SkinnedGeometryRuntimeMeshInstance& instance,
    const usize count,
    const usize stride,
    usize& outBytes,
    const char* label
)const{
    outBytes = 0;
    if(stride == 0u || count > Limit<usize>::s_Max / stride){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryRuntimeMeshCache: runtime mesh '{}' '{}' payload byte size overflows")
            , StringConvert(instance.source.name().c_str())
            , StringConvert(label)
        );
        return false;
    }

    outBytes = count * stride;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

