// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/mesh/frame_math.h>
#include <global/math/constant.h>
#include <global/math/quaternion.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "fps_probe.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gi_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
using NWB::Tests::Smoke::AddSmokeRenderSystems;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DEDICATED GI scene (DDGI U5): an OPEN-TOP BOX with a SATURATED-RED wall (+X) and a SATURATED-BLUE wall (-X) on
// opposite sides, lit by a single DIRECTIONAL light aimed so the walls are lit but the FLOOR between them is in
// DIRECT SHADOW. The indirect bleed onto the shadowed floor -- RED near the red wall, BLUE near the blue wall, a
// red-to-blue gradient across the middle -- is the unfakeable pass signal: the hemiAmbient replacement makes this
// true GI detection (a constant ambient term cannot produce a two-colour directional bounce). No refractive casters
// (caustics gate off = the scene is GI-funded).
//
// The box is built from five opaque planes (four walls + one floor; open top so the directional light reaches the
// interior). One wall is tinted saturated red, the opposite wall saturated blue; the two remaining walls + the floor
// are near-white. The directional light grazes the walls at an angle that lights them while leaving the floor between
// them in cast shadow, so the only light reaching the shadowed floor is the colored bounce off the lit walls.
//
// Reuses the benchmark's cooked ground material + the per-instance colour_tint mutable (no new assets).

static constexpr AStringView s_GroundMeshPath = "project/meshes/shadow_plane";
static constexpr AStringView s_OpaqueMaterialPath = "project/smoke/transparent_multi/materials/ground";  // opaque lambert
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

// Box geometry: a 4x4 unit open-top box centered at the origin. Each wall/floor is a scaled plane.
static constexpr f32 s_BoxHalfExtent = 2.0f;       // half the box's X/Z extent
static constexpr f32 s_BoxHeight = 2.0f;            // wall height
static constexpr f32 s_BoxScale = 4.0f;             // plane scale (matches s_BoxHalfExtent * 2)

// Camera: elevated ABOVE the box (higher than the wall top) looking DOWN into the open top at a moderate angle, so
// it looks OVER the near wall (which would otherwise fill the frame now the walls are two-sided) and frames the
// floor + its coloured bounce below with the red (+X) and blue (-X) side walls' inner faces rising at the left/right
// and the far (+Z) wall across the back. A near-level camera sat below the wall tops and the near wall occluded
// everything; a too-steep top-down angle foreshortened the walls to slivers -- this is the middle ground.
static constexpr f32 s_CameraDistance = 7.5f;
static constexpr f32 s_CameraHeight = 3.6f;
static constexpr f32 s_CameraPitch = 0.42f;         // pulled back + elevated to frame the whole box interior

// Directional light: aimed so the RED wall (at +X) is lit but the FLOOR is in shadow. A high pitch (steep angle)
// lights the wall; the yaw is chosen so the light rakes ACROSS the red wall and its shadow falls on the floor
// beside it. Warm-white sun tint at full intensity.
static constexpr f32 s_DirectionalLightPitch = 0.85f;   // steep: lights the wall face, floor partly shadowed
static constexpr f32 s_DirectionalLightYaw = 0.6f;      // rakes across the +X red wall
static constexpr f32 s_DirectionalLightIntensity = 2.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GiTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("GiTestSmokeProject"));

        // Force ray-tracing emulation so the SOFTWARE GI path runs even on RT-capable hardware -- the SW path is
        // the one that runs the full DDGI chain (probe trace -> blend -> border -> flip), so the _sw_smoke build
        // is the intended way to see the indirect bounce. Default OFF: the HW (hybrid) path.
#if defined(NWB_GI_TEST_FORCE_RT_EMULATION) && !defined(NWB_FINAL)
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingAccelStruct, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingPipeline, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayQuery, true);
#endif

        AddSmokeRenderSystems(*world, context);
        return world;
    }

    void destroyWorld(){
        DestroySmokeRenderWorld(m_context, m_world);
    }


public:
    explicit GiTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~GiTestSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraHeight, s_CameraDistance, s_CameraPitch);

        // ONE directional light: aimed so the red wall is lit but the floor beside it is in direct shadow. The
        // indirect red bounce off the lit wall onto the shadowed floor is the DDGI pass signal.
        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DirectionalLightPitch,
            s_DirectionalLightYaw,
            0.0f,
            Float4(1.00f, 0.96f, 0.88f), // warm white sun
            s_DirectionalLightIntensity
        );
        (void)directionalLight;

        // The FLOOR: a near-white opaque plane filling the box bottom. This is the receiver of the indirect red
        // bounce (the floor area in the wall's direct shadow lights up red from the GI).
        m_floorEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMeshPath,
            s_OpaqueMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.90f, 0.90f, 0.90f, 1.0f),   // near-white floor
            Float4(0.0f, 0.0f, 0.0f, 0.0f),       // at the origin
            Float4(s_BoxScale, 1.0f, s_BoxScale, 0.0f)
        );

        // The RED wall (+X side): saturated red so its indirect bounce onto the shadowed floor is unmistakable.
        // Rotated to face inward (-X direction) and positioned at the +X edge of the box.
        m_redWallEntity = createWall(
            Float4(s_BoxHalfExtent, s_BoxHeight * 0.5f, 0.0f, 0.0f),  // position at +X edge, half-height up
            Float4(0.80f, 0.08f, 0.08f, 1.0f),                          // saturated red
            -90.0f                                                       // yaw so the single-sided plane's front faces -X (inward)
        );

        // The BLUE wall (-X side, opposite the red wall): a saturated blue so its indirect bounce onto the shadowed
        // floor is a distinct colour from the red wall's. Red bleed on the +X-side floor + blue bleed on the -X-side
        // floor = an unmistakable colored-GI signal (a constant ambient term cannot produce a red-to-blue gradient).
        m_blueWallNegX = createWall(
            Float4(-s_BoxHalfExtent, s_BoxHeight * 0.5f, 0.0f, 0.0f),
            Float4(0.08f, 0.08f, 0.80f, 1.0f),
            90.0f                                                        // yaw so the single-sided plane's front faces +X (inward)
        );
        // The far WHITE wall (+Z side): the box's back wall. The two-sided material makes it render whichever way it
        // faces. The -Z side is left OPEN (Cornell-box style) -- a near wall there would sit between the camera and
        // the interior and occlude the whole view, so it is omitted; the open front is where the camera looks in.
        m_whiteWallPosZ = createWall(
            Float4(0.0f, s_BoxHeight * 0.5f, s_BoxHalfExtent, 0.0f),
            Float4(0.88f, 0.88f, 0.88f, 1.0f),
            180.0f
        );

        NWB_FATAL_ASSERT_MSG(
            activeCamera.valid()
            && m_floorEntity.valid()
            && m_redWallEntity.valid()
            && m_blueWallNegX.valid()
            && m_whiteWallPosZ.valid(),
            NWB_TEXT("GiTestSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("GiTestSmokeProject: open-top box + red/blue opposite walls, directional light -> indirect red+blue bleed on shadowed floor")
        );
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_world->tick(safeDelta);
        return true;
    }


private:
    // Creates a wall plane: a scaled shadow_plane tinted `color`, positioned at `position`, rotated `yawDeg`
    // around the Y axis (so the plane's normal points inward). The shadow_plane mesh lies in the XZ plane by
    // default; a wall needs it vertical (in the XY or ZY plane), so the rotation is applied to the entity's
    // transform.
    NWB::Core::ECS::EntityID createWall(const Float4& position, const Float4& colorTint, const f32 yawDeg){
        // The shadow_plane is a horizontal XZ plane; to make a vertical wall we rotate it 90 degrees about the X
        // axis (pitch) so it stands up, then apply the per-wall yaw about Y to orient it. The combined rotation
        // is pitch * yaw (applied as a quaternion). The math API is SIMD (SIMDVector); StoreFloat writes it into
        // the TransformComponent's Float4 rotation field.
        const SIMDVector pitchQuat = QuaternionRotationRollPitchYaw(s_PIDIV2, 0.0f, 0.0f);  // 90deg pitch -> vertical
        const f32 yawRad = yawDeg * (s_PI / 180.0f);
        const SIMDVector yawQuat = QuaternionRotationRollPitchYaw(0.0f, yawRad, 0.0f);
        const SIMDVector wallRotation = QuaternionMultiply(pitchQuat, yawQuat);

        auto entity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMeshPath,
            s_OpaqueMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            colorTint,
            position,
            Float4(s_BoxScale, 1.0f, s_BoxHeight, 0.0f)
        );
        if(entity.valid()){
            if(auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity))
                StoreFloat(wallRotation, &transform->rotation);
        }
        return entity;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_floorEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_redWallEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_blueWallNegX = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_whiteWallPosZ = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("GiTestSmokeProject") };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB GI Test");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_gi_test_smoke::GiTestSmokeProject>(context);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

