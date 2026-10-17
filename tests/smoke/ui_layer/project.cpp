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


UiLayerSmokeProject::UiLayerSmokeProject(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World> world)noexcept
    : m_context(context)
    , m_world(Move(world))
{}

UiLayerSmokeProject::~UiLayerSmokeProject()noexcept{
    destroyRuntime();
}

bool UiLayerSmokeProject::onStartup(){
    auto* const layer = m_world->getSystem<Impl::UiLayerSystem>();
    NWB_ASSERT(layer);
    m_context.graphics.addRenderPassToBack(*layer);
    FramebufferCaptureOptions captureOptions;
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RESIZE_CAPTURE")){
        captureOptions.requiredWidth = 800u;
        captureOptions.requiredHeight = 600u;
    }
    auto capture = ConfigureSmokeFramebufferCapture(m_context, NWB_TEXT("UiLayerSmokeProject"), 60u, captureOptions);
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

bool UiLayerSmokeProject::onUpdate(const f32 delta){
    if(m_framebufferCapture)
        m_framebufferCapture->update();
    const auto updated = m_world->tick(delta);
    if(!updated){
        NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
        return false;
    }
    return true;
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

