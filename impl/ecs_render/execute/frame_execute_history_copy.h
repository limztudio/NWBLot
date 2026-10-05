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
// Separated from RendererFramePipeline::render(): the history-copy context plus its
// recorded/accepted callbacks are one functionality — build filtered subsets for the
// three return states, then commit them together on acceptance.
class FrameExecuteHistoryCopy final{
public:
    FrameExecuteHistoryCopy() = delete;


    struct Context{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* shadowStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* causticStateCandidate = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* surfelStateCandidate = nullptr;
        bool finalStateReady = false;
        bool acceptedStateReady = false;
    };


    [[nodiscard]] static bool PrepareHistoryCopyFinalState(
        void* rawContext,
        const Core::CommandListResourceStateHandoff* finalState
    );
    [[nodiscard]] static bool AcceptHistoryCopyFinalState(
        void* rawContext,
        const Core::QueueSubmissionToken& token
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END
