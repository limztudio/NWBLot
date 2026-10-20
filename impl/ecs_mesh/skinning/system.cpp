// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "arena_names.h"
#include "graph_resource_uses.h"
#include "live_state_buffers.h"
#include "local_bounds.h"
#include "resource_names.h"
#include "runtime_mesh_liveness.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <impl/ecs_skeleton/runtime_helpers.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ResolveSkeletonComponents(Core::ECS::World& world, const Core::ECS::EntityID fallbackEntity, const Core::ECS::EntityID skeletonEntity, const SkeletonJointPaletteComponent*& outJointPalette, const SkeletonPoseComponent*& outSkeletonPose)noexcept{
    const Core::ECS::EntityID resolvedEntity = skeletonEntity.valid() ? skeletonEntity : fallbackEntity;
    outJointPalette = world.tryGetComponent<SkeletonJointPaletteComponent>(resolvedEntity);
    outSkeletonPose = world.tryGetComponent<SkeletonPoseComponent>(resolvedEntity);
}

static constexpr bool s_RuntimeSkinningMeshletFrustumCullingEnabled = true;
static constexpr bool s_RuntimeSkinningMeshletConeCullingEnabled = false; // Deformations make cones unsafe.
static constexpr Name s_SerialMergeDomain("mesh_skinning.serial");
static constexpr AStringView s_SkinnedPositionsLabel = "Runtime Skinned Positions";
static constexpr AStringView s_SkinnedNormalsLabel = "Runtime Skinned Normals";
static constexpr AStringView s_MeshletDescriptorsLabel = "Runtime Meshlet Descriptors";
static constexpr AStringView s_MeshletLocalVertexRefsLabel = "Runtime Meshlet Local Vertex Refs";
static constexpr AStringView s_MeshletPrimitiveIndicesLabel = "Runtime Meshlet Primitive Indices";
static constexpr AStringView s_MeshletAttributeDeltasLabel = "Runtime Meshlet Attribute Deltas";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::GpuTaskSchedulingHint JointPaletteUploadScheduling()noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    // The external renderer consumes one accepted skinning state on this serial timeline.
    scheduling.mergeWithPrevious = true;
    scheduling.frontierScoredMergeDomain = s_SerialMergeDomain;
    return scheduling;
}

[[nodiscard]] static Core::GpuTaskSchedulingHint SkinningDispatchScheduling()noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    // The external renderer consumes one accepted skinning state on this serial timeline.
    scheduling.mergeWithPrevious = true;
    scheduling.frontierScoredMergeDomain = s_SerialMergeDomain;
    return scheduling;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deformation and post-dispatch share the same plan loop; only the per-plan recorder differs.
using SkinningPlanRecorder = bool (MeshSkinningSystem::*)(const MeshSkinningGraphDispatchPlan&, Core::CommandList&, const Core::GpuTaskRecordContext&);

[[nodiscard]] inline bool RecordSkinningPlans(
    MeshSkinningSystem& system,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& plans,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context,
    const SkinningPlanRecorder recorder
){
    if(plans.empty())
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
    for(const MeshSkinningGraphDispatchPlan& plan : plans){
        if(!(system.*recorder)(plan, commandList, context))
            return false;
    }
    return true;
}


// Deformation produces UAV streams; compiler owns the UAV-to-SRV handoff.
struct MeshSkinningSystem::TaskGraphSkinningDeformationTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The skinning system and the timing ticket are required: deformation always records against this frame's ticket.
    struct Payload{
        MeshSkinningSystem& system;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> plans;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, MeshSkinningSystem& systemIn, Core::GpuTimingSubmissionTicket& timingTicketIn)noexcept
            : system(systemIn)
            , timingTicket(timingTicketIn)
            , plans(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSkinningPlans(payload.system, payload.timingTicket, payload.plans, commandList, context, &MeshSkinningSystem::recordGraphOwnedSkinningDeformation);
    }
};


// Bounds/repack consume deformation and publish UAV outputs for the local-bounds reduction.
struct MeshSkinningSystem::TaskGraphSkinningPostDispatchTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The skinning system and the timing ticket are required: bounds/repack always records against this frame's ticket.
    struct Payload{
        MeshSkinningSystem& system;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> plans;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, MeshSkinningSystem& systemIn, Core::GpuTimingSubmissionTicket& timingTicketIn)noexcept
            : system(systemIn)
            , timingTicket(timingTicketIn)
            , plans(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSkinningPlans(payload.system, payload.timingTicket, payload.plans, commandList, context, &MeshSkinningSystem::recordGraphOwnedSkinningPostDispatch);
    }
};


// Outputs end in ShaderResource state; only complete accepted bounds/repack/reduction publish a geometry generation.
struct MeshSkinningSystem::TaskGraphSkinningFinalizerTask{
    // Native-state publication feeds the renderer without a separate cross-graph readiness edge.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::None, true };

    struct Payload{
        MeshSkinningSystem& system;
        Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> plans;


    public:
        Payload(Core::Alloc::GlobalArena& arena, MeshSkinningSystem& system)noexcept
            : system(system)
            , plans(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context)noexcept{
        if(payload.plans.empty())
            return false;

        for(const MeshSkinningGraphDispatchPlan& plan : payload.plans){
            if(plan.hasActiveSkin || plan.copiedRestStreams){
                Core::Buffer* const skinnedPosition = context.declarations.bufferForResource(plan.skinnedPositionResource);
                Core::Buffer* const skinnedNormal = context.declarations.bufferForResource(plan.skinnedNormalResource);
                Core::Buffer* const skinnedTangent = context.declarations.bufferForResource(plan.skinnedTangentResource);
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
                Core::Buffer* const meshletBounds = context.declarations.bufferForResource(plan.meshletBoundsResource);
                Core::Buffer* const localBounds = context.declarations.bufferForResource(plan.localBoundsResource);
                if(!meshletBounds || !localBounds || commandList.getBufferState(meshletBounds) != Core::ResourceStates::ShaderResource || commandList.getBufferState(localBounds) != Core::ResourceStates::ShaderResource)
                    return false;
            }
            if(plan.repacksNormals){
                Core::Buffer* const attributes = context.declarations.bufferForResource(plan.attributeResource);
                if(!attributes || commandList.getBufferState(attributes) != Core::ResourceStates::ShaderResource)
                    return false;
            }
        }
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        if(!token.valid())
            return;
        for(const MeshSkinningGraphDispatchPlan& plan : payload.plans)
            payload.system.confirmGraphOwnedSkinningDispatch(plan);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSkinningSystem::MeshSkinningSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, IRuntimeMeshRegistry& runtimeMeshRegistry, ShaderPathResolveCallback shaderPathResolver)
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
    , m_frameDispatchPlans(arena)
    , m_frameLiveBuffers(arena)
    , m_frameTaskGraph(arena)
    , m_frameTaskGraphAnalysis(arena)
    , m_frameTaskGraphQueueAssignments(arena)
    , m_frameCompiledGraph(arena)
    , m_frameRecordedGraph(arena)
    , m_frameSubmissionTransaction(arena)
    , m_acceptedSkinningState(arena)
{
    writeAccess<SkinnedMeshBindingComponent>();
    readAccess<SkeletonJointPaletteComponent>();
    readAccess<SkeletonPoseComponent>();

    m_runtimeMeshRegistry.registerRuntimeMeshProvider(*this);
}

MeshSkinningSystem::~MeshSkinningSystem()noexcept{
    // Retire payloads while their transaction, plan, and timing owners still exist; destruction does not initialize another frame.
    {
        const Core::GpuCompiledGraph::ReadView planAccess(m_frameCompiledGraph);
        resolveFrameTaskGraphAttempt(planAccess);
    }
    m_frameTaskGraph.reset();
    m_runtimeMeshRegistry.unregisterRuntimeMeshProvider(*this);
}

void MeshSkinningSystem::prepare(Core::ECS::World& world){
    static_cast<void>(world);
    m_runtimeSetupReady = false;
    if(!m_skinningComputePipeline || !m_boundsComputePipeline || !m_localBoundsComputePipeline || !m_repackComputePipeline)
        return;
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_PrepareRuntimeArena);
    m_runtimeMeshCache.prepareResources(m_world, scratchArena);
    pruneRuntimeResources();
    bool ready = true;
    m_world.view<SkinnedMeshBindingComponent>().each([&](Core::ECS::EntityID, SkinnedMeshBindingComponent& binding){
            if(!ready)
                return;
            if(!binding.runtimeMesh.valid())
                return;

            MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance)
                return;
            NWB_ASSERT(instance->valid());

            ready = prepareRuntimeMeshResources(*instance, scratchArena);
        }
    );

    m_runtimeSetupReady = ready;
}

Expected<void, Core::CpuTaskAdmissionError::Enum> MeshSkinningSystem::update(Core::ECS::World& world, const f32 delta)noexcept{
    static_cast<void>(world);
    static_cast<void>(delta);
    return {};
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
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_LocalBounds.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_RepackNormals.identity, device, s_PerRuntimeMeshTimingQueries)
    ;
    if(!timingReady)
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: GPU timing scope preparation failed; timing samples may be skipped"));

    // Pipeline creation lives in resource validation, never in the per-frame prepare path below; prepare only consumes the handles created here.
    if(!ensureSkinningPipeline() || !ensureBoundsPipeline() || !ensureLocalBoundsPipeline() || !ensureRepackPipeline()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skinning pipelines during resource validation"));
        return false;
    }
    return true;
}

bool MeshSkinningSystem::prepareResources(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_PrepareRuntimeArena);
    // Frames may lack native submissions; filter retired buffers so handles pin nothing.
    if(m_acceptedSkinningState.valid() && !replaceAcceptedSkinningState(*m_acceptedSkinningState.source(), scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prune the accepted skinning state handoff"));
        return false;
    }

    return m_runtimeSetupReady;
}

void MeshSkinningSystem::render(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    if(!submitFrameSkinningGraph())
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: skipped skinning because graph-owned work was not accepted"));
}

void MeshSkinningSystem::invalidateResources(){
    m_runtimeSetupReady = false;
    resetFrameTaskGraph();
    NWB_FATAL_ASSERT_MSG(m_frameRecordedGraph.releaseCommandListCache(), "skinning invalidation requires joined graph recording before device-bound cache release");
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
    m_localBoundsComputeShader.reset();
    m_localBoundsComputePipeline.reset();
    m_repackBindingLayout.reset();
    m_repackComputeShader.reset();
    m_repackComputePipeline.reset();
}

Expected<RuntimeMeshDesc> MeshSkinningSystem::resolveRuntimeMesh(const Core::ECS::EntityID entity){
    RuntimeMeshHandle runtimeMesh;
    if(const SkinnedMeshBindingComponent* binding = m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
        runtimeMesh = binding->runtimeMesh;
    const MeshSkinningRuntimeInstance* const instance = runtimeMesh.valid() ? m_runtimeMeshCache.findInstance(runtimeMesh) : nullptr;
    return BuildSkinnedRuntimeMeshDesc(entity, runtimeMesh, instance, __hidden_system::s_RuntimeSkinningMeshletFrustumCullingEnabled, __hidden_system::s_RuntimeSkinningMeshletConeCullingEnabled);
}

bool MeshSkinningSystem::hasRuntimeMeshBinding(const Core::ECS::EntityID entity)const noexcept{
    return m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity) != nullptr;
}

void MeshSkinningSystem::markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests){
    MarkLiveSkinnedRuntimeMeshes(m_world, requests, [this](const Core::ECS::EntityID entity, const SkinnedMeshBindingComponent& binding){
        const MeshSkinningRuntimeInstance* const instance = binding.runtimeMesh.valid() ? m_runtimeMeshCache.findInstance(binding.runtimeMesh) : nullptr;
        return ResolveSkinnedRuntimeMeshIdentity(entity, binding.runtimeMesh, instance);
    }
    );
}

bool MeshSkinningSystem::submitFrameSkinningGraph(){
    auto& device = m_graphics.getDevice();
    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: cannot declare graph-owned skinning without a physical queue topology"));
        return false;
    }

    // Compiler artifacts are reused member storage reset per submission; render() never constructs graph objects.
    resetFrameTaskGraph();
    Core::GpuTaskGraph& graph = m_frameTaskGraph;
    Core::GpuTaskId terminalTask;
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_FrameUploadArena);
    // Dispatch plans are reused member storage; only capacity growth may allocate, never per-frame creation.
    Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& dispatchPlans = m_frameDispatchPlans;
    dispatchPlans.clear();
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    dispatchPlans.reserve(skinningBindings.candidateCount());
    bool declarationFailed = false;

    const auto importComputePipeline = [&](const Core::ComputePipelineHandle& pipeline, const Name identity, const AStringView label){
        return graph.importComputePipeline(pipeline, Core::GpuGraphPipelineDesc{}.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphPipelineType::Compute));
    };

    skinningBindings.each([&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
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
        RuntimeSkinPayloadScratch payload{ scratchArena };
        // Preserve the direct path's per-mesh retry semantics: an invalid pose never prevents another ready mesh from declaring its immutable packet plan.
        if(!MeshSkinningPayload::BuildRuntimeSkinPayload(*instance, jointPalette, skeletonPose, payload)){
            instance->deformationState.invalidateCurrent();
            return;
        }

        const bool hasActiveSkin = payload.hasActiveSkin();
        Span<const SkeletonJointMatrix> jointPaletteUpload;
        if(hasActiveSkin){
            const auto upload = MeshSkinningPayload::ResolveGpuJointPalette(*instance, payload);
            if(!upload){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: current joint palette does not match Skin upload bounds for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            jointPaletteUpload = *upload;
        }
        const bool skinningInputDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::SkinningInputDirty) != 0u;
        const bool meshletBoundsDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u;
        if(instance->meshlets.empty())
            return;

        if(foundResources == m_runtimeResources.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no prepared graph resources"), instance->handle.value);
            declarationFailed = true;
            return;
        }
        const RuntimeResources& resources = foundResources.value();
        const MeshSkinningDeformationInputs deformationInputs{ .resourceSlotsBuffer = resources.bindlessResourceSlotsBuffer, .editRevision = instance->editRevision, .skinningMode = payload.resolvedSkinningMode, };
        instance->deformationState.refreshCurrent(deformationInputs, payload.jointMatrices.data(), payload.jointMatrices.size(), skinningInputDirty || meshletBoundsDirty);
        const bool updatesMeshletBounds = instance->deformationState.contentRevision() == 0u;
        if(!updatesMeshletBounds)
            return;
        const bool copiesRestStreams = !hasActiveSkin;
        if(
            !resources.bindlessResourceSlotsBuffer
            || !resources.bindlessHeapHandles.resourceSlots.valid()
            || resources.bindlessHeapHandles.resourceSlots.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
            || !m_boundsComputePipeline || !m_localBoundsComputePipeline
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

        MeshSkinningGraphDispatchPlan plan;
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
        plan.skinCount = static_cast<u32>(payload.skinInfluenceCount);
        plan.jointCount = static_cast<u32>(jointPaletteUpload.size());
        plan.skinningMode = payload.resolvedSkinningMode;
        plan.bindlessResourceSlots = resources.bindlessHeapHandles.resourceSlots.slot();
        plan.bindlessResourceSlotsBuffer = resources.bindlessResourceSlotsBuffer;
        plan.bindlessResourceSlotsDescriptor = resources.bindlessHeapHandles.resourceSlots;
        plan.bindlessResourceSlotsPayload = resources.bindlessResourceSlots;

        if(plan.repacksNormals && !m_repackComputePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' requires a missing normal-repack pipeline"), instance->handle.value);
            declarationFailed = true;
            return;
        }

        const auto importBuffer = [&](const Core::BufferHandle& buffer, const AStringView label) -> Expected<Core::GpuGraphResourceId>{
            if(!buffer)
                return MakeUnexpected(Failure{});
            const Core::BufferDesc& description = buffer->getCreationDescription();
            if(!description.debugName)
                return MakeUnexpected(Failure{});
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

        Expected<Core::GpuGraphResourceId> bindlessResourceSlotsResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> restPositionResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> restNormalResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> restTangentResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> skinnedPositionResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> skinnedNormalResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> skinnedTangentResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> meshletDescResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> meshletPositionRefDeltaResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> meshletLocalVertexRefResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> meshletPrimitiveIndexResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> meshletBoundsResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> meshletLocalBoundsResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> localBoundsResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> meshletAttributeRefDeltaResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> attributeSkinResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> skinResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> jointPaletteResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphResourceId> attributeResourceResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphPipelineId> boundsPipelineResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphPipelineId> localBoundsPipelineResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphPipelineId> skinningPipelineResult = MakeUnexpected(Failure{});
        Expected<Core::GpuGraphPipelineId> repackPipelineResult = MakeUnexpected(Failure{});

        bindlessResourceSlotsResourceResult = importBuffer(plan.bindlessResourceSlotsBuffer, "Skinning Bindless Slots");
        if(hasActiveSkin || copiesRestStreams){
            restPositionResourceResult = importBuffer(instance->restPositionBuffer, "Runtime Rest Positions");
            restNormalResourceResult = importBuffer(instance->restNormalBuffer, "Runtime Rest Normals");
            restTangentResourceResult = importBuffer(instance->restTangentBuffer, "Runtime Rest Tangents");
            skinnedPositionResourceResult = importBuffer(instance->skinnedPositionBuffer, __hidden_system::s_SkinnedPositionsLabel);
            skinnedNormalResourceResult = importBuffer(instance->skinnedNormalBuffer, __hidden_system::s_SkinnedNormalsLabel);
            skinnedTangentResourceResult = importBuffer(instance->skinnedTangentBuffer, "Runtime Skinned Tangents");
        }
        if(updatesMeshletBounds){
            if(!skinnedPositionResourceResult.has_value())
                skinnedPositionResourceResult = importBuffer(instance->skinnedPositionBuffer, __hidden_system::s_SkinnedPositionsLabel);
            meshletDescResourceResult = importBuffer(instance->meshletDescBuffer, __hidden_system::s_MeshletDescriptorsLabel);
            meshletPositionRefDeltaResourceResult = importBuffer(instance->meshletPositionRefDeltaBuffer, "Runtime Meshlet Position Deltas");
            meshletLocalVertexRefResourceResult = importBuffer(instance->meshletLocalVertexRefBuffer, __hidden_system::s_MeshletLocalVertexRefsLabel);
            meshletPrimitiveIndexResourceResult = importBuffer(instance->meshletPrimitiveIndexBuffer, __hidden_system::s_MeshletPrimitiveIndicesLabel);
            meshletBoundsResourceResult = importBuffer(instance->meshletBoundsBuffer, "Runtime Meshlet Bounds");
            meshletLocalBoundsResourceResult = importBuffer(instance->meshletLocalBoundsBuffer, "Runtime Meshlet Local Bounds");
            localBoundsResourceResult = importBuffer(instance->localBoundsBuffer, "Runtime Local Bounds");
        }
        if(hasActiveSkin){
            meshletAttributeRefDeltaResourceResult = importBuffer(instance->meshletAttributeRefDeltaBuffer, __hidden_system::s_MeshletAttributeDeltasLabel);
            attributeSkinResourceResult = importBuffer(instance->attributeSkinBuffer, "Runtime Attribute Skins");
            skinResourceResult = importBuffer(resources.skinBuffer, "Skinning Influences");
            jointPaletteResourceResult = importBuffer(resources.jointPaletteBuffer, "Skinning Joint Palette");
        }
        if(plan.repacksNormals){
            if(!skinnedNormalResourceResult.has_value())
                skinnedNormalResourceResult = importBuffer(instance->skinnedNormalBuffer, __hidden_system::s_SkinnedNormalsLabel);
            if(!meshletDescResourceResult.has_value())
                meshletDescResourceResult = importBuffer(instance->meshletDescBuffer, __hidden_system::s_MeshletDescriptorsLabel);
            if(!meshletPrimitiveIndexResourceResult.has_value())
                meshletPrimitiveIndexResourceResult = importBuffer(instance->meshletPrimitiveIndexBuffer, __hidden_system::s_MeshletPrimitiveIndicesLabel);
            if(!meshletAttributeRefDeltaResourceResult.has_value())
                meshletAttributeRefDeltaResourceResult = importBuffer(instance->meshletAttributeRefDeltaBuffer, __hidden_system::s_MeshletAttributeDeltasLabel);
            if(!meshletLocalVertexRefResourceResult.has_value())
                meshletLocalVertexRefResourceResult = importBuffer(instance->meshletLocalVertexRefBuffer, __hidden_system::s_MeshletLocalVertexRefsLabel);
            attributeResourceResult = importBuffer(instance->attributeBuffer, "Runtime Ray Trace Attributes");
        }

        boundsPipelineResult = importComputePipeline(m_boundsComputePipeline, Name("mesh_skinning.graph_bounds_pipeline"), "Skinning Bounds Pipeline");
        localBoundsPipelineResult = importComputePipeline(m_localBoundsComputePipeline, Name("mesh_skinning.graph_local_bounds_pipeline"), "Skinning Local Bounds Pipeline");
        if(hasActiveSkin){
            skinningPipelineResult = importComputePipeline(m_skinningComputePipeline, Name("mesh_skinning.graph_skinning_pipeline"), "Skinning Compute Pipeline");
        }
        if(plan.repacksNormals){
            repackPipelineResult = importComputePipeline(m_repackComputePipeline, Name("mesh_skinning.graph_repack_pipeline"), "Skinning Repack Pipeline");
        }

        const bool importsValid =
            bindlessResourceSlotsResourceResult.has_value()
            && skinnedPositionResourceResult.has_value()
            && meshletDescResourceResult.has_value()
            && meshletPositionRefDeltaResourceResult.has_value()
            && meshletLocalVertexRefResourceResult.has_value()
            && meshletPrimitiveIndexResourceResult.has_value()
            && meshletBoundsResourceResult.has_value()
            && boundsPipelineResult.has_value()
            && meshletLocalBoundsResourceResult.has_value()
            && localBoundsResourceResult.has_value()
            && localBoundsPipelineResult.has_value()
            && (!hasActiveSkin || (
                restPositionResourceResult.has_value()
                && restNormalResourceResult.has_value()
                && restTangentResourceResult.has_value()
                && skinnedNormalResourceResult.has_value()
                && skinnedTangentResourceResult.has_value()
                && meshletAttributeRefDeltaResourceResult.has_value()
                && attributeSkinResourceResult.has_value()
                && skinResourceResult.has_value()
                && jointPaletteResourceResult.has_value()
                && skinningPipelineResult.has_value()
            ))
            && (!copiesRestStreams || (restPositionResourceResult.has_value() && restNormalResourceResult.has_value() && restTangentResourceResult.has_value() && skinnedNormalResourceResult.has_value() && skinnedTangentResourceResult.has_value()))
            && (!plan.repacksNormals || (skinnedNormalResourceResult.has_value() && meshletAttributeRefDeltaResourceResult.has_value() && attributeResourceResult.has_value() && repackPipelineResult.has_value()))
        ;
        if(!importsValid){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no graph-importable dispatch resource"), instance->handle.value);
            declarationFailed = true;
            return;
        }

        if(bindlessResourceSlotsResourceResult)
            plan.bindlessResourceSlotsResource = *bindlessResourceSlotsResourceResult;
        if(restPositionResourceResult)
            plan.restPositionResource = *restPositionResourceResult;
        if(restNormalResourceResult)
            plan.restNormalResource = *restNormalResourceResult;
        if(restTangentResourceResult)
            plan.restTangentResource = *restTangentResourceResult;
        if(skinnedPositionResourceResult)
            plan.skinnedPositionResource = *skinnedPositionResourceResult;
        if(skinnedNormalResourceResult)
            plan.skinnedNormalResource = *skinnedNormalResourceResult;
        if(skinnedTangentResourceResult)
            plan.skinnedTangentResource = *skinnedTangentResourceResult;
        if(meshletDescResourceResult)
            plan.meshletDescResource = *meshletDescResourceResult;
        if(meshletPositionRefDeltaResourceResult)
            plan.meshletPositionRefDeltaResource = *meshletPositionRefDeltaResourceResult;
        if(meshletLocalVertexRefResourceResult)
            plan.meshletLocalVertexRefResource = *meshletLocalVertexRefResourceResult;
        if(meshletPrimitiveIndexResourceResult)
            plan.meshletPrimitiveIndexResource = *meshletPrimitiveIndexResourceResult;
        if(meshletBoundsResourceResult)
            plan.meshletBoundsResource = *meshletBoundsResourceResult;
        if(meshletLocalBoundsResourceResult)
            plan.meshletLocalBoundsResource = *meshletLocalBoundsResourceResult;
        if(localBoundsResourceResult)
            plan.localBoundsResource = *localBoundsResourceResult;
        if(meshletAttributeRefDeltaResourceResult)
            plan.meshletAttributeRefDeltaResource = *meshletAttributeRefDeltaResourceResult;
        if(attributeSkinResourceResult)
            plan.attributeSkinResource = *attributeSkinResourceResult;
        if(skinResourceResult)
            plan.skinResource = *skinResourceResult;
        if(jointPaletteResourceResult)
            plan.jointPaletteResource = *jointPaletteResourceResult;
        if(attributeResourceResult)
            plan.attributeResource = *attributeResourceResult;
        if(boundsPipelineResult)
            plan.boundsPipeline = *boundsPipelineResult;
        if(localBoundsPipelineResult)
            plan.localBoundsPipeline = *localBoundsPipelineResult;
        if(skinningPipelineResult)
            plan.skinningPipeline = *skinningPipelineResult;
        if(repackPipelineResult)
            plan.repackPipeline = *repackPipelineResult;

        if(!resources.bindlessResourceSlotsUploaded){
            const auto uploadIdentity = DeriveRuntimeResourceName(instance->sourceName, instance->handle.value, instance->editRevision, "mesh_skinning_bindless_slots_upload");
            const auto selectorSourceResult = graph.copyUploadData(&resources.bindlessResourceSlots, sizeof(resources.bindlessResourceSlots), alignof(MeshSkinningBindlessResourceSlots));
            if(!uploadIdentity || !selectorSourceResult){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned bindless slots for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            const Core::GpuUploadBlobId selectorSource = *selectorSourceResult;

            Core::GpuTaskDesc selectorUploadDesc;
            selectorUploadDesc.setIdentity(*uploadIdentity).setMarkerLabel("Skinning Bindless Slots Upload").setScheduling(__hidden_system::JointPaletteUploadScheduling());
            if(terminalTask.valid())
                selectorUploadDesc.setDependencies(&terminalTask, 1u);

            const auto selectorUploadTaskResult = graph.addUploadBufferTask(
                selectorUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = selectorSource,
                    .destination = plan.bindlessResourceSlotsResource,
                    // The following graph-owned compute task declares the descriptor-visible ConstantBuffer state.
                    .finalState = Core::ResourceStates::Common, }
            );
            if(!selectorUploadTaskResult){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned bindless slots for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            const Core::GpuTaskId selectorUploadTask = *selectorUploadTaskResult;
            plan.submissionCommit.bindlessResourceSlotsUploadRecorded = true;
            terminalTask = selectorUploadTask;
        }

        if(copiesRestStreams){
            const auto copyByteCounts = ResolveRestToSkinnedCopyByteCounts(*instance);
            if(!copyByteCounts || copyByteCounts->positionBytes == 0u || copyByteCounts->normalBytes == 0u || copyByteCounts->tangentBytes == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to resolve rest-to-skinned copy sizes for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            const Core::GpuCopyBufferTaskRegion copyRegions[] = {
                Core::GpuCopyBufferTaskRegion{ .source = plan.restPositionResource, .destination = plan.skinnedPositionResource, .dataSizeBytes = copyByteCounts->positionBytes, },
                Core::GpuCopyBufferTaskRegion{
                    .source = plan.restNormalResource,
                    .destination = plan.skinnedNormalResource,
                    .dataSizeBytes = copyByteCounts->normalBytes,
                },
                Core::GpuCopyBufferTaskRegion{ .source = plan.restTangentResource, .destination = plan.skinnedTangentResource, .dataSizeBytes = copyByteCounts->tangentBytes, },
            };
            Core::GpuTaskDesc copyDesc;
            const auto copyDescIdentity = DeriveRuntimeResourceName(instance->sourceName, instance->handle.value, instance->editRevision, "mesh_skinning_rest_to_skinned_copy");
            if(!copyDescIdentity){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive graph identity for rest-to-skinned copy"));
                declarationFailed = true;
                return;
            }
            copyDesc.setIdentity(*copyDescIdentity).setMarkerLabel("Skinning Rest-to-Skinned Copy").setScheduling(__hidden_system::JointPaletteUploadScheduling());

            if(terminalTask.valid())
                copyDesc.setDependencies(&terminalTask, 1u);

            const auto copyTaskResult = graph.addCopyBufferTask(copyDesc, Core::GpuCopyBufferTaskDesc{ .regions = copyRegions, .regionCount = LengthOf(copyRegions), });
            if(!copyTaskResult){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned rest-to-skinned copy for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            const Core::GpuTaskId copyTask = *copyTaskResult;
            terminalTask = copyTask;
        }

        if(hasActiveSkin){
            usize jointPaletteBytes = 0u;
            if(jointPaletteUpload.size() > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix) || (jointPaletteBytes = jointPaletteUpload.size() * sizeof(SkeletonJointMatrix)) == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette payload byte size overflows"));
                declarationFailed = true;
                return;
            }
            const auto jointPaletteSourceResult = graph.copyUploadData(jointPaletteUpload.data(), jointPaletteBytes, alignof(SkeletonJointMatrix));
            if(!jointPaletteSourceResult){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned joint palette data for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            const Core::GpuUploadBlobId jointPaletteSource = *jointPaletteSourceResult;

            Core::GpuTaskDesc jointPaletteUploadDesc;
            const auto jointPaletteUploadDescIdentity = DeriveRuntimeResourceName(instance->sourceName, instance->handle.value, instance->editRevision, "mesh_skinning_joint_palette_upload");
            if(!jointPaletteUploadDescIdentity){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive graph identity for joint palette upload"));
                declarationFailed = true;
                return;
            }
            jointPaletteUploadDesc.setIdentity(*jointPaletteUploadDescIdentity).setMarkerLabel("Skinning Joint Palette Upload").setScheduling(__hidden_system::JointPaletteUploadScheduling());

            if(terminalTask.valid())
                jointPaletteUploadDesc.setDependencies(&terminalTask, 1u);

            const auto jointPaletteUploadTaskResult = graph.addUploadBufferTask(
                jointPaletteUploadDesc,
                Core::GpuUploadBufferTaskDesc{ .source = jointPaletteSource, .destination = plan.jointPaletteResource, .finalState = Core::ResourceStates::ShaderResource, }
            );
            if(!jointPaletteUploadTaskResult){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned joint palette upload for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            const Core::GpuTaskId jointPaletteUploadTask = *jointPaletteUploadTaskResult;
            terminalTask = jointPaletteUploadTask;
        }

        const auto deformationCandidate = instance->deformationState.stage(deformationInputs, payload.jointMatrices.data(), payload.jointMatrices.size());
        if(!deformationCandidate){
            declarationFailed = true;
            return;
        }
        plan.deformationCandidate = *deformationCandidate;
        dispatchPlans.push_back(Move(plan));
    }
    );

    if(declarationFailed)
        return false;
    if(dispatchPlans.empty())
        return true;

    const auto resourceUses = BuildMeshSkinningGraphResourceUses(dispatchPlans.data(), dispatchPlans.size(), scratchArena);
    if(!resourceUses)
        return false;

    // Retain prior-frame state with its consuming task; source stays serial by contract.
    const Core::GpuTaskExternalStateSource previousFrameStateSources[] = { Core::GpuTaskExternalStateSource{ .states = m_acceptedSkinningState.source(), }, };
    const usize previousFrameStateSourceCount = m_acceptedSkinningState.valid() ? LengthOf(previousFrameStateSources) : 0u;

    Core::GpuTimingSubmissionTicket timingTicket(m_graphics.gpuTiming());
    // Each graph payload owns the dispatch data it needs until submission acceptance.
    TaskGraphSkinningFinalizerTask::Payload finalizerPayload(m_arena, *this);
    finalizerPayload.plans.clear();
    finalizerPayload.plans.reserve(dispatchPlans.size());
    MeshSkinningLocalBoundsTask::Payload localBoundsPayload(m_arena, m_graphics, timingTicket);
    localBoundsPayload.dispatches.reserve(dispatchPlans.size());
    for(const MeshSkinningGraphDispatchPlan& plan : dispatchPlans){
        finalizerPayload.plans.push_back(plan);
        if(plan.updatesMeshletBounds){
            localBoundsPayload.dispatches.push_back({
                .bindlessResourceSlotsResource = plan.bindlessResourceSlotsResource,
                .meshletLocalBoundsResource = plan.meshletLocalBoundsResource,
                .localBoundsResource = plan.localBoundsResource,
                .localBoundsPipeline = plan.localBoundsPipeline,
                .meshletCount = plan.meshletCount,
                .bindlessResourceSlots = plan.bindlessResourceSlots,
            }
            );
        }
    }
    Core::GpuTaskId postDispatchDependency = terminalTask;
    if(!resourceUses->deformation.empty()){
        TaskGraphSkinningDeformationTask::Payload deformationPayload(m_arena, *this, timingTicket);
        deformationPayload.plans.clear();
        deformationPayload.plans.reserve(dispatchPlans.size());
        for(const MeshSkinningGraphDispatchPlan& plan : dispatchPlans){
            if(plan.hasActiveSkin)
                deformationPayload.plans.push_back(plan);
        }

        Core::GpuTaskDesc deformationDesc;
        deformationDesc
            .setIdentity(Name("mesh_skinning.frame_deformation"))
            .setMarkerLabel("Runtime Skinning Deformation")
            .setScheduling(__hidden_system::SkinningDispatchScheduling())
            .setResourceUses(resourceUses->deformation.data(), resourceUses->deformation.size())
        ;
        if(previousFrameStateSourceCount != 0u)
            deformationDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
        if(terminalTask.valid())
            deformationDesc.setDependencies(&terminalTask, 1u);
        const auto deformationTaskResult = graph.addTask<TaskGraphSkinningDeformationTask>(deformationDesc, Move(deformationPayload));
        if(!deformationTaskResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning deformation"));
            return false;
        }
        const Core::GpuTaskId deformationTask = *deformationTaskResult;
        postDispatchDependency = deformationTask;
    }

    TaskGraphSkinningPostDispatchTask::Payload postDispatchPayload(m_arena, *this, timingTicket);
    postDispatchPayload.plans = Move(dispatchPlans);
    Core::GpuTaskDesc postDispatchDesc;
    postDispatchDesc
        .setIdentity(Name("mesh_skinning.frame_bounds_repack"))
        .setMarkerLabel("Runtime Skinning Bounds and Repack")
        .setScheduling(__hidden_system::SkinningDispatchScheduling())
        .setResourceUses(resourceUses->postDispatch.data(), resourceUses->postDispatch.size())
    ;
    if(previousFrameStateSourceCount != 0u)
        postDispatchDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
    if(postDispatchDependency.valid())
        postDispatchDesc.setDependencies(&postDispatchDependency, 1u);
    const auto postDispatchTaskResult = graph.addTask<TaskGraphSkinningPostDispatchTask>(postDispatchDesc, Move(postDispatchPayload));
    if(!postDispatchTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning bounds/repack"));
        return false;
    }
    const Core::GpuTaskId postDispatchTask = *postDispatchTaskResult;

    Core::GpuTaskDesc localBoundsDesc;
    localBoundsDesc
        .setIdentity(Name("mesh_skinning.frame_local_bounds"))
        .setMarkerLabel("Runtime Skinning Local Bounds")
        .setScheduling(__hidden_system::SkinningDispatchScheduling())
        .setDependencies(&postDispatchTask, 1u)
        .setResourceUses(resourceUses->localBounds.data(), resourceUses->localBounds.size())
    ;
    if(previousFrameStateSourceCount != 0u)
        localBoundsDesc.setExternalStateSources(previousFrameStateSources, previousFrameStateSourceCount);
    const auto localBoundsTaskResult = graph.addTask<MeshSkinningLocalBoundsTask>(localBoundsDesc, Move(localBoundsPayload));
    if(!localBoundsTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned local bounds reduction"));
        return false;
    }
    const Core::GpuTaskId localBoundsTask = *localBoundsTaskResult;

    const Core::GpuTaskDesc finalizerDesc = Core::GpuTaskDesc{}
        .setIdentity(Name("mesh_skinning.frame_finalize_states"))
        .setMarkerLabel("Runtime Skinning Finalize States")
        .setScheduling(__hidden_system::SkinningDispatchScheduling())
        .setDependencies(&localBoundsTask, 1u)
        .setResourceUses(resourceUses->finalizer.data(), resourceUses->finalizer.size())
    ;
    const auto finalizerTaskResult = graph.addTask<TaskGraphSkinningFinalizerTask>(finalizerDesc, Move(finalizerPayload));
    if(!finalizerTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned skinning final-state handoff"));
        return false;
    }
    const Core::GpuTaskId finalizerTask = *finalizerTaskResult;
    terminalTask = finalizerTask;

    Core::GpuTaskGraphAnalysis& analysis = m_frameTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments& assignments = m_frameTaskGraphQueueAssignments;
    Core::GpuCompiledGraph& compiledGraph = m_frameCompiledGraph;
    Core::GpuRecordedGraph& recordedGraph = m_frameRecordedGraph;
    Core::GpuGraphSubmissionTransaction& transaction = m_frameSubmissionTransaction;
    Core::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;
    const Core::GpuTaskScheduler& scheduler = m_graphics.gpuTasks();
    if(!scheduler.scheduleGraph(graph, analysis, assignments, compiledGraph, recordedGraph, transaction, scratchArena, compileOptions)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prepare graph-owned skinning work"));
        return false;
    }

    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
    if(!declarations.valid() || !compiledPlan.validFor(declarations)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned skinning compiler inputs"));
        return false;
    }

    if(compiledPlan.packetCount() != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not merge into one primary Graphics packet"));
        return false;
    }

    const Core::GpuPhysicalQueueInfo* const terminalQueue = compiledPlan.queueInfoForTask(terminalTask);
    const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(!terminalQueue || terminalQueue->id != graphicsQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning work did not retain the primary Graphics queue"));
        return false;
    }

    // Live-buffer list is reused member storage; only capacity growth may allocate, never per-frame creation.
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& liveBuffers = m_frameLiveBuffers;
    liveBuffers.clear();
    collectLiveSkinningStateBuffers(liveBuffers, scratchArena);
    Core::GpuPersistentResourceStateCache::Candidate acceptedStateCandidate(m_acceptedSkinningState);
    struct SkinningStateContext{
        Core::GpuPersistentResourceStateCache* cache = nullptr;
        Core::Alloc::ScratchArena& scratchArena;
        Core::GpuPersistentResourceStateCache::Candidate* candidate = nullptr;
        const Core::BufferHandle* buffers = nullptr;
        usize bufferCount = 0u;
        bool preparationInvoked = false;
        bool statePrepared = false;
        bool stateAccepted = false;
    } skinningState{ .cache = &m_acceptedSkinningState, .scratchArena = scratchArena, .candidate = &acceptedStateCandidate, .buffers = liveBuffers.data(), .bufferCount = liveBuffers.size(), };
    const auto prepareSkinningState = [](void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState) -> bool {
        SkinningStateContext* const context = static_cast<SkinningStateContext*>(rawContext);
        if(!context)
            return false;
        context->preparationInvoked = true;
        if(!context->cache || !context->candidate || !finalState || (context->bufferCount != 0u && !context->buffers))
            return false;
        context->statePrepared = context->cache->buildMergedBufferSubset(*context->candidate, *finalState, context->buffers, context->bufferCount, context->scratchArena);
        return context->statePrepared;
    };
    const Core::GpuTaskGraphTaskRecordedCallback recordedCallback{ .task = terminalTask, .context = &skinningState, .invoke = prepareSkinningState, };
    const auto acceptSkinningTask = [](void* const rawContext, const Core::QueueSubmissionToken& token)noexcept -> bool {
        static_cast<void>(token);
        SkinningStateContext* const context = static_cast<SkinningStateContext*>(rawContext);
        if(!context || !context->cache || !context->candidate || !context->statePrepared)
            return false;
        context->stateAccepted = context->cache->commit(*context->candidate);
        return context->stateAccepted;
    };
    const Core::GpuTaskGraphTaskAcceptedCallback acceptedCallback{ .task = terminalTask, .context = &skinningState, .invoke = acceptSkinningTask, };

    const Core::GpuTaskGraphTaskTimingTicket timingTickets[] = { Core::GpuTaskGraphTaskTimingTicket{ .task = terminalTask, .timingTicket = &timingTicket, }, };
    Core::GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.taskRecordedCallbacks = &recordedCallback;
    normalExecution.taskRecordedCallbackCount = 1u;
    normalExecution.taskTimingTickets = timingTickets;
    normalExecution.taskTimingTicketCount = LengthOf(timingTickets);
    normalExecution.taskAcceptedCallbacks = &acceptedCallback;
    normalExecution.taskAcceptedCallbackCount = 1u;
    const bool skinningGraphAccepted = scheduler.executeGraph(graph, compiledGraph, recordedGraph, normalExecution, transaction, nullptr, scratchArena);
    const Core::QueueSubmissionToken skinningToken = transaction.taskToken(compiledPlan, terminalTask);
    if(!skinningToken.valid()){
        if(!transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration()))
            m_graphics.requestDeviceRecreation();
        if(!skinningState.preparationInvoked)
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record graph-owned skinning work"));
        else if(!skinningState.statePrepared)
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to prepare graph-owned skinning frame state"));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned skinning submission was rejected"));
        return false;
    }
    if(!skinningGraphAccepted || !skinningState.stateAccepted){
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

        // Heap descriptors retain backing buffers; retire before erasing for quarantine.
        releaseRuntimeResourceBindlessHeapHandles(resources);
        it = m_runtimeResources.erase(it);
    }
}

void MeshSkinningSystem::resolveFrameTaskGraphAttempt(const Core::GpuCompiledGraph::ReadView& planAccess)noexcept{
    // This graph owns only skinning tasks and built-in copies/uploads; their discard hooks are absent or clear a token without callbacks or allocation.
    if(!planAccess.valid())
        return;
    const u64 recordingAttemptGeneration = m_frameRecordedGraph.recordingAttemptGeneration();
    if(recordingAttemptGeneration != 0u && !m_frameSubmissionTransaction.discardUnaccepted(m_frameTaskGraph, m_frameCompiledGraph, recordingAttemptGeneration))
        NWB_FATAL_ASSERT_MSG(false, "skinning task-graph reset must resolve every unaccepted packet");
}

void MeshSkinningSystem::resetFrameTaskGraph(){
    {
        const Core::GpuCompiledGraph::ReadView planAccess(m_frameCompiledGraph);
        resolveFrameTaskGraphAttempt(planAccess);
        if(planAccess.valid()){
            m_frameSubmissionTransaction.reset(m_frameCompiledGraph);
            m_frameRecordedGraph.reset(m_frameCompiledGraph);
        }
    }

    m_frameTaskGraph.reset();
    m_frameTaskGraphAnalysis.reset();
    m_frameTaskGraphQueueAssignments.reset();
    m_frameCompiledGraph.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

