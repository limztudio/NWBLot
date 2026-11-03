// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"
#include "world.h"
#include "../framebuffer_capture.h"

#include <impl/ecs_ui/layer_system.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/task/gpu/persistent_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_ResizePreparationPresentations = 4u;

static b8 CheckPersistentStateSelfFilter(ProjectRuntimeContext& context){
    Core::Alloc::ScratchArena scratch(Name("tests/ui_layer/persistent_state"));
    Core::BufferDesc desc;
    desc.setDebugName(Name("tests/ui_layer/persistent_state_buffer")).setByteSize(16u);
    const auto first = context.graphics.createBuffer(desc);
    const auto second = context.graphics.createBuffer(desc);
    auto& device = context.graphics.getDevice();
    const auto list = device.createCommandList();
    if(!first || !second || !list)
        return false;

    Core::CommandListResourceStateHandoff state(context.objectArena);
    (*list)->open();
    (*list)->beginTrackingBufferState(first->get(), Core::ResourceStates::CopyDest);
    (*list)->beginTrackingBufferState(second->get(), Core::ResourceStates::CopyDest);
    (*list)->close(&state);
    const Core::GpuPhysicalQueueId queue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(!state.valid() || !queue.valid() || (*list)->commandRecordingFailed())
        return false;

    Core::GpuPersistentResourceStateCache cache(context.objectArena);
    Core::GpuPersistentResourceStateCache::Candidate candidate(cache);
    const Array<Core::BufferHandle, 2u> buffers{ *first, *second };
    if(!cache.buildFilteredBufferSubset(state, buffers, scratch, candidate) || !candidate.source() || cache.valid())
        return false;
    if(!cache.buildFilteredBufferSubset(*candidate.source(), Span<const Core::BufferHandle>(buffers.data(), 1u), scratch, candidate))
        return false;
    const auto* filtered = candidate.source();
    if(!filtered || !filtered->coversBufferWithOwnership(first->get(), queue, {}) || filtered->coversBufferWithOwnership(second->get(), queue, {}) || cache.valid())
        return false;
    if(!cache.commit(candidate) || candidate.valid() || !cache.source())
        return false;
    return cache.source()->coversBufferWithOwnership(first->get(), queue, {}) && !cache.source()->coversBufferWithOwnership(second->get(), queue, {});
}


UiLayerSmokeProject::UiLayerSmokeProject(NotNullUniquePtr<Core::ECS::World> world, ProjectRuntimeContext& context)noexcept
    : m_world(Move(world))
    , m_context(context)
    , m_resizeRenderPass(context.graphics)
{}

UiLayerSmokeProject::~UiLayerSmokeProject()noexcept{
    destroyRuntime();
}

b8 UiLayerSmokeProject::onStartup(){
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_PERSISTENT_STATE_TEST")){
        if(!CheckPersistentStateSelfFilter(m_context)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: persistent state self-filter failed"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: persistent state self-filter passed"));
    }
    auto& layer = *MakeNotNull(m_world->getSystem<Impl::UiLayerSystem>());
    m_context.graphics.addRenderPassToBack(layer);
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RESIZE_RENDER")){
        m_resizePresentationBaseline = m_context.graphics.getSuccessfulPresentationCount();
        m_context.graphics.addRenderPassToBack(m_resizeRenderPass);
        m_resizeRenderPassRegistered = true;
    }
    FramebufferCaptureOptions captureOptions;
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RESIZE_CAPTURE")){
        captureOptions.requiredWidth = 800u;
        captureOptions.requiredHeight = 600u;
    }
    constexpr u32 s_DefaultCaptureFrameCount = 60u;
    auto capture = ConfigureSmokeFramebufferCapture(NWB_TEXT("UiLayerSmokeProject"), s_DefaultCaptureFrameCount, captureOptions, m_context);
    if(!capture)
        return false;
    m_framebufferCapture = Move(*capture);

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: standalone layer ready; default atlas; SDR; empty startup frames=2"));
    return true;
}

void UiLayerSmokeProject::onShutdown(){
    destroyRuntime();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: shutdown"));
}

b8 UiLayerSmokeProject::onUpdate(const f32 delta){
    if(m_resizeRenderPassRegistered && !m_resizePresentationReported){
        const u64 presentations = m_context.graphics.getSuccessfulPresentationCount();
        if(presentations >= m_resizePresentationBaseline && presentations - m_resizePresentationBaseline >= s_ResizePreparationPresentations){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: resize scene presented"));
            m_resizePresentationReported = true;
        }
    }
    return TickSmokeWorldWithCapture(delta, *m_world, m_framebufferCapture);
}

void UiLayerSmokeProject::destroyRuntime(){
    if(m_framebufferCapture){
        m_framebufferCapture->stop();
        m_framebufferCapture.reset();
    }
    if(m_resizeRenderPassRegistered){
        m_context.graphics.removeRenderPass(m_resizeRenderPass);
        m_resizeRenderPassRegistered = false;
    }
    ClearUiLayerSmokeWorld(m_context, *m_world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

