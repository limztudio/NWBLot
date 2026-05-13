// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting.h"

#include <core/ecs/ecs.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void StoreRotatedLightBasisVector(Float4& outVector, const Float4& localVector, const SIMDVector rotation){
    StoreFloat(Vector3Rotate(LoadFloat(localVector), rotation), &outVector);
}

static void StoreDirectionalLightDirection(Float4& outDirection, const Float4& forward){
    const SIMDVector lightDirection = VectorNegate(LoadFloat(forward));
    const f32 lightDirectionLengthSquared = VectorGetX(Vector3LengthSq(lightDirection));
    if(!IsFinite(lightDirectionLengthSquared) || lightDirectionLengthSquared <= 0.0001f){
        outDirection = Float4(0.0f, 0.0f, -1.0f, 0.0f);
        return;
    }

    StoreFloat(VectorSetW(Vector3Normalize(lightDirection), 0.0f), &outDirection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneDirectionalLight BuildDefaultSceneDirectionalLight(const SceneViewBasis& basis){
    SceneDirectionalLight light;
    __hidden_lighting::StoreDirectionalLightDirection(light.direction, basis.forward);
    light.colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    return light;
}

bool TryBuildSceneDirectionalLight(const TransformComponent& transform, const LightComponent& light, SceneDirectionalLight& outLight){
    outLight = SceneDirectionalLight{};
    if(light.type != LightType::Directional)
        return false;

    const SIMDVector rotation = LoadFloat(transform.rotation);
    const f32 rotationLengthSquared = VectorGetX(QuaternionLengthSq(rotation));
    if(
        QuaternionIsNaN(rotation)
        || QuaternionIsInfinite(rotation)
        || !IsFinite(rotationLengthSquared)
        || rotationLengthSquared <= 0.0001f
    ){
        return false;
    }

    const SIMDVector lightColorIntensity = LoadFloat(light.colorIntensity);
    if(
        Vector3IsNaN(lightColorIntensity)
        || Vector3IsInfinite(lightColorIntensity)
        || !IsFinite(light.intensity())
        || light.intensity() <= 0.0f
    ){
        return false;
    }

    Float4 lightForward;
    __hidden_lighting::StoreRotatedLightBasisVector(lightForward, Float4(0.0f, 0.0f, 1.0f), rotation);
    __hidden_lighting::StoreDirectionalLightDirection(outLight.direction, lightForward);
    outLight.colorIntensity = light.colorIntensity;
    return true;
}

SceneDirectionalLight ResolveSceneDirectionalLight(Core::ECS::World& world, const SceneViewBasis& defaultBasis){
    const auto lightView = world.view<TransformComponent, LightComponent>();
    for(auto it = lightView.begin(); it != lightView.end(); ++it){
        auto&& [entity, transform, light] = *it;
        static_cast<void>(entity);

        SceneDirectionalLight resolvedLight;
        if(TryBuildSceneDirectionalLight(transform, light, resolvedLight))
            return resolvedLight;
    }

    return BuildDefaultSceneDirectionalLight(defaultBasis);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

