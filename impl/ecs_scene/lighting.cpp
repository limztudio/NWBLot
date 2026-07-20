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

static SIMDVector BuildLightEmissionVector(const SIMDVector forward){
    return Vector3NormalizeOr(
        forward,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
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

static bool IsValidLightColorIntensity(const SIMDVector colorIntensity){
    const f32 intensity = VectorGetW(colorIntensity);
    return
        !Vector3IsNaN(colorIntensity)
        && !Vector3IsInfinite(colorIntensity)
        && IsFinite(intensity)
        && intensity > 0.0f
    ;
}

static bool IsValidLightCone(const f32 innerConeCos, const f32 outerConeCos){
    return
        IsFinite(innerConeCos)
        && IsFinite(outerConeCos)
        && outerConeCos >= -1.0f
        && outerConeCos <= innerConeCos
        && innerConeCos <= 1.0f
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

Core::ECS::EntityID CreatePointLightEntity(
    Core::ECS::World& world,
    const Float4& position,
    const Float4& color,
    const f32 intensity,
    const f32 range
){
    auto lightEntity = world.createEntity();
    auto& transform = lightEntity.addComponent<TransformComponent>();
    transform.position = position;

    auto& light = lightEntity.addComponent<LightComponent>();
    light.type = LightType::Point;
    light.setColor(color);
    light.setIntensity(intensity);
    light.range = range;
    return lightEntity.id();
}

Core::ECS::EntityID CreateSpotLightEntity(
    Core::ECS::World& world,
    const Float4& position,
    const f32 pitchRadians,
    const f32 yawRadians,
    const f32 rollRadians,
    const Float4& color,
    const f32 intensity,
    const f32 range,
    const f32 innerConeCos,
    const f32 outerConeCos
){
    auto lightEntity = world.createEntity();
    auto& transform = lightEntity.addComponent<TransformComponent>();
    transform.position = position;
    StoreFloat(
        QuaternionRotationRollPitchYaw(pitchRadians, yawRadians, rollRadians),
        &transform.rotation
    );

    auto& light = lightEntity.addComponent<LightComponent>();
    light.type = LightType::Spot;
    light.setColor(color);
    light.setIntensity(intensity);
    light.range = range;
    light.innerConeCos = innerConeCos;
    light.outerConeCos = outerConeCos;
    return lightEntity.id();
}

SceneLight BuildDefaultSceneLight(const SceneViewBasis& basis){
    SceneLight light;
    StoreFloat(
        __hidden_lighting::BuildDirectionalLightDirectionVector(LoadFloat(basis.forward)),
        &light.direction
    );
    light.colorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    light.type = LightType::Directional;
    return light;
}

bool TryBuildSceneLight(const TransformComponent& transform, const LightComponent& light, SceneLight& outLight){
    outLight = SceneLight{};
    const SIMDVector colorIntensity = LoadFloat(light.colorIntensity);
    if(!__hidden_lighting::IsValidLightColorIntensity(colorIntensity))
        return false;

    outLight.colorIntensity = light.colorIntensity;
    outLight.type = light.type;
    outLight.enableCaustics = light.enableCaustics;
    outLight.angularRadius = light.angularRadius;
    outLight.sourceRadius = light.sourceRadius;

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
    case LightType::Spot:{
        const SIMDVector rotation = LoadFloat(transform.rotation);
        if(!__hidden_lighting::IsValidLightRotation(rotation))
            return false;

        const SIMDVector position = LoadFloat(transform.position);
        if(Vector3IsNaN(position) || Vector3IsInfinite(position))
            return false;
        if(!IsFinite(light.range) || light.range <= 0.0f)
            return false;
        if(!__hidden_lighting::IsValidLightCone(light.innerConeCos, light.outerConeCos))
            return false;

        StoreFloat(VectorSetW(position, light.innerConeCos), &outLight.position);
        StoreFloat(
            VectorSetW(
                __hidden_lighting::BuildLightEmissionVector(Vector3Rotate(s_SIMDIdentityR2, rotation)),
                light.outerConeCos
            ),
            &outLight.direction
        );
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
        outLights[0] = BuildDefaultSceneLight(defaultBasis);
        count = 1u;
    }

    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

