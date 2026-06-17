// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererRayTracingSystem::RendererRayTracingSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase<RendererSystem>(renderer)
{}


void RendererRayTracingSystem::logCapabilityOnce(){
    if(rayTracingState().m_capabilityLogged)
        return;

    rayTracingState().m_capabilityLogged = true;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: ray tracing capability - accel struct {}, pipeline {}, ray query {}")
        , accelStructSupported()
        , rayTracingPipelineSupported()
        , rayQuerySupported()
    );
}

bool RendererRayTracingSystem::accelStructSupported()const{
    return graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct);
}

bool RendererRayTracingSystem::rayTracingPipelineSupported()const{
    return graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline);
}

bool RendererRayTracingSystem::rayQuerySupported()const{
    return graphics().queryFeatureSupport(Core::Feature::RayQuery);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

