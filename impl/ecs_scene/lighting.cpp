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


static SIMDVectorSingle BuildDirectionalLightDirectionVector(const SIMDVectorSingle forward)noexcept{
    return Vector3NormalizeOr(VectorNegate(forward), VectorSet(0.0f, 0.0f, -1.0f, 0.0f), s_NormalizeLengthSquaredEpsilon);
}

static SIMDVectorSingle BuildLightEmissionVector(const SIMDVectorSingle forward)noexcept{
    return Vector3NormalizeOr(forward, VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_NormalizeLengthSquaredEpsilon);
}

static bool IsValidLightRotation(const SIMDVectorSingle rotation)noexcept{
    const SIMDVectorSingle rotationLengthSquared = QuaternionLengthSq(rotation);
    return !QuaternionIsNaN(rotation) && !QuaternionIsInfinite(rotation) && VectorIsFinite(rotationLengthSquared, VectorComponentMask::s_XYZW) && Vector4Greater(rotationLengthSquared, VectorReplicate(s_NormalizeLengthSquaredEpsilon));
}

static bool IsValidLightColorIntensity(const SIMDVectorSingle colorIntensity)noexcept{
    return VectorIsFinite(colorIntensity, VectorComponentMask::s_XYZW) && Vector4Greater(VectorSplatW(colorIntensity), VectorZero());
}

static bool IsValidLightCone(const f32 innerConeCos, const f32 outerConeCos)noexcept{
    return IsFinite(innerConeCos) && IsFinite(outerConeCos) && outerConeCos >= s_ConeCosineMin && outerConeCos <= innerConeCos && innerConeCos <= s_ConeCosineMax;
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

namespace __hidden_lighting{

[[nodiscard]] Expected<SIMDMatrix> TryBuildSceneLightVectors(
    const SIMDVectorSingle position,
    const SIMDVectorSingle rotation,
    const SIMDVectorSingle colorIntensity,
    const f32 range,
    const f32 innerConeCos,
    const f32 outerConeCos,
    const LightType::Enum type
)noexcept{
    SIMDMatrix light{ .v = { VectorSet(0.0f, 0.0f, 0.0f, 1.0f), VectorSet(0.0f, 0.0f, -1.0f, 1.0f), colorIntensity, VectorZero() } };
    if(!__hidden_lighting::IsValidLightColorIntensity(colorIntensity))
        return MakeUnexpected(Failure{});

    switch(type){
    case LightType::Directional:{
        if(!__hidden_lighting::IsValidLightRotation(rotation))
            return MakeUnexpected(Failure{});

        light.v[1] = __hidden_lighting::BuildDirectionalLightDirectionVector(Vector3Rotate(s_SIMDIdentityR2, rotation));
        return light;
    }
    case LightType::Point:{
        if(Vector3IsNaN(position) || Vector3IsInfinite(position))
            return MakeUnexpected(Failure{});
        if(!IsFinite(range) || range <= 0.0f)
            return MakeUnexpected(Failure{});

        light.v[0] = VectorSetW(position, 1.0f);
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

        light.v[0] = VectorSetW(position, innerConeCos);
        light.v[1] = VectorSetW(__hidden_lighting::BuildLightEmissionVector(Vector3Rotate(s_SIMDIdentityR2, rotation)), outerConeCos);
        return light;
    }
    default:
        return MakeUnexpected(Failure{});
    }
}

};

usize GatherSceneLights(Core::ECS::World& world, const SIMDVectorSingle defaultForward, const usize maxLights, SceneLight* outLights)noexcept{
    if(maxLights == 0u)
        return 0u;

    usize count = 0u;
    const auto lightView = world.view<TransformComponent, LightComponent>();
    for(auto it = lightView.begin(); it != lightView.end(); ++it){
        if(count >= maxLights)
            break;

        auto&& [entity, transform, light] = *it;
        static_cast<void>(entity);

        const SIMDVectorSingle colorIntensity = LoadFloat(light.colorIntensity);
        Expected<SIMDMatrix> resolvedLight = MakeUnexpected(Failure{});
        switch(light.type){
        case LightType::Directional:
            resolvedLight = __hidden_lighting::TryBuildSceneLightVectors(s_SIMDZero, LoadFloat(transform.rotation), colorIntensity, light.range, light.innerConeCos, light.outerConeCos, light.type);
            break;
        case LightType::Point:
            resolvedLight = __hidden_lighting::TryBuildSceneLightVectors(LoadFloat(transform.position), s_SIMDIdentityR3, colorIntensity, light.range, light.innerConeCos, light.outerConeCos, light.type);
            break;
        case LightType::Spot:
            resolvedLight = __hidden_lighting::TryBuildSceneLightVectors(LoadFloat(transform.position), LoadFloat(transform.rotation), colorIntensity, light.range, light.innerConeCos, light.outerConeCos, light.type);
            break;
        default:
            break;
        }
        if(resolvedLight){
            SceneLight& destination = outLights[count];
            StoreFloat(resolvedLight->v[0], destination.position);
            StoreFloat(resolvedLight->v[1], destination.direction);
            StoreFloat(colorIntensity, destination.colorIntensity);
            destination.range = light.type == LightType::Directional ? 0.0f : light.range;
            destination.angularRadius = light.angularRadius;
            destination.sourceRadius = light.sourceRadius;
            destination.type = light.type;
            destination.enableCaustics = light.enableCaustics;
            ++count;
        }
    }

    if(count == 0u){
        outLights[0] = SceneLight{};
        StoreFloat(__hidden_lighting::BuildDirectionalLightDirectionVector(defaultForward), outLights[0].direction);
        StoreFloat(s_SIMDOne, outLights[0].colorIntensity);
        count = 1u;
    }

    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

