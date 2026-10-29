// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Lagged-lighting history-copy tail acceptance (shadow/caustic/surfel returns).
// Context and recorded/accepted callbacks filter the three return states and commit them together on acceptance.
class FrameExecuteHistoryCopy final{
public:
    struct Context{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<DeferredFrameTargets*> targets;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> shadowStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> causticStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> surfelStateCandidate;
        b8 finalStateReady = false;
        b8 acceptedStateReady = false;
    };


public:
    [[nodiscard]] static b8 PrepareHistoryCopyFinalState(void* rawContext, const Core::CommandListResourceStateHandoff* finalState);
    [[nodiscard]] static b8 AcceptHistoryCopyFinalState(void* rawContext, const Core::QueueSubmissionToken& token)noexcept;


public:
    FrameExecuteHistoryCopy() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

