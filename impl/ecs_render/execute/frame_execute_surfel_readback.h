// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel-counter readback tail acceptance (Transfer queue).
// Separated from RendererFramePipeline::render(): the readback context plus its
// recorded/accepted callbacks are one functionality — retain the counter candidate
// privately until Transfer accepts, then publish the submission token.
class FrameExecuteSurfelReadback final{
public:
    FrameExecuteSurfelReadback() = delete;


    struct Context{
        Core::Alloc::ScratchArena& scratchArena;
        RendererFramePipeline* renderer = nullptr;
        Core::GpuPersistentResourceStateCache::Candidate* candidate = nullptr;
        const Core::BufferHandle* buffers = nullptr;
        usize bufferCount = 0u;
        bool finalStateReady = false;
        bool acceptedStateReady = false;
    };


    [[nodiscard]] static bool prepareReadbackFinalState(
        void* rawContext,
        const Core::CommandListResourceStateHandoff* finalState
    );
    [[nodiscard]] static bool acceptReadbackFinalState(
        void* rawContext,
        const Core::QueueSubmissionToken& token
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END
