// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"
#include "world.h"
#include "../framebuffer_capture.h"

#include <impl/ecs_ui/layer_system.h>

#include <core/common/log.h>
#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiLayerSmokeProject::UiLayerSmokeProject(NotNullUniquePtr<Core::ECS::World> world, ProjectRuntimeContext& context)noexcept
    : m_world(Move(world))
    , m_context(context)
{}

UiLayerSmokeProject::~UiLayerSmokeProject()noexcept{
    destroyRuntime();
}

b8 UiLayerSmokeProject::onStartup(){
    auto& layer = *MakeNotNull(m_world->getSystem<Impl::UiLayerSystem>());
    m_context.graphics.addRenderPassToBack(layer);
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
    return TickSmokeWorldWithCapture(delta, *m_world, m_framebufferCapture);
}

void UiLayerSmokeProject::destroyRuntime(){
    if(m_framebufferCapture){
        m_framebufferCapture->stop();
        m_framebufferCapture.reset();
    }
    ClearUiLayerSmokeWorld(m_context, *m_world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

