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
    FrameExecuteLifecycle() = delete;


    struct ShadowPrepareStateLifecycleContext{
        RendererFramePipeline* renderer = nullptr;
        Core::Alloc::ScratchArena& scratchArena;
        Core::GpuPersistentResourceStateCache::Candidate* stateCandidate = nullptr;
        const Core::BufferHandle* buffers = nullptr;
        usize bufferCount = 0u;
        bool stateCandidateRequired = false;
        bool statePrepared = false;
        bool stateReady = true;
    };

    struct ShadowVisibilityStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* returnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* scratchStateCandidate = nullptr;
        const Core::TextureHandle* returnTextures = nullptr;
        const Core::TextureHandle* scratchTextures = nullptr;
        const Core::BufferHandle* scratchBuffers = nullptr;
        const bool* lightSpacePrepared = nullptr;
        LightSpaceCaptureTicket lightSpaceCaptureTicket;
        usize returnTextureCount = 0u;
        usize scratchTextureCount = 0u;
        usize scratchBufferCount = 0u;
        bool runsOnCompute = false;
        bool statePrepared = false;
        bool stateReady = false;
    };

    struct SoftwareCausticsStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* returnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* scratchStateCandidate = nullptr;
        const Core::TextureHandle* irradianceTextures = nullptr;
        const Core::TextureHandle* scratchTextures = nullptr;
        usize irradianceTextureCount = 0u;
        usize scratchTextureCount = 0u;
        bool runsOnCompute = false;
        bool statePrepared = false;
        bool stateReady = false;
    };

    struct SurfelGiStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* returnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* counterStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* computeStateCandidate = nullptr;
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
        RendererFramePipeline* renderer = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* accumulatorStateCandidate = nullptr;
        const Core::TextureHandle* accumulatorTextures = nullptr;
        usize accumulatorTextureCount = 0u;
        bool statePrepared = false;
        bool stateReady = false;
    };

    struct DeferredLightingStateLifecycleContext{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* shadowReturnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* causticReturnStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* surfelReturnStateCandidate = nullptr;
        const Core::TextureHandle* shadowReturnTextures = nullptr;
        const Core::TextureHandle* causticReturnTextures = nullptr;
        const Core::TextureHandle* surfelReturnTextures = nullptr;
        usize shadowReturnTextureCount = 0u;
        usize causticReturnTextureCount = 0u;
        usize surfelReturnTextureCount = 0u;
        bool runsOnCompute = false;
        bool usesLaggedHistory = false;
        bool statePrepared = false;
        bool stateReady = false;
    };

    [[nodiscard]] static bool prepareShadowPrepareTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool acceptShadowPrepareTask( void* const rawContext, const Core::QueueSubmissionToken& token );
    [[nodiscard]] static bool prepareShadowVisibilityTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool acceptShadowVisibilityTask( void* const rawContext, const Core::QueueSubmissionToken& token );
    [[nodiscard]] static bool prepareSoftwareCausticsTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool acceptSoftwareCausticsTask( void* const rawContext, const Core::QueueSubmissionToken& token );
    [[nodiscard]] static bool prepareSurfelGiTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool acceptSurfelGiTask( void* const rawContext, const Core::QueueSubmissionToken& token );
    [[nodiscard]] static bool prepareHardwareCausticsTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool acceptHardwareCausticsTask( void* const rawContext, const Core::QueueSubmissionToken& token );
    [[nodiscard]] static bool prepareDeferredLightingTask( void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState );
    [[nodiscard]] static bool acceptDeferredLightingTask( void* const rawContext, const Core::QueueSubmissionToken& token );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

