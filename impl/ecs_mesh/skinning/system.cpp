// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "arena_names.h"
#include "resource_names.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <impl/ecs_skeleton/runtime_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasPotentialSkinningWork(
    const MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose
){
    if((instance.dirtyFlags & (RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return true;

    return
        !instance.skin.empty()
        && (
            (jointPalette && !jointPalette->joints.empty())
            || SkeletonRuntime::HasSkeletonPose(skeletonPose)
        )
    ;
}

static void ResolveSkeletonComponents(
    Core::ECS::World& world,
    const Core::ECS::EntityID fallbackEntity,
    const Core::ECS::EntityID skeletonEntity,
    const SkeletonJointPaletteComponent*& outJointPalette,
    const SkeletonPoseComponent*& outSkeletonPose
){
    const Core::ECS::EntityID resolvedEntity = skeletonEntity.valid() ? skeletonEntity : fallbackEntity;
    outJointPalette = world.tryGetComponent<SkeletonJointPaletteComponent>(resolvedEntity);
    outSkeletonPose = world.tryGetComponent<SkeletonPoseComponent>(resolvedEntity);
}

static constexpr bool s_RuntimeSkinningMeshletFrustumCullingEnabled = true;
static constexpr bool s_RuntimeSkinningMeshletConeCullingEnabled = false; // Runtime deformations can make meshlet cones unsafe; benchmarks override through a test provider only.

struct PendingRuntimeMeshSubmission{
    RuntimeMeshHandle handle;
    MeshSkinningSubmissionCommit commit;
};

[[nodiscard]] static Core::GpuQueueRequest JointPaletteUploadQueueRequest(){
    Core::GpuQueueRequest request;
    request.requiredCapabilities = Core::GpuQueueCapability::Transfer;
    request.preferredQueue = Core::GpuQueuePreference::Graphics;
    request.allowFallback = false;
    // The following legacy compute list opens from this packet's final handoff. Keep both submissions on the
    // primary Graphics transport until skinning itself becomes a graph-native consumer.
    request.compilerMayOverridePreference = false;
    return request;
}

[[nodiscard]] static Core::GpuTaskSchedulingHint JointPaletteUploadScheduling(const bool mergeWithPrevious){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = mergeWithPrevious;
    return scheduling;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningSystem::resolveRestToSkinnedCopyByteCounts(
    const MeshSkinningRuntimeInstance& instance,
    usize& outPositionBytes,
    usize& outNormalBytes,
    usize& outTangentBytes
){
    const auto resolvePayloadBytes = [](const usize count, const usize stride, usize& outBytes, const tchar* label){
        outBytes = 0u;
        if(stride != 0u && TryMultiply<usize>(count, stride, outBytes))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: {} payload byte size overflows"), label);
        return false;
    };
    return
        resolvePayloadBytes(instance.restPositions.size(), sizeof(Float3U), outPositionBytes, NWB_TEXT("rest position"))
        && resolvePayloadBytes(instance.restNormals.size(), sizeof(Half4U), outNormalBytes, NWB_TEXT("rest normal"))
        && resolvePayloadBytes(instance.restTangents.size(), sizeof(Half4U), outTangentBytes, NWB_TEXT("rest tangent"))
    ;
}

void MeshSkinningSystem::resetAcceptedSkinningStateHandoff()noexcept{
    m_acceptedSkinningStateHandoff.reset();
    m_acceptedSkinningStateBuffers.clear();
}

bool MeshSkinningSystem::replaceAcceptedSkinningStateHandoff(
    const Core::CommandListResourceStateHandoff& state
){
    if(!state.valid()){
        resetAcceptedSkinningStateHandoff();
        return false;
    }

    // The handoff stores raw RHI pointers. Retain and filter against every current skinning buffer before replacing
    // it, so runtime-resource pruning/rebuild cannot leave an external source dangling or accidentally revive an
    // old buffer whose address was reused by a new generation.
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> retainedBuffers(m_arena);
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_FrameUploadArena);
    Vector<Core::Buffer*, Core::Alloc::ScratchArena> currentBuffers(scratchArena);
    const auto retainBuffer = [&](const Core::BufferHandle& buffer){
        if(!buffer)
            return;
        retainedBuffers.push_back(buffer);
        currentBuffers.push_back(buffer.get());
    };
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID, const SkinnedMeshBindingComponent& binding){
            if(!binding.runtimeMesh.valid())
                return;

            const MeshSkinningRuntimeInstance* const instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance || !instance->valid())
                return;

            retainBuffer(instance->restPositionBuffer);
            retainBuffer(instance->restNormalBuffer);
            retainBuffer(instance->restTangentBuffer);
            retainBuffer(instance->skinnedPositionBuffer);
            retainBuffer(instance->skinnedNormalBuffer);
            retainBuffer(instance->skinnedTangentBuffer);
            retainBuffer(instance->uv0Buffer);
            retainBuffer(instance->colorBuffer);
            retainBuffer(instance->meshletDescBuffer);
            retainBuffer(instance->meshletBoundsBuffer);
            retainBuffer(instance->meshletPositionRefDeltaBuffer);
            retainBuffer(instance->meshletAttributeRefDeltaBuffer);
            retainBuffer(instance->meshletLocalVertexRefBuffer);
            retainBuffer(instance->meshletPrimitiveIndexBuffer);
            retainBuffer(instance->attributeSkinBuffer);
            retainBuffer(instance->triangleIndexBuffer);
            retainBuffer(instance->attributeBuffer);

            const auto foundResources = m_runtimeResources.find(instance->handle.value);
            if(
                foundResources != m_runtimeResources.end()
                && foundResources.value().editRevision == instance->editRevision
            ){
                retainBuffer(foundResources.value().skinBuffer);
                retainBuffer(foundResources.value().jointPaletteBuffer);
                retainBuffer(foundResources.value().bindlessResourceSlotsBuffer);
            }
        }
    );

    Core::CommandListResourceStateHandoff filteredState(m_arena);
    if(!filteredState.buildResourceSubset(
        state,
        nullptr,
        0u,
        currentBuffers.data(),
        currentBuffers.size()
    ) || !m_acceptedSkinningStateHandoff.copyFrom(filteredState)){
        resetAcceptedSkinningStateHandoff();
        return false;
    }
    m_acceptedSkinningStateBuffers.clear();
    for(Core::BufferHandle& buffer : retainedBuffers)
        m_acceptedSkinningStateBuffers.push_back(Move(buffer));
    return true;
}


MeshSkinningSystem::MeshSkinningSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    IRuntimeMeshRegistry& runtimeMeshRegistry,
    ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_runtimeMeshRegistry(runtimeMeshRegistry)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_runtimeMeshCache(arena, graphics, assetManager)
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
    , m_graphOwnedRestCopyPlans(arena)
    , m_graphOwnedBindlessResourceSlotsPlans(arena)
    , m_acceptedSkinningStateBuffers(arena)
    , m_acceptedSkinningStateHandoff(arena)
    , m_graphOwnedJointPaletteStateHandoff(arena)
{
    writeAccess<SkinnedMeshBindingComponent>();
    readAccess<SkeletonJointPaletteComponent>();
    readAccess<SkeletonPoseComponent>();

    m_runtimeMeshRegistry.registerRuntimeMeshProvider(*this);
}

MeshSkinningSystem::~MeshSkinningSystem(){
    m_runtimeMeshRegistry.unregisterRuntimeMeshProvider(*this);
}

void MeshSkinningSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

bool MeshSkinningSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;

    if(!ensureFrameCommandList())
        return false;

    auto& device = m_graphics.getDevice();

    constexpr u32 s_PerRuntimeMeshTimingQueries = 128u;
    const bool timingReady =
        m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_Skinning.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_MeshletBounds.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_RepackNormals.identity, device, s_PerRuntimeMeshTimingQueries)
    ;
    if(!timingReady)
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: GPU timing scope preparation failed; timing samples may be skipped"));
    return true;
}

bool MeshSkinningSystem::ensureFrameCommandList(){
    auto& device = m_graphics.getDevice();

    if(!m_renderCommandList){
        m_renderCommandList = device.createCommandList();
        if(!m_renderCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create render command list"));
            return false;
        }
    }

    return true;
}

bool MeshSkinningSystem::prepareResources(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    m_runtimeMeshCache.prepareResources(m_world);
    pruneRuntimeResources();

    bool ready = true;
    bool hasRenderWork = false;
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
            if(!ready)
                return;
            if(!binding.runtimeMesh.valid())
                return;

            MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance)
                return;
            NWB_ASSERT(instance->valid());

            const SkeletonJointPaletteComponent* jointPalette = nullptr;
            const SkeletonPoseComponent* skeletonPose = nullptr;
            __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
            ready = prepareRuntimeMeshResources(*instance, jointPalette, skeletonPose);
            const auto foundResources = m_runtimeResources.find(instance->handle.value);
            const bool hasSkinningResources = foundResources != m_runtimeResources.end() && foundResources.value().usesSkinning();
            hasRenderWork =
                hasRenderWork
                || hasSkinningResources
                || __hidden_system::HasPotentialSkinningWork(*instance, jointPalette, skeletonPose)
            ;
        }
    );

    // A frame may have no native skinning submission after bindings are removed. Keep the accepted state source
    // useful for surviving generations, but filter out retired buffers now so its ownership handles cannot pin an
    // otherwise-pruned runtime mesh indefinitely.
    if(
        m_acceptedSkinningStateHandoff.valid()
        && !replaceAcceptedSkinningStateHandoff(m_acceptedSkinningStateHandoff)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prune the accepted skinning state handoff"));
        return false;
    }

    if(!ready || !hasRenderWork)
        return ready;

    NWB_ASSERT(m_renderCommandList);

    return true;
}

bool MeshSkinningSystem::resolveRuntimeMesh(const Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh){
    outMesh = RuntimeMeshDesc{};
    RuntimeMeshHandle runtimeMesh;
    if(const SkinnedMeshBindingComponent* binding = m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
        runtimeMesh = binding->runtimeMesh;
    if(!runtimeMesh.valid())
        return false;

    const MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(runtimeMesh);
    if(!instance || instance->entity != entity)
        return false;
    if((instance->dirtyFlags & (RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return false;
    NWB_ASSERT(instance->valid());
    NWB_ASSERT(instance->meshlets.size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(instance->meshletPrimitiveIndices.size() <= static_cast<usize>(Limit<u32>::s_Max));

    outMesh.entity = entity;
    outMesh.meshKey = DeriveRuntimeResourceName(
        instance->sourceName,
        instance->handle.value,
        instance->editRevision,
        "skinned_draw"
    );
    outMesh.positionBuffer = instance->skinnedPositionBuffer;
    outMesh.normalBuffer = instance->skinnedNormalBuffer;
    outMesh.tangentBuffer = instance->skinnedTangentBuffer;
    outMesh.uv0Buffer = instance->uv0Buffer;
    outMesh.colorBuffer = instance->colorBuffer;
    outMesh.meshletDescBuffer = instance->meshletDescBuffer;
    outMesh.meshletBoundsBuffer = instance->meshletBoundsBuffer;
    outMesh.meshletPositionRefDeltaBuffer = instance->meshletPositionRefDeltaBuffer;
    outMesh.meshletAttributeRefDeltaBuffer = instance->meshletAttributeRefDeltaBuffer;
    outMesh.meshletLocalVertexRefBuffer = instance->meshletLocalVertexRefBuffer;
    outMesh.meshletPrimitiveIndexBuffer = instance->meshletPrimitiveIndexBuffer;
    outMesh.triangleIndexBuffer = instance->triangleIndexBuffer;
    outMesh.attributeBuffer = instance->attributeBuffer;
    outMesh.localBounds = instance->localBounds;
    outMesh.meshletCount = static_cast<u32>(instance->meshlets.size());
    outMesh.meshletPrimitiveIndexCount = static_cast<u32>(instance->meshletPrimitiveIndices.size());
    outMesh.version = instance->editRevision;
    outMesh.dynamicMeshletBoundsFresh = __hidden_system::s_RuntimeSkinningMeshletFrustumCullingEnabled;
    outMesh.dynamicMeshletConesFresh = __hidden_system::s_RuntimeSkinningMeshletConeCullingEnabled;
    NWB_ASSERT(outMesh.valid());
    return true;
}

bool MeshSkinningSystem::containsRuntimeMesh(const Name& meshKey, const u64 version){
    if(!meshKey)
        return false;

    auto testEntity = [&](Core::ECS::EntityID entity, bool& found){
            if(found)
                return;

            RuntimeMeshDesc desc;
            if(!resolveRuntimeMesh(entity, desc))
                return;

            found = desc.meshKey == meshKey && desc.version == version;
    };

    bool found = false;
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
            static_cast<void>(component);
            testEntity(entity, found);
        }
    );
    return found;
}

bool MeshSkinningSystem::submitFrameJointPaletteUploads(){
    m_graphOwnedJointPaletteStateHandoff.reset();
    m_graphOwnedRestCopyPlans.clear();
    m_graphOwnedBindlessResourceSlotsPlans.clear();

    auto& device = m_graphics.getDevice();
    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: cannot declare joint-palette uploads without a physical queue topology"));
        return false;
    }

    Core::GpuTaskGraph graph(m_arena);
    Core::GpuTaskId terminalTask;
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_FrameUploadArena);
    const auto discardGraphOwnedFrameUpdates = [&](){
        m_graphOwnedJointPaletteStateHandoff.reset();
        m_graphOwnedRestCopyPlans.clear();
        m_graphOwnedBindlessResourceSlotsPlans.clear();
    };
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    bool declarationFailed = false;
    skinningBindings.each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
            if(declarationFailed || !binding.runtimeMesh.valid())
                return;

            MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance)
                return;
            NWB_ASSERT(instance->valid());

            const SkeletonJointPaletteComponent* jointPalette = nullptr;
            const SkeletonPoseComponent* skeletonPose = nullptr;
            __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
            const auto foundResources = m_runtimeResources.find(instance->handle.value);
            const bool hadSkinningResources = foundResources != m_runtimeResources.end() && foundResources.value().usesSkinning();
            if(!__hidden_system::HasPotentialSkinningWork(*instance, jointPalette, skeletonPose) && !hadSkinningResources)
                return;

            RuntimeSkinPayloadScratch payload{ scratchArena };
            // Keep a failed pose local to this mesh, matching the established direct-recording behavior.  The
            // compute path will report the same invalid payload and leave that mesh retryable next frame.
            if(!MeshSkinningPayload::BuildRuntimeSkinPayload(*instance, jointPalette, skeletonPose, payload))
                return;

            // The selector is consumed by every native skinning, bounds, and normal-repack dispatch. It is not
            // coupled to an active palette or a rest-stream copy: an inactive meshlet-bounds update is still a
            // selector-only graph frame. Freeze its immutable 64-byte payload before any early no-active return.
            const RuntimeMeshDirtyFlags dispatchDirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
                RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty
            );
            const bool dispatchConsumesSelector =
                !instance->meshlets.empty()
                && (
                    payload.hasActiveSkin()
                    || (instance->dirtyFlags & dispatchDirtyFlags) != 0u
                    || hadSkinningResources
                )
            ;
            if(
                dispatchConsumesSelector
                && foundResources != m_runtimeResources.end()
                && !foundResources.value().bindlessResourceSlotsUploaded
            ){
                const RuntimeResources& resources = foundResources.value();
                const Core::BufferHandle& slotsBuffer = resources.bindlessResourceSlotsBuffer;
                if(
                    !slotsBuffer
                    || !resources.bindlessHeapHandles.resourceSlots.valid()
                    || resources.bindlessHeapHandles.resourceSlots.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no valid bindless selector resource"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }
                const Core::BufferDesc& slotsDesc = slotsBuffer->getDescription();
                const Name uploadIdentity = DeriveRuntimeResourceName(
                    instance->sourceName,
                    instance->handle.value,
                    instance->editRevision,
                    "mesh_skinning_bindless_slots_upload"
                );
                if(!slotsDesc.debugName || !uploadIdentity){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no graph identity for its bindless slot upload"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }
                const Core::GpuGraphResourceId slotsDestination = graph.importBuffer(
                    slotsBuffer,
                    Core::GpuGraphResourceDesc{}
                        .setIdentity(slotsDesc.debugName)
                        .setMarkerLabel("Skinning Bindless Slots")
                        .setType(Core::GpuGraphResourceType::Buffer)
                        .setInitialState(slotsDesc.initialState)
                        .setQueueSharing(slotsDesc.queueSharing)
                );
                const Core::GpuUploadBlobId slotsSource = graph.copyUploadData(
                    &resources.bindlessResourceSlots,
                    sizeof(resources.bindlessResourceSlots),
                    alignof(RuntimeBindlessResourceSlots)
                );
                if(!slotsDestination.valid() || !slotsSource.valid()){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned bindless slots for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                Core::GpuTaskDesc slotsUploadDesc;
                slotsUploadDesc
                    .setIdentity(uploadIdentity)
                    .setMarkerLabel("Skinning Bindless Slots Upload")
                    .setQueue(__hidden_system::JointPaletteUploadQueueRequest())
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling(terminalTask.valid()))
                ;
                if(terminalTask.valid())
                    slotsUploadDesc.setDependencies(&terminalTask, 1u);

                // Automatic state tracking restores this selector to Common at packet close. The following native
                // command list owns the Common -> ConstantBuffer transition before it binds the selector heap slot.
                const Core::GpuTaskId slotsUploadTask = graph.addUploadBufferTask(
                    slotsUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = slotsSource,
                        .destination = slotsDestination,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!slotsUploadTask.valid()){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned bindless slots for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                GraphOwnedBindlessResourceSlotsPlan plan;
                plan.handle = instance->handle;
                plan.editRevision = instance->editRevision;
                plan.slotsBuffer = slotsBuffer;
                plan.slotsDescriptor = resources.bindlessHeapHandles.resourceSlots;
                plan.payload = resources.bindlessResourceSlots;
                plan.uploadTask = slotsUploadTask;
                m_graphOwnedBindlessResourceSlotsPlans.push_back(Move(plan));
                terminalTask = slotsUploadTask;
            }
            if(!payload.hasActiveSkin()){
                const bool copiesRestBuffers =
                    (instance->dirtyFlags & RuntimeMeshDirtyFlag::SkinningInputDirty) != 0u
                    || hadSkinningResources
                ;
                if(!copiesRestBuffers)
                    return;

                usize positionBytes = 0u;
                usize normalBytes = 0u;
                usize tangentBytes = 0u;
                if(!resolveRestToSkinnedCopyByteCounts(
                    *instance,
                    positionBytes,
                    normalBytes,
                    tangentBytes
                ) || positionBytes == 0u || normalBytes == 0u || tangentBytes == 0u){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to resolve rest-to-skinned copy sizes for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                const auto importCopyBuffer = [&](
                    const Core::BufferHandle& buffer,
                    const AStringView label,
                    const usize requiredBytes
                ){
                    if(!buffer)
                        return Core::GpuGraphResourceId{};
                    const Core::BufferDesc& bufferDesc = buffer->getDescription();
                    if(!bufferDesc.debugName || requiredBytes > bufferDesc.byteSize)
                        return Core::GpuGraphResourceId{};
                    return graph.importBuffer(
                        buffer,
                        Core::GpuGraphResourceDesc{}
                            .setIdentity(bufferDesc.debugName)
                            .setMarkerLabel(label)
                            .setType(Core::GpuGraphResourceType::Buffer)
                            .setInitialState(bufferDesc.initialState)
                            .setQueueSharing(bufferDesc.queueSharing)
                    );
                };
                const Core::GpuGraphResourceId restPositions = importCopyBuffer(
                    instance->restPositionBuffer,
                    "Runtime Rest Positions",
                    positionBytes
                );
                const Core::GpuGraphResourceId restNormals = importCopyBuffer(
                    instance->restNormalBuffer,
                    "Runtime Rest Normals",
                    normalBytes
                );
                const Core::GpuGraphResourceId restTangents = importCopyBuffer(
                    instance->restTangentBuffer,
                    "Runtime Rest Tangents",
                    tangentBytes
                );
                const Core::GpuGraphResourceId skinnedPositions = importCopyBuffer(
                    instance->skinnedPositionBuffer,
                    "Runtime Skinned Positions",
                    positionBytes
                );
                const Core::GpuGraphResourceId skinnedNormals = importCopyBuffer(
                    instance->skinnedNormalBuffer,
                    "Runtime Skinned Normals",
                    normalBytes
                );
                const Core::GpuGraphResourceId skinnedTangents = importCopyBuffer(
                    instance->skinnedTangentBuffer,
                    "Runtime Skinned Tangents",
                    tangentBytes
                );
                if(
                    !restPositions.valid()
                    || !restNormals.valid()
                    || !restTangents.valid()
                    || !skinnedPositions.valid()
                    || !skinnedNormals.valid()
                    || !skinnedTangents.valid()
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no graph-importable rest-to-skinned buffers"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                const Core::GpuCopyBufferTaskRegion copyRegions[] = {
                    Core::GpuCopyBufferTaskRegion{
                        .source = restPositions,
                        .destination = skinnedPositions,
                        .dataSizeBytes = positionBytes,
                    },
                    Core::GpuCopyBufferTaskRegion{
                        .source = restNormals,
                        .destination = skinnedNormals,
                        .dataSizeBytes = normalBytes,
                    },
                    Core::GpuCopyBufferTaskRegion{
                        .source = restTangents,
                        .destination = skinnedTangents,
                        .dataSizeBytes = tangentBytes,
                    },
                };
                Core::GpuTaskDesc copyDesc;
                copyDesc
                    .setIdentity(Name("mesh_skinning.frame_rest_to_skinned_copy"))
                    .setMarkerLabel("Skinning Rest-to-Skinned Copy")
                    .setQueue(__hidden_system::JointPaletteUploadQueueRequest())
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling(terminalTask.valid()))
                ;
                if(terminalTask.valid())
                    copyDesc.setDependencies(&terminalTask, 1u);

                const Core::GpuTaskId copyTask = graph.addCopyBufferTask(
                    copyDesc,
                    Core::GpuCopyBufferTaskDesc{
                        .regions = copyRegions,
                        .regionCount = LengthOf(copyRegions),
                    }
                );
                if(!copyTask.valid()){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned rest-to-skinned copy for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                GraphOwnedRestCopyPlan plan;
                plan.handle = instance->handle;
                plan.editRevision = instance->editRevision;
                plan.restPositionBuffer = instance->restPositionBuffer;
                plan.restNormalBuffer = instance->restNormalBuffer;
                plan.restTangentBuffer = instance->restTangentBuffer;
                plan.skinnedPositionBuffer = instance->skinnedPositionBuffer;
                plan.skinnedNormalBuffer = instance->skinnedNormalBuffer;
                plan.skinnedTangentBuffer = instance->skinnedTangentBuffer;
                plan.positionBytes = positionBytes;
                plan.normalBytes = normalBytes;
                plan.tangentBytes = tangentBytes;
                m_graphOwnedRestCopyPlans.push_back(Move(plan));
                terminalTask = copyTask;
                return;
            }

            if(foundResources == m_runtimeResources.end() || !foundResources.value().jointPaletteBuffer){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: active runtime mesh '{}' has no joint palette buffer"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            usize jointPaletteBytes = 0u;
            if(
                payload.jointMatrices.size() > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix)
                || (jointPaletteBytes = payload.jointMatrices.size() * sizeof(SkeletonJointMatrix)) == 0u
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette payload byte size overflows"));
                declarationFailed = true;
                return;
            }

            const Core::BufferHandle& destinationBuffer = foundResources.value().jointPaletteBuffer;
            const Core::BufferDesc& destinationDesc = destinationBuffer->getDescription();
            if(!destinationDesc.debugName){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette buffer for runtime mesh '{}' has no graph identity"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            const Core::GpuGraphResourceId destination = graph.importBuffer(
                destinationBuffer,
                Core::GpuGraphResourceDesc{}
                    .setIdentity(destinationDesc.debugName)
                    .setMarkerLabel("Skinning Joint Palette")
                    .setType(Core::GpuGraphResourceType::Buffer)
                    .setInitialState(destinationDesc.initialState)
                    .setQueueSharing(destinationDesc.queueSharing)
            );
            const Core::GpuUploadBlobId source = graph.copyUploadData(
                payload.jointMatrices.data(),
                jointPaletteBytes,
                alignof(SkeletonJointMatrix)
            );
            if(!destination.valid() || !source.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned joint palette data for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            Core::GpuTaskDesc uploadDesc;
            uploadDesc
                .setIdentity(Name("mesh_skinning.frame_joint_palette_upload"))
                .setMarkerLabel("Skinning Joint Palette Upload")
                .setQueue(__hidden_system::JointPaletteUploadQueueRequest())
                .setScheduling(__hidden_system::JointPaletteUploadScheduling(terminalTask.valid()))
            ;
            if(terminalTask.valid())
                uploadDesc.setDependencies(&terminalTask, 1u);

            const Core::GpuTaskId uploadTask = graph.addUploadBufferTask(
                uploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = source,
                    .destination = destination,
                    .finalState = Core::ResourceStates::ShaderResource,
                }
            );
            if(!uploadTask.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned joint palette upload for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            terminalTask = uploadTask;
        }
    );

    if(declarationFailed){
        discardGraphOwnedFrameUpdates();
        return false;
    }
    if(!terminalTask.valid())
        return true;

    Core::GpuTaskGraphAnalysis analysis(m_arena);
    Core::GpuTaskGraphQueueAssignments assignments(m_arena);
    Core::GpuCompiledGraph compiledGraph(m_arena);
    Core::GpuRecordedGraph recordedGraph(m_arena);
    Core::GpuGraphSubmissionTransaction transaction(m_arena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena)){
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to compile graph-owned joint palette uploads"));
        return false;
    }

    if(compiledGraph.packetCount() != 1u){
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning frame updates did not merge into one primary Graphics packet"));
        return false;
    }

    const Core::GpuSubmissionPacketId terminalPacket = compiledGraph.packetForTask(terminalTask);
    if(!terminalPacket.valid()){
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette upload graph has no terminal packet"));
        return false;
    }
    for(const GraphOwnedBindlessResourceSlotsPlan& plan : m_graphOwnedBindlessResourceSlotsPlans){
        const Core::GpuSubmissionPacketId selectorPacket = compiledGraph.packetForTask(plan.uploadTask);
        if(!selectorPacket.valid() || selectorPacket != terminalPacket){
            discardGraphOwnedFrameUpdates();
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: bindless selector upload detached from the native handoff packet"));
            return false;
        }
    }
    const Core::GpuSubmissionPacket& packet = compiledGraph.packet(terminalPacket);
    const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(packet.queue != graphicsQueue){
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette upload graph did not retain the primary Graphics queue"));
        return false;
    }

    transaction.reset(compiledGraph);
    const Core::GpuNativePacketRecorder recorder(device);
    Core::GpuExternalPacketStateSource previousFrameStateSources[1u] = {};
    Core::GpuNativePacketRecordDesc recordOverrides[1u] = {};
    usize recordOverrideCount = 0u;
    if(m_acceptedSkinningStateHandoff.valid()){
        previousFrameStateSources[0u] = Core::GpuExternalPacketStateSource{
            .states = &m_acceptedSkinningStateHandoff,
        };
        recordOverrides[0u] = Core::GpuNativePacketRecordDesc{
            .packet = terminalPacket,
            .externalStateSources = previousFrameStateSources,
            .externalStateSourceCount = LengthOf(previousFrameStateSources),
        };
        recordOverrideCount = LengthOf(recordOverrides);
    }
    if(!recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        recordOverrides,
        recordOverrideCount,
        recordedGraph
    )){
        transaction.discardUnaccepted(graph, compiledGraph);
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record graph-owned joint palette uploads"));
        return false;
    }

    const Core::CommandListResourceStateHandoff* const finalStates = recordedGraph.packetFinalStateSeed(terminalPacket);
    Core::CommandListResourceStateHandoff graphStateHandoff(m_arena);
    const Core::CommandListResourceStateHandoff* const graphStateBranches[] = { finalStates };
    if(
        !finalStates
        || (
            m_acceptedSkinningStateHandoff.valid()
            ? !graphStateHandoff.buildFanIn(
                m_acceptedSkinningStateHandoff,
                graphStateBranches,
                LengthOf(graphStateBranches)
            )
            : !graphStateHandoff.copyFrom(*finalStates)
        )
    ){
        transaction.discardUnaccepted(graph, compiledGraph);
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned skinning frame state"));
        return false;
    }

    const Core::GpuTaskGraphSubmitter submitter(device);
    if(!submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    )){
        transaction.discardUnaccepted(graph, compiledGraph);
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned joint palette submission was rejected"));
        return false;
    }

    const Core::QueueSubmissionToken uploadToken = transaction.packetToken(terminalPacket);
    if(!uploadToken.valid() || !uploadToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration)){
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned joint palette submission lost its Graphics queue identity"));
        return false;
    }
    // Filter the accepted state before publishing the graph-to-native seed. The fan-in can still contain a retired
    // runtime-buffer generation from the previous frame; replaceAcceptedSkinningStateHandoff drops it and retains
    // every live buffer before releasing the old handles. Copying the unfiltered fan-in here would leave the native
    // command list with dangling raw pointers after a resource rebuild.
    if(
        !replaceAcceptedSkinningStateHandoff(graphStateHandoff)
        || !m_graphOwnedJointPaletteStateHandoff.copyFrom(m_acceptedSkinningStateHandoff)
    ){
        discardGraphOwnedFrameUpdates();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain accepted graph-owned skinning state"));
        return false;
    }
    confirmGraphOwnedBindlessResourceSlotsUploads();
    return true;
}

void MeshSkinningSystem::render(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    auto& device = m_graphics.getDevice();
    Core::CommandList* commandList = m_renderCommandList.get();
    NWB_ASSERT(commandList);

    if(!submitFrameJointPaletteUploads()){
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: skipped skinning dispatch because graph-owned joint palette uploads were not accepted"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_SubmissionArena);
    Vector<__hidden_system::PendingRuntimeMeshSubmission, Core::Alloc::ScratchArena> pendingSubmissions{ scratchArena };
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    pendingSubmissions.reserve(skinningBindings.candidateCount());

    Core::GpuTimingSubmissionTicket timingTicket(m_graphics.gpuTiming());
    bool submittedWork = false;
    Core::CommandListResourceStateHandoff nativeFinalStateHandoff(m_arena);

    {
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        commandList->open(
            m_graphOwnedJointPaletteStateHandoff.valid()
                ? &m_graphOwnedJointPaletteStateHandoff
                : (
                    m_acceptedSkinningStateHandoff.valid()
                        ? &m_acceptedSkinningStateHandoff
                        : nullptr
                )
        );

        skinningBindings.each(
            [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
                if(!binding.runtimeMesh.valid())
                    return;

                MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
                if(!instance)
                    return;
                NWB_ASSERT(instance->valid());

                const SkeletonJointPaletteComponent* jointPalette = nullptr;
                const SkeletonPoseComponent* skeletonPose = nullptr;
                __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
                const auto foundResources = m_runtimeResources.find(instance->handle.value);
                const bool hadSkinningResources = foundResources != m_runtimeResources.end() && foundResources.value().usesSkinning();
                if(!__hidden_system::HasPotentialSkinningWork(
                    *instance,
                    jointPalette,
                    skeletonPose
                ) && !hadSkinningResources)
                    return;
                MeshSkinningSubmissionCommit submissionCommit;
                if(dispatchRuntimeMesh(*commandList, *instance, jointPalette, skeletonPose, submissionCommit)){
                    submittedWork = true;
                    if(!submissionCommit.empty()){
                        __hidden_system::PendingRuntimeMeshSubmission pendingSubmission;
                        pendingSubmission.handle = instance->handle;
                        pendingSubmission.commit = submissionCommit;
                        pendingSubmissions.push_back(pendingSubmission);
                    }
                }
            }
        );

        commandList->close(&nativeFinalStateHandoff);
    }

    if(submittedWork){
        if(!nativeFinalStateHandoff.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record the native skinning state handoff"));
            return;
        }
        Core::CommandList* commandLists[] = { commandList };
        const bool submissionAccepted = timingTicket.submit(device, commandLists, 1u);
        if(submissionAccepted && !replaceAcceptedSkinningStateHandoff(nativeFinalStateHandoff))
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain the accepted native skinning state handoff"));
        // CPU visibility follows GPU queue acceptance. If this submission is rejected, both the deformation dirty
        // state and the selector upload remain pending so the next frame records the complete pose again.
        for(const __hidden_system::PendingRuntimeMeshSubmission& pendingSubmission : pendingSubmissions){
            MeshSkinningRuntimeInstance* submittedInstance = m_runtimeMeshCache.findInstance(pendingSubmission.handle);
            if(!submittedInstance)
                continue;
            const auto foundResources = m_runtimeResources.find(pendingSubmission.handle.value);
            if(
                foundResources == m_runtimeResources.end()
                || foundResources.value().editRevision != pendingSubmission.commit.editRevision
            )
                continue;

            ApplyMeshSkinningSubmissionCommit(
                submissionAccepted,
                submittedInstance->editRevision,
                submittedInstance->dirtyFlags,
                foundResources.value().bindlessResourceSlotsUploaded,
                pendingSubmission.commit
            );
        }
        if(!submissionAccepted)
            NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: skinning command submission was rejected"));
    }
}

void MeshSkinningSystem::pruneRuntimeResources(){
    if(m_runtimeResources.empty())
        return;

    for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
        RuntimeResources& resources = it.value();
        const MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(resources.handle);
        if(instance && instance->valid() && instance->editRevision == resources.editRevision){
            ++it;
            continue;
        }

        // Heap descriptors retain their backing buffers independently of this cache. Retire the generation before
        // erasing it so the heap's in-flight quarantine can release both descriptor storage and retained resources.
        releaseRuntimeResourceBindlessHeapHandles(resources);
        it = m_runtimeResources.erase(it);
    }
}

void MeshSkinningSystem::invalidateResources(){
    m_renderCommandList.reset();
    resetAcceptedSkinningStateHandoff();
    m_graphOwnedJointPaletteStateHandoff.reset();
    m_graphOwnedRestCopyPlans.clear();
    m_graphOwnedBindlessResourceSlotsPlans.clear();
    for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end(); ++it)
        releaseRuntimeResourceBindlessHeapHandles(it.value());
    m_runtimeResources.clear();
    m_runtimeMeshCache.clear();

    m_skinningBindingLayout.reset();
    m_skinningComputeShader.reset();
    m_skinningComputePipeline.reset();
    m_boundsBindingLayout.reset();
    m_boundsComputeShader.reset();
    m_boundsComputePipeline.reset();
    m_repackBindingLayout.reset();
    m_repackComputeShader.reset();
    m_repackComputePipeline.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

