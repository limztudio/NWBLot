// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/execute/frame_execute_lifecycle.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/shadow/light_space_shadow.h>

#include <core/task/gpu/packet_runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Persistent-state candidates, retained resource lists, and record/accept callbacks for the stateful normal-execution tasks of one frame.
// Contexts point at sibling members, so the set stays in place for the whole execution.
struct FrameExecuteStateLifecycleSet final : NoCopy{
    // Capacity, not a runtime packet-count assumption.
    static constexpr usize s_CallbackCapacity = 6u;
    // Scene TLAS backing and the three BVH sort buffers; the CSG trace context adds one.
    static constexpr usize s_ShadowPrepareFixedStateBufferCapacity = 4u;
    static constexpr usize s_ShadowPrepareCsgStateBufferCapacity = 5u;

    // Prep/prefix stay serial; later upload packets may use workers.
    Core::Alloc::ScratchArena shadowPrepareStateScratchArena;
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> meshAccelerationStateBuffers;
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> shadowPrepareLiveStateBuffers;
    // Build the sparse candidate after recording; commit on Shadow Preparation accept.
    Core::GpuPersistentResourceStateCache::Candidate shadowPrepareAcceptedStateCandidate;
    Core::Alloc::ScratchArena normalExecutionScratchArena;
    Core::TextureHandle shadowVisibilityReturnTextures[1];
    // Freeze the resource generation selected during graph declaration from preflight-owned light-space storage.
    LightSpaceShadowSnapshot lightSpaceShadowResources;
    Core::TextureHandle shadowComputeScratchTextures[15];
    Core::BufferHandle shadowComputeScratchBuffers[12];
    Core::GpuPersistentResourceStateCache::Candidate shadowVisibilityReturnStateCandidate;
    Core::GpuPersistentResourceStateCache::Candidate shadowComputeScratchStateCandidate;
    Core::TextureHandle causticsComputeScratchTextures[4];
    Core::TextureHandle causticIrradianceTextures[1];
    Core::GpuPersistentResourceStateCache::Candidate causticReturnStateCandidate;
    Core::GpuPersistentResourceStateCache::Candidate causticsScratchStateCandidate;
    Core::TextureHandle surfelIrradianceReturnTextures[1];
    Core::BufferHandle surfelGiCounterBuffers[1];
    Core::TextureHandle surfelGiComputeScratchTextures[1];
    Core::BufferHandle surfelGiComputeScratchBuffers[6];
    Core::GpuPersistentResourceStateCache::Candidate surfelIrradianceReturnStateCandidate;
    Core::GpuPersistentResourceStateCache::Candidate surfelGiCounterStateCandidate;
    Core::GpuPersistentResourceStateCache::Candidate surfelGiComputeStateCandidate;
    Core::TextureHandle hardwareCausticAccumulatorTextures[1];
    Core::GpuPersistentResourceStateCache::Candidate hardwareCausticAccumulatorStateCandidate;
    Core::GpuPersistentResourceStateCache::Candidate deferredLightingShadowReturnStateCandidate;
    Core::GpuPersistentResourceStateCache::Candidate deferredLightingCausticReturnStateCandidate;
    Core::GpuPersistentResourceStateCache::Candidate deferredLightingSurfelReturnStateCandidate;
    FrameExecuteLifecycle::ShadowPrepareStateLifecycleContext shadowPrepareStateLifecycle;
    FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext shadowVisibilityStateLifecycle;
    FrameExecuteLifecycle::SoftwareCausticsStateLifecycleContext softwareCausticsStateLifecycle;
    FrameExecuteLifecycle::SurfelGiStateLifecycleContext surfelGiStateLifecycle;
    FrameExecuteLifecycle::HardwareCausticsStateLifecycleContext hardwareCausticsStateLifecycle;
    FrameExecuteLifecycle::DeferredLightingStateLifecycleContext deferredLightingStateLifecycle;
    Core::GpuTaskGraphTaskRecordedCallback recordedCallbacks[s_CallbackCapacity] = {};
    Core::GpuTaskGraphTaskAcceptedCallback acceptedCallbacks[s_CallbackCapacity] = {};
    usize recordedCallbackCount = 0u;
    usize acceptedCallbackCount = 0u;


public:
    FrameExecuteStateLifecycleSet(
        RendererFramePipeline& renderer,
        DeferredFrameTargets& targets,
        const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
        const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources,
        const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources,
        bool hardwareShadowSupported,
        bool usesLaggedHistory
    );


public:
    [[nodiscard]] bool selectedCausticsStateReady(const bool hardwareShadowSupported)const noexcept{
        return hardwareShadowSupported ? hardwareCausticsStateLifecycle.stateReady : softwareCausticsStateLifecycle.stateReady;
    }


private:
    void collectShadowPrepareStateBuffers(RendererFramePipeline& renderer, const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources, const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources);
    void bindCallbacks(const RendererFramePipeline& renderer, bool hardwareShadowSupported)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

