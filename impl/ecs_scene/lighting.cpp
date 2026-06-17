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

static bool IsValidLightRotation(const SIMDVector rotation){
    const f32 rotationLengthSquared = VectorGetX(QuaternionLengthSq(rotation));
    return
        !QuaternionIsNaN(rotation)
        && !QuaternionIsInfinite(rotation)
        && IsFinite(rotationLengthSquared)
        && rotationLengthSquared > 0.0001f
    ;
}

static bool IsValidLightColorIntensity(const LightComponent& light){
    const SIMDVector colorIntensity = LoadFloat(light.colorIntensity);
    return
        !Vector3IsNaN(colorIntensity)
        && !Vector3IsInfinite(colorIntensity)
        && IsFinite(light.intensity())
        && light.intensity() > 0.0f
    ;
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
    if(!__hidden_lighting::IsValidLightRotation(rotation))
        return false;
    if(!__hidden_lighting::IsValidLightColorIntensity(light))
        return false;

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

bool TryBuildSceneLight(const TransformComponent& transform, const LightComponent& light, SceneLight& outLight){
    outLight = SceneLight{};
    if(!__hidden_lighting::IsValidLightColorIntensity(light))
        return false;

    outLight.colorIntensity = light.colorIntensity;
    outLight.type = light.type;

    switch(light.type){
    case LightType::Directional:{
        const SIMDVector rotation = LoadFloat(transform.rotation);
        if(!__hidden_lighting::IsValidLightRotation(rotation))
            return false;

        StoreFloat(
            __hidden_lighting::BuildDirectionalLightDirectionVector(
                Vector3Rotate(s_SIMDIdentityR2, rotation)
            ),
            &outLight.direction
        );
        return true;
    }
    case LightType::Point:{
        const SIMDVector position = LoadFloat(transform.position);
        if(Vector3IsNaN(position) || Vector3IsInfinite(position))
            return false;
        if(!IsFinite(light.range) || light.range <= 0.0f)
            return false;

        StoreFloat(VectorSetW(position, 1.0f), &outLight.position);
        outLight.range = light.range;
        return true;
    }
    default:
        return false;
    }
}

usize GatherSceneLights(Core::ECS::World& world, const SceneViewBasis& defaultBasis, SceneLight* outLights, usize maxLights){
    if(maxLights == 0u)
        return 0u;

    usize count = 0u;
    const auto lightView = world.view<TransformComponent, LightComponent>();
    for(auto it = lightView.begin(); it != lightView.end(); ++it){
        if(count >= maxLights)
            break;

        auto&& [entity, transform, light] = *it;
        static_cast<void>(entity);

        SceneLight resolvedLight;
        if(TryBuildSceneLight(transform, light, resolvedLight)){
            outLights[count] = resolvedLight;
            ++count;
        }
    }

    if(count == 0u){
        SceneLight& fallbackLight = outLights[0];
        fallbackLight = SceneLight{};

        const SceneDirectionalLight defaultLight = BuildDefaultSceneDirectionalLight(defaultBasis);
        fallbackLight.direction = defaultLight.direction;
        fallbackLight.colorIntensity = defaultLight.colorIntensity;
        fallbackLight.type = LightType::Directional;
        count = 1u;
    }

    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

