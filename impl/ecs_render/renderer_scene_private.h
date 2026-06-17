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

inline u32 ResolveSceneLights(Core::ECS::World& world, SceneLightGpuData* outLights, const u32 maxLights){
    if(maxLights == 0u)
        return 0u;

    u32 capacity = maxLights;
    if(capacity > NWB_SCENE_MAX_LIGHTS)
        capacity = NWB_SCENE_MAX_LIGHTS;

    const NWB::Impl::Scene::SceneViewBasis defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    NWB::Impl::Scene::SceneLight sceneLights[NWB_SCENE_MAX_LIGHTS];
    const usize gatheredCount = NWB::Impl::Scene::GatherSceneLights(world, defaultBasis, sceneLights, capacity);

    for(usize i = 0u; i < gatheredCount; ++i){
        const NWB::Impl::Scene::SceneLight& src = sceneLights[i];
        SceneLightGpuData& dst = outLights[i];
        dst.position = src.position;
        dst.direction = src.direction;
        dst.colorIntensity = src.colorIntensity;
        dst.params = Float4(src.range, static_cast<f32>(src.type), 0.f, 0.f);
    }

    return static_cast<u32>(gatheredCount);
}

inline SceneShadingGpuData ResolveSceneShadingState(Core::ECS::World& world, const f32 fallbackAspectRatio, const u32 lightCount){
    SceneShadingGpuData state;
    const NWB::Impl::Scene::SceneViewBasis defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    const SIMDVector cameraPosition = cameraView.valid()
        ? LoadFloat(cameraView.transform->position)
        : LoadFloat(defaultBasis.positionDepthBias)
    ;
    StoreFloat(VectorSetW(cameraPosition, static_cast<f32>(lightCount)), &state.cameraPositionLightCount);
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

