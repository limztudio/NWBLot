// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_runtime_mesh_cache.h"

#include <core/alloc/scratch.h>
#include <core/ecs/world.h>
#include <impl/assets_graphics/deformable_geometry_validation.h>
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

void LogRuntimeMorphPayloadFailure(
    const TString& sourceText,
    const Vector<DeformableMorph>& morphs,
    const DeformableValidation::MorphPayloadFailureInfo& failure)
{
    const DeformableMorph* morph = failure.morphIndex < morphs.size()
        ? &morphs[failure.morphIndex]
        : nullptr
    ;
    const TString morphNameText = (morph && morph->name)
        ? StringConvert(morph->name.c_str())
        : TString(NWB_TEXT("<unnamed>"))
    ;

    switch(failure.reason){
    case DeformableValidation::MorphPayloadFailure::MorphCountLimit:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph count exceeds u32 limits"),
            sourceText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::EmptyMorph:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has an unnamed or empty morph"),
            sourceText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::DuplicateMorphName:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' contains duplicate morph '{}'"),
            sourceText,
            morphNameText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::MorphDeltaCountLimit:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph '{}' delta count exceeds u32 limits"),
            sourceText,
            morphNameText
        );
        break;
    case DeformableValidation::MorphPayloadFailure::InvalidMorphDelta:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph '{}' delta {} is invalid"),
            sourceText,
            morphNameText,
            failure.deltaIndex
        );
        break;
    case DeformableValidation::MorphPayloadFailure::DuplicateMorphDeltaVertex:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' morph '{}' has duplicate vertex {}"),
            sourceText,
            morphNameText,
            failure.vertexId
        );
        break;
    case DeformableValidation::MorphPayloadFailure::None:
        break;
    }
}

[[nodiscard]] bool ValidateRuntimeMeshUploadPayload(const DeformableRuntimeMeshInstance& instance){
    const auto sourceText = [&instance]() -> TString{
        return instance.source.name()
            ? StringConvert(instance.source.name().c_str())
            : TString(NWB_TEXT("<unnamed>"))
        ;
    };

    if(instance.restVertices.empty() || instance.indices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has incomplete rest/index payload"),
            sourceText()
        );
        return false;
    }
    const usize vertexCount = instance.restVertices.size();
    const usize indexCount = instance.indices.size();
    if(vertexCount > static_cast<usize>(Limit<u32>::s_Max)
        || indexCount > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' exceeds u32 vertex/index count limits"),
            sourceText()
        );
        return false;
    }
    if((indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' index count {} is not a multiple of 3"),
            sourceText(),
            indexCount
        );
        return false;
    }
    if(!ValidDeformableDisplacementDescriptor(instance.displacement)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has an invalid displacement descriptor"),
            sourceText()
        );
        return false;
    }

    for(usize vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex){
        if(!DeformableValidation::ValidRestVertexFrame(instance.restVertices[vertexIndex])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' rest vertex {} has invalid data or frame"),
                sourceText(),
                vertexIndex
            );
            return false;
        }
    }

    const auto indexOutOfRange = [&](const u32 index) -> bool {
        if(static_cast<usize>(index) < vertexCount)
            return false;

        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' index {} exceeds {} vertices"),
            sourceText(),
            index,
            vertexCount
        );
        return true;
    };
    for(usize indexBase = 0; indexBase < indexCount; indexBase += 3u){
        const u32 a = instance.indices[indexBase + 0u];
        const u32 b = instance.indices[indexBase + 1u];
        const u32 c = instance.indices[indexBase + 2u];
        if(indexOutOfRange(a) || indexOutOfRange(b) || indexOutOfRange(c))
            return false;
        if(a == b || a == c || b == c){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' triangle {} is degenerate"),
                sourceText(),
                indexBase / 3u
            );
            return false;
        }

        const SIMDVector aPosition = LoadFloat(instance.restVertices[a].position);
        const SIMDVector ab = VectorSubtract(LoadFloat(instance.restVertices[b].position), aPosition);
        const SIMDVector ac = VectorSubtract(LoadFloat(instance.restVertices[c].position), aPosition);
        const f32 areaLengthSquared = VectorGetX(Vector3LengthSq(Vector3Cross(ab, ac)));
        if(!(areaLengthSquared > DeformableValidation::s_TriangleAreaLengthSquaredEpsilon)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' triangle {} has zero area"),
                sourceText(),
                indexBase / 3u
            );
            return false;
        }
    }

    if(!instance.skin.empty() && instance.skin.size() != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' skin count {} does not match vertex count {}"),
            sourceText(),
            instance.skin.size(),
            vertexCount
        );
        return false;
    }
    if(!instance.skin.empty() && instance.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' has skin but no skeleton joint count"),
            sourceText()
        );
        return false;
    }
    if(instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' skeleton joint count {} exceeds skin stream limits"),
            sourceText(),
            instance.skeletonJointCount
        );
        return false;
    }
    if(!DeformableValidation::ValidInverseBindMatrices(instance.inverseBindMatrices, instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' inverse bind matrices are invalid"),
            sourceText()
        );
        return false;
    }
    for(usize vertexIndex = 0; vertexIndex < instance.skin.size(); ++vertexIndex){
        if(!DeformableValidation::ValidSkinInfluence(instance.skin[vertexIndex])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' skin influence {} is invalid"),
                sourceText(),
                vertexIndex
            );
            return false;
        }
        u32 failedJoint = 0u;
        if(!DeformableValidation::SkinInfluenceFitsSkeleton(
            instance.skin[vertexIndex],
            instance.skeletonJointCount,
            failedJoint
        )){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' skin joint {} for vertex {} exceeds skeleton joint count {}"),
                sourceText(),
                failedJoint,
                vertexIndex,
                instance.skeletonJointCount
            );
            return false;
        }
    }

    if(!instance.sourceSamples.empty() && instance.sourceSamples.size() != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' source sample count {} does not match vertex count {}"),
            sourceText(),
            instance.sourceSamples.size(),
            vertexCount
        );
        return false;
    }
    for(usize vertexIndex = 0; vertexIndex < instance.sourceSamples.size(); ++vertexIndex){
        if(!DeformableValidation::ValidSourceSample(instance.sourceSamples[vertexIndex], instance.sourceTriangleCount)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' source sample {} is invalid"),
                sourceText(),
                vertexIndex
            );
            return false;
        }
    }
    const usize triangleCount = indexCount / 3u;
    if(!instance.editMaskPerTriangle.empty() && instance.editMaskPerTriangle.size() != triangleCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' edit mask count {} does not match triangle count {}"),
            sourceText(),
            instance.editMaskPerTriangle.size(),
            triangleCount
        );
        return false;
    }
    for(usize triangleIndex = 0; triangleIndex < instance.editMaskPerTriangle.size(); ++triangleIndex){
        if(!ValidDeformableEditMaskFlags(instance.editMaskPerTriangle[triangleIndex])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformableRuntimeMeshCache: runtime mesh '{}' edit mask {} is invalid"),
                sourceText(),
                triangleIndex
            );
            return false;
        }
    }

    const DeformableValidation::MorphPayloadFailureInfo morphFailure =
        DeformableValidation::FindMorphPayloadFailure(instance.morphs, vertexCount)
    ;
    if(morphFailure.reason != DeformableValidation::MorphPayloadFailure::None){
        LogRuntimeMorphPayloadFailure(sourceText(), instance.morphs, morphFailure);
        return false;
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


DeformableRuntimeMeshCache::DeformableRuntimeMeshCache(Core::Alloc::CustomArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager)
    : m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_sources(0, Hasher<Name>(), EqualTo<Name>(), SourceMapAllocator(arena))
    , m_instances(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), InstanceMapAllocator(arena))
    , m_handleToEntity(0, Hasher<u64>(), EqualTo<u64>(), HandleMapAllocator(arena))
{}


void DeformableRuntimeMeshCache::update(Core::ECS::World& world){
    auto deformableRendererView = world.view<DeformableRendererComponent>();
    if(m_instances.empty()){
        deformableRendererView.each(
            [&](Core::ECS::EntityID entity, DeformableRendererComponent& component){
                if(!ensureRuntimeMesh(entity, component))
                    component.runtimeMesh.reset();
            }
        );
        return;
    }

    if(deformableRendererView.candidateCount() == 0u){
        m_handleToEntity.clear();
        m_instances.clear();
        m_sources.clear();
        return;
    }

    deformableRendererView.each(
        [&](Core::ECS::EntityID entity, DeformableRendererComponent& component){
            if(!ensureRuntimeMesh(entity, component))
                component.runtimeMesh.reset();
        }
    );

    for(auto it = m_instances.begin(); it != m_instances.end();){
        const Core::ECS::EntityID entity = it->first;
        if(world.tryGetComponent<DeformableRendererComponent>(entity)){
            ++it;
            continue;
        }

        const DeformableRuntimeMeshInstance& instance = it.value();
        m_handleToEntity.erase(instance.handle.value);
        releaseSource(instance.source.name());
        it = m_instances.erase(it);
    }
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
        instance.dirtyFlags & ~__hidden_deformable_runtime_mesh_cache::s_GpuUploadHandledDirtyFlags
    );

    ++source->referenceCount;
    const RuntimeMeshHandle handle = instance.handle;
    auto result = m_instances.try_emplace(entity, Move(instance));
    auto it = result.first;
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

    auto result = m_sources.try_emplace(sourceName, Move(source));
    auto it = result.first;
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
        handle.value = m_nextHandleValue;
        ++m_nextHandleValue;
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
    m_instances.erase(foundInstance);
}

void DeformableRuntimeMeshCache::releaseSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource == m_sources.end())
        return;

    DeformableGeometrySource& source = foundSource.value();
    if(source.referenceCount > 0u)
        --source.referenceCount;
    if(source.referenceCount == 0u)
        m_sources.erase(foundSource);
}

void DeformableRuntimeMeshCache::eraseUnusedSource(const Name& sourceName){
    const auto foundSource = m_sources.find(sourceName);
    if(foundSource != m_sources.end() && foundSource.value().referenceCount == 0u)
        m_sources.erase(foundSource);
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
            StringConvert(label)
        );
        return false;
    }

    outBytes = count * stride;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

