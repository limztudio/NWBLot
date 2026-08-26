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

[[nodiscard]] static Core::GpuTaskSchedulingHint JointPaletteUploadScheduling(){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.frontierScoredMergeDomain = Name("mesh_skinning.serial");
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
    scheduling.frontierScoredMergeDomain = Name("mesh_skinning.serial");
    return scheduling;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deformation produces the three skinned streams as graph-declared UAV writes. The following graph task consumes
// position/normal as ShaderResource inputs for bounds/repack, so the compiler owns their UAV-to-SRV handoff.
struct MeshSkinningSystem::TaskGraphSkinningDeformationTask{
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
            if(!payload.system->recordGraphOwnedSkinningDeformation(plan, commandList, context))
                return false;
        }
        return true;
    }
};


// Bounds and normal repack consume the deformation stage (or graph-owned rest copies) and publish their respective
// UAV outputs. Their accepted callback is the sole dirty-state commit point for the complete packet.
struct MeshSkinningSystem::TaskGraphSkinningPostDispatchTask{
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
            if(!payload.system->recordGraphOwnedSkinningPostDispatch(plan, commandList, context))
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


// Every generated output ends the packet in a descriptor-visible ShaderResource state. This getter-only task owns
// tail-only finalization (including deformation tangent) without replaying a native state bridge.
struct MeshSkinningSystem::TaskGraphSkinningFinalizerTask{
    struct Payload{
        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plans(arena)
        {}

        Vector<GraphOwnedSkinningDispatchPlan, Core::Alloc::GlobalArena> plans;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(payload.plans.empty())
            return false;

        for(const GraphOwnedSkinningDispatchPlan& plan : payload.plans){
            if(plan.hasActiveSkin || plan.copiedRestStreams){
                Core::Buffer* const skinnedPosition = context.taskGraph.bufferForResource(plan.skinnedPositionResource);
                Core::Buffer* const skinnedNormal = context.taskGraph.bufferForResource(plan.skinnedNormalResource);
                Core::Buffer* const skinnedTangent = context.taskGraph.bufferForResource(plan.skinnedTangentResource);
                if(
                    !skinnedPosition
                    || !skinnedNormal
                    || !skinnedTangent
                    || commandList.getBufferState(skinnedPosition) != Core::ResourceStates::ShaderResource
                    || commandList.getBufferState(skinnedNormal) != Core::ResourceStates::ShaderResource
                    || commandList.getBufferState(skinnedTangent) != Core::ResourceStates::ShaderResource
                )
                    return false;
            }
            if(plan.updatesMeshletBounds){
                Core::Buffer* const meshletBounds = context.taskGraph.bufferForResource(plan.meshletBoundsResource);
                if(!meshletBounds || commandList.getBufferState(meshletBounds) != Core::ResourceStates::ShaderResource)
                    return false;
            }
            if(plan.repacksNormals){
                Core::Buffer* const attributes = context.taskGraph.bufferForResource(plan.attributeResource);
                if(!attributes || commandList.getBufferState(attributes) != Core::ResourceStates::ShaderResource)
                    return false;
            }
        }
        return true;
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

void MeshSkinningSystem::collectLiveSkinningStateBuffers(
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers
)const{
    outBuffers.clear();
    const auto retainBuffer = [&](const Core::BufferHandle& buffer){
        if(!buffer)
            return;
        for(const Core::BufferHandle& existing : outBuffers){
            if(existing.get() == buffer.get())
                return;
        }
        outBuffers.push_back(buffer);
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
}

bool MeshSkinningSystem::replaceAcceptedSkinningState(const Core::CommandListResourceStateHandoff& state){
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> liveBuffers(m_arena);
    collectLiveSkinningStateBuffers(liveBuffers);
    return m_acceptedSkinningState.replaceBufferSubset(state, liveBuffers.data(), liveBuffers.size());
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
    , m_acceptedSkinningState(arena)
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
        m_acceptedSkinningState.valid()
        && !replaceAcceptedSkinningState(*m_acceptedSkinningState.source())
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
                const Core::BufferDesc& description = buffer->getCreationDescription();
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
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling())
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
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling())
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
                    .setScheduling(__hidden_system::JointPaletteUploadScheduling())
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

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> deformationResourceUses(scratchArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> postDispatchResourceUses(scratchArena);
    const auto addResourceUse = [](
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
        const Core::GpuGraphResourceId resource,
        const Core::ResourceStates::Mask state,
        const Core::GpuTaskResourceAccess::Enum access
    ){
        if(!resource.valid())
            return false;
        for(Core::GpuTaskResourceUse& existing : resourceUses){
            if(existing.resource != resource)
                continue;
            if(existing.requiredState != state)
                return false;
            if(existing.access != access)
                existing.access = Core::GpuTaskResourceAccess::ReadWrite;
            return true;
        }
        resourceUses.push_back(Core::GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = state,
            .access = access,
        });
        return true;
    };
    for(const GraphOwnedSkinningDispatchPlan& plan : dispatchPlans){
        if(!plan.hasActiveSkin)
            continue;
        if(
            !addResourceUse(
                deformationResourceUses,
                plan.bindlessResourceSlotsResource,
                Core::ResourceStates::ConstantBuffer,
                Core::GpuTaskResourceAccess::Read
            )
            || !addResourceUse(deformationResourceUses, plan.restPositionResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.restNormalResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.restTangentResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.skinnedPositionResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || !addResourceUse(deformationResourceUses, plan.skinnedNormalResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || !addResourceUse(deformationResourceUses, plan.skinnedTangentResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || !addResourceUse(deformationResourceUses, plan.meshletDescResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.meshletPositionRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.meshletAttributeRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.attributeSkinResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.skinResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(deformationResourceUses, plan.jointPaletteResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph resource uses for skinning deformation"));
            return false;
        }
    }

    for(const GraphOwnedSkinningDispatchPlan& plan : dispatchPlans){
        if(
            !addResourceUse(
                postDispatchResourceUses,
                plan.bindlessResourceSlotsResource,
                Core::ResourceStates::ConstantBuffer,
                Core::GpuTaskResourceAccess::Read
            )
            || !addResourceUse(postDispatchResourceUses, plan.skinnedPositionResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(postDispatchResourceUses, plan.meshletDescResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(postDispatchResourceUses, plan.meshletPositionRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(postDispatchResourceUses, plan.meshletLocalVertexRefResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(postDispatchResourceUses, plan.meshletPrimitiveIndexResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
            || !addResourceUse(postDispatchResourceUses, plan.meshletBoundsResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            || (plan.repacksNormals && (
                !addResourceUse(postDispatchResourceUses, plan.skinnedNormalResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !addResourceUse(postDispatchResourceUses, plan.meshletAttributeRefDeltaResource, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                || !addResourceUse(postDispatchResourceUses, plan.attributeResource, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write)
            ))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph resource uses for skinning bounds/repack"));
            return false;
        }
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> finalizerResourceUses(scratchArena);
    const auto addFinalizerUse = [&](const Core::GpuGraphResourceId resource){
        if(!resource.valid())
            return false;
        for(const Core::GpuTaskResourceUse& existing : finalizerResourceUses){
            if(existing.resource != resource)
                continue;
            return existing.requiredState == Core::ResourceStates::ShaderResource;
        }
        finalizerResourceUses.push_back(Core::GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        });
        return true;
    };
    for(const GraphOwnedSkinningDispatchPlan& plan : dispatchPlans){
        if(
            ((plan.hasActiveSkin || plan.copiedRestStreams) && (
                !addFinalizerUse(plan.skinnedPositionResource)
                || !addFinalizerUse(plan.skinnedNormalResource)
                || !addFinalizerUse(plan.skinnedTangentResource)
            ))
            || (plan.updatesMeshletBounds && !addFinalizerUse(plan.meshletBoundsResource))
            || (plan.repacksNormals && !addFinalizerUse(plan.attributeResource))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning final states"));
            return false;
        }
    }
    if(finalizerResourceUses.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning dispatch has no final state"));
        return false;
    }

    // Retain the accepted prior-frame state with the graph task that consumes it, rather than selecting a native
    // packet-record override after compilation.  The source stays serial by contract, while all current-frame
    // deformation/bounds dependencies remain compiler-owned.
    const Core::GpuTaskExternalStateSource previousFrameStateSources[] = {
        Core::GpuTaskExternalStateSource{
            .states = m_acceptedSkinningState.source(),
        },
    };
    const usize previousFrameStateSourceCount = m_acceptedSkinningState.valid()
        ? LengthOf(previousFrameStateSources)
        : 0u
    ;

    Core::GpuTimingSubmissionTicket timingTicket(m_graphics.gpuTiming());
    TaskGraphSkinningFinalizerTask::Payload finalizerPayload(m_arena);
    for(const GraphOwnedSkinningDispatchPlan& plan : dispatchPlans)
        finalizerPayload.plans.push_back(plan);
    Core::GpuTaskId postDispatchDependency = terminalTask;
    if(!deformationResourceUses.empty()){
        TaskGraphSkinningDeformationTask::Payload deformationPayload(m_arena);
        deformationPayload.system = this;
        deformationPayload.timingTicket = &timingTicket;
        for(const GraphOwnedSkinningDispatchPlan& plan : dispatchPlans){
            if(plan.hasActiveSkin)
                deformationPayload.plans.push_back(plan);
        }

        Core::GpuTaskDesc deformationDesc;
        deformationDesc
            .setIdentity(Name("mesh_skinning.frame_deformation"))
            .setMarkerLabel("Runtime Skinning Deformation")
            .setQueue(__hidden_system::SkinningDispatchQueueRequest())
            .setScheduling(__hidden_system::SkinningDispatchScheduling())
            .setResourceUses(deformationResourceUses.data(), deformationResourceUses.size())
        ;
        if(previousFrameStateSourceCount != 0u)
            deformationDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
        if(terminalTask.valid())
            deformationDesc.setDependencies(&terminalTask, 1u);
        const Core::GpuTaskId deformationTask = graph.addTask<TaskGraphSkinningDeformationTask>(
            deformationDesc,
            Move(deformationPayload)
        );
        if(!deformationTask.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning deformation"));
            return false;
        }
        postDispatchDependency = deformationTask;
    }

    TaskGraphSkinningPostDispatchTask::Payload postDispatchPayload(m_arena);
    postDispatchPayload.system = this;
    postDispatchPayload.timingTicket = &timingTicket;
    postDispatchPayload.plans = Move(dispatchPlans);
    Core::GpuTaskDesc postDispatchDesc;
    postDispatchDesc
        .setIdentity(Name("mesh_skinning.frame_bounds_repack"))
        .setMarkerLabel("Runtime Skinning Bounds and Repack")
        .setQueue(__hidden_system::SkinningDispatchQueueRequest())
        .setScheduling(__hidden_system::SkinningDispatchScheduling())
        .setResourceUses(postDispatchResourceUses.data(), postDispatchResourceUses.size())
    ;
    if(previousFrameStateSourceCount != 0u)
        postDispatchDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
    if(postDispatchDependency.valid())
        postDispatchDesc.setDependencies(&postDispatchDependency, 1u);
    const Core::GpuTaskId postDispatchTask = graph.addTask<TaskGraphSkinningPostDispatchTask>(
        postDispatchDesc,
        Move(postDispatchPayload)
    );
    if(!postDispatchTask.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning bounds/repack"));
        return false;
    }

    const Core::GpuTaskDesc finalizerDesc = Core::GpuTaskDesc{}
        .setIdentity(Name("mesh_skinning.frame_finalize_states"))
        .setMarkerLabel("Runtime Skinning Finalize States")
        .setQueue(__hidden_system::SkinningDispatchQueueRequest())
        .setScheduling(__hidden_system::SkinningDispatchScheduling())
        .setDependencies(&postDispatchTask, 1u)
        .setResourceUses(finalizerResourceUses.data(), finalizerResourceUses.size())
    ;
    const Core::GpuTaskId finalizerTask = graph.addTask<TaskGraphSkinningFinalizerTask>(
        finalizerDesc,
        Move(finalizerPayload)
    );
    if(!finalizerTask.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning final-state handoff"));
        return false;
    }
    terminalTask = finalizerTask;

    Core::GpuTaskGraphAnalysis analysis(m_arena);
    Core::GpuTaskGraphQueueAssignments assignments(m_arena);
    Core::GpuCompiledGraph compiledGraph(m_arena);
    Core::GpuRecordedGraph recordedGraph(m_arena);
    Core::GpuGraphSubmissionTransaction transaction(m_arena);
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to compile graph-owned skinning work"));
        return false;
    }

    if(compiledGraph.packetCount() != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not merge into one primary Graphics packet"));
        return false;
    }

    const Core::GpuPhysicalQueueInfo* const terminalQueue = compiledGraph.queueInfoForTask(terminalTask);
    const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(!terminalQueue || terminalQueue->id != graphicsQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not retain the primary Graphics queue"));
        return false;
    }

    transaction.reset(compiledGraph);
    const Core::GpuNativePacketRecorder recorder(device);
    if(!recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph
    )){
        if(!transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        ))
            m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record graph-owned skinning work"));
        return false;
    }

    const Core::CommandListResourceStateHandoff* const finalStates = recordedGraph.taskFinalStateSeed(
        compiledGraph,
        terminalTask
    );
    if(!finalStates){
        if(!transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        ))
            m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned skinning frame state"));
        return false;
    }

    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> liveBuffers(m_arena);
    collectLiveSkinningStateBuffers(liveBuffers);
    Core::GpuPersistentResourceStateCache::Candidate acceptedStateCandidate(m_arena);
    if(!m_acceptedSkinningState.buildMergedBufferSubset(
        acceptedStateCandidate,
        *finalStates,
        liveBuffers.data(),
        liveBuffers.size()
    )){
        if(!transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        ))
            m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prepare graph-owned skinning frame state"));
        return false;
    }

    struct SkinningAcceptanceContext{
        Core::GpuPersistentResourceStateCache* cache = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* candidate = nullptr;
        bool stateReady = false;
    } skinningAcceptance{
        .cache = &m_acceptedSkinningState,
        .candidate = &acceptedStateCandidate,
    };
    const auto acceptSkinningTask = [](
        void* const rawContext,
        const Core::QueueSubmissionToken& token
    ) -> bool {
        static_cast<void>(token);
        SkinningAcceptanceContext* const context = static_cast<SkinningAcceptanceContext*>(rawContext);
        if(!context || !context->cache || !context->candidate)
            return false;
        context->stateReady = context->cache->commit(*context->candidate);
        return context->stateReady;
    };
    const Core::GpuTaskGraphTaskAcceptedCallback acceptedCallback{
        .task = terminalTask,
        .context = &skinningAcceptance,
        .invoke = acceptSkinningTask,
    };

    const Core::GpuTaskGraphTaskTimingTicket timingTickets[] = {
        Core::GpuTaskGraphTaskTimingTicket{
            .task = terminalTask,
            .timingTicket = &timingTicket,
        },
    };
    const Core::GpuTaskGraphSubmitter submitter(device);
    const bool skinningSubmitted = submitter.submitPacketRangeInCompileOrderFromTasks(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena,
        nullptr,
        nullptr,
        nullptr,
        0u,
        &acceptedCallback,
        1u
    );
    const Core::QueueSubmissionToken skinningToken = transaction.taskToken(compiledGraph, terminalTask);
    if(!skinningToken.valid()){
        if(!transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        ))
            m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission was rejected"));
        return false;
    }
    if(!skinningSubmitted || !skinningAcceptance.stateReady){
        m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: accepted graph-owned skinning submission lost its retained state"));
        return false;
    }
    if(!skinningToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration)){
        m_graphics.requestDeviceRecreation();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission lost its Graphics queue identity"));
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
    m_acceptedSkinningState.reset();
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

