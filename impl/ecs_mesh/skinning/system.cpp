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
#include <core/graphics/gpu_timing.h>
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

[[nodiscard]] static Core::GpuQueueRequest JointPaletteUploadQueueRequest(){
    Core::GpuQueueRequest request;
    request.requiredCapabilities = Core::GpuQueueCapability::Transfer;
    request.preferredQueue = Core::GpuQueuePreference::Graphics;
    request.allowFallback = false;
    // The graph-owned compute continuation merges with these uploads on primary Graphics. Keep the full skinning
    // chain on that transport until an explicitly profiled async-compute variant is introduced.
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

[[nodiscard]] static Core::GpuQueueRequest SkinningDispatchQueueRequest(){
    Core::GpuQueueRequest request;
    request.requiredCapabilities = Core::GpuQueueCapability::Compute;
    request.preferredQueue = Core::GpuQueuePreference::Graphics;
    request.allowFallback = false;
    // Runtime skinning remains immediately before the renderer on the primary Graphics transport. The graph owns
    // this ordering now; a later async-compute migration can opt into a distinct physical queue deliberately.
    request.compilerMayOverridePreference = false;
    return request;
}

[[nodiscard]] static Core::GpuTaskSchedulingHint SkinningDispatchScheduling(){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    return scheduling;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshSkinningSystem::TaskGraphSkinningDispatchTask{
    struct Payload{
        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plans(arena)
        {}

        MeshSkinningSystem* system = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Vector<GraphOwnedSkinningDispatchPlan, Core::Alloc::GlobalArena> plans;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(!payload.system || !payload.timingTicket || payload.plans.empty())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        for(const GraphOwnedSkinningDispatchPlan& plan : payload.plans){
            if(!payload.system->recordGraphOwnedSkinningDispatch(plan, commandList, context))
                return false;
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(!payload.system || !token.valid())
            return;

        for(const GraphOwnedSkinningDispatchPlan& plan : payload.plans)
            payload.system->confirmGraphOwnedSkinningDispatch(plan);
    }
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
    , m_acceptedSkinningStateBuffers(arena)
    , m_acceptedSkinningStateHandoff(arena)
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

bool MeshSkinningSystem::submitFrameSkinningGraph(){
    m_graphOwnedRestCopyPlans.clear();

    auto& device = m_graphics.getDevice();
    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: cannot declare graph-owned skinning without a physical queue topology"));
        return false;
    }

    Core::GpuTaskGraph graph(m_arena);
    Core::GpuTaskId terminalTask;
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_FrameUploadArena);
    Vector<GraphOwnedSkinningDispatchPlan, Core::Alloc::GlobalArena> dispatchPlans(m_arena);
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    dispatchPlans.reserve(skinningBindings.candidateCount());
    bool declarationFailed = false;

    const auto importComputePipeline = [&](
        const Core::ComputePipelineHandle& pipeline,
        const Name identity,
        const AStringView label
    ){
        return graph.importComputePipeline(
            pipeline,
            Core::GpuGraphPipelineDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(Core::GpuGraphPipelineType::Compute)
        );
    };

    skinningBindings.each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
            if(declarationFailed || !binding.runtimeMesh.valid())
                return;

            MeshSkinningRuntimeInstance* const instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
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
            // Preserve the direct path's per-mesh retry semantics: an invalid pose never prevents another ready
            // mesh from declaring its immutable packet plan.
            if(!MeshSkinningPayload::BuildRuntimeSkinPayload(*instance, jointPalette, skeletonPose, payload))
                return;

            const bool hasActiveSkin = payload.hasActiveSkin();
            const bool skinningInputDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::SkinningInputDirty) != 0u;
            const bool meshletBoundsDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u;
            const bool copiesRestStreams = !hasActiveSkin && (skinningInputDirty || hadSkinningResources);
            const bool updatesMeshletBounds = hasActiveSkin || meshletBoundsDirty || copiesRestStreams;
            if(!updatesMeshletBounds || instance->meshlets.empty())
                return;

            if(foundResources == m_runtimeResources.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no prepared graph resources"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            const RuntimeResources& resources = foundResources.value();
            if(
                !resources.bindlessResourceSlotsBuffer
                || !resources.bindlessHeapHandles.resourceSlots.valid()
                || resources.bindlessHeapHandles.resourceSlots.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
                || !m_boundsComputePipeline
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has incomplete graph-owned dispatch state"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            if(hasActiveSkin && (!resources.skinBuffer || !resources.jointPaletteBuffer || !m_skinningComputePipeline)){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: active runtime mesh '{}' has no graph-owned skinning pipeline or payload"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            GraphOwnedSkinningDispatchPlan plan;
            plan.handle = instance->handle;
            plan.submissionCommit.editRevision = instance->editRevision;
            plan.submissionCommit.handledDirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
                (skinningInputDirty ? RuntimeMeshDirtyFlag::SkinningInputDirty : RuntimeMeshDirtyFlag::None)
                | (meshletBoundsDirty ? RuntimeMeshDirtyFlag::MeshletBoundsDirty : RuntimeMeshDirtyFlag::None)
            );
            plan.hasActiveSkin = hasActiveSkin;
            plan.copiedRestStreams = copiesRestStreams;
            plan.updatesMeshletBounds = updatesMeshletBounds;
            plan.repacksNormals = (hasActiveSkin || copiesRestStreams) && instance->attributeBuffer != nullptr;
            plan.meshletCount = static_cast<u32>(instance->meshlets.size());
            plan.skinCount = static_cast<u32>(payload.skinInfluences.size());
            plan.jointCount = static_cast<u32>(payload.jointMatrices.size());
            plan.skinningMode = payload.resolvedSkinningMode;
            plan.attributeCount = instance->meshletAttributeRefCount;
            plan.bindlessResourceSlots = resources.bindlessHeapHandles.resourceSlots.slot();
            plan.bindlessResourceSlotsBuffer = resources.bindlessResourceSlotsBuffer;
            plan.bindlessResourceSlotsDescriptor = resources.bindlessHeapHandles.resourceSlots;
            plan.bindlessResourceSlotsPayload = resources.bindlessResourceSlots;

            if(plan.repacksNormals && !m_repackComputePipeline){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' requires a missing normal-repack pipeline"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            const auto importBuffer = [&](const Core::BufferHandle& buffer, const AStringView label){
                if(!buffer)
                    return Core::GpuGraphResourceId{};
                const Core::BufferDesc& description = buffer->getDescription();
                if(!description.debugName)
                    return Core::GpuGraphResourceId{};
                return graph.importBuffer(
                    buffer,
                    Core::GpuGraphResourceDesc{}
                        .setIdentity(description.debugName)
                        .setMarkerLabel(label)
                        .setType(Core::GpuGraphResourceType::Buffer)
                        .setInitialState(description.initialState)
                        .setQueueSharing(description.queueSharing)
                );
            };

            plan.bindlessResourceSlotsResource = importBuffer(plan.bindlessResourceSlotsBuffer, "Skinning Bindless Slots");
            if(hasActiveSkin || copiesRestStreams){
                plan.restPositionResource = importBuffer(instance->restPositionBuffer, "Runtime Rest Positions");
                plan.restNormalResource = importBuffer(instance->restNormalBuffer, "Runtime Rest Normals");
                plan.restTangentResource = importBuffer(instance->restTangentBuffer, "Runtime Rest Tangents");
                plan.skinnedPositionResource = importBuffer(instance->skinnedPositionBuffer, "Runtime Skinned Positions");
                plan.skinnedNormalResource = importBuffer(instance->skinnedNormalBuffer, "Runtime Skinned Normals");
                plan.skinnedTangentResource = importBuffer(instance->skinnedTangentBuffer, "Runtime Skinned Tangents");
            }
            if(updatesMeshletBounds){
                if(!plan.skinnedPositionResource.valid())
                    plan.skinnedPositionResource = importBuffer(instance->skinnedPositionBuffer, "Runtime Skinned Positions");
                plan.meshletDescResource = importBuffer(instance->meshletDescBuffer, "Runtime Meshlet Descriptors");
                plan.meshletPositionRefDeltaResource = importBuffer(instance->meshletPositionRefDeltaBuffer, "Runtime Meshlet Position Deltas");
                plan.meshletLocalVertexRefResource = importBuffer(instance->meshletLocalVertexRefBuffer, "Runtime Meshlet Local Vertex Refs");
                plan.meshletPrimitiveIndexResource = importBuffer(instance->meshletPrimitiveIndexBuffer, "Runtime Meshlet Primitive Indices");
                plan.meshletBoundsResource = importBuffer(instance->meshletBoundsBuffer, "Runtime Meshlet Bounds");
            }
            if(hasActiveSkin){
                plan.meshletAttributeRefDeltaResource = importBuffer(instance->meshletAttributeRefDeltaBuffer, "Runtime Meshlet Attribute Deltas");
                plan.attributeSkinResource = importBuffer(instance->attributeSkinBuffer, "Runtime Attribute Skins");
                plan.skinResource = importBuffer(resources.skinBuffer, "Skinning Influences");
                plan.jointPaletteResource = importBuffer(resources.jointPaletteBuffer, "Skinning Joint Palette");
            }
            if(plan.repacksNormals){
                if(!plan.skinnedNormalResource.valid())
                    plan.skinnedNormalResource = importBuffer(instance->skinnedNormalBuffer, "Runtime Skinned Normals");
                if(!plan.meshletDescResource.valid())
                    plan.meshletDescResource = importBuffer(instance->meshletDescBuffer, "Runtime Meshlet Descriptors");
                if(!plan.meshletPrimitiveIndexResource.valid())
                    plan.meshletPrimitiveIndexResource = importBuffer(instance->meshletPrimitiveIndexBuffer, "Runtime Meshlet Primitive Indices");
                if(!plan.meshletAttributeRefDeltaResource.valid())
                    plan.meshletAttributeRefDeltaResource = importBuffer(instance->meshletAttributeRefDeltaBuffer, "Runtime Meshlet Attribute Deltas");
                if(!plan.meshletLocalVertexRefResource.valid())
                    plan.meshletLocalVertexRefResource = importBuffer(instance->meshletLocalVertexRefBuffer, "Runtime Meshlet Local Vertex Refs");
                plan.attributeResource = importBuffer(instance->attributeBuffer, "Runtime Ray Trace Attributes");
            }

            plan.boundsPipeline = importComputePipeline(
                m_boundsComputePipeline,
                Name("mesh_skinning.graph_bounds_pipeline"),
                "Skinning Bounds Pipeline"
            );
            if(hasActiveSkin){
                plan.skinningPipeline = importComputePipeline(
                    m_skinningComputePipeline,
                    Name("mesh_skinning.graph_skinning_pipeline"),
                    "Skinning Compute Pipeline"
                );
            }
            if(plan.repacksNormals){
                plan.repackPipeline = importComputePipeline(
                    m_repackComputePipeline,
                    Name("mesh_skinning.graph_repack_pipeline"),
                    "Skinning Repack Pipeline"
                );
            }

            const bool importsValid =
                plan.bindlessResourceSlotsResource.valid()
                && plan.skinnedPositionResource.valid()
                && plan.meshletDescResource.valid()
                && plan.meshletPositionRefDeltaResource.valid()
                && plan.meshletLocalVertexRefResource.valid()
                && plan.meshletPrimitiveIndexResource.valid()
                && plan.meshletBoundsResource.valid()
                && plan.boundsPipeline.valid()
                && (!hasActiveSkin || (
                    plan.restPositionResource.valid()
                    && plan.restNormalResource.valid()
                    && plan.restTangentResource.valid()
                    && plan.skinnedNormalResource.valid()
                    && plan.skinnedTangentResource.valid()
                    && plan.meshletAttributeRefDeltaResource.valid()
                    && plan.attributeSkinResource.valid()
                    && plan.skinResource.valid()
                    && plan.jointPaletteResource.valid()
                    && plan.skinningPipeline.valid()
                ))
                && (!copiesRestStreams || (
                    plan.restPositionResource.valid()
                    && plan.restNormalResource.valid()
                    && plan.restTangentResource.valid()
                    && plan.skinnedNormalResource.valid()
                    && plan.skinnedTangentResource.valid()
                ))
                && (!plan.repacksNormals || (
                    plan.skinnedNormalResource.valid()
                    && plan.meshletAttributeRefDeltaResource.valid()
                    && plan.attributeResource.valid()
                    && plan.repackPipeline.valid()
                ))
            ;
            if(!importsValid){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no graph-importable dispatch resource"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            if(!resources.bindlessResourceSlotsUploaded){
                const Name uploadIdentity = DeriveRuntimeResourceName(
                    instance->sourceName,
                    instance->handle.value,
                    instance->editRevision,
                    "mesh_skinning_bindless_slots_upload"
                );
                const Core::GpuUploadBlobId selectorSource = graph.copyUploadData(
                    &resources.bindlessResourceSlots,
                    sizeof(resources.bindlessResourceSlots),
                    alignof(RuntimeBindlessResourceSlots)
                );
                if(!uploadIdentity || !selectorSource.valid()){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned bindless slots for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                Core::GpuTaskDesc selectorUploadDesc;
                selectorUploadDesc
                    .setIdentity(uploadIdentity)
                    .setMarkerLabel("Skinning Bindless Slots Upload")
                    .setQueue(__hidden_system::JointPaletteUploadQueueRequest())
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling(terminalTask.valid()))
                ;
                if(terminalTask.valid())
                    selectorUploadDesc.setDependencies(&terminalTask, 1u);

                const Core::GpuTaskId selectorUploadTask = graph.addUploadBufferTask(
                    selectorUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = selectorSource,
                        .destination = plan.bindlessResourceSlotsResource,
                        // The following graph-owned compute task declares the descriptor-visible ConstantBuffer state.
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!selectorUploadTask.valid()){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned bindless slots for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }
                plan.submissionCommit.bindlessResourceSlotsUploadRecorded = true;
                terminalTask = selectorUploadTask;
            }

            if(copiesRestStreams){
                usize positionBytes = 0u;
                usize normalBytes = 0u;
                usize tangentBytes = 0u;
                if(
                    !resolveRestToSkinnedCopyByteCounts(*instance, positionBytes, normalBytes, tangentBytes)
                    || positionBytes == 0u
                    || normalBytes == 0u
                    || tangentBytes == 0u
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to resolve rest-to-skinned copy sizes for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                const Core::GpuCopyBufferTaskRegion copyRegions[] = {
                    Core::GpuCopyBufferTaskRegion{
                        .source = plan.restPositionResource,
                        .destination = plan.skinnedPositionResource,
                        .dataSizeBytes = positionBytes,
                    },
                    Core::GpuCopyBufferTaskRegion{
                        .source = plan.restNormalResource,
                        .destination = plan.skinnedNormalResource,
                        .dataSizeBytes = normalBytes,
                    },
                    Core::GpuCopyBufferTaskRegion{
                        .source = plan.restTangentResource,
                        .destination = plan.skinnedTangentResource,
                        .dataSizeBytes = tangentBytes,
                    },
                };
                Core::GpuTaskDesc copyDesc;
                copyDesc
                    .setIdentity(DeriveRuntimeResourceName(
                        instance->sourceName,
                        instance->handle.value,
                        instance->editRevision,
                        "mesh_skinning_rest_to_skinned_copy"
                    ))
                    .setMarkerLabel("Skinning Rest-to-Skinned Copy")
                    .setQueue(__hidden_system::JointPaletteUploadQueueRequest())
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling(terminalTask.valid()))
                ;
                if(!copyDesc.identity){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive graph identity for rest-to-skinned copy"));
                    declarationFailed = true;
                    return;
                }
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
                terminalTask = copyTask;
            }

            if(hasActiveSkin){
                usize jointPaletteBytes = 0u;
                if(
                    payload.jointMatrices.size() > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix)
                    || (jointPaletteBytes = payload.jointMatrices.size() * sizeof(SkeletonJointMatrix)) == 0u
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette payload byte size overflows"));
                    declarationFailed = true;
                    return;
                }
                const Core::GpuUploadBlobId jointPaletteSource = graph.copyUploadData(
                    payload.jointMatrices.data(),
                    jointPaletteBytes,
                    alignof(SkeletonJointMatrix)
                );
                if(!jointPaletteSource.valid()){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned joint palette data for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }

                Core::GpuTaskDesc jointPaletteUploadDesc;
                jointPaletteUploadDesc
                    .setIdentity(DeriveRuntimeResourceName(
                        instance->sourceName,
                        instance->handle.value,
                        instance->editRevision,
                        "mesh_skinning_joint_palette_upload"
                    ))
                    .setMarkerLabel("Skinning Joint Palette Upload")
                    .setQueue(__hidden_system::JointPaletteUploadQueueRequest())
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling(terminalTask.valid()))
                ;
                if(!jointPaletteUploadDesc.identity){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive graph identity for joint palette upload"));
                    declarationFailed = true;
                    return;
                }
                if(terminalTask.valid())
                    jointPaletteUploadDesc.setDependencies(&terminalTask, 1u);

                const Core::GpuTaskId jointPaletteUploadTask = graph.addUploadBufferTask(
                    jointPaletteUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = jointPaletteSource,
                        .destination = plan.jointPaletteResource,
                        .finalState = Core::ResourceStates::ShaderResource,
                    }
                );
                if(!jointPaletteUploadTask.valid()){
                    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned joint palette upload for runtime mesh '{}'"), instance->handle.value);
                    declarationFailed = true;
                    return;
                }
                terminalTask = jointPaletteUploadTask;
            }

            dispatchPlans.push_back(Move(plan));
        }
    );

    if(declarationFailed)
        return false;
    if(dispatchPlans.empty())
        return true;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> dispatchResourceUses(scratchArena);
    const auto addResourceUse = [&](const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state, const Core::GpuTaskResourceAccess::Enum access){
        if(!resource.valid())
            return false;
        for(Core::GpuTaskResourceUse& existing : dispatchResourceUses){
            if(existing.resource != resource)
                continue;
            if(existing.requiredState != state)
                return false;
            if(existing.access != access)
                existing.access = Core::GpuTaskResourceAccess::ReadWrite;
            return true;
        }
        dispatchResourceUses.push_back(Core::GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = state,
            .access = access,
        });
        return true;
    };
    for(const GraphOwnedSkinningDispatchPlan& plan : dispatchPlans){
        const auto addRead = [&](const Core::GpuGraphResourceId resource){
            return addResourceUse(resource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
        };
        const auto addWrite = [&](const Core::GpuGraphResourceId resource){
            return addResourceUse(resource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Write);
        };
        const auto addReadWrite = [&](const Core::GpuGraphResourceId resource){
            return addResourceUse(resource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::ReadWrite);
        };
        if(
            !addResourceUse(
                plan.bindlessResourceSlotsResource,
                Core::ResourceStates::ConstantBuffer,
                Core::GpuTaskResourceAccess::Read
            )
            || !addRead(plan.skinnedPositionResource)
            || !addRead(plan.meshletDescResource)
            || !addRead(plan.meshletPositionRefDeltaResource)
            || !addRead(plan.meshletLocalVertexRefResource)
            || !addRead(plan.meshletPrimitiveIndexResource)
            || !addWrite(plan.meshletBoundsResource)
            || (plan.hasActiveSkin && (
                !addRead(plan.restPositionResource)
                || !addRead(plan.restNormalResource)
                || !addRead(plan.restTangentResource)
                || !addReadWrite(plan.skinnedPositionResource)
                || !addReadWrite(plan.skinnedNormalResource)
                || !addReadWrite(plan.skinnedTangentResource)
                || !addRead(plan.meshletAttributeRefDeltaResource)
                || !addRead(plan.attributeSkinResource)
                || !addRead(plan.skinResource)
                || !addRead(plan.jointPaletteResource)
            ))
            || (plan.copiedRestStreams && (
                !addRead(plan.skinnedNormalResource)
                || !addRead(plan.skinnedTangentResource)
            ))
            || (plan.repacksNormals && (
                !addRead(plan.skinnedNormalResource)
                || !addRead(plan.meshletAttributeRefDeltaResource)
                || !addWrite(plan.attributeResource)
            ))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph resource uses for runtime skinning"));
            return false;
        }
    }

    Core::GpuTimingSubmissionTicket timingTicket(m_graphics.gpuTiming());
    TaskGraphSkinningDispatchTask::Payload dispatchPayload(m_arena);
    dispatchPayload.system = this;
    dispatchPayload.timingTicket = &timingTicket;
    dispatchPayload.plans = Move(dispatchPlans);
    Core::GpuTaskDesc dispatchDesc;
    dispatchDesc
        .setIdentity(Name("mesh_skinning.frame_dispatch"))
        .setMarkerLabel("Runtime Skinning Dispatch")
        .setQueue(__hidden_system::SkinningDispatchQueueRequest())
        .setScheduling(__hidden_system::SkinningDispatchScheduling())
        .setResourceUses(dispatchResourceUses.data(), dispatchResourceUses.size())
    ;
    if(terminalTask.valid())
        dispatchDesc.setDependencies(&terminalTask, 1u);
    const Core::GpuTaskId dispatchTask = graph.addTask<TaskGraphSkinningDispatchTask>(
        dispatchDesc,
        Move(dispatchPayload)
    );
    if(!dispatchTask.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning compute continuation"));
        return false;
    }
    terminalTask = dispatchTask;

    Core::GpuTaskGraphAnalysis analysis(m_arena);
    Core::GpuTaskGraphQueueAssignments assignments(m_arena);
    Core::GpuCompiledGraph compiledGraph(m_arena);
    Core::GpuRecordedGraph recordedGraph(m_arena);
    Core::GpuGraphSubmissionTransaction transaction(m_arena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to compile graph-owned skinning work"));
        return false;
    }

    if(compiledGraph.packetCount() != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not merge into one primary Graphics packet"));
        return false;
    }

    const Core::GpuSubmissionPacketId terminalPacket = compiledGraph.packetForTask(terminalTask);
    if(!terminalPacket.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work has no terminal packet"));
        return false;
    }
    const Core::GpuSubmissionPacket& packet = compiledGraph.packet(terminalPacket);
    const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(packet.queue != graphicsQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not retain the primary Graphics queue"));
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
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record graph-owned skinning work"));
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
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned skinning frame state"));
        return false;
    }

    const Core::GpuTaskGraphPacketTimingTicket timingTickets[] = {
        Core::GpuTaskGraphPacketTimingTicket{
            .packet = terminalPacket,
            .timingTicket = &timingTicket,
        },
    };
    const Core::GpuTaskGraphSubmitter submitter(device);
    if(!submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    )){
        transaction.discardUnaccepted(graph, compiledGraph);
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission was rejected"));
        return false;
    }

    const Core::QueueSubmissionToken skinningToken = transaction.packetToken(terminalPacket);
    if(!skinningToken.valid() || !skinningToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission lost its Graphics queue identity"));
        return false;
    }
    // Filter the accepted state before retaining it across frames. The graph task already owns every live
    // deformation/bounds/repack transition; filtering merely drops retired runtime-buffer generations.
    if(!replaceAcceptedSkinningStateHandoff(graphStateHandoff)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain accepted graph-owned skinning state"));
        return false;
    }
    return true;
}

void MeshSkinningSystem::render(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    if(!submitFrameSkinningGraph())
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: skipped skinning because graph-owned work was not accepted"));
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
    resetAcceptedSkinningStateHandoff();
    m_graphOwnedRestCopyPlans.clear();
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

