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
        Span<const Core::BufferHandle> buffers{};
        b8 stateCandidateRequired = false;
        b8 statePrepared = false;
        b8 stateReady = true;
    };
    struct ShadowVisibilityStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<DeferredFrameTargets*> targets;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> returnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> scratchStateCandidate;
        Span<const Core::TextureHandle> returnTextures{};
        Span<const Core::TextureHandle> scratchTextures{};
        Span<const Core::BufferHandle> scratchBuffers{};
        const b8* lightSpacePrepared = nullptr;
        LightSpaceCaptureTicket lightSpaceCaptureTicket;
        b8 statePrepared = false;
        b8 stateReady = false;
    };
    struct SoftwareCausticsStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> returnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> scratchStateCandidate;
        Span<const Core::TextureHandle> irradianceTextures{};
        Span<const Core::TextureHandle> scratchTextures{};
        b8 statePrepared = false;
        b8 stateReady = false;
    };
    struct SurfelGiStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> returnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> counterStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> computeStateCandidate;
        Span<const Core::TextureHandle> returnTextures{};
        Span<const Core::BufferHandle> counterBuffers{};
        Span<const Core::TextureHandle> computeTextures{};
        Span<const Core::BufferHandle> computeBuffers{};
        b8 statePrepared = false;
        b8 stateReady = false;
    };
    struct HardwareCausticsStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> accumulatorStateCandidate;
        Span<const Core::TextureHandle> accumulatorTextures{};
        b8 statePrepared = false;
        b8 stateReady = false;
    };
    struct DeferredLightingStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<DeferredFrameTargets*> targets;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> shadowReturnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> causticReturnStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> surfelReturnStateCandidate;
        Span<const Core::TextureHandle> shadowReturnTextures{};
        Span<const Core::TextureHandle> causticReturnTextures{};
        Span<const Core::TextureHandle> surfelReturnTextures{};
        b8 usesLaggedHistory = false;
        b8 statePrepared = false;
        b8 stateReady = false;
    };


public:
    FrameExecuteLifecycle() = delete;


public:
    [[nodiscard]] static b8 PrepareShadowPrepareTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState);
    [[nodiscard]] static b8 AcceptShadowPrepareTask(void* const rawContext, const Core::QueueSubmissionToken& token);
    [[nodiscard]] static b8 PrepareShadowVisibilityTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState);
    [[nodiscard]] static b8 AcceptShadowVisibilityTask(void* const rawContext, const Core::QueueSubmissionToken& token);
    [[nodiscard]] static b8 PrepareSoftwareCausticsTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState);
    [[nodiscard]] static b8 AcceptSoftwareCausticsTask(void* const rawContext, const Core::QueueSubmissionToken& token)noexcept;
    [[nodiscard]] static b8 PrepareSurfelGiTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState);
    [[nodiscard]] static b8 AcceptSurfelGiTask(void* const rawContext, const Core::QueueSubmissionToken& token)noexcept;
    [[nodiscard]] static b8 PrepareHardwareCausticsTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState);
    [[nodiscard]] static b8 AcceptHardwareCausticsTask(void* const rawContext, const Core::QueueSubmissionToken& token)noexcept;
    [[nodiscard]] static b8 PrepareDeferredLightingTask(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState);
    [[nodiscard]] static b8 AcceptDeferredLightingTask(void* const rawContext, const Core::QueueSubmissionToken& token);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

