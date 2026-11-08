// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stress_crowd_scene.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_render/material/material_instance.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/frame.h>
#include <global/simd/constant.h>
#include <global/simd/quaternion.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stress_crowd_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;

using StressModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using StressMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using StressMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Spinning opaque/glass crowds exercise TLAS updates and overlapping shadows inside the GI box.
static constexpr StressModelRef s_Model{"project/characters/body/model"};
static constexpr StressModelRef s_CausticModel{"project/characters/caustic_closed_body/model"};
static constexpr StressMaterialRef s_TransparentMaterial{"project/smoke/transparent_multi/materials/shared"};
static constexpr StressMaterialRef s_OpaqueMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr StressMaterialRef s_GroundMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr StressMeshRef s_GroundMesh{"project/meshes/shadow_plane"};
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr u32 s_CharacterTintCount = 5u;
static constexpr f32 s_CharacterSpacingX = 0.72f;                     // tight so neighbours' shadows overlap
static constexpr f32 s_TransparentRowZ = -0.55f;
static constexpr f32 s_OpaqueRowZ = 0.55f;
static constexpr f32 s_CharacterLift = 0.0f;

// Lanes are side half-width and depth half-width.
static constexpr Float2U s_BoxHalf = Float2U(4.0f, 4.5f);
static constexpr f32 s_BoxHeight = 4.0f;
static constexpr f32 s_GroundScale = 2.0f * s_BoxHalf.y;

static constexpr f32 s_ComparisonCameraDistance = 4.8f;
static constexpr f32 s_ComparisonCameraHeight = 1.8f;
static constexpr f32 s_ComparisonCameraPitch = 0.2f;
static constexpr f32 s_CameraDistance = 7.2f;
static constexpr f32 s_CameraHeight = 2.7f;
static constexpr f32 s_CameraPitch = 0.25f;

static constexpr f32 s_DirectionalLightPitch = 0.9f;
static constexpr f32 s_DirectionalLightYaw = 0.65f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;

static constexpr f32 s_PointLightHeight = 2.6f;
static constexpr f32 s_PointLightIntensity = 9.0f;
static constexpr f32 s_PointLightRange = 16.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cycle the five-color palette across each material row.
[[nodiscard]] static Float4 CharacterTint(const u32 classIndex, const b8 transparent)noexcept{
    static const Array<Float4, s_CharacterTintCount> s_TransparentTints = {
        Float4(0.72f, 0.86f, 1.00f, 0.42f),
        Float4(0.60f, 1.00f, 0.85f, 0.42f),
        Float4(1.00f, 0.82f, 0.88f, 0.44f),
        Float4(0.86f, 0.80f, 1.00f, 0.45f),
        Float4(0.70f, 0.96f, 1.00f, 0.40f),
    };
    static const Array<Float4, s_CharacterTintCount> s_OpaqueTints = {
        Float4(0.90f, 0.52f, 0.42f, 1.0f),
        Float4(0.52f, 0.70f, 0.90f, 1.0f),
        Float4(0.62f, 0.82f, 0.52f, 1.0f),
        Float4(0.88f, 0.78f, 0.50f, 1.0f),
        Float4(0.74f, 0.62f, 0.84f, 1.0f),
    };
    const u32 slot = classIndex % s_CharacterTintCount;
    return transparent ? s_TransparentTints[slot] : s_OpaqueTints[slot];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StressCrowdScene::StressCrowdScene(ProjectRuntimeContext& context, Core::ECS::World& world)noexcept
    : m_world(world)
    , m_characterOwners(context.objectArena)
{}

b8 StressCrowdScene::initialize([[maybe_unused]] ProjectRuntimeContext& context, const u32 charactersPerClass, const b8 comparison, const b8 opaqueOnly, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_stress_crowd_scene;

    m_charactersPerClass = charactersPerClass;
    m_comparison = comparison;
    m_opaqueOnly = opaqueOnly;
    const f32 cameraHeight = comparison ? s_ComparisonCameraHeight : s_CameraHeight;
    const f32 cameraDistance = comparison ? s_ComparisonCameraDistance : s_CameraDistance;
    const f32 cameraPitch = comparison ? s_ComparisonCameraPitch : s_CameraPitch;
    const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(cameraHeight, cameraDistance, cameraPitch, m_world);
    const auto* const camera = m_world.tryGetComponent<NWB::Impl::Scene::CameraComponent>(activeCamera);
    if(!camera){
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: workload camera is unavailable"));
        return false;
    }

    const f32 cameraVerticalFov = camera->verticalFovRadians();
    const f32 cameraNearPlane = camera->nearPlane();
    const f32 cameraFarPlane = camera->farPlane();
    const f32 cameraAspectRatio = camera->aspectRatio();

    const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(m_world, s_DirectionalLightPitch, s_DirectionalLightYaw, 0.0f, Float4(1.0f, 0.96f, 0.88f), s_DirectionalLightIntensity);
    const NWB::Core::ECS::EntityID pointLight = NWB::Impl::Scene::CreatePointLightEntity(m_world, Float4(0.8f, s_PointLightHeight, -0.6f, 0.0f), Float4(0.60f, 0.76f, 1.00f), s_PointLightIntensity, s_PointLightRange);

    m_groundEntity = CreateTintedStaticMeshEntity(s_GroundMesh, s_GroundMaterial, s_SmokeSurfaceMaterialInterface, Float4(0.82f, 0.82f, 0.85f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(s_GroundScale, 1.0f, s_GroundScale, 0.0f), m_world);

    // Lanes are side-wall extent and back-wall extent.
    const SIMDVectorSingle wallSpans = VectorMultiply(VectorReplicate(2.0f), VectorSet(s_BoxHalf.y, s_BoxHalf.x, 0.0f, 0.0f));
    m_wallPosX = createWall(Float4(s_BoxHalf.x, s_BoxHeight * 0.5f, 0.0f, 0.0f), Float4(0.80f, 0.08f, 0.08f, 1.0f), -90.0f, VectorGetX(wallSpans));
    m_wallNegX = createWall(Float4(-s_BoxHalf.x, s_BoxHeight * 0.5f, 0.0f, 0.0f), Float4(0.08f, 0.12f, 0.80f, 1.0f), 90.0f, VectorGetX(wallSpans));
    m_wallPosZ = createWall(Float4(0.0f, s_BoxHeight * 0.5f, s_BoxHalf.y, 0.0f), Float4(0.10f, 0.72f, 0.14f, 1.0f), 180.0f, VectorGetY(wallSpans));
    m_ceiling = createCeiling(Float4(0.90f, 0.86f, 0.72f, 1.0f));

    const u32 characterCount = m_charactersPerClass * 2u;
    m_characterOwners.reserve(characterCount);
    for(u32 index = 0u; index < characterCount; ++index)
        m_characterOwners.push_back(createCharacter(index));

    SyncSmokeModelRuntimes(m_world);
    for(usize index = 0u; index < m_characterOwners.size(); ++index){
        if(m_opaqueOnly || index % 2u != 0u)
            continue;
        const auto meshEntity = NWB::Tests::Smoke::FindSpawnedModelObject(m_characterOwners[index], Name("mesh"), NWB::Impl::ModelObjectKind::SkinnedMesh, m_world);
        auto* const renderer = m_world.tryGetComponent<NWB::Impl::RendererComponent>(meshEntity);
        if(!renderer){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: closed refractor renderer is unavailable (index {})"), index);
            return false;
        }
        renderer->opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;
    }
    if(!m_csgScene.initialize(m_characterOwners, m_opaqueOnly, m_world, scratchArena))
        return false;

    b8 allCharactersValid = m_characterOwners.size() == characterCount;
    for(const NWB::Core::ECS::EntityID owner : m_characterOwners)
        allCharactersValid = allCharactersValid && owner.valid();

    NWB_FATAL_ASSERT_MSG(
        activeCamera.valid() && directionalLight.valid() && pointLight.valid() && m_groundEntity.valid() && allCharactersValid
        && m_wallPosX.valid() && m_wallNegX.valid() && m_wallPosZ.valid() && m_ceiling.valid(),
        NWB_TEXT("StressTestSmokeProject failed to create all scene entities")
    );

#if defined(NWB_HARDWARE_SHADOW_BOUNDARY_BENCHMARK)
    const b8 rayQueryCapable = context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct) && context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
    if(!rayQueryCapable)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: hardware shadow boundary skipped because RayQuery-capable hardware is unavailable"));
    else{
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: RayQuery-capable hardware shadow route available"));
        if(m_opaqueOnly)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: enabled natural opaque hardware-shadow baseline"));
        else
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: enabled healthy hardware transparent-shadow benchmark"));
    }
#endif

    const u32 transparentCharacterCount = m_opaqueOnly ? 0u : m_charactersPerClass;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: spawned {} spinning characters ({} transparent + {} opaque) over ground, directional + point light")
        , m_characterOwners.size()
        , transparentCharacterCount
        , characterCount - transparentCharacterCount
    );
    const TStringView layout = comparison ? NWB_TEXT("zigzag_v1") : NWB_TEXT("two_rows_v1");
    const SIMDVectorSingle rowSpacing = VectorMultiply(VectorReplicate(s_CharacterSpacingX), VectorSet(comparison ? 2.0f : 1.0f, comparison ? 0.5f : 0.25f, 0.0f, 0.0f));
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: workload characters_per_class={} total={} transparent={} opaque={}")
        NWB_TEXT(" layout={} rows=2 columns={} row_spacing_x={} row_stagger_x={} front_z={} back_z={} body_scale=1")
        NWB_TEXT(" camera_x=0 camera_y={} camera_z={} camera_pitch={} vertical_fov={} near_plane={} far_plane={} aspect={}")
        , m_charactersPerClass
        , m_characterOwners.size()
        , transparentCharacterCount
        , characterCount - transparentCharacterCount
        , layout
        , m_charactersPerClass
        , VectorGetX(rowSpacing)
        , VectorGetY(rowSpacing)
        , s_TransparentRowZ
        , s_OpaqueRowZ
        , cameraHeight
        , -cameraDistance
        , cameraPitch
        , cameraVerticalFov
        , cameraNearPlane
        , cameraFarPlane
        , cameraAspectRatio
    );
    return true;
}

b8 StressCrowdScene::update(const f32 yaw){
    spinCharacters(yaw);
    return m_csgScene.update(yaw, m_world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Core::ECS::EntityID StressCrowdScene::createCharacter(const u32 index){
    using namespace __hidden_stress_crowd_scene;

    // Comparison mode uses the zigzag layout instead of staggered material rows.
    const b8 transparentMaterialClass = (index % 2u) == 0u;
    const b8 transparent = !m_opaqueOnly && transparentMaterialClass;
    const u32 classIndex = index / 2u;
    const b8 comparison = m_comparison;
    const f32 slot = static_cast<f32>(comparison ? index : classIndex);
    const f32 lastSlot = static_cast<f32>(comparison ? m_charactersPerClass * 2u - 1u : m_charactersPerClass - 1u);
    const f32 slotOffset = slot - lastSlot * 0.5f;
    const f32 x = comparison ? slotOffset * s_CharacterSpacingX : slotOffset * s_CharacterSpacingX + (transparentMaterialClass ? -0.25f : 0.25f) * s_CharacterSpacingX;
    const f32 z = transparentMaterialClass ? s_TransparentRowZ : s_OpaqueRowZ;

    const auto modelResult = CreateTintedModelEntity(
        transparentMaterialClass ? s_CausticModel : s_Model,
        transparent ? s_TransparentMaterial : s_OpaqueMaterial,
        s_SmokeSurfaceMaterialInterface,
        CharacterTint(classIndex, transparent),
        Float4(x, s_CharacterLift, z, 0.0f),
        Float4(1.0f, 1.0f, 1.0f, 0.0f),
        m_world
    );
    const NWB::Core::ECS::EntityID entity = modelResult.entity;
    if(!modelResult.tintApplied)
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: failed to set character tint (index {})"), index);

    return entity;
}

// The XZ source plane spans -1..+1, so wall scales are half-extents.
NWB::Core::ECS::EntityID StressCrowdScene::createWall(const Float4& position, const Float4& colorTint, const f32 yawDeg, const f32 spanScale){
    using namespace __hidden_stress_crowd_scene;

    const SIMDVectorSingle pitchQuat = QuaternionRotationRollPitchYaw(s_PIDIV2, 0.0f, 0.0f);
    const SIMDVectorSingle yawQuat = QuaternionRotationRollPitchYawFromVector(VectorScale(VectorSet(0.0f, yawDeg, 0.0f, 0.0f), s_PI / 180.0f));
    // QuaternionMultiply applies its right operand first: pitch before yaw keeps side walls facing inward.
    const SIMDVectorSingle wallRotation = QuaternionMultiply(yawQuat, pitchQuat);

    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(s_GroundMesh, s_OpaqueMaterial, s_SmokeSurfaceMaterialInterface, colorTint, position, Float4(spanScale, 1.0f, s_BoxHeight, 0.0f), m_world);
    if(entity.valid()){
        if(auto* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity))
            StoreFloat(wallRotation, transform->rotation);
    }
    return entity;
}

// Face the ceiling downward so it bounces light into the crowd.
NWB::Core::ECS::EntityID StressCrowdScene::createCeiling(const Float4& colorTint){
    using namespace __hidden_stress_crowd_scene;

    const SIMDVectorSingle ceilingRotation = QuaternionRotationRollPitchYaw(s_PI, 0.0f, 0.0f);
    const Float4 ceilingScale(2.0f * s_BoxHalf.x, 1.0f, 2.0f * s_BoxHalf.y, 0.0f);
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(s_GroundMesh, s_OpaqueMaterial, s_SmokeSurfaceMaterialInterface, colorTint, Float4(0.0f, s_BoxHeight, 0.0f, 0.0f), ceilingScale, m_world);
    if(entity.valid()){
        if(auto* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity))
            StoreFloat(ceilingRotation, transform->rotation);
    }
    return entity;
}

void StressCrowdScene::spinCharacters(const f32 yawBase)noexcept{
    const f32 phaseStep = s_2PI / static_cast<f32>(m_charactersPerClass * 2u);
    for(usize index = 0u; index < m_characterOwners.size(); ++index){
        auto* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_characterOwners[index]);
        if(!transform)
            continue;

        const f32 yaw = yawBase + static_cast<f32>(index) * phaseStep;
        StoreFloat(QuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f), transform->rotation);
    }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

