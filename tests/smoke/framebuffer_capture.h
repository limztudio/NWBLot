// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/project_entry.h>

#include "smoke_environment.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/presentation_contributor.h>

#include <global/filesystem/path.h>
#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FramebufferCaptureOptions{
    // Borrowed predicate context must outlive stop.
    b8 (*shouldCapture)(void*, u64) = nullptr;
    void* predicateContext = nullptr;
    u32 requiredWidth = 0u;
    u32 requiredHeight = 0u;
    b8 quitWhenReady = true;
};

// Ref-count completion state so teardown can finish after the project stops the observer.
class FramebufferCapture final
    : public Core::IRenderPass
    , public Core::IGpuTaskGraphPresentationContributor
{
private:
    struct CompletionStateData{
        Core::QueueSubmissionToken acceptedToken;
        u64 graphicsFrameIndex = Limit<u64>::s_Max;
        u64 generation = 1u;
    };


private:
    using CompletionState = RefCounter<CompletionStateData>;


private:
    struct ReadbackTask;


public:
    FramebufferCapture(ProjectRuntimeContext& context, AStringView outputPath, u32 captureFrameCount, FramebufferCaptureOptions options = {});
    FramebufferCapture(ProjectRuntimeContext& context, ::Path<Core::Alloc::GlobalArena>&& outputPath, u32 captureFrameCount, FramebufferCaptureOptions options = {});
    virtual ~FramebufferCapture()noexcept override;


public:
    [[nodiscard]] b8 start();
    void stop();
    void update();
    [[nodiscard]] b8 rearm(::Path<Core::Alloc::GlobalArena>&& outputPath)noexcept;
    [[nodiscard]] b8 captureReady()const noexcept{ return m_captureReady; }

    [[nodiscard]] u64 capturedGraphicsFrameIndex()const noexcept{ return m_completionState->graphicsFrameIndex; }
    void finish();


public:
    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }
    virtual b8 validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual void backBufferResizing()override;


public:
    [[nodiscard]] virtual b8 prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame)override;
    [[nodiscard]] virtual b8 hasTaskGraphPresentationWork()const noexcept override;
    [[nodiscard]] virtual Expected<Core::GpuTaskId> declareTaskGraphPresentation(Core::GpuTaskGraph& graph, const Core::AcquiredPresentationFrame& frame, Core::GpuGraphResourceId backbuffer, Core::GpuTaskId previousTask)override;


private:
    void markFailed(TStringView reason);
    void requestTerminalQuit();
    void resetPendingReadback();
    [[nodiscard]] b8 stagingMatches(const Core::TextureDesc& description)const noexcept;
    [[nodiscard]] b8 prepareReadback(const Core::TextureDesc& description);
    [[nodiscard]] b8 writeCapture(NotNull<const u8*> sourceBytes, usize sourceRowPitch, Core::Alloc::ScratchArena& scratchArena);


private:
    ProjectRuntimeContext& m_context;
    FramebufferCaptureOptions m_options;
    ::Path<Core::Alloc::GlobalArena> m_outputPath;
    RefCountPtr<CompletionState> m_completionState;
    Core::StagingTextureHandle m_readback;
    Core::TextureDesc m_captureDescription;
    Core::AcquiredPresentationFrame m_preparedFrame;
    u64 m_preparedGraphGeneration = 0u;
    u64 m_lastCountedGraphicsFrame = Limit<u64>::s_Max;
    u32 m_captureFrameCount = 0u;
    u32 m_preparedFrameCount = 0u;
    b8 m_registered = false;
    b8 m_taskGraphPresentationPrepared = false;
    b8 m_taskGraphPresentationClaimed = false;
    b8 m_captureReady = false;
    b8 m_skipped = false;
    b8 m_failed = false;
    b8 m_quitRequested = false;
    b8 m_stopped = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Return a started observer when capture is configured.
[[nodiscard]] inline Expected<UniquePtr<FramebufferCapture>> ConfigureSmokeFramebufferCapture(
    ProjectRuntimeContext& context,
    Core::Alloc::ScratchArena& scratchArena,
    const TStringView projectName,
    const u32 defaultFrameCount,
    const FramebufferCaptureOptions& options = {}
){
    const auto outputPath = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
    if(!outputPath)
        return UniquePtr<FramebufferCapture>{};

    u32 captureFrameCount = defaultFrameCount;
    const auto frameCountText = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT");
    if(frameCountText){
        const auto parsedFrameCount = ParseU64(AStringView(frameCountText->data(), frameCountText->size()));
        if(!parsedFrameCount || *parsedFrameCount == 0u || *parsedFrameCount > static_cast<u64>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: capture frame count must be a positive u32"), projectName);
            return MakeUnexpected(Failure{});
        }
        captureFrameCount = static_cast<u32>(*parsedFrameCount);
    }

    auto capture = MakeUnique<FramebufferCapture>(context, AStringView(outputPath->data(), outputPath->size()), captureFrameCount, options);
    if(!capture || !capture->start())
        return MakeUnexpected(Failure{});
    return capture;
}

[[nodiscard]] inline Expected<UniquePtr<FramebufferCapture>> ConfigureSmokeFramebufferCapture(ProjectRuntimeContext& context, const TStringView projectName, const u32 defaultFrameCount, const FramebufferCaptureOptions& options = {}){
    Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
    return ConfigureSmokeFramebufferCapture(context, scratchArena, projectName, defaultFrameCount, options);
}

struct SmokeCaptureSeries{
    Vector<::Path<Core::Alloc::GlobalArena>, Core::Alloc::GlobalArena> paths;
    UniquePtr<FramebufferCapture> capture;


public:
    explicit SmokeCaptureSeries(Core::Alloc::GlobalArena& arena)noexcept
        : paths(arena)
    {}
};

[[nodiscard]] inline Expected<SmokeCaptureSeries> PrepareSmokeCaptureSeries(ProjectRuntimeContext& context, const AStringView outputPath, const u32 sampleCount, const FramebufferCaptureOptions& options){
    SmokeCaptureSeries result(context.objectArena);
    result.paths.reserve(sampleCount);
    for(u32 sample = 0u; sample < sampleCount; ++sample){
        const auto path = sample + 1u == sampleCount ? SmokeEnvironmentString(outputPath, context.objectArena) : StringFormat(context.objectArena, "{}.{}.bmp", outputPath, sample);
        result.paths.emplace_back(context.objectArena, AStringView(path));
    }
    result.capture = MakeUnique<FramebufferCapture>(context, Move(result.paths.front()), 1u, options);
    if(!result.capture || !result.capture->start())
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

