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
// Separated from RendererFramePipeline::render(): the readback context plus its recorded/accepted callbacks are one functionality — retain the counter candidate privately until Transfer accepts, then publish the submission token.
class FrameExecuteSurfelReadback final{
public:
    struct Context{
        Core::Alloc::ScratchArena& scratchArena;
        NotNull<RendererFramePipeline*> renderer;
        NotNull<Core::GpuPersistentResourceStateCache::Candidate*> candidate;
        const Core::BufferHandle* buffers = nullptr;
        usize bufferCount = 0u;
        bool finalStateReady = false;
        bool acceptedStateReady = false;
    };


public:
    [[nodiscard]] static bool PrepareReadbackFinalState(void* rawContext, const Core::CommandListResourceStateHandoff* finalState);
    [[nodiscard]] static bool AcceptReadbackFinalState(void* rawContext, const Core::QueueSubmissionToken& token)noexcept;


public:
    FrameExecuteSurfelReadback() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

