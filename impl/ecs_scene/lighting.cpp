// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting.h"

#include <core/ecs/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Length-squared epsilon for degenerate normalization inputs.
inline constexpr f32 s_NormalizeLengthSquaredEpsilon = 0.0001f;
// Cosine clamping bounds for valid light cone angles.
inline constexpr f32 s_ConeCosineMin = -1.0f;
inline constexpr f32 s_ConeCosineMax = 1.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static SIMDVector BuildDirectionalLightDirectionVector(const SIMDVector forward)noexcept{
    return Vector3NormalizeOr(
        VectorNegate(forward),
        VectorSet(0.0f, 0.0f, -1.0f, 0.0f),
        s_NormalizeLengthSquaredEpsilon
    );
}

static SIMDVector BuildLightEmissionVector(const SIMDVector forward)noexcept{
    return Vector3NormalizeOr(
        forward,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        s_NormalizeLengthSquaredEpsilon
    );
}

static bool IsValidLightRotation(const SIMDVector rotation)noexcept{
    const f32 rotationLengthSquared = VectorGetX(QuaternionLengthSq(rotation));
    return
        !QuaternionIsNaN(rotation)
        && !QuaternionIsInfinite(rotation)
        && IsFinite(rotationLengthSquared)
        && rotationLengthSquared > s_NormalizeLengthSquaredEpsilon
    ;
}

static bool IsValidLightColorIntensity(const SIMDVector colorIntensity)noexcept{
    const f32 intensity = VectorGetW(colorIntensity);
    return
        !Vector3IsNaN(colorIntensity)
        && !Vector3IsInfinite(colorIntensity)
        && IsFinite(intensity)
        && intensity > 0.0f
    ;
}

static bool IsValidLightCone(const f32 innerConeCos, const f32 outerConeCos)noexcept{
    return
        IsFinite(innerConeCos)
        && IsFinite(outerConeCos)
        && outerConeCos >= s_ConeCosineMin
        && outerConeCos <= innerConeCos
        && innerConeCos <= s_ConeCosineMax
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
        transform.rotation
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
        transform.rotation
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

SceneLight BuildDefaultSceneLight(const SIMDVector forward)noexcept{
    SceneLight light;
    StoreFloat(__hidden_lighting::BuildDirectionalLightDirectionVector(forward), light.direction);
    StoreFloat(s_SIMDOne, light.colorIntensity);
    light.type = LightType::Directional;
    return light;
}

Expected<SceneLight> TryBuildSceneLight(
    const SIMDVector position,
    const SIMDVector rotation,
    const SIMDVector colorIntensity,
    const f32 range,
    const f32 innerConeCos,
    const f32 outerConeCos,
    const f32 angularRadius,
    const f32 sourceRadius,
    const LightType::Enum type,
    const bool enableCaustics
)noexcept{
    SceneLight light;
    if(!__hidden_lighting::IsValidLightColorIntensity(colorIntensity))
        return MakeUnexpected(Failure{});

    StoreFloat(colorIntensity, light.colorIntensity);
    light.type = type;
    light.enableCaustics = enableCaustics;
    light.angularRadius = angularRadius;
    light.sourceRadius = sourceRadius;

    switch(type){
    case LightType::Directional:{
        if(!__hidden_lighting::IsValidLightRotation(rotation))
            return MakeUnexpected(Failure{});

        StoreFloat(
            __hidden_lighting::BuildDirectionalLightDirectionVector(Vector3Rotate(s_SIMDIdentityR2, rotation)),
            light.direction
        );
        return light;
    }
    case LightType::Point:{
        if(Vector3IsNaN(position) || Vector3IsInfinite(position))
            return MakeUnexpected(Failure{});
        if(!IsFinite(range) || range <= 0.0f)
            return MakeUnexpected(Failure{});

        StoreFloat(VectorSetW(position, 1.0f), light.position);
        light.range = range;
        return light;
    }
    case LightType::Spot:{
        if(!__hidden_lighting::IsValidLightRotation(rotation))
            return MakeUnexpected(Failure{});

        if(Vector3IsNaN(position) || Vector3IsInfinite(position))
            return MakeUnexpected(Failure{});
        if(!IsFinite(range) || range <= 0.0f)
            return MakeUnexpected(Failure{});
        if(!__hidden_lighting::IsValidLightCone(innerConeCos, outerConeCos))
            return MakeUnexpected(Failure{});

        StoreFloat(VectorSetW(position, innerConeCos), light.position);
        StoreFloat(
            VectorSetW(
                __hidden_lighting::BuildLightEmissionVector(Vector3Rotate(s_SIMDIdentityR2, rotation)),
                outerConeCos
            ),
            light.direction
        );
        light.range = range;
        return light;
    }
    default:
        return MakeUnexpected(Failure{});
    }
}

usize GatherSceneLights(Core::ECS::World& world, const SIMDVector defaultForward, SceneLight* outLights, const usize maxLights){
    if(maxLights == 0u)
        return 0u;

    usize count = 0u;
    const auto lightView = world.view<TransformComponent, LightComponent>();
    for(auto it = lightView.begin(); it != lightView.end(); ++it){
        if(count >= maxLights)
            break;

        auto&& [entity, transform, light] = *it;
        static_cast<void>(entity);

        const SIMDVector colorIntensity = LoadFloat(light.colorIntensity);
        Expected<SceneLight> resolvedLight = MakeUnexpected(Failure{});
        switch(light.type){
        case LightType::Directional:
            resolvedLight = TryBuildSceneLight(
                s_SIMDZero,
                LoadFloat(transform.rotation),
                colorIntensity,
                light.range,
                light.innerConeCos,
                light.outerConeCos,
                light.angularRadius,
                light.sourceRadius,
                light.type,
                light.enableCaustics
            );
            break;
        case LightType::Point:
            resolvedLight = TryBuildSceneLight(
                LoadFloat(transform.position),
                s_SIMDIdentityR3,
                colorIntensity,
                light.range,
                light.innerConeCos,
                light.outerConeCos,
                light.angularRadius,
                light.sourceRadius,
                light.type,
                light.enableCaustics
            );
            break;
        case LightType::Spot:
            resolvedLight = TryBuildSceneLight(
                LoadFloat(transform.position),
                LoadFloat(transform.rotation),
                colorIntensity,
                light.range,
                light.innerConeCos,
                light.outerConeCos,
                light.angularRadius,
                light.sourceRadius,
                light.type,
                light.enableCaustics
            );
            break;
        default:
            break;
        }
        if(resolvedLight){
            outLights[count] = *resolvedLight;
            ++count;
        }
    }

    if(count == 0u){
        outLights[0] = BuildDefaultSceneLight(defaultForward);
        count = 1u;
    }

    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

