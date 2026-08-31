// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/project_entry.h>

#include <core/alloc/scratch.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/presentation_contributor.h>
#include <global/filesystem/path.h>
#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One-shot framebuffer observer for unattended smoke acceptance. Completion state is ref-counted independently so
// graph rejection/teardown can resolve an old payload after the owning project has stopped the observer.
class FramebufferCapture final
    : public Core::IRenderPass
    , public Core::IGpuTaskGraphPresentationContributor
{
private:
    struct CompletionStateData{
        Core::QueueSubmissionToken acceptedToken;
    };
    using CompletionState = RefCounter<CompletionStateData>;


private:
    struct ReadbackTask;


public:
    FramebufferCapture(ProjectRuntimeContext& context, AStringView outputPath, u32 captureFrameCount);
    virtual ~FramebufferCapture()override;


public:
    [[nodiscard]] bool start();
    void stop();
    void update();


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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

