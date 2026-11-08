// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "soft_shadow_scene.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <tests/smoke/scene_types.adhd.h>

#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_render/material/material_instance.h>

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_soft_shadow_scene{

using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;
using NWB::Tests::Smoke::s_SmokeSurfaceMaterialInterface;
using SoftShadowModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using SoftShadowMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using SoftShadowMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;

// Opaque and glass casters under directional, point, and spot soft shadows.
static constexpr SoftShadowModelRef s_Model{"project/characters/body/model"};
static constexpr SoftShadowMaterialRef s_OpaqueMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr SoftShadowMaterialRef s_TransparentMaterial{"project/smoke/transparent_multi/materials/shared"};
static constexpr SoftShadowMaterialRef s_TimingOpaqueMaterial{"project/smoke/soft_shadow/materials/timing_opaque"};
static constexpr SoftShadowMaterialRef s_TimingTransparentMaterial{"project/smoke/soft_shadow/materials/timing_transparent"};
static constexpr SoftShadowMeshRef s_GroundMesh{"project/meshes/shadow_plane"};

static constexpr f32 s_GroundScale = NWB::Tests::Smoke::s_SoftShadowGroundScale;

static constexpr f32 s_CameraDistance = NWB::Tests::Smoke::s_SoftShadowCameraDistance;
static constexpr f32 s_CameraHeight = NWB::Tests::Smoke::s_SoftShadowCameraHeight;
static constexpr f32 s_CameraPitch = NWB::Tests::Smoke::s_SoftShadowCameraPitch;

// Low sun angle casts long shadows that soften with distance.
static constexpr f32 s_DirectionalLightPitch = NWB::Tests::Smoke::s_SoftShadowDirectionalLightPitch;
static constexpr f32 s_DirectionalLightYaw = NWB::Tests::Smoke::s_SoftShadowDirectionalLightYaw;
static constexpr f32 s_DirectionalLightIntensity = NWB::Tests::Smoke::s_SoftShadowDirectionalLightIntensity;

// Different light colors and directions separate their overlapping penumbras.
static constexpr f32 s_PointLightIntensity = NWB::Tests::Smoke::s_SoftShadowPointLightIntensity;
static constexpr f32 s_PointLightRange = NWB::Tests::Smoke::s_SoftShadowPointLightRange;
static constexpr f32 s_SpotLightIntensity = NWB::Tests::Smoke::s_SoftShadowSpotLightIntensity;
static constexpr f32 s_SpotLightRange = NWB::Tests::Smoke::s_SoftShadowSpotLightRange;
static constexpr f32 s_SpotLightPitch = NWB::Tests::Smoke::s_SoftShadowSpotLightPitch;
static constexpr f32 s_SpotLightYaw = NWB::Tests::Smoke::s_SoftShadowSpotLightYaw;
static constexpr f32 s_SpotInnerConeCos = NWB::Tests::Smoke::s_SoftShadowSpotInnerConeCos;
static constexpr f32 s_SpotOuterConeCos = NWB::Tests::Smoke::s_SoftShadowSpotOuterConeCos;


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 NWB::Tests::Smoke::SoftShadowScene::create(NWB::Core::ECS::World& world, const b8 timingEnabled, const f32 angularRadius, const f32 sourceRadius){
    using namespace __hidden_soft_shadow_scene;
    const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(s_CameraHeight, s_CameraDistance, s_CameraPitch, world);

    const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(world, s_DirectionalLightPitch, s_DirectionalLightYaw, 0.0f, Float4(1.00f, 0.96f, 0.88f), s_DirectionalLightIntensity);
    if(auto* light = world.tryGetComponent<NWB::Impl::Scene::LightComponent>(directionalLight)){
        light->angularRadius = angularRadius;
        light->enableCaustics = false;
    }

    const NWB::Core::ECS::EntityID pointLight = NWB::Impl::Scene::CreatePointLightEntity(world, Float4(1.5f, 2.2f, 0.3f, 0.0f), Float4(1.00f, 0.35f, 0.30f), s_PointLightIntensity, s_PointLightRange);
    if(auto* light = world.tryGetComponent<NWB::Impl::Scene::LightComponent>(pointLight)){
        light->sourceRadius = sourceRadius;
        light->enableCaustics = false;
    }

    const NWB::Core::ECS::EntityID spotLight = NWB::Impl::Scene::CreateSpotLightEntity(
        world,
        Float4(0.4f, 3.0f, 0.4f, 0.0f),
        s_SpotLightPitch,
        s_SpotLightYaw,
        0.0f,
        Float4(0.35f, 0.55f, 1.00f),
        s_SpotLightIntensity,
        s_SpotLightRange,
        s_SpotInnerConeCos,
        s_SpotOuterConeCos
    );
    if(auto* light = world.tryGetComponent<NWB::Impl::Scene::LightComponent>(spotLight)){
        light->sourceRadius = sourceRadius;
        light->enableCaustics = false;
    }

    // Timing materials omit stochastic surfel GI while keeping direct shadows.
    const auto& opaqueMaterial = timingEnabled ? s_TimingOpaqueMaterial : s_OpaqueMaterial;
    const auto& transparentMaterial = timingEnabled ? s_TimingTransparentMaterial : s_TransparentMaterial;
    m_groundEntity = CreateTintedStaticMeshEntity(s_GroundMesh, opaqueMaterial, s_SmokeSurfaceMaterialInterface, Float4(0.82f, 0.82f, 0.85f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(s_GroundScale, 1.0f, s_GroundScale, 0.0f), world);

    // The opaque caster provides the contact-hardening reference.
    const auto characterModel = CreateTintedModelEntity(s_Model, opaqueMaterial, s_SmokeSurfaceMaterialInterface, Float4(0.86f, 0.80f, 0.74f, 1.0f), Float4(0.7f, 0.0f, -1.1f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 0.0f), world);
    m_characterOwner = characterModel.entity;
    if(!characterModel.tintApplied)
        NWB_LOGGER_ERROR(NWB_TEXT("SoftShadowTestSmokeProject: failed to set character tint"));

    // The glass caster must preserve tint while its penumbra widens with receiver distance.
    const auto glassModel = CreateTintedModelEntity(
        s_Model,
        transparentMaterial,
        s_SmokeSurfaceMaterialInterface,
        // Glass caster tint and density for Beer-Lambert shadow absorption.
        Float4(0.20f, 0.55f, 0.12f, 0.6f), Float4(-0.6f, 0.0f, -1.1f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 0.0f),
        world
    );
    m_glassOwner = glassModel.entity;
    if(!glassModel.tintApplied)
        NWB_LOGGER_ERROR(NWB_TEXT("SoftShadowTestSmokeProject: failed to set glass tint"));

    SyncSmokeModelRuntimes(world);

    NWB_FATAL_ASSERT_MSG(activeCamera.valid() && m_groundEntity.valid() && m_characterOwner.valid() && m_glassOwner.valid(), NWB_TEXT("SoftShadowTestSmokeProject failed to create all scene entities"));

    if(timingEnabled){
        const Array<NWB::Core::ECS::EntityID, 3> lights = { directionalLight, pointLight, spotLight };
        for(const auto lightEntity : lights){
            const auto* light = world.tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity);
            if(!light || light->enableCaustics){
                NWB_LOGGER_ERROR(NWB_TEXT("ShadowTimingProbe: timing light policy unavailable"));
                return false;
            }
        }
    }
    return true;
}


void NWB::Tests::Smoke::SoftShadowScene::spinCasters(NWB::Core::ECS::World& world, const f32 yaw)const noexcept{
    for(const NWB::Core::ECS::EntityID owner : { m_characterOwner, m_glassOwner }){
        auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(owner);
        if(transform)
            StoreFloat(QuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f), transform->rotation);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

