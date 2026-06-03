// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting.h"

#include <core/ecs/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static SIMDVector BuildDirectionalLightDirectionVector(const SIMDVector forward){
    return Vector3NormalizeOr(
        VectorNegate(forward),
        VectorSet(0.0f, 0.0f, -1.0f, 0.0f),
        0.0001f
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneDirectionalLight BuildDefaultSceneDirectionalLight(const SceneViewBasis& basis){
    SceneDirectionalLight light;
    StoreFloat(
        __hidden_lighting::BuildDirectionalLightDirectionVector(LoadFloat(basis.forward)),
        &light.direction
    );
    light.colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    return light;
}

Core::ECS::EntityID CreateDirectionalLightEntity(
    Core::ECS::World& world,
    const f32 pitchRadians,
    const f32 yawRadians,
    const f32 rollRadians,
    const Float4& color,
    const f32 intensity
){
    auto lightEntity = world.createEntity();
    auto& transform = lightEntity.addComponent<TransformComponent>();
    StoreFloat(
        QuaternionRotationRollPitchYaw(pitchRadians, yawRadians, rollRadians),
        &transform.rotation
    );

    auto& light = lightEntity.addComponent<LightComponent>();
    light.type = LightType::Directional;
    light.setColor(color);
    light.setIntensity(intensity);
    return lightEntity.id();
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

    StoreFloat(
        __hidden_lighting::BuildDirectionalLightDirectionVector(
            Vector3Rotate(s_SIMDIdentityR2, rotation)
        ),
        &outLight.direction
    );
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


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

