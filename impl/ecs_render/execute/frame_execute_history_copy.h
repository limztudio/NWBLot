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
// Separated from render(): context plus recorded/accepted callbacks are one unit — build filtered subsets for the three return states, then commit together on acceptance.
class FrameExecuteHistoryCopy final{
public:
    struct Context{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<DeferredFrameTargets*> targets;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> shadowStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> causticStateCandidate;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> surfelStateCandidate;
        bool finalStateReady = false;
        bool acceptedStateReady = false;
    };


public:
    [[nodiscard]] static bool PrepareHistoryCopyFinalState(void* rawContext, const Core::CommandListResourceStateHandoff* finalState);
    [[nodiscard]] static bool AcceptHistoryCopyFinalState(void* rawContext, const Core::QueueSubmissionToken& token)noexcept;


public:
    FrameExecuteHistoryCopy() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

