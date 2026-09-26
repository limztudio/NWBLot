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
    // Optional borrowed predicate context must remain alive until stop() completes.
    bool (*shouldCapture)(void*, u64) = nullptr;
    void* predicateContext = nullptr;
    bool quitWhenReady = true;
};

// One-shot framebuffer observer for unattended smoke acceptance. Completion state is ref-counted independently so
// graph rejection/teardown can resolve an old payload after the owning project has stopped the observer.
class FramebufferCapture final
    : public Core::IRenderPass
    , public Core::IGpuTaskGraphPresentationContributor
{
private:
    struct CompletionStateData{
        Core::QueueSubmissionToken acceptedToken;
        u64 graphicsFrameIndex = Limit<u64>::s_Max;
    };
    using CompletionState = RefCounter<CompletionStateData>;


private:
    struct ReadbackTask;


public:
    FramebufferCapture(ProjectRuntimeContext& context, AStringView outputPath, u32 captureFrameCount, FramebufferCaptureOptions options = {});
    virtual ~FramebufferCapture()override;


public:
    [[nodiscard]] bool start();
    void stop();
    void update();
    [[nodiscard]] bool captureReady()const{ return m_captureReady; }
    [[nodiscard]] u64 capturedGraphicsFrameIndex()const{ return m_completionState->graphicsFrameIndex; }
    void finish();


public:
    virtual bool shouldRenderUnfocused()override;
    virtual void invalidateResources()override;
    virtual void backBufferResizing()override;


public:
    [[nodiscard]] virtual bool prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame)override;
    [[nodiscard]] virtual bool hasTaskGraphPresentationWork()const override;
    [[nodiscard]] virtual Core::GpuTaskId declareTaskGraphPresentation(
        Core::GpuTaskGraph& graph,
        const Core::AcquiredPresentationFrame& frame,
        Core::GpuGraphResourceId backbuffer,
        Core::GpuTaskId previousTask
    )override;


private:
    void markFailed(const tchar* reason);
    void requestTerminalQuit();
    void resetPendingReadback();
    [[nodiscard]] bool stagingMatches(const Core::TextureDesc& description)const;
    [[nodiscard]] bool prepareReadback(const Core::TextureDesc& description);
    [[nodiscard]] bool writeCapture(
        const u8* sourceBytes,
        usize sourceRowPitch,
        Core::Alloc::ScratchArena& scratchArena
    );


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
    bool m_registered = false;
    bool m_taskGraphPresentationPrepared = false;
    bool m_taskGraphPresentationClaimed = false;
    bool m_captureReady = false;
    bool m_skipped = false;
    bool m_failed = false;
    bool m_quitRequested = false;
    bool m_stopped = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared smoke framebuffer-capture prologue: reads the capture path + frame count, validates the count, and
// constructs a started capture. Returns true when capture is disabled (outCapture stays null) or when a started
// capture is published; false on config/startup failure. Projects with extra capture options pass them through.
[[nodiscard]] inline bool ConfigureSmokeFramebufferCapture(
    ProjectRuntimeContext& context,
    const tchar* const projectName,
    const u32 defaultFrameCount,
    UniquePtr<FramebufferCapture>& outCapture,
    const FramebufferCaptureOptions& options = {}
){
    outCapture.reset();
    SmokeEnvironmentString outputPath(context.objectArena);
    if(!ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", outputPath))
        return true;

    u32 captureFrameCount = defaultFrameCount;
    SmokeEnvironmentString frameCountText(context.objectArena);
    if(ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", frameCountText)){
        u64 parsedFrameCount = 0u;
        if(
            !ParseU64(AStringView(frameCountText.data(), frameCountText.size()), parsedFrameCount)
            || parsedFrameCount == 0u
            || parsedFrameCount > static_cast<u64>(Limit<u32>::s_Max)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: capture frame count must be a positive u32"), projectName);
            return false;
        }
        captureFrameCount = static_cast<u32>(parsedFrameCount);
    }

    auto capture = MakeUnique<FramebufferCapture>(
        context, AStringView(outputPath.data(), outputPath.size()), captureFrameCount, options
    );
    if(!capture || !capture->start())
        return false;
    outCapture = Move(capture);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

