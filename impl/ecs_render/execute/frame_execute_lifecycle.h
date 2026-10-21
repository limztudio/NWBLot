// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/shadow/light_space_shadow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameExecuteLifecycle final{
public:
    struct ShadowPrepareStateLifecycleContext{
        NotNull<RendererFramePipeline*> renderer;
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> stateCandidate;
        const Core::BufferHandle* buffers = nullptr;
        usize bufferCount = 0u;
        bool stateCandidateRequired = false;
        bool statePrepared = false;
        bool stateReady = true;
    };
    struct ShadowVisibilityStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<DeferredFrameTargets*> targets;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> returnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> scratchStateCandidate;
        const Core::TextureHandle* returnTextures = nullptr;
        const Core::TextureHandle* scratchTextures = nullptr;
        const Core::BufferHandle* scratchBuffers = nullptr;
        const bool* lightSpacePrepared = nullptr;
        LightSpaceCaptureTicket lightSpaceCaptureTicket;
        usize returnTextureCount = 0u;
        usize scratchTextureCount = 0u;
        usize scratchBufferCount = 0u;
        bool statePrepared = false;
        bool stateReady = false;
    };
    struct SoftwareCausticsStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> returnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> scratchStateCandidate;
        const Core::TextureHandle* irradianceTextures = nullptr;
        const Core::TextureHandle* scratchTextures = nullptr;
        usize irradianceTextureCount = 0u;
        usize scratchTextureCount = 0u;
        bool statePrepared = false;
        bool stateReady = false;
    };
    struct SurfelGiStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> returnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> counterStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> computeStateCandidate;
        const Core::TextureHandle* returnTextures = nullptr;
        const Core::BufferHandle* counterBuffers = nullptr;
        const Core::TextureHandle* computeTextures = nullptr;
        const Core::BufferHandle* computeBuffers = nullptr;
        usize returnTextureCount = 0u;
        usize counterBufferCount = 0u;
        usize computeTextureCount = 0u;
        usize computeBufferCount = 0u;
        bool statePrepared = false;
        bool stateReady = false;
    };
    struct HardwareCausticsStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> accumulatorStateCandidate;
        const Core::TextureHandle* accumulatorTextures = nullptr;
        usize accumulatorTextureCount = 0u;
        bool statePrepared = false;
        bool stateReady = false;
    };
    struct DeferredLightingStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<DeferredFrameTargets*> targets;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> shadowReturnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> causticReturnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> surfelReturnStateCandidate;
        const Core::TextureHandle* shadowReturnTextures = nullptr;
        const Core::TextureHandle* causticReturnTextures = nullptr;
        const Core::TextureHandle* surfelReturnTextures = nullptr;
        usize shadowReturnTextureCount = 0u;
        usize causticReturnTextureCount = 0u;
        usize surfelReturnTextureCount = 0u;
        bool usesLaggedHistory = false;
        bool statePrepared = false;
        bool stateReady = false;
    };


public:
    [[nodiscard]] static bool PrepareShadowPrepareTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool AcceptShadowPrepareTask( void* const rawContext, const Core::QueueSubmissionToken& token );
    [[nodiscard]] static bool PrepareShadowVisibilityTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool AcceptShadowVisibilityTask( void* const rawContext, const Core::QueueSubmissionToken& token );
    [[nodiscard]] static bool PrepareSoftwareCausticsTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool AcceptSoftwareCausticsTask( void* const rawContext, const Core::QueueSubmissionToken& token )noexcept;
    [[nodiscard]] static bool PrepareSurfelGiTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool AcceptSurfelGiTask( void* const rawContext, const Core::QueueSubmissionToken& token )noexcept;
    [[nodiscard]] static bool PrepareHardwareCausticsTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool AcceptHardwareCausticsTask( void* const rawContext, const Core::QueueSubmissionToken& token )noexcept;
    [[nodiscard]] static bool PrepareDeferredLightingTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool AcceptDeferredLightingTask( void* const rawContext, const Core::QueueSubmissionToken& token );


public:
    FrameExecuteLifecycle() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

