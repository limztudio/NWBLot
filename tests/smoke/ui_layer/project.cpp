// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"
#include "world.h"

#include "../framebuffer_capture.h"

#include <core/common/log.h>
#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiLayerSmokeProject::UiLayerSmokeProject(ProjectRuntimeContext& context)
    : m_context(context)
{}

UiLayerSmokeProject::~UiLayerSmokeProject(){
    destroyRuntime();
}

bool UiLayerSmokeProject::onStartup(){
    if(!CreateUiLayerSmokeWorld(m_context, m_world))
        return false;
    FramebufferCaptureOptions captureOptions;
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RESIZE_CAPTURE")){
        captureOptions.requiredWidth = 800u;
        captureOptions.requiredHeight = 600u;
    }
    if(!ConfigureSmokeFramebufferCapture(m_context, NWB_TEXT("UiLayerSmokeProject"), 60u, m_framebufferCapture, captureOptions))
        return false;

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
    if(m_world)
        m_world->tick(delta);
    return true;
}

void UiLayerSmokeProject::destroyRuntime(){
    if(m_framebufferCapture){
        m_framebufferCapture->stop();
        m_framebufferCapture.reset();
    }
    DestroyUiLayerSmokeWorld(m_context, m_world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

