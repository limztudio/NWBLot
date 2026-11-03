// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting.h"

#include <core/ecs/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 IsValidLightRotation(const SIMDVectorSingle rotation)noexcept{
    const SIMDVectorSingle rotationLengthSquared = QuaternionLengthSq(rotation);
    return !QuaternionIsNaN(rotation) && !QuaternionIsInfinite(rotation) && VectorIsFinite(rotationLengthSquared, VectorComponentMask::s_XYZW) && Vector4Greater(rotationLengthSquared, VectorReplicate(s_NormalizeLengthSquaredEpsilon));
}

[[nodiscard]] static b8 TryBuildSceneLightVectors(const TransformComponent& transform, const LightComponent& light, Float4& outPosition, Float4& outDirection)noexcept{
    const SIMDVectorSingle colorIntensity = LoadFloat(light.colorIntensity);
    if(!VectorIsFinite(colorIntensity, VectorComponentMask::s_XYZW) || !Vector4Greater(VectorSplatW(colorIntensity), VectorZero()))
        return false;

    switch(light.type){
    case LightType::Directional:{
        const SIMDVectorSingle rotation = LoadFloat(transform.rotation);
        if(!IsValidLightRotation(rotation))
            return false;

        outPosition = Float4(0.0f, 0.0f, 0.0f, 1.0f);
        StoreFloat(Vector3NormalizeOr(VectorNegate(Vector3Rotate(s_SIMDIdentityR2, rotation)), VectorSet(0.0f, 0.0f, -1.0f, 0.0f), s_NormalizeLengthSquaredEpsilon), outDirection);
        return true;
    }
    case LightType::Point:{
        const SIMDVectorSingle position = LoadFloat(transform.position);
        if(Vector3IsNaN(position) || Vector3IsInfinite(position))
            return false;
        if(!IsFinite(light.range) || light.range <= 0.0f)
            return false;

        StoreFloat(VectorSetW(position, 1.0f), outPosition);
        outDirection = Float4(0.0f, 0.0f, -1.0f, 1.0f);
        return true;
    }
    case LightType::Spot:{
        const SIMDVectorSingle rotation = LoadFloat(transform.rotation);
        if(!IsValidLightRotation(rotation))
            return false;

        const SIMDVectorSingle position = LoadFloat(transform.position);
        if(Vector3IsNaN(position) || Vector3IsInfinite(position))
            return false;
        if(!IsFinite(light.range) || light.range <= 0.0f)
            return false;
        if(!IsValidLightCone(light.innerConeCos, light.outerConeCos))
            return false;

        StoreFloat(VectorSetW(position, light.innerConeCos), outPosition);
        StoreFloat(VectorSetW(Vector3NormalizeOr(Vector3Rotate(s_SIMDIdentityR2, rotation), VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_NormalizeLengthSquaredEpsilon), light.outerConeCos), outDirection);
        return true;
    }
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::ECS::EntityID CreateDirectionalLightEntity(Core::ECS::World& world, const f32 pitchRadians, const f32 yawRadians, const f32 rollRadians, const Float4& color, const f32 intensity){
    auto lightEntity = world.createEntity();
    auto& transform = lightEntity.addComponent<TransformComponent>();
    StoreFloat(QuaternionRotationRollPitchYaw(pitchRadians, yawRadians, rollRadians), transform.rotation);

    auto& light = lightEntity.addComponent<LightComponent>();
    light.type = LightType::Directional;
    light.setColor(color);
    light.setIntensity(intensity);
    return lightEntity.id();
}

Core::ECS::EntityID CreatePointLightEntity(Core::ECS::World& world, const Float4& position, const Float4& color, const f32 intensity, const f32 range){
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
    StoreFloat(QuaternionRotationRollPitchYaw(pitchRadians, yawRadians, rollRadians), transform.rotation);

    auto& light = lightEntity.addComponent<LightComponent>();
    light.type = LightType::Spot;
    light.setColor(color);
    light.setIntensity(intensity);
    light.range = range;
    light.innerConeCos = innerConeCos;
    light.outerConeCos = outerConeCos;
    return lightEntity.id();
}

usize GatherSceneLights(Core::ECS::World& world, const SIMDVectorSingle defaultForward, const Span<SceneLight> outLights)noexcept{
    const usize maxLights = outLights.size();
    if(maxLights == 0u)
        return 0u;

    usize count = 0u;
    const auto lightView = world.view<TransformComponent, LightComponent>();
    for(auto it = lightView.begin(); it != lightView.end(); ++it){
        if(count >= maxLights)
            break;

        [[maybe_unused]] auto&& [entity, transform, light] = *it;

        Float4 position;
        Float4 direction;
        if(__hidden_lighting::TryBuildSceneLightVectors(transform, light, position, direction)){
            outLights[count] = MakeSceneLight(light, position, direction);
            ++count;
        }
    }

    if(count == 0u){
        Float4 direction;
        StoreFloat(Vector3NormalizeOr(VectorNegate(defaultForward), VectorSet(0.0f, 0.0f, -1.0f, 0.0f), s_NormalizeLengthSquaredEpsilon), direction);
        outLights[0] = MakeDefaultSceneLight(direction);
        count = 1u;
    }

    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

