// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_push_constants_private.h"

#include <core/ecs/world.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline f32 ResolveExtentAspectRatio(const u32 width, const u32 height){
    if(width != 0u && height != 0u)
        return static_cast<f32>(width) / static_cast<f32>(height);

    return 1.0f;
}

inline f32 ResolveFramebufferAspectRatio(const Core::FramebufferInfoEx& framebufferInfo){
    return ResolveExtentAspectRatio(framebufferInfo.width, framebufferInfo.height);
}

inline SceneShadingGpuData ResolveSceneShadingState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    SceneShadingGpuData state;
    const NWB::Impl::Scene::SceneViewBasis defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        StoreFloat(VectorSetW(LoadFloat(cameraView.transform->position), 1.0f), &state.cameraPosition);
    }
    else
        StoreFloat(VectorSetW(LoadFloat(defaultBasis.positionDepthBias), 1.0f), &state.cameraPosition);

    const NWB::Impl::Scene::SceneDirectionalLight light = NWB::Impl::Scene::ResolveSceneDirectionalLight(world, defaultBasis);
    state.directionalLightDirection = light.direction;
    state.directionalLightColorIntensity = light.colorIntensity;

    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

