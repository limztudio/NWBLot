// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <impl/ecs_scene/components.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Each timed packet owns one query-reservation ticket. Packet identity is the only registry key, so adding a normal
// packet to FrameExecutionPlan automatically creates, resolves, and discards its ticket without a matching variable,
// switch case, or failure-path cleanup list in RendererSystem.
class FrameExecutionPlanTimingTickets final{
public:
    // Work IDs are already the plan's single source of packet ownership. Keep a recording scope keyed by that same
    // ID so RendererSystem never needs a parallel list of per-work ticket pointers.
    class WorkRecordingScope final : NoCopy{
    public:
        WorkRecordingScope(
            FrameExecutionPlanTimingTickets& timingTickets,
            const FrameExecutionWork::Enum work
        )
            : m_recordingScope(timingTickets.requiredTicketForWork(work))
        {}


    private:
        Core::GpuTimingSubmissionTicket::RecordingScope m_recordingScope;
    };


public:
    explicit FrameExecutionPlanTimingTickets(
        const FrameExecutionPlan& plan,
        Core::GpuTimingRecorder& recorder
    )
        : m_plan(plan)
    {
        for(usize packetIndex = 0u; packetIndex < FrameExecutionPacket::kCount; ++packetIndex){
            const FrameExecutionPacket::Enum packet = static_cast<FrameExecutionPacket::Enum>(packetIndex);
            const FrameExecutionPacketPlan& packetPlan = m_plan.packet(packet);
            if(packetPlan.enabled && packetPlan.recordsTiming)
                m_tickets[packetIndex].emplace(recorder);
        }
    }


public:
    [[nodiscard]] Core::GpuTimingSubmissionTicket* ticketForPacket(
        const FrameExecutionPacket::Enum packet
    )noexcept{
        const FrameExecutionPacketPlan& packetPlan = m_plan.packet(packet);
        if(!packetPlan.enabled || !packetPlan.recordsTiming)
            return nullptr;

        Optional<Core::GpuTimingSubmissionTicket>& timingTicket = m_tickets[static_cast<usize>(packet)];
        NWB_ASSERT(timingTicket.has_value());
        return timingTicket ? &timingTicket.value() : nullptr;
    }
    [[nodiscard]] Core::GpuTimingSubmissionTicket* ticketForWork(
        const FrameExecutionWork::Enum work
    )noexcept{
        return m_plan.hasWork(work) ? ticketForPacket(m_plan.packetForWork(work)) : nullptr;
    }
    void discardAll()noexcept{
        for(Optional<Core::GpuTimingSubmissionTicket>& timingTicket : m_tickets){
            if(timingTicket)
                timingTicket->discard();
        }
    }


private:
    [[nodiscard]] Core::GpuTimingSubmissionTicket& requiredTicketForWork(
        const FrameExecutionWork::Enum work
    )noexcept{
        Core::GpuTimingSubmissionTicket* const timingTicket = ticketForWork(work);
        NWB_ASSERT(timingTicket);
        return *timingTicket;
    }
    const FrameExecutionPlan& m_plan;
    Optional<Core::GpuTimingSubmissionTicket> m_tickets[FrameExecutionPacket::kCount] = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_csgShapeRegistry(arena)
    , m_meshState(arena)
    , m_materialState(arena)
    , m_rayTracingState(arena)
    , m_shadowPrepareStateHandoff(arena)
    , m_meshViewSetupStateHandoff(arena)
    , m_sceneShadingSetupStateHandoff(arena)
    , m_deferredClearStateHandoff(arena)
    , m_frameSetupStateFanInHandoff(arena)
    , m_gbufferStateHandoff(arena)
    , m_postGbufferNormalizedStateHandoff(arena)
    , m_shadowComputeBaseStateHandoff(arena)
    , m_shadowComputeInputStateHandoff(arena)
    , m_shadowComputePersistentStateHandoff(arena)
    , m_shadowVisibilityStateHandoff(arena)
    , m_shadowVisibilityLightingStateHandoff(arena)
    , m_shadowVisibilityReturnStateHandoff(arena)
    , m_causticsComputeBaseStateHandoff(arena)
    , m_causticsComputeInputStateHandoff(arena)
    , m_causticsComputePersistentStateHandoff(arena)
    , m_causticsStateHandoff(arena)
    , m_causticIrradianceLightingStateHandoff(arena)
    , m_causticIrradianceReturnStateHandoff(arena)
    , m_surfelGiComputeBaseStateHandoff(arena)
    , m_surfelGiComputeInputStateHandoff(arena)
    , m_surfelGiComputePersistentStateHandoff(arena)
    , m_surfelGiStateHandoff(arena)
    , m_surfelIrradianceLightingStateHandoff(arena)
    , m_surfelIrradianceReturnStateHandoff(arena)
    , m_deferredLightingBaseStateHandoff(arena)
    , m_deferredLightingInputStateHandoff(arena)
    , m_deferredLightingStateHandoff(arena)
    , m_avboitLightingStateHandoff(arena)
    , m_avboitCompositeStateHandoff(arena)
    , m_opaqueColorCompositeStateHandoff(arena)
    , m_deferredCompositeBaseStateHandoff(arena)
    , m_deferredCompositeInputStateHandoff(arena)
    , m_deferredCompositeStateHandoff(arena)
    , m_compositeColorPresentStateHandoff(arena)
    , m_deferredPresentBaseStateHandoff(arena)
    , m_deferredPresentInputStateHandoff(arena)
    , m_deferredPresentStateHandoff(arena)
    , m_laggedLightingStashInputStateHandoff(arena)
    , m_laggedLightingStashStateHandoff(arena)
    , m_avboitPreStateHandoff(arena)
    , m_avboitDepthWarpInputStateHandoff(arena)
    , m_avboitDepthWarpStateHandoff(arena)
    , m_avboitExtinctionInputStateHandoff(arena)
    , m_avboitExtinctionStateHandoff(arena)
    , m_avboitIntegrationInputStateHandoff(arena)
    , m_avboitIntegrationStateHandoff(arena)
    , m_avboitAccumulationInputStateHandoff(arena)
    , m_avboitStateHandoff(arena)
    , m_shaderSystem(*this)
    , m_meshSystem(*this)
    , m_materialSystem(*this)
    , m_csgSystem(*this)
    , m_deferredSystem(*this)
    , m_avboitSystem(*this)
    , m_raytracingSystem(*this)
{
    if(!RegisterBuiltInCsgShapeTypes(m_csgShapeRegistry))
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register built-in CSG shape types"));

    readAccess<NWB::Impl::Scene::ActiveCameraComponent>();
    readAccess<NWB::Impl::Scene::TransformComponent>();
    readAccess<NWB::Impl::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    readAccess<MaterialInstanceComponent>();
    readAccess<StaticCsgMeshComponent>();
    readAccess<SkinnedCsgMeshComponent>();
    readAccess<CsgCutterComponent>();
}
RendererSystem::~RendererSystem(){}


void RendererSystem::reportLaggedLightingTransition(const LaggedLightingReport report, const u64 targetGeneration){
    if(m_laggedLightingReport == report && m_laggedLightingReportGeneration == targetGeneration)
        return;

    m_laggedLightingReport = report;
    m_laggedLightingReportGeneration = targetGeneration;
    switch(report){
    case LaggedLightingReport::Unreported:
        break;
    case LaggedLightingReport::NoDedicatedAsyncCompute:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting fallback accepted (no dedicated AsyncCompute lane, target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::BootstrapAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting bootstrap accepted (target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::ActiveHistoryAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting active history accepted (target generation {})"),
            targetGeneration
        );
        break;
    case LaggedLightingReport::CurrentFrameFallbackAccepted:
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: frame-lagged async lighting current-frame fallback accepted (target generation {})"),
            targetGeneration
        );
        break;
    }
}

void RendererSystem::resetLaggedLightingStashStateHandoffs()noexcept{
    m_laggedLightingStashInputStateHandoff.reset();
    m_laggedLightingStashStateHandoff.reset();
}

void RendererSystem::invalidateLaggedLightingHistorySubmission()noexcept{
    m_laggedLightingHistorySubmissionToken = Core::QueueSubmissionToken{};
}

void RendererSystem::resetLaggedLightingHistoryTracking()noexcept{
    invalidateLaggedLightingHistorySubmission();
    m_laggedLightingHistoryGeneration = 0u;
}

void RendererSystem::resetTargetGenerationStateHandoffs()noexcept{
    // Target replacement invalidates retained Compute scratch state and producer returns because the backing images
    // have changed. Frame-prefix handoffs are intentionally excluded: the preparation path owns those separately.
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowComputePersistentStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_shadowVisibilityReturnStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsComputePersistentStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_causticIrradianceReturnStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiComputePersistentStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_surfelIrradianceReturnStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
    resetLaggedLightingStashStateHandoffs();
    m_avboitPreStateHandoff.reset();
    m_avboitDepthWarpInputStateHandoff.reset();
    m_avboitDepthWarpStateHandoff.reset();
    m_avboitExtinctionInputStateHandoff.reset();
    m_avboitExtinctionStateHandoff.reset();
    m_avboitIntegrationInputStateHandoff.reset();
    m_avboitIntegrationStateHandoff.reset();
    m_avboitAccumulationInputStateHandoff.reset();
    m_avboitStateHandoff.reset();
}

void RendererSystem::resetInvalidatedResourceStateHandoffs()noexcept{
    // Full resource invalidation also abandons the preparation and frame-prefix handoffs, unlike a target-generation
    // replacement which can still rely on the owning preparation path to recreate those states.
    m_shadowPrepareStateHandoff.reset();
    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    resetTargetGenerationStateHandoffs();
}

void RendererSystem::resetFrameRecordingStateHandoffs()noexcept{
    // A fresh recording discards only the previous frame's transient handoffs. Accepted Compute-local scratch and
    // producer-return state remains available for the next fan-in.
    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
    resetLaggedLightingStashStateHandoffs();
    m_avboitPreStateHandoff.reset();
    m_avboitDepthWarpInputStateHandoff.reset();
    m_avboitDepthWarpStateHandoff.reset();
    m_avboitExtinctionInputStateHandoff.reset();
    m_avboitExtinctionStateHandoff.reset();
    m_avboitIntegrationInputStateHandoff.reset();
    m_avboitIntegrationStateHandoff.reset();
    m_avboitAccumulationInputStateHandoff.reset();
    m_avboitStateHandoff.reset();
}

void RendererSystem::resetAbandonedFrameStateHandoffs()noexcept{
    // No submission accepted this frame, so discard its recorded prefix and downstream state while retaining only the
    // cross-frame Compute handoffs that belonged to an earlier accepted producer. Caustic producer inputs deliberately
    // remain outside this set, matching the established recovery path.
    m_meshViewSetupStateHandoff.reset();
    m_sceneShadingSetupStateHandoff.reset();
    m_deferredClearStateHandoff.reset();
    m_frameSetupStateFanInHandoff.reset();
    m_gbufferStateHandoff.reset();
    m_postGbufferNormalizedStateHandoff.reset();
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
    resetLaggedLightingStashStateHandoffs();
    m_avboitPreStateHandoff.reset();
    m_avboitDepthWarpInputStateHandoff.reset();
    m_avboitDepthWarpStateHandoff.reset();
    m_avboitExtinctionInputStateHandoff.reset();
    m_avboitExtinctionStateHandoff.reset();
    m_avboitIntegrationInputStateHandoff.reset();
    m_avboitIntegrationStateHandoff.reset();
    m_avboitAccumulationInputStateHandoff.reset();
    m_avboitStateHandoff.reset();
}

void RendererSystem::resetRejectedAsyncRayEffectsStateHandoffs()noexcept{
    // The Graphics prefix is already accepted when this submission is attempted. Preserve its state and every prior
    // producer return while discarding the unaccepted effects branch and all downstream work that depends on it.
    m_shadowComputeBaseStateHandoff.reset();
    m_shadowComputeInputStateHandoff.reset();
    m_shadowVisibilityStateHandoff.reset();
    m_shadowVisibilityLightingStateHandoff.reset();
    m_causticsComputeBaseStateHandoff.reset();
    m_causticsComputeInputStateHandoff.reset();
    m_causticsStateHandoff.reset();
    m_causticIrradianceLightingStateHandoff.reset();
    m_surfelGiComputeBaseStateHandoff.reset();
    m_surfelGiComputeInputStateHandoff.reset();
    m_surfelGiStateHandoff.reset();
    m_surfelIrradianceLightingStateHandoff.reset();
    m_deferredLightingBaseStateHandoff.reset();
    m_deferredLightingInputStateHandoff.reset();
    m_deferredLightingStateHandoff.reset();
    m_avboitLightingStateHandoff.reset();
    m_avboitCompositeStateHandoff.reset();
    m_opaqueColorCompositeStateHandoff.reset();
    m_deferredCompositeBaseStateHandoff.reset();
    m_deferredCompositeInputStateHandoff.reset();
    m_deferredCompositeStateHandoff.reset();
    m_compositeColorPresentStateHandoff.reset();
    m_deferredPresentBaseStateHandoff.reset();
    m_deferredPresentInputStateHandoff.reset();
    m_deferredPresentStateHandoff.reset();
    m_avboitPreStateHandoff.reset();
    m_avboitDepthWarpInputStateHandoff.reset();
    m_avboitDepthWarpStateHandoff.reset();
    m_avboitExtinctionInputStateHandoff.reset();
    m_avboitExtinctionStateHandoff.reset();
    m_avboitIntegrationInputStateHandoff.reset();
    m_avboitIntegrationStateHandoff.reset();
    m_avboitAccumulationInputStateHandoff.reset();
    m_avboitStateHandoff.reset();
}


bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    m_raytracingSystem.logCapabilityOnce();
    if(width == 0 || height == 0)
        return true;

    if(!ensureFrameCommandLists())
        return false;

    if(!prepareGpuTimingScopes())
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU timing scope preparation failed; timing samples may be skipped"));

    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;
    bool targetsReady = deferredTargets.valid() && deferredTargets.width == width && deferredTargets.height == height;
    if(!targetsReady){
        // Target-generation replacement invalidates every retained Compute scratch state and the visibility ownership
        // return. The new image can be claimed by its first Compute use without a stale handoff.
        resetTargetGenerationStateHandoffs();
        resetLaggedLightingHistoryTracking();
        targetsReady = m_deferredSystem.createDeferredFrameTargets(width, height);
    }
    if(!targetsReady)
        return false;

    if(Core::Framebuffer* presentationFramebuffer = m_graphics.getCurrentFramebuffer()){
        if(!m_deferredSystem.createDeferredPresentPipeline(presentationFramebuffer))
            return false;
    }

    if(!m_avboitSystem.createAvboitPipelines())
        return false;

    if(!m_graphics.queryFeatureSupport(Core::Feature::Meshlets)){
        if(!m_materialSystem.createComputeEmulationResources())
            return false;
    }

    if(!m_meshSystem.createMeshViewBuffer())
        return false;

    if(!m_csgSystem.createCsgIntervalPeelResources(deferredTargets, true))
        return false;

    return true;
}

void RendererSystem::invalidateResources(){
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;
    m_preparedHasTransparentRenderers = false;
    m_preparedShadowVisibilityReady = false;
    resetInvalidatedResourceStateHandoffs();
    m_meshViewSetupCommandList.reset();
    m_sceneShadingSetupCommandList.reset();
    m_deferredClearCommandList.reset();
    m_gbufferCommandList.reset();
    m_postGbufferNormalizeCommandList.reset();
    m_shadowVisibilityCommandList.reset();
    m_asyncRecoveryCommandList.reset();
    m_asyncEffectsTimingBeginCommandList.reset();
    m_asyncEffectsTimingEndCommandList.reset();
    m_asyncCausticsCommandList.reset();
    m_causticsCommandList.reset();
    m_asyncSurfelGiCommandList.reset();
    m_surfelGiCommandList.reset();
    m_asyncDeferredLightingCommandList.reset();
    m_deferredLightingCommandList.reset();
    m_asyncDeferredCompositeCommandList.reset();
    m_asyncLaggedLightingStashCommandList.reset();
    m_avboitCommandList.reset();
    m_asyncAvboitDepthWarpCommandList.reset();
    m_avboitExtinctionCommandList.reset();
    m_asyncAvboitIntegrationCommandList.reset();
    m_avboitAccumulateCommandList.reset();
    m_deferredCompositeCommandList.reset();
    m_deferredPresentCommandList.reset();
    m_shadowPrepareCommandList.reset();
    resetLaggedLightingHistoryTracking();
    m_asyncRenderRecoveryFailed = false;
    // The descriptor-buffer TLAS descriptor owns a retained acceleration-structure handle until its in-flight-frame
    // quarantine matures. Retire it before RendererRayTracingState releases the current TLAS so resource invalidation
    // cannot strand a descriptor-buffer block (or its retained AS) until device shutdown.
    if(m_rayTracingState.m_tlasHeapHandle.valid()){
        auto& device = m_graphics.getDevice();
        device.getDescriptorHeap().free(m_rayTracingState.m_tlasHeapHandle);
    }
    // The persistent caustic-emission and trace material-context heap descriptors retain their backing buffers just
    // like the TLAS descriptor. Retire them while the device heap is still live, before RendererRayTracingState
    // releases those buffers below.
    m_raytracingSystem.releaseCausticEmissionTargetHeapHandle();
    m_raytracingSystem.releaseRayTraceMaterialContextHeapHandles();
    m_raytracingSystem.releaseSwBvhScratchHeapHandles();
    m_raytracingSystem.releaseSurfelGiHeapHandles();
    // Deferred target generations own ordinary image/sampler heap slots. Release those handles while both the target
    // resources and the device heap are still live; RendererDeferredState then drops the remaining resource handles.
    m_deferredSystem.resetDeferredFrameTargets();
    // Mesh geometry heap descriptors retain their backing buffers independently of the MeshResources cache. Retire
    // them before clearing that cache so descriptor slots are eventually recycled after the in-flight quarantine.
    m_meshSystem.releaseAllMeshGeometryHeapHandles();
    m_meshSystem.releaseMeshFrameHeapHandles();
    m_meshState.invalidateResources();
    // Material fixture descriptors retain their tiny checker image and sampler just like other heap residents.
    // Retire them while the heap is live, then leave cached CPU material info with its unpatched constant bytes so
    // the next device generation can resolve fresh slots.
    m_materialSystem.releaseMaterialResourceFixtures();
    m_materialState.invalidateResources();
    m_drawState.invalidateResources();
    // CSG's persistent clip descriptors retain their receiver/cutter buffers, so retire them before the CSG state
    // releases those buffers and its slot cbuffer.
    m_csgSystem.releaseCsgClipContextHeapHandles();
    m_csgState.invalidateResources();
    m_deferredState.invalidateResources();
    m_avboitState.invalidateResources();
    m_rayTracingState.invalidateResources();
}

void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

bool RendererSystem::ensureFrameCommandLists(){
    auto& device = m_graphics.getDevice();

    if(!m_meshViewSetupCommandList){
        m_meshViewSetupCommandList = device.createCommandList();
        if(!m_meshViewSetupCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh-view setup command list"));
            return false;
        }
    }

    if(!m_sceneShadingSetupCommandList){
        m_sceneShadingSetupCommandList = device.createCommandList();
        if(!m_sceneShadingSetupCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene-shading setup command list"));
            return false;
        }
    }

    if(!m_deferredClearCommandList){
        m_deferredClearCommandList = device.createCommandList();
        if(!m_deferredClearCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-clear command list"));
            return false;
        }
    }

    if(!m_gbufferCommandList){
        m_gbufferCommandList = device.createCommandList();
        if(!m_gbufferCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create G-buffer command list"));
            return false;
        }
    }

    if(!m_postGbufferNormalizeCommandList){
        m_postGbufferNormalizeCommandList = device.createCommandList();
        if(!m_postGbufferNormalizeCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create post-G-buffer normalization command list"));
            return false;
        }
    }

    if(!m_shadowVisibilityCommandList){
        Core::CommandListParameters shadowVisibilityCommandListParameters;
        shadowVisibilityCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
        m_shadowVisibilityCommandList = device.createCommandList(shadowVisibilityCommandListParameters);
        if(!m_shadowVisibilityCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow-visibility command list"));
            return false;
        }
    }

    if(!m_asyncRecoveryCommandList){
        m_asyncRecoveryCommandList = device.createCommandList();
        if(!m_asyncRecoveryCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async recovery command list"));
            return false;
        }
    }

    if(device.isRenderLaneDedicated(Core::RenderLane::AsyncCompute)){
        if(!m_asyncEffectsTimingBeginCommandList){
            m_asyncEffectsTimingBeginCommandList = device.createCommandList();
            if(!m_asyncEffectsTimingBeginCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async effects timing-begin command list"));
                return false;
            }
        }
        if(!m_asyncEffectsTimingEndCommandList){
            m_asyncEffectsTimingEndCommandList = device.createCommandList();
            if(!m_asyncEffectsTimingEndCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async effects timing-end command list"));
                return false;
            }
        }
        if(!m_asyncCausticsCommandList){
            Core::CommandListParameters asyncCausticsCommandListParameters;
            asyncCausticsCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncCausticsCommandList = device.createCommandList(asyncCausticsCommandListParameters);
            if(!m_asyncCausticsCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async caustics command list"));
                return false;
            }
        }
        if(!m_asyncSurfelGiCommandList){
            Core::CommandListParameters asyncSurfelGiCommandListParameters;
            asyncSurfelGiCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncSurfelGiCommandList = device.createCommandList(asyncSurfelGiCommandListParameters);
            if(!m_asyncSurfelGiCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async surfel-GI command list"));
                return false;
            }
        }
        if(!m_asyncDeferredLightingCommandList){
            Core::CommandListParameters asyncDeferredLightingCommandListParameters;
            asyncDeferredLightingCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncDeferredLightingCommandList = device.createCommandList(asyncDeferredLightingCommandListParameters);
            if(!m_asyncDeferredLightingCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async deferred-lighting command list"));
                return false;
            }
        }
        if(!m_asyncDeferredCompositeCommandList){
            Core::CommandListParameters asyncDeferredCompositeCommandListParameters;
            asyncDeferredCompositeCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncDeferredCompositeCommandList = device.createCommandList(asyncDeferredCompositeCommandListParameters);
            if(!m_asyncDeferredCompositeCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async deferred-composite command list"));
                return false;
            }
        }
        if(!m_asyncLaggedLightingStashCommandList){
            Core::CommandListParameters asyncLaggedLightingStashCommandListParameters;
            asyncLaggedLightingStashCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncLaggedLightingStashCommandList = device.createCommandList(asyncLaggedLightingStashCommandListParameters);
            if(!m_asyncLaggedLightingStashCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async lagged-lighting stash command list"));
                return false;
            }
        }
        if(!m_asyncAvboitDepthWarpCommandList){
            Core::CommandListParameters asyncAvboitDepthWarpCommandListParameters;
            asyncAvboitDepthWarpCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncAvboitDepthWarpCommandList = device.createCommandList(asyncAvboitDepthWarpCommandListParameters);
            if(!m_asyncAvboitDepthWarpCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async AVBOIT depth-warp command list"));
                return false;
            }
        }
        if(!m_avboitExtinctionCommandList){
            m_avboitExtinctionCommandList = device.createCommandList();
            if(!m_avboitExtinctionCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction command list"));
                return false;
            }
        }
        if(!m_asyncAvboitIntegrationCommandList){
            Core::CommandListParameters asyncAvboitIntegrationCommandListParameters;
            asyncAvboitIntegrationCommandListParameters.setRenderLane(Core::RenderLane::AsyncCompute);
            m_asyncAvboitIntegrationCommandList = device.createCommandList(asyncAvboitIntegrationCommandListParameters);
            if(!m_asyncAvboitIntegrationCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create async AVBOIT integration command list"));
                return false;
            }
        }
        if(!m_avboitAccumulateCommandList){
            m_avboitAccumulateCommandList = device.createCommandList();
            if(!m_avboitAccumulateCommandList){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation command list"));
                return false;
            }
        }
    }

    if(!m_causticsCommandList){
        m_causticsCommandList = device.createCommandList();
        if(!m_causticsCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustics command list"));
            return false;
        }
    }

    if(!m_surfelGiCommandList){
        m_surfelGiCommandList = device.createCommandList();
        if(!m_surfelGiCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel-GI command list"));
            return false;
        }
    }

    if(!m_deferredLightingCommandList){
        m_deferredLightingCommandList = device.createCommandList();
        if(!m_deferredLightingCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-lighting command list"));
            return false;
        }
    }

    if(!m_avboitCommandList){
        m_avboitCommandList = device.createCommandList();
        if(!m_avboitCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT command list"));
            return false;
        }
    }

    if(!m_deferredCompositeCommandList){
        m_deferredCompositeCommandList = device.createCommandList();
        if(!m_deferredCompositeCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-composite command list"));
            return false;
        }
    }

    if(!m_deferredPresentCommandList){
        m_deferredPresentCommandList = device.createCommandList();
        if(!m_deferredPresentCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred-present command list"));
            return false;
        }
    }

    if(!m_shadowPrepareCommandList){
        m_shadowPrepareCommandList = device.createCommandList();
        if(!m_shadowPrepareCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow preparation command list"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::prepareGpuTimingScopes(){
    auto& device = m_graphics.getDevice();

    struct ScopeReservation{
        const Core::GpuTimingScopeDefinition* scope;
        u32 queryCount;
    };
    const ScopeReservation scopeReservations[] = {
        { &RendererGpuTimingScope::s_MeshDispatch, 128u },
        { &RendererGpuTimingScope::s_Raster, 128u },
        { &RendererGpuTimingScope::s_Frame, 2u },
        { &RendererGpuTimingScope::s_AsyncPrefix, 2u },
        { &RendererGpuTimingScope::s_AsyncShadow, 2u },
        { &RendererGpuTimingScope::s_AsyncSurfelGi, 2u },
        { &RendererGpuTimingScope::s_AsyncEffects, 2u },
        { &RendererGpuTimingScope::s_AsyncFinal, 2u },
        { &RendererGpuTimingScope::s_DeferredClear, 2u },
        { &RendererGpuTimingScope::s_ShadowVisibility, 2u },
        { &RendererGpuTimingScope::s_ShadowOpaqueTrace, 2u },
        { &RendererGpuTimingScope::s_ShadowGeometryDownsample, 2u },
        { &RendererGpuTimingScope::s_ShadowOpaqueTemporal, 2u },
        { &RendererGpuTimingScope::s_ShadowOpaqueResolve, 2u },
        { &RendererGpuTimingScope::s_ShadowTransparentTrace, 2u },
        { &RendererGpuTimingScope::s_ShadowTransparentTemporal, 2u },
        { &RendererGpuTimingScope::s_ShadowTransparentResolve, 2u },
        { &RendererGpuTimingScope::s_SwBvhSort, 4u },
        { &RendererGpuTimingScope::s_CausticPhotons, 2u },
        { &RendererGpuTimingScope::s_CausticResolve, 2u },
        { &RendererGpuTimingScope::s_DeferredLighting, 2u },
        { &RendererGpuTimingScope::s_DeferredComposite, 2u },
        { &RendererGpuTimingScope::s_DeferredPresent, 2u },
        { &RendererGpuTimingScope::s_MaterialUpload, 2u },
        { &RendererGpuTimingScope::s_OpaqueRegular, 2u },
        { &RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, 2u },
        { &RendererGpuTimingScope::s_OpaqueCsg, 2u },
        { &RendererGpuTimingScope::s_CsgUpload, 2u },
        { &RendererGpuTimingScope::s_CsgSampleStateUpload, 2u },
        { &RendererGpuTimingScope::s_CsgIntervalClear, 4u },
        { &RendererGpuTimingScope::s_CsgIntervalPeel, 2u },
        { &RendererGpuTimingScope::s_CsgReceiverSpanBuild, 2u },
        { &RendererGpuTimingScope::s_CsgIntervalCombine, 2u },
        { &RendererGpuTimingScope::s_CsgCapFill, 2u },
        { &RendererGpuTimingScope::s_TransparentCsgIntervals, 2u },
        { &RendererGpuTimingScope::s_AvboitClear, 2u },
        { &RendererGpuTimingScope::s_AvboitOccupancy, 2u },
        { &RendererGpuTimingScope::s_AvboitDepthWarp, 2u },
        { &RendererGpuTimingScope::s_AvboitExtinction, 2u },
        { &RendererGpuTimingScope::s_AvboitIntegration, 2u },
        { &RendererGpuTimingScope::s_AvboitAccumulate, 2u },
        { &RendererGpuTimingScope::s_SurfelSpawn, 2u },
        { &RendererGpuTimingScope::s_SurfelAgeFree, 2u },
        { &RendererGpuTimingScope::s_SurfelHashBuild, 2u },
        { &RendererGpuTimingScope::s_SurfelTrace, 2u },
        { &RendererGpuTimingScope::s_SurfelResolve, 2u },
        { &RendererGpuTimingScope::s_SurfelUpsample, 2u },
    };

    for(const ScopeReservation& reservation : scopeReservations){
        if(!m_graphics.gpuTiming().prepareScopeQueries(reservation.scope->identity, device, reservation.queryCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare GPU timing scope '{}'"), StringConvert(reservation.scope->identity.c_str()));
            return false;
        }
    }

    if(
        device.supportsGraphicsAndComputeTimestamps()
        && !m_graphics.gpuTiming().prepareOverlapMetric(
            RendererGpuTimingScope::s_AsyncShadow.identity,
            RendererGpuTimingScope::s_AsyncEffects.identity,
            RendererGpuTimingScope::s_AsyncShadowEffectsOverlap.identity
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to prepare async shadow/effects overlap metric"));
    }

    return true;
}

bool RendererSystem::prepareResources(Core::Framebuffer* framebuffer){
    m_preparedShadowVisibilityReady = false;
    m_preparedHasTransparentRenderers = false;

    if(!framebuffer)
        return false;

    m_meshSystem.pruneRuntimeMeshResources();
    m_preparedHasTransparentRenderers = m_materialSystem.prepareVisibleMaterialSurfaceInfos();
    m_materialSystem.prepareVisibleMaterialInstanceMutableCache();
    m_preparedCsgFrameState = CsgFrameState{};
    m_preparedCsgFrameStateValid = false;

    if(!m_deferredState.m_targets.valid())
        return true;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);
    m_preparedCsgFrameState = HasCsgFrameCandidates(m_world)
        ? m_csgSystem.buildFrameState(scratchArena)
        : CsgFrameState{}
    ;
    m_preparedCsgFrameStateValid = true;
    const bool hasCsgFrameWork = !m_preparedCsgFrameState.empty();
    if(hasCsgFrameWork && !deferredTargets.csgIntervalTargetsValid())
        return false;

    if(!m_materialSystem.prepareMaterialPassResources(
        deferredTargets.framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        m_preparedCsgFrameState,
        nullptr
    ))
        return false;

    if(
        m_preparedHasTransparentRenderers
        && !m_avboitSystem.prepareAvboitPassResources(deferredTargets, m_preparedCsgFrameState)
    )
        return false;

    // Transparent preparation can grow shared instance and material buffers, which invalidates all mesh
    // heap registrations. Refresh opaque descriptors after the final possible grow so render only consumes prepared
    // resources.
    if(
        m_preparedHasTransparentRenderers
        && !m_materialSystem.prepareMaterialPassResources(
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            false,
            m_preparedCsgFrameState,
            nullptr
        )
    )
        return false;

    NWB_ASSERT(m_meshViewSetupCommandList);
    NWB_ASSERT(m_sceneShadingSetupCommandList);
    NWB_ASSERT(m_deferredClearCommandList);
    NWB_ASSERT(m_gbufferCommandList);
    NWB_ASSERT(m_postGbufferNormalizeCommandList);
    NWB_ASSERT(m_shadowVisibilityCommandList);
    NWB_ASSERT(m_causticsCommandList);
    NWB_ASSERT(!m_graphics.getDevice().isRenderLaneDedicated(Core::RenderLane::AsyncCompute) || m_asyncSurfelGiCommandList);
    NWB_ASSERT(m_surfelGiCommandList);
    NWB_ASSERT(!m_graphics.getDevice().isRenderLaneDedicated(Core::RenderLane::AsyncCompute) || m_asyncDeferredLightingCommandList);
    NWB_ASSERT(m_deferredLightingCommandList);
    NWB_ASSERT(!m_graphics.getDevice().isRenderLaneDedicated(Core::RenderLane::AsyncCompute) || m_asyncDeferredCompositeCommandList);
    NWB_ASSERT(m_avboitCommandList);
    NWB_ASSERT(m_deferredCompositeCommandList);
    NWB_ASSERT(m_deferredPresentCommandList);
    NWB_ASSERT(m_shadowPrepareCommandList);

    auto& device = m_graphics.getDevice();

    // The preparation list owns the first upload of this target generation's descriptor-slot payload. It also clears a
    // new surfel pool on the Graphics fallback; the dedicated Compute packet owns that clear when async is active.
    const bool deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded;
    m_raytracingSystem.discardSurfelResourceInitialization();
    Core::GpuTimingSubmissionTicket shadowPrepareTimingTicket(m_graphics.gpuTiming());
    const auto discardShadowPrepare = [&](){
        // Scene-cache keys are advanced while recording the preparation command list. If that list is abandoned or
        // rejected, a capacity grow may already have replaced the backing resource even though its build/upload never
        // reached the GPU. Do not restore the old key in that case: it could describe the retired resource rather than
        // the new, uninitialized one. Force one conservative rebuild after every failed preparation submission.
        m_rayTracingState.m_tlasStaticSceneHashValid = false;
        m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
        m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
        m_rayTracingState.m_swShadowMaterialContextHashValid = false;
        shadowPrepareTimingTicket.discard();
        m_shadowPrepareStateHandoff.reset();
        m_preparedShadowVisibilityReady = false;
        deferredTargets.bindless.slotsUploaded = deferredBindlessSlotsWereUploaded;
        m_raytracingSystem.discardSurfelResourceInitialization();
    };
    bool shadowPrepareRecorded = false;
    const Core::Graphics::JobHandle shadowPrepareJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        &shadowPrepareRecorded,
        &shadowPrepareTimingTicket
    ](){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(shadowPrepareTimingTicket);
        shadowPrepareRecorded = recordShadowPrepareCommandList(deferredTargets);
    });
    if(!shadowPrepareJob.valid()){
        discardShadowPrepare();
        return false;
    }

    m_graphics.waitJob(shadowPrepareJob);
    if(!shadowPrepareRecorded){
        discardShadowPrepare();
        return false;
    }

    Core::CommandList* shadowPrepareCommandLists[] = { m_shadowPrepareCommandList.get() };
    if(!shadowPrepareTimingTicket.submit(device, shadowPrepareCommandLists, 1u)){
        discardShadowPrepare();
        return false;
    }
    m_raytracingSystem.finalizeSurfelResourceInitialization();

    return true;
}

bool RendererSystem::recordShadowPrepareCommandList(DeferredFrameTargets& deferredTargets){
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);

    m_shadowPrepareCommandList->open();
    // The software-shadow trace selects its G-buffer heap descriptors through the target-generation slot cbuffer. It
    // runs before deferred lighting, so make the cbuffer resident on the ordered shadow-preparation command list first.
    const bool deferredBindlessResourcesUploaded = m_deferredSystem.uploadDeferredBindlessFrameResources(
        *m_shadowPrepareCommandList,
        deferredTargets
    );
    // Surfel GI resources are prepared inside prepareShadowVisibilityResources, after the ray-tracing scene
    // structures are resident, so the producer can run on the same frame without startup latency.
    const bool shadowResourcesPrepared = deferredBindlessResourcesUploaded
        && m_raytracingSystem.prepareShadowVisibilityResources(
            *m_shadowPrepareCommandList,
            deferredTargets,
            scratchArena,
            m_preparedShadowVisibilityReady
        )
    ;
    // Scene/material gathers can replace a capacity-grown buffer and therefore its heap slot. Upload the completed
    // five-slot indirection only after the full shadow/GI/caustic preparation path has settled on this frame's
    // resource generations, but before the command list that will dispatch their heap-selected passes is submitted.
    const bool traceMaterialContextUploaded = shadowResourcesPrepared
        && m_raytracingSystem.uploadRayTraceMaterialContextSlots(*m_shadowPrepareCommandList)
    ;
    // This submission precedes render on Graphics. Preserve its final resource state so the first render
    // command buffer never falls back to Unknown/UNDEFINED for preparation outputs.
    m_shadowPrepareCommandList->close(&m_shadowPrepareStateHandoff);
    return traceMaterialContextUploaded && m_shadowPrepareStateHandoff.valid();
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return;

    if(!m_deferredState.m_targets.valid())
        return;
    DeferredFrameTargets& deferredTargets = m_deferredState.m_targets;

    NWB_ASSERT(m_preparedCsgFrameStateValid);
    NWB_ASSERT(m_shadowPrepareStateHandoff.valid());
    // AVBOIT can record before deferred lighting, so its heap-selected resources must have been uploaded by the
    // ordered shadow-preparation packet rather than relying on deferred lighting's otherwise-idempotent upload.
    NWB_ASSERT(deferredTargets.bindless.slotsUploaded);

    const CsgFrameState csgFrameState = m_preparedCsgFrameState;
    const bool hasOpaqueCsgFrameWork = csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    const bool hasTransparentRenderers = m_preparedHasTransparentRenderers;
    NWB_ASSERT(csgFrameState.empty() || deferredTargets.csgIntervalTargetsValid());
    auto& device = m_graphics.getDevice();
    if(m_graphics.isDeviceRecreationRequested() || device.isDeviceLost()){
        if(device.isDeviceLost())
            m_graphics.requestDeviceRecreation();
        return;
    }
    if(m_asyncRenderRecoveryFailed){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: async render recovery failed; rendering is suspended until resources are recreated"));
        return;
    }
    const bool dedicatedAsyncCompute = device.isRenderLaneDedicated(Core::RenderLane::AsyncCompute);
    const bool laggedAsyncLightingRequested = m_frameLaggedAsyncLightingEnabled && dedicatedAsyncCompute;
    const bool laggedLightingHistoryResourcesReady = deferredTargets.laggedLightingHistory.valid();
    if(laggedAsyncLightingRequested && !laggedLightingHistoryResourcesReady){
        NWB_ASSERT(laggedLightingHistoryResourcesReady);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: lagged async lighting requires validated history targets"));
        return;
    }

    if(laggedAsyncLightingRequested){
        const u64 historyGeneration = deferredTargets.laggedLightingHistory.generation;
        if(m_laggedLightingHistoryGeneration != historyGeneration){
            // A resize/recreate can recycle descriptor slots and allocator addresses while replacing the images.
            // The validated target generation guarantees a fresh history starts with a current-frame seed.
            invalidateLaggedLightingHistorySubmission();
            resetLaggedLightingStashStateHandoffs();
            m_laggedLightingHistoryGeneration = historyGeneration;
        }
    }
    else{
        resetLaggedLightingHistoryTracking();
    }
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    const ECSRenderDetail::FrameExecutionPlanInput frameExecutionPlanInput{
        dedicatedAsyncCompute,
        m_frameLaggedAsyncLightingEnabled,
        laggedLightingHistoryResourcesReady,
        m_laggedLightingHistorySubmissionToken.valid(),
        hasTransparentRenderers,
        hardwareShadowSupported,
    };
    const ECSRenderDetail::FrameExecutionPlan frameExecutionPlan(frameExecutionPlanInput);
    const bool asyncShadowSchedule = frameExecutionPlan.workRunsOnLane(
        ECSRenderDetail::FrameExecutionWork::RayEffects,
        Core::RenderLane::AsyncCompute
    );
    const bool recordsAsyncEffectsTiming = frameExecutionPlan.hasWork(
        ECSRenderDetail::FrameExecutionWork::AsyncEffectsTiming
    );
    const bool laggedAsyncLightingSchedule = frameExecutionPlan.workWaitsForExternalToken(
        ECSRenderDetail::FrameExecutionWork::DeferredLighting,
        ECSRenderDetail::FrameExecutionExternalWait::LaggedLightingHistory
    );
    // A history snapshot is captured after every opt-in frame, including the current-frame bootstrap. Once the
    // previous accepted snapshot exists this packet shades independently on Graphics while the producer runs on Async.
    const bool captureLaggedLightingHistory = frameExecutionPlan.hasWork(
        ECSRenderDetail::FrameExecutionWork::LaggedLightingStash
    );
    // Hardware caustics stay in the independent Graphics-support packet: their trace-rays workload overlaps Async
    // shadow/surfel work instead of extending the Compute critical path. Software caustics remain Compute dispatches.
    const bool asyncCausticsSchedule = frameExecutionPlan.workRunsOnLane(
        ECSRenderDetail::FrameExecutionWork::Caustics,
        Core::RenderLane::AsyncCompute
    );
    const bool asyncSurfelGiSchedule = frameExecutionPlan.workRunsOnLane(
        ECSRenderDetail::FrameExecutionWork::SurfelGi,
        Core::RenderLane::AsyncCompute
    );
    // AVBOIT alternates Graphics raster passes with two pure dispatches. Only split it when transparent work exists;
    // a clear-only frame remains one Graphics packet and avoids needless cross-lane submissions.
    const bool asyncAvboitSchedule = frameExecutionPlan.workRunsOnLane(
        ECSRenderDetail::FrameExecutionWork::AvboitDepthWarp,
        Core::RenderLane::AsyncCompute
    );
    Core::CommandList* meshViewSetupCommandList = m_meshViewSetupCommandList.get();
    Core::CommandList* sceneShadingSetupCommandList = m_sceneShadingSetupCommandList.get();
    Core::CommandList* deferredClearCommandList = m_deferredClearCommandList.get();
    Core::CommandList* gbufferCommandList = m_gbufferCommandList.get();
    Core::CommandList* postGbufferNormalizeCommandList = m_postGbufferNormalizeCommandList.get();
    Core::CommandList* shadowVisibilityCommandList = m_shadowVisibilityCommandList.get();
    Core::CommandList* asyncRecoveryCommandList = m_asyncRecoveryCommandList.get();
    Core::CommandList* asyncEffectsTimingBeginCommandList = m_asyncEffectsTimingBeginCommandList.get();
    Core::CommandList* asyncEffectsTimingEndCommandList = m_asyncEffectsTimingEndCommandList.get();
    Core::CommandList* const causticsCommandList = frameExecutionPlan.commandListForWork(
        ECSRenderDetail::FrameExecutionWork::Caustics,
        ECSRenderDetail::FrameExecutionLaneCommandListPair{
            m_causticsCommandList.get(),
            m_asyncCausticsCommandList.get(),
        }
    );
    Core::CommandList* const surfelGiCommandList = frameExecutionPlan.commandListForWork(
        ECSRenderDetail::FrameExecutionWork::SurfelGi,
        ECSRenderDetail::FrameExecutionLaneCommandListPair{
            m_surfelGiCommandList.get(),
            m_asyncSurfelGiCommandList.get(),
        }
    );
    Core::CommandList* const deferredLightingCommandList = frameExecutionPlan.commandListForWork(
        ECSRenderDetail::FrameExecutionWork::DeferredLighting,
        ECSRenderDetail::FrameExecutionLaneCommandListPair{
            m_deferredLightingCommandList.get(),
            m_asyncDeferredLightingCommandList.get(),
        }
    );
    Core::CommandList* const deferredCompositeCommandList = frameExecutionPlan.commandListForWork(
        ECSRenderDetail::FrameExecutionWork::DeferredComposite,
        ECSRenderDetail::FrameExecutionLaneCommandListPair{
            m_deferredCompositeCommandList.get(),
            m_asyncDeferredCompositeCommandList.get(),
        }
    );
    Core::CommandList* asyncLaggedLightingStashCommandList = m_asyncLaggedLightingStashCommandList.get();
    Core::CommandList* avboitCommandList = m_avboitCommandList.get();
    Core::CommandList* asyncAvboitDepthWarpCommandList = m_asyncAvboitDepthWarpCommandList.get();
    Core::CommandList* avboitExtinctionCommandList = m_avboitExtinctionCommandList.get();
    Core::CommandList* asyncAvboitIntegrationCommandList = m_asyncAvboitIntegrationCommandList.get();
    Core::CommandList* avboitAccumulateCommandList = m_avboitAccumulateCommandList.get();
    Core::CommandList* deferredPresentCommandList = m_deferredPresentCommandList.get();
    NWB_ASSERT(meshViewSetupCommandList);
    NWB_ASSERT(sceneShadingSetupCommandList);
    NWB_ASSERT(deferredClearCommandList);
    NWB_ASSERT(gbufferCommandList);
    NWB_ASSERT(postGbufferNormalizeCommandList);
    NWB_ASSERT(shadowVisibilityCommandList);
    NWB_ASSERT(asyncRecoveryCommandList);
    NWB_ASSERT(!recordsAsyncEffectsTiming || asyncEffectsTimingBeginCommandList);
    NWB_ASSERT(!recordsAsyncEffectsTiming || asyncEffectsTimingEndCommandList);
    NWB_ASSERT(m_causticsCommandList);
    NWB_ASSERT(causticsCommandList);
    NWB_ASSERT(m_surfelGiCommandList);
    NWB_ASSERT(surfelGiCommandList);
    NWB_ASSERT(m_deferredLightingCommandList);
    NWB_ASSERT(deferredLightingCommandList);
    NWB_ASSERT(m_deferredCompositeCommandList);
    NWB_ASSERT(deferredCompositeCommandList);
    NWB_ASSERT(!captureLaggedLightingHistory || asyncLaggedLightingStashCommandList);
    NWB_ASSERT(avboitCommandList);
    NWB_ASSERT(!asyncAvboitSchedule || asyncAvboitDepthWarpCommandList);
    NWB_ASSERT(!asyncAvboitSchedule || avboitExtinctionCommandList);
    NWB_ASSERT(!asyncAvboitSchedule || asyncAvboitIntegrationCommandList);
    NWB_ASSERT(!asyncAvboitSchedule || avboitAccumulateCommandList);
    NWB_ASSERT(deferredPresentCommandList);

    resetFrameRecordingStateHandoffs();
    m_raytracingSystem.discardSoftShadowTemporalHistory();

    // Recording a packet can advance CPU mirrors (temporal phases, readback cadence, and the AVBOIT clear latch)
    // before the recorded command buffers have reached their owning queues. Preserve them so every rejection path below can
    // make the following frame record the same GPU work again instead of treating an abandoned packet as completed.
    struct PostGbufferPacketCpuState{
        bool avboitTargetsNeedClear = true;
        bool deferredBindlessSlotsUploaded = false;
        u32 softShadowFrameIndex = 0u;
        u32 swShadowEdgeStatsTick = 0u;
        bool swShadowEdgeStatsPending = false;
        u32 swShadowEdgeStatsPendingTick = 0u;
        u64 swShadowEdgeStatsPendingSubmissionID = 0u;
        Core::CommandQueue::Enum swShadowEdgeStatsPendingSubmissionQueue = Core::CommandQueue::kCount;
        bool swShadowEdgeStatsPendingSubmissionUnconfirmed = false;
        bool swShadowDispatchLogged = false;
        bool causticAccumulatorInitialized = false;
        u32 causticTemporalReuseFrameCount = 0u;
        u32 swCausticFrameIndex = 0u;
        u32 hwCausticFrameIndex = 0u;
        bool swCausticDispatchLogged = false;
        bool hwCausticDispatchLogged = false;
        bool causticEmissionGateLogged = false;
        u32 surfelFrameIndex = 0u;
        bool surfelSeeded = false;
        bool surfelCountReadbackPending = false;
        u32 surfelCountReadbackFrame = 0u;
        u64 surfelCountReadbackPendingSubmissionID = 0u;
        Core::CommandQueue::Enum surfelCountReadbackPendingSubmissionQueue = Core::CommandQueue::kCount;
        bool surfelCountReadbackPendingSubmissionUnconfirmed = false;
    };
    const PostGbufferPacketCpuState postGbufferPacketCpuState{
        m_avboitState.m_targetsNeedClear,
        deferredTargets.bindless.slotsUploaded,
        m_rayTracingState.m_softShadowFrameIndex,
        m_rayTracingState.m_swShadowEdgeStatsTick,
        m_rayTracingState.m_swShadowEdgeStatsPending,
        m_rayTracingState.m_swShadowEdgeStatsPendingTick,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionQueue,
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionUnconfirmed,
        m_rayTracingState.m_swShadowDispatchLogged,
        m_rayTracingState.m_causticAccumulatorInitialized,
        m_rayTracingState.m_causticTemporalReuseFrameCount,
        m_rayTracingState.m_swCausticFrameIndex,
        m_rayTracingState.m_hwCausticFrameIndex,
        m_rayTracingState.m_swCausticDispatchLogged,
        m_rayTracingState.m_hwCausticDispatchLogged,
        m_rayTracingState.m_causticEmissionGateLogged,
        m_rayTracingState.m_surfelFrameIndex,
        m_rayTracingState.m_surfelSeeded,
        m_rayTracingState.m_surfelCountReadbackPending,
        m_rayTracingState.m_surfelCountReadbackFrame,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionID,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionQueue,
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionUnconfirmed,
    };
    const auto restorePrefixCpuState = [&](){
        // The G-buffer writer updates its CPU upload mirrors while recording. Its writes did not reach the device if
        // this packet batch is abandoned, so force both uploads on the retry rather than restoring stale byte caches.
        m_drawState.m_meshViewGpuDataValid = false;
        m_deferredState.m_sceneShadingGpuDataValid = false;
        m_deferredState.m_lightGpuDataValid = false;
    };

    const auto restoreShadowCpuState = [&](){
        m_rayTracingState.m_softShadowFrameIndex = postGbufferPacketCpuState.softShadowFrameIndex;
        m_rayTracingState.m_swShadowEdgeStatsTick = postGbufferPacketCpuState.swShadowEdgeStatsTick;
        m_rayTracingState.m_swShadowEdgeStatsPending = postGbufferPacketCpuState.swShadowEdgeStatsPending;
        m_rayTracingState.m_swShadowEdgeStatsPendingTick = postGbufferPacketCpuState.swShadowEdgeStatsPendingTick;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionID;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionQueue = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionQueue;
        m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionUnconfirmed = postGbufferPacketCpuState.swShadowEdgeStatsPendingSubmissionUnconfirmed;
        m_rayTracingState.m_swShadowDispatchLogged = postGbufferPacketCpuState.swShadowDispatchLogged;
    };

    const auto restoreCausticsCpuState = [&](){
        m_rayTracingState.m_causticAccumulatorInitialized = postGbufferPacketCpuState.causticAccumulatorInitialized;
        m_rayTracingState.m_causticTemporalReuseFrameCount = postGbufferPacketCpuState.causticTemporalReuseFrameCount;
        m_rayTracingState.m_swCausticFrameIndex = postGbufferPacketCpuState.swCausticFrameIndex;
        m_rayTracingState.m_hwCausticFrameIndex = postGbufferPacketCpuState.hwCausticFrameIndex;
        m_rayTracingState.m_swCausticDispatchLogged = postGbufferPacketCpuState.swCausticDispatchLogged;
        m_rayTracingState.m_hwCausticDispatchLogged = postGbufferPacketCpuState.hwCausticDispatchLogged;
        m_rayTracingState.m_causticEmissionGateLogged = postGbufferPacketCpuState.causticEmissionGateLogged;
    };
    const auto restoreSurfelGiCpuState = [&](){
        m_rayTracingState.m_surfelFrameIndex = postGbufferPacketCpuState.surfelFrameIndex;
        m_rayTracingState.m_surfelSeeded = postGbufferPacketCpuState.surfelSeeded;
        m_rayTracingState.m_surfelCountReadbackPending = postGbufferPacketCpuState.surfelCountReadbackPending;
        m_rayTracingState.m_surfelCountReadbackFrame = postGbufferPacketCpuState.surfelCountReadbackFrame;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionID = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionID;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionQueue = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionQueue;
        m_rayTracingState.m_surfelCountReadbackPendingSubmissionUnconfirmed = postGbufferPacketCpuState.surfelCountReadbackPendingSubmissionUnconfirmed;
    };
    const auto restoreGraphicsEffectsCpuState = [&](){
        m_avboitState.m_targetsNeedClear = postGbufferPacketCpuState.avboitTargetsNeedClear;
    };
    const auto restoreEffectsCpuState = [&](){
        restoreCausticsCpuState();
        restoreSurfelGiCpuState();
        restoreGraphicsEffectsCpuState();
    };
    // Any producer that joined the accepted Compute packet owns its temporal/readback state. A later Graphics-effects
    // rejection must restore only work that has not crossed that acceptance boundary.
    const auto restoreUnacceptedGraphicsEffectsCpuState = [&](){
        if(!asyncCausticsSchedule)
            restoreCausticsCpuState();
        if(!asyncSurfelGiSchedule)
            restoreSurfelGiCpuState();
        restoreGraphicsEffectsCpuState();
    };
    const auto restorePostGbufferPacketCpuState = [&](){
        deferredTargets.bindless.slotsUploaded = postGbufferPacketCpuState.deferredBindlessSlotsUploaded;
        restorePrefixCpuState();
        restoreShadowCpuState();
        restoreEffectsCpuState();
    };

    // The Graphics-only fallback keeps its established one-ticket, one-submission timing transaction. A dedicated
    // AsyncCompute lane instead owns one ticket per accepted packet so no timestamp reservation can straddle queues.
    // The packet-keyed collection supplies those local ticket lifetimes without duplicating the plan's packet list.
    ECSRenderDetail::FrameExecutionPlanTimingTickets frameExecutionTimingTickets(
        frameExecutionPlan,
        m_graphics.gpuTiming()
    );
    // This scope crosses two synchronously-waited Graphics jobs, so it cannot live in either worker's scratch
    // arena. The fallback owns it directly; the dedicated schedule uses the acceptance-aware transaction below.
    Optional<Core::GpuTimingMeasure> frameTiming;
    // The dedicated schedule keeps render.frame as an end-to-end Graphics-timeline critical path. Its endpoint is
    // committed only after Graphics final accepts; a rejected later packet records a non-publishing recovery end so
    // the accepted prefix timestamp never leaks into a later frame's query pool.
    Core::GpuTimingFrameTransaction asyncFrameTiming(m_graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncPrefixTiming;
    Optional<Core::GpuTimingMeasure> asyncEffectsTiming;
    Optional<Core::GpuTimingMeasure> asyncFinalTiming;
    const auto discardTimingTickets = [&frameExecutionTimingTickets](){
        frameExecutionTimingTickets.discardAll();
    };
    const auto discardRenderPackets = [&](){
        if(frameTiming){
            frameTiming->discardTiming();
            frameTiming.reset();
        }
        if(asyncPrefixTiming){
            asyncPrefixTiming->discardTiming();
            asyncPrefixTiming.reset();
        }
        if(asyncEffectsTiming){
            asyncEffectsTiming->discardTiming();
            asyncEffectsTiming.reset();
        }
        if(asyncFinalTiming){
            asyncFinalTiming->discardTiming();
            asyncFinalTiming.reset();
        }
        asyncFrameTiming.discard();
        discardTimingTickets();
        restorePostGbufferPacketCpuState();
        m_raytracingSystem.discardSoftShadowTemporalHistory();
        m_raytracingSystem.discardSurfelResourceInitialization();
        resetAbandonedFrameStateHandoffs();
    };

    // Mesh-view and scene-shading uploads plus the non-CSG deferred-target clear have no shared CPU or GPU outputs.
    // Record them from the completed shadow-preparation snapshot on sibling workers, then merge their disjoint final
    // states before the opaque producer gathers its CSG work region from the freshly cached mesh-view data. The CSG
    // interval clear stays with that producer because its rect is not known until after the gather.
    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(deferredTargets.framebuffer->getFramebufferInfo());
    bool meshViewSetupReady = false;
    bool sceneShadingSetupReady = false;
    bool meshViewSetupCommandListReady = false;
    bool sceneShadingSetupCommandListReady = false;
    bool deferredClearCommandListReady = false;
    const Core::Graphics::JobHandle meshViewSetupRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        meshViewAspectRatio,
        &device,
        meshViewSetupCommandList,
        &frameTiming,
        &asyncFrameTiming,
        &asyncPrefixTiming,
        asyncShadowSchedule,
        &meshViewSetupReady,
        &meshViewSetupCommandListReady,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::GraphicsPrefix
        );
        meshViewSetupCommandList->open(&m_shadowPrepareStateHandoff);
        if(!meshViewSetupCommandList->hasCommandBuffer())
            return;

        bool asyncFrameTimingStarted = true;
        if(!asyncShadowSchedule){
            // Graphics has already reset every timer-query pool in the frame preamble, before shadow preparation and
            // every other render pass. The legacy whole-frame scope remains valid only for the one-submit fallback.
            frameTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_Frame,
                device,
                *meshViewSetupCommandList
            );
            if(!frameTiming)
                return;
        }
        else{
            asyncPrefixTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncPrefix,
                device,
                *meshViewSetupCommandList
            );
            asyncPrefixTiming->finishMarker();
            asyncFrameTimingStarted = asyncFrameTiming.begin(
                RendererGpuTimingScope::s_Frame,
                device,
                *meshViewSetupCommandList
            );
        }

        const bool meshViewReady = m_meshSystem.updateMeshViewBuffer(*meshViewSetupCommandList, meshViewAspectRatio);
        // A split timing scope must close its debug marker on the same list that opened it. Its end timestamp is
        // deliberately deferred to the ordered deferred-composite packet below.
        if(frameTiming)
            frameTiming->finishMarker();
        meshViewSetupCommandList->close(&m_meshViewSetupStateHandoff);
        meshViewSetupReady = meshViewReady;
        meshViewSetupCommandListReady =
            asyncFrameTimingStarted
            && m_meshViewSetupStateHandoff.valid()
            && meshViewSetupCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle sceneShadingSetupRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        meshViewAspectRatio,
        sceneShadingSetupCommandList,
        &sceneShadingSetupReady,
        &sceneShadingSetupCommandListReady,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::GraphicsPrefix
        );
        sceneShadingSetupCommandList->open(&m_shadowPrepareStateHandoff);
        if(!sceneShadingSetupCommandList->hasCommandBuffer())
            return;

        const bool sceneShadingReady = m_deferredSystem.updateSceneShadingBuffer(*sceneShadingSetupCommandList, meshViewAspectRatio);
        sceneShadingSetupCommandList->close(&m_sceneShadingSetupStateHandoff);
        sceneShadingSetupReady = sceneShadingReady;
        sceneShadingSetupCommandListReady =
            m_sceneShadingSetupStateHandoff.valid()
            && sceneShadingSetupCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle deferredClearRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        deferredClearCommandList,
        &deferredClearCommandListReady,
        asyncSurfelGiSchedule,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::GraphicsPrefix
        );
        deferredClearCommandList->open(&m_shadowPrepareStateHandoff);
        if(!deferredClearCommandList->hasCommandBuffer())
            return;

        // CSG interval clearing is intentionally deferred to the opaque packet: its work rect is calculated from
        // the freshly gathered CSG receiver data. The remaining deferred targets are independent of both setup
        // uploads and can record here in parallel.
        m_deferredSystem.clearDeferredTargets(
            *deferredClearCommandList,
            deferredTargets,
            false,
            Core::Rect{},
            !asyncSurfelGiSchedule
        );
        deferredClearCommandList->close(&m_deferredClearStateHandoff);
        deferredClearCommandListReady =
            m_deferredClearStateHandoff.valid()
            && deferredClearCommandList->hasCommandBuffer()
        ;
    });
    if(
        !meshViewSetupRecordingJob.valid()
        || !sceneShadingSetupRecordingJob.valid()
        || !deferredClearRecordingJob.valid()
    ){
        if(meshViewSetupRecordingJob.valid())
            m_graphics.waitJob(meshViewSetupRecordingJob);
        if(sceneShadingSetupRecordingJob.valid())
            m_graphics.waitJob(sceneShadingSetupRecordingJob);
        if(deferredClearRecordingJob.valid())
            m_graphics.waitJob(deferredClearRecordingJob);
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(meshViewSetupRecordingJob);
    m_graphics.waitJob(sceneShadingSetupRecordingJob);
    m_graphics.waitJob(deferredClearRecordingJob);
    if(
        !meshViewSetupCommandListReady
        || !sceneShadingSetupCommandListReady
        || !deferredClearCommandListReady
        || (!asyncShadowSchedule && !frameTiming)
    ){
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* frameSetupBranchStates[] = {
        &m_meshViewSetupStateHandoff,
        &m_sceneShadingSetupStateHandoff,
        &m_deferredClearStateHandoff,
    };
    if(!m_frameSetupStateFanInHandoff.buildFanIn(
        m_shadowPrepareStateHandoff,
        frameSetupBranchStates,
        3u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame-setup packet state fan-in failed"));
        discardRenderPackets();
        return;
    }

    const bool frameSetupReady = meshViewSetupReady && sceneShadingSetupReady;
    bool gbufferCommandListReady = false;
    const Core::Graphics::JobHandle gbufferRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        csgFrameState,
        hasOpaqueCsgFrameWork,
        frameSetupReady,
        &device,
        gbufferCommandList,
        &gbufferCommandListReady,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::GraphicsPrefix
        );
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        Core::CommandList* commandList = gbufferCommandList;
        commandList->open(&m_frameSetupStateFanInHandoff);
        if(!commandList->hasCommandBuffer())
            return;

        MaterialPassDrawItemPartitions opaqueDrawItems{scratchArena};
        InstanceGpuDataVector instanceData{scratchArena};
        CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{scratchArena};
#endif
        MaterialTypedByteDataVector materialTypedBytes{scratchArena};

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

        if(frameSetupReady){
            m_materialSystem.gatherMaterialPassDrawItems(
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                false,
                csgFrameState,
                opaqueDrawItems,
                instanceData,
                csgFrameData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes,
                RendererResourceLookupMode::PreparedOnly
            );
        }

        const Core::Rect opaqueCsgClearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height);
        if(hasOpaqueCsgFrameWork)
            m_deferredSystem.clearCsgIntervalTargets(*commandList, deferredTargets, opaqueCsgClearRect);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && m_materialSystem.materialPassDrawBuffersReady(instanceData, materialTypedBytes)
        ;
        const bool regularDrawResourcesReady =
            deferredResourcesReady
            && m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.regular)
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && (opaqueDrawItems.csg.empty() || m_csgSystem.csgFrameBuffersReady(csgFrameData))
        ;
        const bool csgDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csg.empty() || m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty() || m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        const bool deferredUploadReady =
            deferredResourcesReady
            && m_materialSystem.uploadMaterialPassDrawBuffers(
                *commandList,
                instanceData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes
            )
        ;
        if(deferredUploadReady){
            const bool csgUploadReady = csgResourcesReady && (opaqueDrawItems.csg.empty() || m_csgSystem.uploadCsgFrameBuffers(*commandList, csgFrameData));
            const bool csgSampleStateReady =
                csgUploadReady
                && (!csgFrameData.hasWork() || m_csgSystem.uploadCsgIntervalSampleState(*commandList, deferredTargets, csgFrameData))
            ;
            if(csgSampleStateReady && csgFrameData.hasWork())
                m_csgSystem.dispatchCsgIntervalPeels(*commandList, deferredTargets, csgFrameData);
            const MaterialPassDrawContext opaqueDrawContext{
                *commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                nullptr,
                deferredViewportState
            };
            if(regularDrawResourcesReady && !opaqueDrawItems.regular.empty()){
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueRegular, device, *commandList);

                m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.regular);
            }

            Core::ViewportState csgIntervalViewportState;
            csgIntervalViewportState
                .addViewport(deferredTargets.framebuffer->getFramebufferInfo().getViewport())
                .addScissorRect(csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height))
            ;
            const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
                *commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::CsgReceiverSurface,
                nullptr,
                csgIntervalViewportState
            };
            if(csgSampleStateReady && csgReceiverSurfaceDrawResourcesReady && !opaqueDrawItems.csgReceiverSurface.empty()){
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, device, *commandList);

                m_materialSystem.renderMaterialPassDrawItems(csgReceiverSurfaceDrawContext, opaqueDrawItems.csgReceiverSurface);
            }
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgReceiverSpanBuild(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                m_csgSystem.dispatchCsgIntervalCombine(*commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgDrawResourcesReady){
                if(!opaqueDrawItems.csg.empty()){
                    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, device, *commandList);

                    m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
                }
                if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                    m_csgSystem.renderCsgIntervalCaps(*commandList, deferredTargets, csgFrameData);
            }
        }
        commandList->endRenderPass();

        // The opaque producer exports its final tracked state after close-time keepInitialState restores. The next
        // packet is a normalization prelude; it imports this snapshot before the four independent workers record.
        commandList->close(&m_gbufferStateHandoff);
        if(!m_gbufferStateHandoff.valid())
            return;
        gbufferCommandListReady = commandList->hasCommandBuffer();
    });
    if(!gbufferRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(gbufferRecordingJob);
    if(!gbufferCommandListReady){
        discardRenderPackets();
        return;
    }

    // Normalize every G-buffer/trace input shared by the sibling packets once. The four workers import the exact
    // same snapshot below, so none can record a stale transition from the opaque producer's final state.
    bool postGbufferNormalizeCommandListReady = false;
    const Core::Graphics::JobHandle postGbufferNormalizeRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        postGbufferNormalizeCommandList,
        &postGbufferNormalizeCommandListReady,
        &asyncPrefixTiming,
        asyncShadowSchedule,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::GraphicsPrefix
        );
        postGbufferNormalizeCommandList->open(&m_gbufferStateHandoff);
        if(!postGbufferNormalizeCommandList->hasCommandBuffer())
            return;

        m_raytracingSystem.normalizePostGbufferPacketResources(*postGbufferNormalizeCommandList, deferredTargets);
        if(asyncShadowSchedule && asyncPrefixTiming){
            asyncPrefixTiming->finishTiming(*postGbufferNormalizeCommandList);
            asyncPrefixTiming.reset();
        }
        postGbufferNormalizeCommandList->close(&m_postGbufferNormalizedStateHandoff);
        postGbufferNormalizeCommandListReady =
            m_postGbufferNormalizedStateHandoff.valid()
            && postGbufferNormalizeCommandList->hasCommandBuffer()
        ;
    });
    if(!postGbufferNormalizeRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(postGbufferNormalizeRecordingJob);
    if(!postGbufferNormalizeCommandListReady){
        discardRenderPackets();
        return;
    }

    // The Compute packet must not import the broad post-G-buffer snapshot: it also carries exclusive Graphics-only
    // caustic/GI/AVBOIT resources. Select only the shader-visible shadow inputs, then merge the Compute-only scratch
    // state and last frame's Graphics -> Compute visibility release.
    if(asyncShadowSchedule){
        Core::Alloc::ScratchArena shadowInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> shadowInputTextures{ shadowInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> shadowInputBuffers{ shadowInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                shadowInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                shadowInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.normal.get());
        appendTexture(deferredTargets.depth.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        if(m_rayTracingState.m_tlas)
            appendBuffer(m_rayTracingState.m_tlas->getBackingBuffer());
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
            appendBuffer(buffer);

        if(!m_shadowComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            shadowInputTextures.data(),
            shadowInputTextures.size(),
            shadowInputBuffers.data(),
            shadowInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async shadow input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* shadowInputBranches[2] = {};
        usize shadowInputBranchCount = 0u;
        if(m_shadowComputePersistentStateHandoff.valid())
            shadowInputBranches[shadowInputBranchCount++] = &m_shadowComputePersistentStateHandoff;
        if(m_shadowVisibilityReturnStateHandoff.valid())
            shadowInputBranches[shadowInputBranchCount++] = &m_shadowVisibilityReturnStateHandoff;
        if(!m_shadowComputeInputStateHandoff.buildFanIn(
            m_shadowComputeBaseStateHandoff,
            shadowInputBranches,
            shadowInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async shadow input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    // Both caustic producers use cross-lane read-only trace inputs plus their emission target list and camera view.
    // The software producer reads its software-BVH mesh tables; the hardware producer reads the TLAS and corner
    // attributes through its descriptor-buffer material context. Their accumulator/resolve scratch is private to
    // Compute and the lighting-facing irradiance is deliberately excluded here: it comes from the prior Graphics ->
    // Compute ownership return.
    if(asyncCausticsSchedule){
        Core::Alloc::ScratchArena causticsInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> causticsInputTextures{ causticsInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> causticsInputBuffers{ causticsInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                causticsInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                causticsInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.depth.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        appendBuffer(m_rayTracingState.m_causticEmissionTargetBuffer.get());
        appendBuffer(m_drawState.m_meshViewBuffer.get());
        if(hardwareShadowSupported){
            // setAccelStructState tracks the TLAS through this backing allocation. The hardware photon closest-hit
            // shader also fetches the flat corner attributes by heap slot.
            if(m_rayTracingState.m_tlas)
                appendBuffer(m_rayTracingState.m_tlas->getBackingBuffer());
            for(Core::Buffer* buffer : m_rayTracingState.m_shadowMeshAttributeBuffers)
                appendBuffer(buffer);
        }
        else{
            for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
                appendBuffer(buffer);
            for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
                appendBuffer(buffer);
            for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
                appendBuffer(buffer);
            for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
                appendBuffer(buffer);
        }

        if(!m_causticsComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            causticsInputTextures.data(),
            causticsInputTextures.size(),
            causticsInputBuffers.data(),
            causticsInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async caustics input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* causticsInputBranches[2] = {};
        usize causticsInputBranchCount = 0u;
        if(m_causticsComputePersistentStateHandoff.valid())
            causticsInputBranches[causticsInputBranchCount++] = &m_causticsComputePersistentStateHandoff;
        if(m_causticIrradianceReturnStateHandoff.valid())
            causticsInputBranches[causticsInputBranchCount++] = &m_causticIrradianceReturnStateHandoff;
        if(!m_causticsComputeInputStateHandoff.buildFanIn(
            m_causticsComputeBaseStateHandoff,
            causticsInputBranches,
            causticsInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async caustics input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    // Surfel GI is compute-only on both trace backends: its HW variant uses inline RayQuery inside dispatch, rather
    // than dispatchRays. Import the current Graphics-produced G-buffer/trace selectors and retain only the private
    // field/snapshot/readback state from the last accepted Compute packet. The lighting-facing irradiance arrives via
    // the explicit Graphics -> Compute return handoff instead of the broad prefix snapshot.
    if(asyncSurfelGiSchedule){
        Core::Alloc::ScratchArena surfelGiInputScratchArena(RendererArenaScope::s_RenderArena);
        Vector<Core::Texture*, Core::Alloc::ScratchArena> surfelGiInputTextures{ surfelGiInputScratchArena };
        Vector<Core::Buffer*, Core::Alloc::ScratchArena> surfelGiInputBuffers{ surfelGiInputScratchArena };
        const auto appendTexture = [&](Core::Texture* texture){
            if(texture)
                surfelGiInputTextures.push_back(texture);
        };
        const auto appendBuffer = [&](Core::Buffer* buffer){
            if(buffer)
                surfelGiInputBuffers.push_back(buffer);
        };

        appendTexture(deferredTargets.worldPosition.get());
        appendTexture(deferredTargets.normal.get());
        appendBuffer(m_deferredState.m_sceneShadingBuffer.get());
        appendBuffer(m_deferredState.m_lightBuffer.get());
        appendBuffer(deferredTargets.bindless.slotsBuffer.get());
        appendBuffer(m_rayTracingState.m_surfelConstants.get());
        appendBuffer(m_rayTracingState.m_sceneBvhNodeBuffer.get());
        appendBuffer(m_rayTracingState.m_sceneInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceMaterialBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowInstanceBuffer.get());
        appendBuffer(m_rayTracingState.m_shadowMaterialTypedBuffer.get());
        appendBuffer(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get());
        if(m_rayTracingState.m_tlas)
            appendBuffer(m_rayTracingState.m_tlas->getBackingBuffer());
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshNodeBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshPositionBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshIndexBuffers)
            appendBuffer(buffer);
        for(Core::Buffer* buffer : m_rayTracingState.m_swShadowMeshAttributeBuffers)
            appendBuffer(buffer);

        if(!m_surfelGiComputeBaseStateHandoff.buildResourceSubset(
            m_postGbufferNormalizedStateHandoff,
            surfelGiInputTextures.data(),
            surfelGiInputTextures.size(),
            surfelGiInputBuffers.data(),
            surfelGiInputBuffers.size()
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async surfel-GI input state selection failed"));
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* surfelGiInputBranches[2] = {};
        usize surfelGiInputBranchCount = 0u;
        if(m_surfelGiComputePersistentStateHandoff.valid())
            surfelGiInputBranches[surfelGiInputBranchCount++] = &m_surfelGiComputePersistentStateHandoff;
        if(m_surfelIrradianceReturnStateHandoff.valid())
            surfelGiInputBranches[surfelGiInputBranchCount++] = &m_surfelIrradianceReturnStateHandoff;
        if(!m_surfelGiComputeInputStateHandoff.buildFanIn(
            m_surfelGiComputeBaseStateHandoff,
            surfelGiInputBranches,
            surfelGiInputBranchCount
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async surfel-GI input state fan-in failed"));
            discardRenderPackets();
            return;
        }
    }

    const bool shadowVisibilityPrepared = m_preparedShadowVisibilityReady;

    // The shadow, caustics, surfel-GI, and AVBOIT packets own distinct outputs. Every shared input already has its
    // common read state in the prelude, so the workers can record independently from the same snapshot. The hardware
    // caustic producer joins the Graphics AVBOIT-support packet to overlap the Async shadow/surfel packet; deferred
    // lighting is their first GPU consumer.
    bool shadowVisibilityCommandListReady = false;
    bool causticsCommandListReady = false;
    bool surfelGiCommandListReady = false;
    bool avboitCommandListReady = false;
    bool asyncEffectsTimingBeginCommandListReady = !recordsAsyncEffectsTiming;
    if(recordsAsyncEffectsTiming){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::AsyncEffectsTiming
        );
        asyncEffectsTimingBeginCommandList->open();
        if(asyncEffectsTimingBeginCommandList->hasCommandBuffer()){
            asyncEffectsTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncEffects,
                device,
                *asyncEffectsTimingBeginCommandList
            );
            asyncEffectsTiming->finishMarker();
            asyncEffectsTimingBeginCommandList->close();
            asyncEffectsTimingBeginCommandListReady = asyncEffectsTimingBeginCommandList->hasCommandBuffer();
        }
    }
    if(!asyncEffectsTimingBeginCommandListReady){
        discardRenderPackets();
        return;
    }
    const Core::Graphics::JobHandle shadowVisibilityRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        shadowVisibilityCommandList,
        &shadowVisibilityCommandListReady,
        asyncShadowSchedule,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::RayEffects
        );
        shadowVisibilityCommandList->open(
            asyncShadowSchedule
                ? &m_shadowComputeInputStateHandoff
                : &m_postGbufferNormalizedStateHandoff
        );
        if(!shadowVisibilityCommandList->hasCommandBuffer())
            return;

        Optional<Core::GpuTimingMeasure> asyncShadowTiming;
        if(asyncShadowSchedule){
            asyncShadowTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncShadow,
                m_graphics.getDevice(),
                *shadowVisibilityCommandList
            );
        }

        bool shadowVisibilityWritten = false;
        if(shadowVisibilityPrepared && hardwareShadowSupported){
            // The HW opaque trace feeds the soft denoise chain when available. Transparent shadow stays on the
            // software path, with a hybrid multiply fallback when the colored soft fold was not prepared.
            shadowVisibilityWritten = m_raytracingSystem.renderShadowVisibility(*shadowVisibilityCommandList, deferredTargets);
            if(!shadowVisibilityWritten)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
            else if(!m_raytracingSystem.softTransparentShadowReady() && m_raytracingSystem.hybridTransparentShadowReady()){
                if(!m_raytracingSystem.renderGpuBvhShadowVisibility(*shadowVisibilityCommandList, deferredTargets, true))
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow pass failed"));
            }
        }
        else if(shadowVisibilityPrepared){
            shadowVisibilityWritten = m_raytracingSystem.renderGpuBvhShadowVisibility(*shadowVisibilityCommandList, deferredTargets);
        }
        // Deferred lighting samples visibility every frame, so retain the all-lit fallback whenever neither backend
        // emitted it rather than exposing the previous frame's contents.
        if(!shadowVisibilityWritten)
            m_raytracingSystem.clearShadowVisibility(*shadowVisibilityCommandList, deferredTargets);

        if(asyncShadowTiming){
            asyncShadowTiming->finishTiming(*shadowVisibilityCommandList);
            asyncShadowTiming.reset();
        }

        shadowVisibilityCommandList->close(&m_shadowVisibilityStateHandoff);
        shadowVisibilityCommandListReady =
            m_shadowVisibilityStateHandoff.valid()
            && shadowVisibilityCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle causticsRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        causticsCommandList,
        &causticsCommandListReady,
        asyncCausticsSchedule,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::Caustics
        );
        causticsCommandList->open(
            asyncCausticsSchedule
                ? &m_causticsComputeInputStateHandoff
                : &m_postGbufferNormalizedStateHandoff
        );
        if(!causticsCommandList->hasCommandBuffer())
            return;

        // Black is the additive identity for caustics. Keep that valid no-op input even when no refractive scene
        // work was prepared or the selected producer fails to record.
        m_raytracingSystem.clearCausticTargets(*causticsCommandList, deferredTargets);
        if(shadowVisibilityPrepared){
            if(hardwareShadowSupported){
                const bool causticsDispatched = m_raytracingSystem.renderHwCaustics(*causticsCommandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasHwCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic render pass failed"));
            }
            else{
                const bool causticsDispatched = m_raytracingSystem.renderGpuBvhCaustics(*causticsCommandList, deferredTargets);
                if(!causticsDispatched && m_raytracingSystem.hasCausticWork())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic render pass failed"));
            }
        }

        causticsCommandList->close(&m_causticsStateHandoff);
        causticsCommandListReady =
            m_causticsStateHandoff.valid()
            && causticsCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle surfelGiRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        surfelGiCommandList,
        &surfelGiCommandListReady,
        asyncSurfelGiSchedule,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::SurfelGi
        );
        surfelGiCommandList->open(
            asyncSurfelGiSchedule
                ? &m_surfelGiComputeInputStateHandoff
                : &m_postGbufferNormalizedStateHandoff
        );
        if(!surfelGiCommandList->hasCommandBuffer())
            return;

        Optional<Core::GpuTimingMeasure> asyncSurfelGiTiming;
        if(asyncSurfelGiSchedule){
            asyncSurfelGiTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncSurfelGi,
                m_graphics.getDevice(),
                *surfelGiCommandList
            );
        }

        if(asyncSurfelGiSchedule)
            m_raytracingSystem.clearSurfelIrradiance(*surfelGiCommandList, deferredTargets);

        // Spawn -> hash build -> trace -> resolve remains one ordered packet, so its persistent surfel buffers never
        // become visible to another worker halfway through their per-frame update.
        if(!m_raytracingSystem.renderSurfelGi(*surfelGiCommandList, deferredTargets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI render pass failed"));

        if(asyncSurfelGiTiming){
            asyncSurfelGiTiming->finishTiming(*surfelGiCommandList);
            asyncSurfelGiTiming.reset();
        }

        surfelGiCommandList->close(&m_surfelGiStateHandoff);
        surfelGiCommandListReady =
            m_surfelGiStateHandoff.valid()
            && surfelGiCommandList->hasCommandBuffer()
        ;
    });
    const Core::Graphics::JobHandle avboitPreRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        csgFrameState,
        hasTransparentRenderers,
        asyncAvboitSchedule,
        avboitCommandList,
        &avboitCommandListReady,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::AvboitRaster
        );
        avboitCommandList->open(&m_postGbufferNormalizedStateHandoff);
        if(!avboitCommandList->hasCommandBuffer())
            return;

        // A no-transparency frame can still need one clear to retire the previous frame's accumulation. Record an
        // otherwise empty, valid packet as well so deferred lighting always imports the four-branch fan-in snapshot.
        if(hasTransparentRenderers || m_avboitState.m_targetsNeedClear){
            m_avboitSystem.clearAvboitTargets(*avboitCommandList, deferredTargets.avboit);
            m_avboitState.m_targetsNeedClear = hasTransparentRenderers;
        }
        if(hasTransparentRenderers){
            if(asyncAvboitSchedule)
                m_avboitSystem.renderAvboitPreDepthWarpPasses(*avboitCommandList, deferredTargets, csgFrameState);
            else
                m_avboitSystem.renderAvboitPasses(*avboitCommandList, deferredTargets, csgFrameState);
        }

        // Transparent CSG interval construction uses the opaque G-buffer framebuffer, whose automatic attachment
        // tracking temporarily makes normal/world-position render targets and depth a read-only depth attachment.
        // Restore every deferred-lighting input before the later Compute packet records from the merged state. A
        // Compute-only command buffer cannot name color/depth attachment accesses as a local barrier source.
        avboitCommandList->setTextureState(
            deferredTargets.albedo.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
        avboitCommandList->setTextureState(
            deferredTargets.normal.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
        avboitCommandList->setTextureState(
            deferredTargets.worldPosition.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
        avboitCommandList->setTextureState(
            deferredTargets.depth.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );

        avboitCommandList->close(
            asyncAvboitSchedule
                ? &m_avboitPreStateHandoff
                : &m_avboitStateHandoff
        );
        avboitCommandListReady =
            (asyncAvboitSchedule ? m_avboitPreStateHandoff.valid() : m_avboitStateHandoff.valid())
            && avboitCommandList->hasCommandBuffer()
        ;
    });
    if(
        !shadowVisibilityRecordingJob.valid()
        || !causticsRecordingJob.valid()
        || !surfelGiRecordingJob.valid()
        || !avboitPreRecordingJob.valid()
    ){
        if(shadowVisibilityRecordingJob.valid())
            m_graphics.waitJob(shadowVisibilityRecordingJob);
        if(causticsRecordingJob.valid())
            m_graphics.waitJob(causticsRecordingJob);
        if(surfelGiRecordingJob.valid())
            m_graphics.waitJob(surfelGiRecordingJob);
        if(avboitPreRecordingJob.valid())
            m_graphics.waitJob(avboitPreRecordingJob);
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(shadowVisibilityRecordingJob);
    m_graphics.waitJob(causticsRecordingJob);
    m_graphics.waitJob(surfelGiRecordingJob);
    m_graphics.waitJob(avboitPreRecordingJob);
    if(!shadowVisibilityCommandListReady || !causticsCommandListReady || !surfelGiCommandListReady || !avboitCommandListReady){
        discardRenderPackets();
        return;
    }

    if(recordsAsyncEffectsTiming){
        bool asyncEffectsTimingEndCommandListReady = false;
        {
            ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
                frameExecutionTimingTickets,
                ECSRenderDetail::FrameExecutionWork::AsyncEffectsTiming
            );
            asyncEffectsTimingEndCommandList->open();
            if(asyncEffectsTimingEndCommandList->hasCommandBuffer()){
                if(asyncEffectsTiming){
                    asyncEffectsTiming->finishTiming(*asyncEffectsTimingEndCommandList);
                    asyncEffectsTiming.reset();
                }
                asyncEffectsTimingEndCommandList->close();
                asyncEffectsTimingEndCommandListReady = asyncEffectsTimingEndCommandList->hasCommandBuffer();
            }
        }
        if(!asyncEffectsTimingEndCommandListReady){
            discardRenderPackets();
            return;
        }
    }

    if(asyncAvboitSchedule){
        // The depth-warp dispatch imports only resources it can legally access from a compute-only queue. The
        // AVBOIT work buffers and selector cbuffer are concurrent; the full Graphics snapshot remains available to
        // the raster extinction stage through the subsequent fan-in.
        Core::Buffer* const avboitDepthWarpInputBuffers[] = {
            deferredTargets.avboit.coverageBuffer.get(),
            deferredTargets.avboit.depthWarpBuffer.get(),
            deferredTargets.avboit.controlBuffer.get(),
            deferredTargets.bindless.slotsBuffer.get(),
        };
        if(!m_avboitDepthWarpInputStateHandoff.buildResourceSubset(
            m_avboitPreStateHandoff,
            nullptr,
            0u,
            avboitDepthWarpInputBuffers,
            LengthOf(avboitDepthWarpInputBuffers)
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async AVBOIT depth-warp input state selection failed"));
            discardRenderPackets();
            return;
        }

        bool avboitDepthWarpCommandListReady = false;
        const Core::Graphics::JobHandle avboitDepthWarpRecordingJob = m_graphics.scheduleGraphicsJob([
            this,
            &deferredTargets,
            asyncAvboitDepthWarpCommandList,
            &avboitDepthWarpCommandListReady,
            &frameExecutionTimingTickets
        ](){
            ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
                frameExecutionTimingTickets,
                ECSRenderDetail::FrameExecutionWork::AvboitDepthWarp
            );
            asyncAvboitDepthWarpCommandList->open(&m_avboitDepthWarpInputStateHandoff);
            if(!asyncAvboitDepthWarpCommandList->hasCommandBuffer())
                return;

            m_avboitSystem.dispatchAvboitDepthWarp(*asyncAvboitDepthWarpCommandList, deferredTargets.avboit);
            asyncAvboitDepthWarpCommandList->close(&m_avboitDepthWarpStateHandoff);
            avboitDepthWarpCommandListReady =
                m_avboitDepthWarpStateHandoff.valid()
                && asyncAvboitDepthWarpCommandList->hasCommandBuffer()
            ;
        });
        if(!avboitDepthWarpRecordingJob.valid()){
            discardRenderPackets();
            return;
        }
        m_graphics.waitJob(avboitDepthWarpRecordingJob);
        if(!avboitDepthWarpCommandListReady){
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* avboitExtinctionBranches[] = {
            &m_avboitDepthWarpStateHandoff,
        };
        if(!m_avboitExtinctionInputStateHandoff.buildFanIn(
            m_avboitPreStateHandoff,
            avboitExtinctionBranches,
            1u
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async AVBOIT extinction state fan-in failed"));
            discardRenderPackets();
            return;
        }

        bool avboitExtinctionCommandListReady = false;
        const Core::Graphics::JobHandle avboitExtinctionRecordingJob = m_graphics.scheduleGraphicsJob([
            this,
            &deferredTargets,
            csgFrameState,
            avboitExtinctionCommandList,
            &avboitExtinctionCommandListReady,
            &frameExecutionTimingTickets
        ](){
            ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
                frameExecutionTimingTickets,
                ECSRenderDetail::FrameExecutionWork::AvboitExtinction
            );
            avboitExtinctionCommandList->open(&m_avboitExtinctionInputStateHandoff);
            if(!avboitExtinctionCommandList->hasCommandBuffer())
                return;

            m_avboitSystem.renderAvboitExtinctionPass(
                *avboitExtinctionCommandList,
                deferredTargets.avboit,
                csgFrameState
            );
            avboitExtinctionCommandList->close(&m_avboitExtinctionStateHandoff);
            avboitExtinctionCommandListReady =
                m_avboitExtinctionStateHandoff.valid()
                && avboitExtinctionCommandList->hasCommandBuffer()
            ;
        });
        if(!avboitExtinctionRecordingJob.valid()){
            discardRenderPackets();
            return;
        }
        m_graphics.waitJob(avboitExtinctionRecordingJob);
        if(!avboitExtinctionCommandListReady){
            discardRenderPackets();
            return;
        }

        Core::Texture* const avboitIntegrationInputTextures[] = {
            deferredTargets.avboit.transmittanceTexture.get(),
        };
        Core::Buffer* const avboitIntegrationInputBuffers[] = {
            deferredTargets.avboit.extinctionBuffer.get(),
            deferredTargets.avboit.controlBuffer.get(),
            deferredTargets.avboit.extinctionOverflowBuffer.get(),
            deferredTargets.bindless.slotsBuffer.get(),
        };
        if(!m_avboitIntegrationInputStateHandoff.buildResourceSubset(
            m_avboitExtinctionStateHandoff,
            avboitIntegrationInputTextures,
            LengthOf(avboitIntegrationInputTextures),
            avboitIntegrationInputBuffers,
            LengthOf(avboitIntegrationInputBuffers)
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async AVBOIT integration input state selection failed"));
            discardRenderPackets();
            return;
        }

        bool avboitIntegrationCommandListReady = false;
        const Core::Graphics::JobHandle avboitIntegrationRecordingJob = m_graphics.scheduleGraphicsJob([
            this,
            &deferredTargets,
            asyncAvboitIntegrationCommandList,
            &avboitIntegrationCommandListReady,
            &frameExecutionTimingTickets
        ](){
            ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
                frameExecutionTimingTickets,
                ECSRenderDetail::FrameExecutionWork::AvboitIntegration
            );
            asyncAvboitIntegrationCommandList->open(&m_avboitIntegrationInputStateHandoff);
            if(!asyncAvboitIntegrationCommandList->hasCommandBuffer())
                return;

            m_avboitSystem.dispatchAvboitIntegration(*asyncAvboitIntegrationCommandList, deferredTargets.avboit);
            asyncAvboitIntegrationCommandList->close(&m_avboitIntegrationStateHandoff);
            avboitIntegrationCommandListReady =
                m_avboitIntegrationStateHandoff.valid()
                && asyncAvboitIntegrationCommandList->hasCommandBuffer()
            ;
        });
        if(!avboitIntegrationRecordingJob.valid()){
            discardRenderPackets();
            return;
        }
        m_graphics.waitJob(avboitIntegrationRecordingJob);
        if(!avboitIntegrationCommandListReady){
            discardRenderPackets();
            return;
        }

        const Core::CommandListResourceStateHandoff* avboitAccumulationBranches[] = {
            &m_avboitIntegrationStateHandoff,
        };
        if(!m_avboitAccumulationInputStateHandoff.buildFanIn(
            m_avboitExtinctionStateHandoff,
            avboitAccumulationBranches,
            1u
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: async AVBOIT accumulation state fan-in failed"));
            discardRenderPackets();
            return;
        }

        bool avboitAccumulateCommandListReady = false;
        const Core::Graphics::JobHandle avboitAccumulateRecordingJob = m_graphics.scheduleGraphicsJob([
            this,
            &deferredTargets,
            csgFrameState,
            avboitAccumulateCommandList,
            &avboitAccumulateCommandListReady,
            &frameExecutionTimingTickets
        ](){
            ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
                frameExecutionTimingTickets,
                ECSRenderDetail::FrameExecutionWork::AvboitAccumulation
            );
            avboitAccumulateCommandList->open(&m_avboitAccumulationInputStateHandoff);
            if(!avboitAccumulateCommandList->hasCommandBuffer())
                return;

            m_avboitSystem.renderAvboitAccumulatePass(
                *avboitAccumulateCommandList,
                deferredTargets,
                csgFrameState
            );
            avboitAccumulateCommandList->close(&m_avboitStateHandoff);
            avboitAccumulateCommandListReady =
                m_avboitStateHandoff.valid()
                && avboitAccumulateCommandList->hasCommandBuffer()
            ;
        });
        if(!avboitAccumulateRecordingJob.valid()){
            discardRenderPackets();
            return;
        }
        m_graphics.waitJob(avboitAccumulateRecordingJob);
        if(!avboitAccumulateCommandListReady){
            discardRenderPackets();
            return;
        }
    }

    // Keep the deferred-lighting import deliberately narrow. In the default path the three ray-effect results remain
    // exclusively on AsyncCompute through their only consumer. The lagged path deliberately omits those live outputs:
    // its history textures restore to Common and are gated by the last accepted stash token instead.
    Core::Texture* const deferredLightingBaseTextures[] = {
        deferredTargets.albedo.get(),
        deferredTargets.normal.get(),
        deferredTargets.worldPosition.get(),
        deferredTargets.depth.get(),
        deferredTargets.opaqueColor.get(),
    };
    Core::Buffer* const deferredLightingBaseBuffers[] = {
        m_deferredState.m_sceneShadingBuffer.get(),
        m_deferredState.m_lightBuffer.get(),
        deferredTargets.bindless.slotsBuffer.get(),
    };
    if(!m_deferredLightingBaseStateHandoff.buildResourceSubset(
        m_postGbufferNormalizedStateHandoff,
        deferredLightingBaseTextures,
        LengthOf(deferredLightingBaseTextures),
        deferredLightingBaseBuffers,
        LengthOf(deferredLightingBaseBuffers)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-lighting base state selection failed"));
        discardRenderPackets();
        return;
    }
    if(!m_shadowVisibilityLightingStateHandoff.buildTextureSubset(
        m_shadowVisibilityStateHandoff,
        deferredTargets.shadowVisibility.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the shadow visibility lighting handoff"));
        discardRenderPackets();
        return;
    }
    if(!m_causticIrradianceLightingStateHandoff.buildTextureSubset(
        m_causticsStateHandoff,
        deferredTargets.causticIrradiance.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the caustic irradiance lighting handoff"));
        discardRenderPackets();
        return;
    }
    if(!m_surfelIrradianceLightingStateHandoff.buildTextureSubset(
        m_surfelGiStateHandoff,
        deferredTargets.surfelIrradiance.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the surfel irradiance lighting handoff"));
        discardRenderPackets();
        return;
    }
    Core::Texture* const avboitLightingTextures[] = {
        deferredTargets.albedo.get(),
        deferredTargets.normal.get(),
        deferredTargets.worldPosition.get(),
        deferredTargets.depth.get(),
    };
    if(!m_avboitLightingStateHandoff.buildResourceSubset(
        m_avboitStateHandoff,
        avboitLightingTextures,
        LengthOf(avboitLightingTextures),
        nullptr,
        0u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the AVBOIT lighting inputs"));
        discardRenderPackets();
        return;
    }

    const Core::CommandListResourceStateHandoff* deferredLightingBranchStates[4] = {};
    usize deferredLightingBranchCount = 0u;
    if(!laggedAsyncLightingSchedule){
        deferredLightingBranchStates[deferredLightingBranchCount++] = &m_shadowVisibilityLightingStateHandoff;
        deferredLightingBranchStates[deferredLightingBranchCount++] = &m_causticIrradianceLightingStateHandoff;
        deferredLightingBranchStates[deferredLightingBranchCount++] = &m_surfelIrradianceLightingStateHandoff;
    }
    deferredLightingBranchStates[deferredLightingBranchCount++] = &m_avboitLightingStateHandoff;
    if(!m_deferredLightingInputStateHandoff.buildFanIn(
        m_deferredLightingBaseStateHandoff,
        deferredLightingBranchStates,
        deferredLightingBranchCount
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-lighting state fan-in failed"));
        discardRenderPackets();
        return;
    }

    // Deferred lighting consumes the ray-effect outputs after AVBOIT has finished touching the shared G-buffer. The
    // normal dedicated path stays on AsyncCompute; once a history snapshot is accepted the opt-in path instead uses
    // Graphics and only reads the immutable prior-frame ray-effect images.
    bool deferredLightingCommandListReady = false;
    const Core::Graphics::JobHandle deferredLightingRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        deferredLightingCommandList,
        &deferredLightingCommandListReady,
        laggedAsyncLightingSchedule,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::DeferredLighting
        );
        deferredLightingCommandList->open(&m_deferredLightingInputStateHandoff);
        if(!deferredLightingCommandList->hasCommandBuffer())
            return;

        const bool deferredLightingRecorded = m_deferredSystem.renderDeferredLighting(
            *deferredLightingCommandList,
            deferredTargets,
            laggedAsyncLightingSchedule
        );
        deferredLightingCommandList->close(&m_deferredLightingStateHandoff);
        deferredLightingCommandListReady =
            deferredLightingRecorded
            && m_deferredLightingStateHandoff.valid()
            && deferredLightingCommandList->hasCommandBuffer()
        ;
    });
    if(!deferredLightingRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(deferredLightingRecordingJob);
    if(!deferredLightingCommandListReady){
        discardRenderPackets();
        return;
    }

    if(!m_opaqueColorCompositeStateHandoff.buildTextureSubset(
        m_deferredLightingStateHandoff,
        deferredTargets.opaqueColor.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the deferred-lighting output for Compute composite"));
        discardRenderPackets();
        return;
    }
    Core::Texture* const avboitCompositeTextures[] = {
        deferredTargets.avboit.accumColor.get(),
        deferredTargets.avboit.accumExtinction.get(),
    };
    if(!m_avboitCompositeStateHandoff.buildResourceSubset(
        m_avboitStateHandoff,
        avboitCompositeTextures,
        LengthOf(avboitCompositeTextures),
        nullptr,
        0u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the AVBOIT composite inputs"));
        discardRenderPackets();
        return;
    }
    Core::Buffer* const deferredCompositeBaseBuffers[] = {
        deferredTargets.bindless.slotsBuffer.get(),
    };
    if(!m_deferredCompositeBaseStateHandoff.buildResourceSubset(
        m_deferredLightingBaseStateHandoff,
        nullptr,
        0u,
        deferredCompositeBaseBuffers,
        LengthOf(deferredCompositeBaseBuffers)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-composite base state selection failed"));
        discardRenderPackets();
        return;
    }
    const Core::CommandListResourceStateHandoff* deferredCompositeBranchStates[] = {
        &m_avboitCompositeStateHandoff,
        &m_opaqueColorCompositeStateHandoff,
    };
    if(!m_deferredCompositeInputStateHandoff.buildFanIn(
        m_deferredCompositeBaseStateHandoff,
        deferredCompositeBranchStates,
        2u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-composite state fan-in failed"));
        discardRenderPackets();
        return;
    }

    bool deferredCompositeCommandListReady = false;
    const Core::Graphics::JobHandle deferredCompositeRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        &deferredTargets,
        deferredCompositeCommandList,
        &deferredCompositeCommandListReady,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::DeferredComposite
        );
        deferredCompositeCommandList->open(&m_deferredCompositeInputStateHandoff);
        if(!deferredCompositeCommandList->hasCommandBuffer())
            return;

        const bool deferredCompositeRecorded = m_deferredSystem.renderDeferredComposite(*deferredCompositeCommandList, deferredTargets);
        deferredCompositeCommandList->close(&m_deferredCompositeStateHandoff);
        deferredCompositeCommandListReady =
            deferredCompositeRecorded
            && m_deferredCompositeStateHandoff.valid()
            && deferredCompositeCommandList->hasCommandBuffer()
        ;
    });
    if(!deferredCompositeRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(deferredCompositeRecordingJob);
    if(!deferredCompositeCommandListReady){
        discardRenderPackets();
        return;
    }

    if(!m_compositeColorPresentStateHandoff.buildTextureSubset(
        m_deferredCompositeStateHandoff,
        deferredTargets.compositeColor.get()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to isolate the Compute composite output for Graphics presentation"));
        discardRenderPackets();
        return;
    }
    Core::Buffer* const deferredPresentBaseBuffers[] = {
        deferredTargets.bindless.slotsBuffer.get(),
    };
    if(!m_deferredPresentBaseStateHandoff.buildResourceSubset(
        m_deferredCompositeBaseStateHandoff,
        nullptr,
        0u,
        deferredPresentBaseBuffers,
        LengthOf(deferredPresentBaseBuffers)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-present base state selection failed"));
        discardRenderPackets();
        return;
    }
    const Core::CommandListResourceStateHandoff* deferredPresentBranchStates[] = {
        &m_compositeColorPresentStateHandoff,
    };
    if(!m_deferredPresentInputStateHandoff.buildFanIn(
        m_deferredPresentBaseStateHandoff,
        deferredPresentBranchStates,
        1u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred-present state fan-in failed"));
        discardRenderPackets();
        return;
    }

    bool deferredPresentCommandListReady = false;
    const Core::Graphics::JobHandle deferredPresentRecordingJob = m_graphics.scheduleGraphicsJob([
        this,
        framebuffer,
        &deferredTargets,
        deferredPresentCommandList,
        &frameTiming,
        &asyncFrameTiming,
        &asyncFinalTiming,
        &deferredPresentCommandListReady,
        asyncShadowSchedule,
        &frameExecutionTimingTickets
    ](){
        ECSRenderDetail::FrameExecutionPlanTimingTickets::WorkRecordingScope timingRecording(
            frameExecutionTimingTickets,
            ECSRenderDetail::FrameExecutionWork::GraphicsPresent
        );
        deferredPresentCommandList->open(&m_deferredPresentInputStateHandoff);
        if(!deferredPresentCommandList->hasCommandBuffer())
            return;

        if(asyncShadowSchedule){
            asyncFinalTiming.emplace(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncFinal,
                m_graphics.getDevice(),
                *deferredPresentCommandList
            );
            asyncFinalTiming->finishMarker();
        }

        const bool deferredPresentRecorded = m_deferredSystem.renderDeferredPresent(
            *deferredPresentCommandList,
            deferredTargets,
            framebuffer
        );

        bool asyncFrameTimingEnded = true;
        // This endpoint completes the legacy one-submission frame scope and records the dedicated path's deferred
        // critical-path endpoint. The latter is not published until Graphics presentation accepts below.
        if(frameTiming)
            frameTiming->finishTiming(*deferredPresentCommandList);
        if(asyncShadowSchedule && deferredPresentRecorded){
            if(asyncFinalTiming){
                asyncFinalTiming->finishTiming(*deferredPresentCommandList);
                asyncFinalTiming.reset();
            }
            asyncFrameTimingEnded = asyncFrameTiming.recordEnd(*deferredPresentCommandList);
        }
        deferredPresentCommandList->close(&m_deferredPresentStateHandoff);
        deferredPresentCommandListReady =
            deferredPresentRecorded
            && asyncFrameTimingEnded
            && m_deferredPresentStateHandoff.valid()
            && deferredPresentCommandList->hasCommandBuffer()
        ;
    });
    if(!deferredPresentRecordingJob.valid()){
        discardRenderPackets();
        return;
    }

    m_graphics.waitJob(deferredPresentRecordingJob);
    if(!deferredPresentCommandListReady){
        discardRenderPackets();
        return;
    }

    // Record jobs can run in parallel, but packet membership must retain the established submission order. Append the
    // already-recorded lists on this thread rather than letting worker completion order choose it. This one ordered
    // binding table is the renderer-side declaration for ordinary frame work; the plan routes each enabled entry to
    // its packet, including the single-packet Graphics fallback. The lagged-history stash remains below because it is
    // only recorded after presentation is accepted.
    ECSRenderDetail::FrameExecutionPacketCommandLists frameExecutionCommandLists(frameExecutionPlan);
    const ECSRenderDetail::FrameExecutionWorkCommandListBinding recordedFrameWorkCommandLists[] = {
        { ECSRenderDetail::FrameExecutionWork::GraphicsPrefix, meshViewSetupCommandList },
        { ECSRenderDetail::FrameExecutionWork::GraphicsPrefix, sceneShadingSetupCommandList },
        { ECSRenderDetail::FrameExecutionWork::GraphicsPrefix, deferredClearCommandList },
        { ECSRenderDetail::FrameExecutionWork::GraphicsPrefix, gbufferCommandList },
        { ECSRenderDetail::FrameExecutionWork::GraphicsPrefix, postGbufferNormalizeCommandList },
        { ECSRenderDetail::FrameExecutionWork::AsyncEffectsTiming, asyncEffectsTimingBeginCommandList },
        { ECSRenderDetail::FrameExecutionWork::RayEffects, shadowVisibilityCommandList },
        { ECSRenderDetail::FrameExecutionWork::Caustics, causticsCommandList },
        { ECSRenderDetail::FrameExecutionWork::SurfelGi, surfelGiCommandList },
        { ECSRenderDetail::FrameExecutionWork::AvboitRaster, avboitCommandList },
        { ECSRenderDetail::FrameExecutionWork::AsyncEffectsTiming, asyncEffectsTimingEndCommandList },
        { ECSRenderDetail::FrameExecutionWork::AvboitDepthWarp, asyncAvboitDepthWarpCommandList },
        { ECSRenderDetail::FrameExecutionWork::AvboitExtinction, avboitExtinctionCommandList },
        { ECSRenderDetail::FrameExecutionWork::AvboitIntegration, asyncAvboitIntegrationCommandList },
        { ECSRenderDetail::FrameExecutionWork::AvboitAccumulation, avboitAccumulateCommandList },
        { ECSRenderDetail::FrameExecutionWork::DeferredLighting, deferredLightingCommandList },
        { ECSRenderDetail::FrameExecutionWork::DeferredComposite, deferredCompositeCommandList },
        { ECSRenderDetail::FrameExecutionWork::GraphicsPresent, deferredPresentCommandList },
    };
    if(!frameExecutionCommandLists.appendPlannedWorkCommandLists(
        recordedFrameWorkCommandLists,
        LengthOf(recordedFrameWorkCommandLists)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to collect recorded work for the frame execution plan"));
        discardRenderPackets();
        return;
    }

    // Keep submission topology declarative: the per-frame state resolves accepted predecessor and external-token
    // edges, then exposes the outstanding AsyncCompute recovery edge. The renderer still owns every
    // acceptance-dependent state commit and recovery lifecycle decision below.
    ECSRenderDetail::FrameExecutionExternalWaitTokens frameExecutionExternalWaitTokens;
    frameExecutionExternalWaitTokens.tokens[static_cast<usize>(
        ECSRenderDetail::FrameExecutionExternalWait::LaggedLightingHistory
    )] = m_laggedLightingHistorySubmissionToken;
    ECSRenderDetail::FrameExecutionPlanSubmissionState frameExecutionSubmissionState(
        frameExecutionPlan,
        frameExecutionExternalWaitTokens
    );
    const auto submitPlannedFramePacket = [&](
        const ECSRenderDetail::FrameExecutionPacket::Enum packet,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Core::CommandList* const* commandLists,
        const usize commandListCount
    ) -> Core::QueueSubmissionToken {
        Core::QueueSubmissionToken waitTokens[ECSRenderDetail::FrameExecutionPlan::s_MaxSubmissionWaits] = {};
        Core::QueueSubmissionDesc submitDesc;
        if(!frameExecutionSubmissionState.prepareSubmission(
            packet,
            submitDesc,
            waitTokens,
            LengthOf(waitTokens)
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame execution plan dependency was not accepted"));
            timingTicket.discard();
            return {};
        }
        const Core::QueueSubmissionToken submissionToken = timingTicket.submit(
            device,
            commandLists,
            commandListCount,
            frameExecutionPlan.packet(packet).lane,
            submitDesc
        );
        if(submissionToken.valid())
            frameExecutionSubmissionState.acceptSubmission(packet, submissionToken);
        return submissionToken;
    };
    const auto executePlannedFramePacket = [&](
        const ECSRenderDetail::FrameExecutionPacket::Enum packet,
        Core::CommandList* const* commandLists,
        const usize commandListCount
    ) -> Core::QueueSubmissionToken {
        Core::QueueSubmissionToken waitTokens[ECSRenderDetail::FrameExecutionPlan::s_MaxSubmissionWaits] = {};
        Core::QueueSubmissionDesc submitDesc;
        if(!frameExecutionSubmissionState.prepareSubmission(
            packet,
            submitDesc,
            waitTokens,
            LengthOf(waitTokens)
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame execution plan dependency was not accepted"));
            return {};
        }
        const Core::QueueSubmissionToken submissionToken = device.executeCommandLists(
            commandLists,
            commandListCount,
            frameExecutionPlan.packet(packet).lane,
            submitDesc
        );
        if(submissionToken.valid())
            frameExecutionSubmissionState.acceptSubmission(packet, submissionToken);
        return submissionToken;
    };
    const auto submitRecordedFramePacket = [&](const ECSRenderDetail::FrameExecutionPacket::Enum packet)
        -> Core::QueueSubmissionToken {
        const ECSRenderDetail::FrameExecutionPacketCommandListRange packetCommandLists =
            frameExecutionCommandLists.commandLists(packet)
        ;
        Core::GpuTimingSubmissionTicket* const timingTicket = frameExecutionTimingTickets.ticketForPacket(packet);
        if(!timingTicket || packetCommandLists.commandListCount == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame execution packet has no timing ticket or recorded command list"));
            if(timingTicket)
                timingTicket->discard();
            return {};
        }
        return submitPlannedFramePacket(
            packet,
            *timingTicket,
            packetCommandLists.commandLists,
            packetCommandLists.commandListCount
        );
    };
    const auto executeRecordedFramePacket = [&](const ECSRenderDetail::FrameExecutionPacket::Enum packet)
        -> Core::QueueSubmissionToken {
        const ECSRenderDetail::FrameExecutionPacketCommandListRange packetCommandLists =
            frameExecutionCommandLists.commandLists(packet)
        ;
        if(packetCommandLists.commandListCount == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frame execution packet has no recorded command list"));
            return {};
        }
        // Timed packets reject a missing command buffer through their ticket. Keep execution-only packets equally
        // strict so a future plan entry cannot turn an absent list into a synchronization-only submission.
        for(usize commandListIndex = 0u;
            commandListIndex < packetCommandLists.commandListCount;
            ++commandListIndex
        ){
            Core::CommandList* const commandList = packetCommandLists.commandLists[commandListIndex];
            if(!commandList || !commandList->hasCommandBuffer()){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: execution-only frame packet has no recorded command list"));
                return {};
            }
        }
        return executePlannedFramePacket(
            packet,
            packetCommandLists.commandLists,
            packetCommandLists.commandListCount
        );
    };
    // Packet timing identity is declarative. Route ordinary and execution-only packets through their plan flag so a
    // later execution-only work item cannot accidentally require a timing ticket at its call site.
    const auto dispatchRecordedFramePacket = [&](const ECSRenderDetail::FrameExecutionPacket::Enum packet)
        -> Core::QueueSubmissionToken {
        return frameExecutionPlan.packet(packet).recordsTiming
            ? submitRecordedFramePacket(packet)
            : executeRecordedFramePacket(packet)
        ;
    };
    const auto dispatchRecordedFrameBatch = [&](const ECSRenderDetail::FrameExecutionSubmissionBatch::Enum batch)
        -> Core::QueueSubmissionToken {
        const ECSRenderDetail::FrameExecutionSubmissionBatchPlan& batchPlan =
            frameExecutionPlan.submissionBatch(batch)
        ;
        NWB_ASSERT(batchPlan.packetCount > 0u);

        Core::QueueSubmissionToken lastSubmissionToken;
        for(u8 packetIndex = 0u; packetIndex < batchPlan.packetCount; ++packetIndex){
            const ECSRenderDetail::FrameExecutionPacket::Enum packet = batchPlan.packets[packetIndex];
            lastSubmissionToken = dispatchRecordedFramePacket(packet);
            if(!lastSubmissionToken.valid())
                return {};
        }
        return lastSubmissionToken;
    };

    const auto submitAsyncRecoveryJoin = [&](const Core::QueueSubmissionToken* waitToken) -> bool {
        // A rejected dependent packet can leave accepted AsyncCompute work in flight. All remaining cross-lane
        // resources are concurrently shared, so a Graphics join establishes execution order and retires the frame
        // timestamp without attempting an unnecessary queue-family ownership repair.
        if(device.isDeviceLost()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: async recovery join skipped because the graphics device is lost"));
            asyncFrameTiming.discard();
            return false;
        }
        NWB_ASSERT(!waitToken || waitToken->valid());

        const bool retireTiming = asyncFrameTiming.needsRetirement();
        if(retireTiming)
            asyncFrameTiming.prepareForRecovery();
        asyncRecoveryCommandList->open();
        if(!asyncRecoveryCommandList->hasCommandBuffer()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to record async recovery join"));
            asyncFrameTiming.discard();
            return false;
        }
        if(retireTiming && !asyncFrameTiming.recordEnd(*asyncRecoveryCommandList)){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to write async recovery timing endpoint"));
            asyncFrameTiming.discard();
            return false;
        }
        asyncRecoveryCommandList->close();
        if(!asyncRecoveryCommandList->hasCommandBuffer()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: failed to close async recovery join"));
            asyncFrameTiming.discard();
            return false;
        }

        Core::QueueSubmissionDesc recoverySubmitDesc;
        if(waitToken)
            recoverySubmitDesc.setWaitTokens(waitToken, 1u);
        Core::CommandList* recoveryCommandLists[] = { asyncRecoveryCommandList };
        const Core::QueueSubmissionToken recoveryToken = device.executeCommandLists(
            recoveryCommandLists,
            1u,
            Core::RenderLane::Graphics,
            recoverySubmitDesc
        );
        if(!recoveryToken.valid()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: async recovery join submission was rejected"));
            asyncFrameTiming.discard();
            return false;
        }
        if(retireTiming && !asyncFrameTiming.confirmEndSubmission(false)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire async recovery timing query"));
            asyncFrameTiming.discard();
        }
        return true;
    };
    const auto retireAsyncFrameTiming = [&](){
        if(asyncFrameTiming.needsRetirement())
            submitAsyncRecoveryJoin(nullptr);
    };
    const auto recoverPendingAsyncComputeSubmission = [&]() -> bool {
        const Core::QueueSubmissionToken* const waitToken = frameExecutionSubmissionState.asyncRecoveryWaitToken();
        return !waitToken || submitAsyncRecoveryJoin(waitToken);
    };
    const auto failAsyncRenderRecovery = [&](){
        if(m_asyncRenderRecoveryFailed)
            return;
        m_asyncRenderRecoveryFailed = true;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("RendererSystem: cannot safely continue after an unresolved async Compute recovery join; requesting device recreation"));
        // The Graphics owner performs orderly teardown/recreation after this frame returns, rather than invalidating
        // resources while accepted Compute work may still be in flight.
        m_graphics.requestDeviceRecreation();
    };

    // Iterate the topology's ordered batches. The plan determines packet order and grouping; these cases retain only
    // the acceptance-dependent temporal commits, rollback, and resource-state handoff work owned by RendererSystem.
    for(usize submissionBatchIndex = 0u;
        submissionBatchIndex < frameExecutionPlan.submissionBatchCount();
        ++submissionBatchIndex
    ){
        const ECSRenderDetail::FrameExecutionSubmissionBatch::Enum submissionBatch =
            frameExecutionPlan.submissionBatchID(submissionBatchIndex)
        ;
        switch(submissionBatch){
        case ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsFallback:{
            // The plan emits the same one-packet Graphics batch used by the established fallback. Keep its
            // acceptance-dependent timing and temporal commits here with the other batch-specific lifecycle work.
            const Core::QueueSubmissionToken fallbackSubmissionToken = dispatchRecordedFrameBatch(
                ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsFallback
            );
            if(!fallbackSubmissionToken.valid()){
                discardRenderPackets();
                return;
            }

            frameTiming.reset();
            m_raytracingSystem.confirmShadowVisibilitySubmission(fallbackSubmissionToken);
            m_raytracingSystem.confirmSurfelGiSubmission(fallbackSubmissionToken);
            m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
            if(m_frameLaggedAsyncLightingEnabled){
                reportLaggedLightingTransition(
                    LaggedLightingReport::NoDedicatedAsyncCompute,
                    deferredTargets.laggedLightingHistory.generation
                );
            }
            else if(m_laggedLightingCurrentFrameFallbackPending){
                reportLaggedLightingTransition(
                    LaggedLightingReport::CurrentFrameFallbackAccepted,
                    deferredTargets.laggedLightingHistory.generation
                );
                m_laggedLightingCurrentFrameFallbackPending = false;
            }
            return;
        }
        case ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsPrefix:{
            // Distinct queues submit the Graphics producer first. The following Compute producer waits on this batch.
            const Core::QueueSubmissionToken prefixSubmissionToken = dispatchRecordedFrameBatch(
                ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsPrefix
            );
            if(!prefixSubmissionToken.valid()){
                discardRenderPackets();
                return;
            }
            asyncFrameTiming.confirmBeginSubmission();

            break;
        }
        case ECSRenderDetail::FrameExecutionSubmissionBatch::AsyncRayEffects:{
            // Shadow, software caustics when selected, and Compute-only surfel GI share one accepted Compute packet.
            // Hardware caustics instead run in the independent Graphics-support packet; Graphics final waits for both
            // packet branches.
            const Core::QueueSubmissionToken shadowSubmissionToken = dispatchRecordedFrameBatch(
                ECSRenderDetail::FrameExecutionSubmissionBatch::AsyncRayEffects
            );
            if(!shadowSubmissionToken.valid()){
                discardTimingTickets();
                restoreShadowCpuState();
                restoreEffectsCpuState();
                m_raytracingSystem.discardSoftShadowTemporalHistory();
                m_raytracingSystem.discardSurfelResourceInitialization();
                resetRejectedAsyncRayEffectsStateHandoffs();
                retireAsyncFrameTiming();
                return;
            }

            // The Compute command buffer is now committed. Retain only its private scratch/history state for the next
            // Compute use: shared G-buffer and scene inputs must always come from this frame's Graphics prefix rather than
            // allowing a prior Compute handoff to overwrite their current state during the next fan-in.
            m_raytracingSystem.confirmShadowVisibilitySubmission(shadowSubmissionToken);
            m_raytracingSystem.confirmSurfelGiSubmission(shadowSubmissionToken);
            m_raytracingSystem.finalizeSurfelResourceInitialization();

            // Preserve the current producer state as soon as its shared Compute packet is accepted. If a later Graphics or
            // lighting submission is rejected, the next frame can still reopen these exclusive results on AsyncCompute.
            const bool producerReturnStatesReady =
                m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                    m_shadowVisibilityStateHandoff,
                    deferredTargets.shadowVisibility.get()
                )
                && (!asyncCausticsSchedule || m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                    m_causticsStateHandoff,
                    deferredTargets.causticIrradiance.get()
                ))
                && (!asyncSurfelGiSchedule || m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                    m_surfelGiStateHandoff,
                    deferredTargets.surfelIrradiance.get()
                ))
            ;
            if(!producerReturnStatesReady){
                discardTimingTickets();
                restoreUnacceptedGraphicsEffectsCpuState();
                m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
                if(!recoverPendingAsyncComputeSubmission())
                    failAsyncRenderRecovery();
                // The accepted producer state could not be retained, so continuing would require guessing its next layout.
                failAsyncRenderRecovery();
                return;
            }
            Core::Texture* const shadowComputeScratchTextures[] = {
                deferredTargets.shadowCoarseTransmittance.get(),
                deferredTargets.shadowSoftHalfA.get(),
                deferredTargets.shadowSoftHalfB.get(),
                deferredTargets.shadowSoftGeometry.get(),
                deferredTargets.shadowSoftGeometryPrev.get(),
                deferredTargets.shadowHistA.get(),
                deferredTargets.shadowHistB.get(),
                deferredTargets.shadowMomentsA.get(),
                deferredTargets.shadowMomentsB.get(),
                deferredTargets.transparentSoftHalf.get(),
                deferredTargets.transparentHistA.get(),
                deferredTargets.transparentHistB.get(),
                deferredTargets.transparentMomentsA.get(),
                deferredTargets.transparentMomentsB.get(),
            };
            Core::Buffer* const shadowComputeScratchBuffers[] = {
                m_rayTracingState.m_swShadowEdgeStatsBuffer.get(),
                m_rayTracingState.m_swShadowEdgeStatsReadback.get(),
                m_rayTracingState.m_swShadowEdgeCounterBuffer.get(),
                m_rayTracingState.m_swShadowEdgeListBuffer.get(),
                m_rayTracingState.m_swShadowIndirectArgsBuffer.get(),
            };
            if(!m_shadowComputePersistentStateHandoff.buildResourceSubset(
                m_shadowVisibilityStateHandoff,
                shadowComputeScratchTextures,
                LengthOf(shadowComputeScratchTextures),
                shadowComputeScratchBuffers,
                LengthOf(shadowComputeScratchBuffers)
            )){
                discardTimingTickets();
                restoreUnacceptedGraphicsEffectsCpuState();
                m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);
                recoverPendingAsyncComputeSubmission();
                // Without a retained Compute-side scratch snapshot the next packet cannot safely restore its layouts, even
                // after the accepted Compute work has been joined.
                failAsyncRenderRecovery();
                return;
            }
            m_raytracingSystem.finalizeSoftShadowTemporalHistory(deferredTargets);

            if(asyncCausticsSchedule){
                Core::Texture* const causticsComputeScratchTextures[] = {
                    deferredTargets.causticAccumulator.get(),
                    deferredTargets.causticHistory.get(),
                    deferredTargets.causticResolveHalf.get(),
                    deferredTargets.causticResolveGeometry.get(),
                };
                if(!m_causticsComputePersistentStateHandoff.buildResourceSubset(
                    m_causticsStateHandoff,
                    causticsComputeScratchTextures,
                    LengthOf(causticsComputeScratchTextures),
                    nullptr,
                    0u
                )){
                    discardTimingTickets();
                    restoreGraphicsEffectsCpuState();
                    recoverPendingAsyncComputeSubmission();
                    failAsyncRenderRecovery();
                    return;
                }
            }

            if(asyncSurfelGiSchedule){
                Core::Texture* const surfelGiComputeScratchTextures[] = {
                    deferredTargets.surfelIrradianceHalf.get(),
                };
                Core::Buffer* const surfelGiComputeScratchBuffers[] = {
                    m_rayTracingState.m_surfelPoolBuffer.get(),
                    m_rayTracingState.m_surfelCellHeadBuffer.get(),
                    m_rayTracingState.m_surfelCounterBuffer.get(),
                    m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(),
                    m_rayTracingState.m_surfelFreeListBuffer.get(),
                    m_rayTracingState.m_surfelPoolSnapshotBuffer.get(),
                    m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(),
                    m_rayTracingState.m_surfelCounterReadback.get(),
                };
                if(!m_surfelGiComputePersistentStateHandoff.buildResourceSubset(
                    m_surfelGiStateHandoff,
                    surfelGiComputeScratchTextures,
                    LengthOf(surfelGiComputeScratchTextures),
                    surfelGiComputeScratchBuffers,
                    LengthOf(surfelGiComputeScratchBuffers)
                )){
                    discardTimingTickets();
                    restoreUnacceptedGraphicsEffectsCpuState();
                    recoverPendingAsyncComputeSubmission();
                    failAsyncRenderRecovery();
                    return;
                }
            }

            break;
        }
        case ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsEffects:{
            // The plan keeps either the one-packet Graphics path or the five-packet AVBOIT chain together. Its
            // declared order retains the required Graphics -> Compute alternation without a renderer-side sequence.
            const Core::QueueSubmissionToken graphicsEffectsSubmissionToken = dispatchRecordedFrameBatch(
                ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsEffects
            );
            if(!graphicsEffectsSubmissionToken.valid()){
                discardTimingTickets();
                // Before this batch accepts a packet, its CPU state is still recoverable. Once it has started,
                // FrameExecutionPlanSubmissionState owns the accepted-token boundary used by the recovery path.
                if(!frameExecutionSubmissionState.batchHasAcceptedPacket(
                    ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsEffects
                ))
                    restoreUnacceptedGraphicsEffectsCpuState();
                if(!recoverPendingAsyncComputeSubmission())
                    failAsyncRenderRecovery();
                return;
            }

            break;
        }
        case ECSRenderDetail::FrameExecutionSubmissionBatch::DeferredLighting:{
            // The normal dedicated path keeps lighting/composite behind the producer on AsyncCompute. Once the optional
            // history is accepted, Graphics waits only for the previous stash and shades current G-buffer data in parallel
            // with this frame's producer; AVBOIT stays entirely on Graphics in that mode to avoid queueing behind the producer.
            const Core::QueueSubmissionToken lightingSubmissionToken = dispatchRecordedFrameBatch(
                ECSRenderDetail::FrameExecutionSubmissionBatch::DeferredLighting
            );
            if(!lightingSubmissionToken.valid()){
                discardTimingTickets();
                if(!recoverPendingAsyncComputeSubmission())
                    failAsyncRenderRecovery();
                return;
            }
            if(laggedAsyncLightingSchedule)
                deferredTargets.laggedLightingHistory.slotsUploaded = true;
            const bool lightingReturnStatesReady = laggedAsyncLightingSchedule || (
                m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                    m_deferredLightingStateHandoff,
                    deferredTargets.shadowVisibility.get()
                )
                // The bootstrap lighting pass consumes the live caustic irradiance whether its producer was the
                // AsyncCompute software path or the Graphics hardware path. Retain that post-lighting layout for
                // the history stash; the active lagged path instead sources its live image directly from the
                // current producer below.
                && m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                    m_deferredLightingStateHandoff,
                    deferredTargets.causticIrradiance.get()
                )
                && (!asyncSurfelGiSchedule || m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                    m_deferredLightingStateHandoff,
                    deferredTargets.surfelIrradiance.get()
                ))
            );
            if(!lightingReturnStatesReady){
                discardTimingTickets();
                if(!recoverPendingAsyncComputeSubmission())
                    failAsyncRenderRecovery();
                // The accepted lighting packet replaced the producer layouts, so a failed retained snapshot is terminal.
                failAsyncRenderRecovery();
                return;
            }
            if(laggedAsyncLightingSchedule){
                reportLaggedLightingTransition(
                    LaggedLightingReport::ActiveHistoryAccepted,
                    deferredTargets.laggedLightingHistory.generation
                );
            }

            break;
        }
        case ECSRenderDetail::FrameExecutionSubmissionBatch::DeferredComposite:{
            // The selected lane orders composite after lighting. Its token remains the presentation dependency in both modes;
            // in the lagged mode that is simply a Graphics-to-Graphics order edge while AsyncCompute continues the producer.
            const Core::QueueSubmissionToken compositeSubmissionToken = dispatchRecordedFrameBatch(
                ECSRenderDetail::FrameExecutionSubmissionBatch::DeferredComposite
            );
            if(!compositeSubmissionToken.valid()){
                discardTimingTickets();
                if(!recoverPendingAsyncComputeSubmission())
                    failAsyncRenderRecovery();
                return;
            }
            break;
        }
        case ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsPresent:{
            // History removes the producer from the current lighting dependency, not from the frame-lifetime dependency. The
            // final Graphics packet still waits for this frame's Async producer before the next frame can rewrite shared scene
            // inputs; that preserves the existing cross-frame resource contract while exposing the useful overlap above.
            const Core::QueueSubmissionToken finalSubmissionToken = dispatchRecordedFrameBatch(
                ECSRenderDetail::FrameExecutionSubmissionBatch::GraphicsPresent
            );
            if(!finalSubmissionToken.valid()){
                if(!recoverPendingAsyncComputeSubmission())
                    failAsyncRenderRecovery();
                return;
            }
            if(!asyncFrameTiming.confirmEndSubmission(true)){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to confirm async frame critical-path timing"));
                asyncFrameTiming.discard();
            }
            if(m_laggedLightingCurrentFrameFallbackPending){
                reportLaggedLightingTransition(
                    LaggedLightingReport::CurrentFrameFallbackAccepted,
                    deferredTargets.laggedLightingHistory.generation
                );
                m_laggedLightingCurrentFrameFallbackPending = false;
            }

            break;
        }
        default:
            NWB_ASSERT(false);
            break;
        }
    }

    if(captureLaggedLightingHistory){
        // The capture imports the live producer results after their last current-frame consumer. In the bootstrap the
        // consumer was AsyncCompute lighting; in the active mode it was not touched by Graphics lighting at all. The
        // final Graphics token is the conservative read-complete edge in both cases, while AsyncCompute queue order
        // already places this copy after the current producer submission. In active mode the live caustic image
        // comes directly from the current producer: hardware caustics ran on Graphics and therefore never populated
        // the Async lighting return handoff.
        const Core::CommandListResourceStateHandoff* const causticStashSource = laggedAsyncLightingSchedule
            ? &m_causticIrradianceLightingStateHandoff
            : &m_causticIrradianceReturnStateHandoff
        ;
        const Core::CommandListResourceStateHandoff* const stashBranches[] = {
            causticStashSource,
            &m_surfelIrradianceReturnStateHandoff,
        };
        const bool stashInputReady = m_laggedLightingStashInputStateHandoff.buildFanIn(
            m_shadowVisibilityReturnStateHandoff,
            stashBranches,
            LengthOf(stashBranches)
        );
        if(!stashInputReady){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history capture skipped because its source state was unavailable"));
            invalidateLaggedLightingHistorySubmission();
        }
        else{
            asyncLaggedLightingStashCommandList->open(&m_laggedLightingStashInputStateHandoff);
            bool stashRecorded = asyncLaggedLightingStashCommandList->hasCommandBuffer();
            if(stashRecorded){
                DeferredLaggedLightingHistoryResources& history = deferredTargets.laggedLightingHistory;
                stashRecorded = history.valid();
                if(stashRecorded){
                    asyncLaggedLightingStashCommandList->setTextureState(
                        deferredTargets.shadowVisibility.get(),
                        ECSRenderDetail::s_ShadowVisibilitySubresources,
                        Core::ResourceStates::CopySource
                    );
                    asyncLaggedLightingStashCommandList->setTextureState(
                        history.shadowVisibility.get(),
                        ECSRenderDetail::s_ShadowVisibilitySubresources,
                        Core::ResourceStates::CopyDest
                    );
                    asyncLaggedLightingStashCommandList->setTextureState(
                        deferredTargets.causticIrradiance.get(),
                        ECSRenderDetail::s_FramebufferSubresources,
                        Core::ResourceStates::CopySource
                    );
                    asyncLaggedLightingStashCommandList->setTextureState(
                        history.causticIrradiance.get(),
                        ECSRenderDetail::s_FramebufferSubresources,
                        Core::ResourceStates::CopyDest
                    );
                    asyncLaggedLightingStashCommandList->setTextureState(
                        deferredTargets.surfelIrradiance.get(),
                        ECSRenderDetail::s_FramebufferSubresources,
                        Core::ResourceStates::CopySource
                    );
                    asyncLaggedLightingStashCommandList->setTextureState(
                        history.surfelIrradiance.get(),
                        ECSRenderDetail::s_FramebufferSubresources,
                        Core::ResourceStates::CopyDest
                    );
                    asyncLaggedLightingStashCommandList->commitBarriers();

                    for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
                        Core::TextureSlice shadowSlice;
                        shadowSlice.setArraySlice(shadowSlot);
                        asyncLaggedLightingStashCommandList->copyTexture(
                            history.shadowVisibility.get(),
                            shadowSlice,
                            deferredTargets.shadowVisibility.get(),
                            shadowSlice
                        );
                    }
                    const Core::TextureSlice irradianceSlice;
                    asyncLaggedLightingStashCommandList->copyTexture(
                        history.causticIrradiance.get(),
                        irradianceSlice,
                        deferredTargets.causticIrradiance.get(),
                        irradianceSlice
                    );
                    asyncLaggedLightingStashCommandList->copyTexture(
                        history.surfelIrradiance.get(),
                        irradianceSlice,
                        deferredTargets.surfelIrradiance.get(),
                        irradianceSlice
                    );
                }
            }
            asyncLaggedLightingStashCommandList->close(&m_laggedLightingStashStateHandoff);
            stashRecorded =
                stashRecorded
                && m_laggedLightingStashStateHandoff.valid()
                && asyncLaggedLightingStashCommandList->hasCommandBuffer()
            ;

            if(!stashRecorded){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record lagged lighting-history capture; reverting to current-frame lighting"));
                invalidateLaggedLightingHistorySubmission();
                resetLaggedLightingStashStateHandoffs();
            }
            else if(!frameExecutionCommandLists.appendForWork(
                ECSRenderDetail::FrameExecutionWork::LaggedLightingStash,
                asyncLaggedLightingStashCommandList
            )){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to append lagged lighting-history capture to its frame packet; reverting to current-frame lighting"));
                invalidateLaggedLightingHistorySubmission();
                resetLaggedLightingStashStateHandoffs();
            }
            else{
                const Core::QueueSubmissionToken stashSubmissionToken = dispatchRecordedFramePacket(
                    ECSRenderDetail::FrameExecutionPacket::AsyncLaggedLightingStash
                );
                if(!stashSubmissionToken.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: lagged lighting-history capture submission was rejected; reverting to current-frame lighting"));
                    invalidateLaggedLightingHistorySubmission();
                    resetLaggedLightingStashStateHandoffs();
                }
                else{
                    const bool stashReturnStatesReady =
                        m_shadowVisibilityReturnStateHandoff.buildTextureSubset(
                            m_laggedLightingStashStateHandoff,
                            deferredTargets.shadowVisibility.get()
                        )
                        && m_causticIrradianceReturnStateHandoff.buildTextureSubset(
                            m_laggedLightingStashStateHandoff,
                            deferredTargets.causticIrradiance.get()
                        )
                        && m_surfelIrradianceReturnStateHandoff.buildTextureSubset(
                            m_laggedLightingStashStateHandoff,
                            deferredTargets.surfelIrradiance.get()
                        )
                    ;
                    if(!stashReturnStatesReady){
                        if(!recoverPendingAsyncComputeSubmission())
                            failAsyncRenderRecovery();
                        // The accepted copy changed the producer-image layouts. Without an exported next-frame state,
                        // reusing either the history or the live output would require guessing.
                        failAsyncRenderRecovery();
                        return;
                    }

                    m_laggedLightingHistorySubmissionToken = stashSubmissionToken;
                    if(!laggedAsyncLightingSchedule){
                        reportLaggedLightingTransition(
                            LaggedLightingReport::BootstrapAccepted,
                            deferredTargets.laggedLightingHistory.generation
                        );
                    }
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

