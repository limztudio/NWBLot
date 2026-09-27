// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <global/math/frame.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_render/module.h>

#include "csg_smoke_helpers.h"
#include "framebuffer_capture.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_shadow_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;
using EntityID = NWB::Core::ECS::EntityID;

static constexpr SmokeMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/csg_shadow/materials/opaque"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/csg_shadow/materials/glass"};
static constexpr AStringView s_MaterialInterface = "project/shaders/csg_shadow";
static constexpr Name s_Groups[] = {
    Name("csg_shadow/opaque_hole"), Name("csg_shadow/opaque_cap"), Name("csg_shadow/glass_depth"),
    Name("csg_shadow/glass_hole"), Name("csg_shadow/overlap_a"), Name("csg_shadow/overlap_b"),
    Name("csg_shadow/glass_control"),
};
static constexpr Name s_AnalyticGroups[] = {
    Name("csg_shadow/analytic_plane"), Name("csg_shadow/analytic_sphere"), Name("csg_shadow/analytic_capsule"),
    Name("csg_shadow/analytic_axial_capsule"), Name("csg_shadow/analytic_union"), Name("csg_shadow/analytic_control"),
};
static constexpr f32 s_CameraDepth = -4.0f;
static constexpr u32 s_MoveUpdate = 40u;

namespace Arm{
enum Enum : u8{ Reference, Cut, Uncut, Moved, CameraShift };
};

struct Box{
    Float3U minimum;
    Float3U maximum;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgShadowSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorld(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("CsgShadowSmokeProject"));
        auto& renderer = AddSmokeRenderSystems(*world, context);
        NWB::Impl::ReflectionSettings reflection;
        reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("Invalid CSG shadow reflection settings"));
        renderer.setRefractionEnabled(false);
        NWB::Impl::PresentationSettings presentation;
        presentation.toneMap = NWB::Impl::PresentationToneMap::LinearClamp;
        NWB_FATAL_ASSERT_MSG(renderer.setPresentationSettings(presentation), NWB_TEXT("Invalid CSG shadow presentation settings"));
        return world;
    }

    bool readMode(){
        SmokeEnvironmentString value(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_CSG_SHADOW_ARM", value)){
            const AStringView arm(value.data(), value.size());
            if(arm == "reference")
                m_arm = Arm::Reference;
            else if(arm == "cut")
                m_arm = Arm::Cut;
            else if(arm == "uncut")
                m_arm = Arm::Uncut;
            else if(arm == "moved")
                m_arm = Arm::Moved;
            else if(arm == "camera_shift")
                m_arm = Arm::CameraShift;
            else
                return false;
        }
        if(ReadSmokeEnvironmentText("NWB_CSG_SHADOW_LIGHT", value)){
            const AStringView light(value.data(), value.size());
            if(light != "directional" && light != "point")
                return false;
            m_pointLight = light == "point";
        }
        if(ReadSmokeEnvironmentText("NWB_CSG_SHADOW_ATLAS", value)){
            const AStringView atlas(value.data(), value.size());
            if(atlas != "boxes" && atlas != "analytic")
                return false;
            m_analyticAtlas = atlas == "analytic";
        }
        return !m_analyticAtlas || (!m_pointLight && (m_arm == Arm::Cut || m_arm == Arm::Uncut));
    }

    EntityID createBox(const Box& box, const bool transparent){
        const auto entity = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, s_CubeMesh, transparent ? s_GlassMaterial : s_OpaqueMaterial,
            s_MaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f),
            Float4((box.minimum.x + box.maximum.x) * 0.5f, (box.minimum.y + box.maximum.y) * 0.5f,
                (box.minimum.z + box.maximum.z) * 0.5f, 0.0f),
            Float4(box.maximum.x - box.minimum.x, box.maximum.y - box.minimum.y, box.maximum.z - box.minimum.z, 0.0f)
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("CsgShadowSmokeProject: box creation failed"));
        return entity;
    }

    void createReferencePieces(const Box& box, const Box& cutter, const bool transparent){
        const Float3U lower(Max(box.minimum.x, cutter.minimum.x), Max(box.minimum.y, cutter.minimum.y),
            Max(box.minimum.z, cutter.minimum.z));
        const Float3U upper(Min(box.maximum.x, cutter.maximum.x), Min(box.maximum.y, cutter.maximum.y),
            Min(box.maximum.z, cutter.maximum.z));
        if(lower.x >= upper.x || lower.y >= upper.y || lower.z >= upper.z){
            createBox(box, transparent);
            return;
        }
        // Six disjoint ordinary boxes represent the subtraction without CSG components or generated caps.
        const Box pieces[] = {
            { box.minimum, Float3U(lower.x, box.maximum.y, box.maximum.z) },
            { Float3U(upper.x, box.minimum.y, box.minimum.z), box.maximum },
            { Float3U(lower.x, box.minimum.y, box.minimum.z), Float3U(upper.x, lower.y, box.maximum.z) },
            { Float3U(lower.x, upper.y, box.minimum.z), Float3U(upper.x, box.maximum.y, box.maximum.z) },
            { Float3U(lower.x, lower.y, box.minimum.z), Float3U(upper.x, upper.y, lower.z) },
            { Float3U(lower.x, lower.y, upper.z), Float3U(upper.x, upper.y, box.maximum.z) },
        };
        for(const auto& piece : pieces){
            if(piece.minimum.x < piece.maximum.x && piece.minimum.y < piece.maximum.y && piece.minimum.z < piece.maximum.z)
                createBox(piece, transparent);
        }
    }

    void addCaster(const Box& box, const Box& cutterBox, const u32 groupIndex, const bool transparent, const bool moving){
        if(m_arm == Arm::Reference){
            createReferencePieces(box, cutterBox, transparent);
            return;
        }
        const auto entity = createBox(box, transparent);
        AddStaticCsgMeshReceiver(*m_world, entity, s_Groups[groupIndex], !transparent, transparent);
        auto cutterEntity = m_world->createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_Groups[groupIndex];
        cutter.shapeType = Name("engine/csg/box");
        cutter.active = m_arm != Arm::Uncut;
        NWB::Impl::CsgBoxShapeParameters parameters;
        parameters.halfExtents = Float4((cutterBox.maximum.x - cutterBox.minimum.x) * 0.5f,
            (cutterBox.maximum.y - cutterBox.minimum.y) * 0.5f, (cutterBox.maximum.z - cutterBox.minimum.z) * 0.5f, 0.0f);
        AssignCsgCutterParameters(cutter, parameters);
        const Float4 finalCenter((cutterBox.minimum.x + cutterBox.maximum.x) * 0.5f,
            (cutterBox.minimum.y + cutterBox.maximum.y) * 0.5f, (cutterBox.minimum.z + cutterBox.maximum.z) * 0.5f, 0.0f);
        Float4 initialCenter = finalCenter;
        if(moving && m_arm == Arm::Moved)
            initialCenter.x -= 0.6f * (m_pointLight ? 0.5f : 1.0f);
        AssignCsgCutterTransform(cutter, LoadFloat(initialCenter), QuaternionIdentity());
        if(moving){
            NWB_ASSERT(m_movingCount < LengthOf(m_movingCutters));
            m_movingCutters[m_movingCount] = cutterEntity.id();
            m_finalCutterCenters[m_movingCount] = finalCenter;
            ++m_movingCount;
        }
    }

    void createAtlas(){
        const f32 xyScale = m_pointLight ? 0.5f : 1.0f;
        for(u32 slot = 0u; slot < 6u; ++slot){
            const f32 x = (static_cast<f32>(slot % 3u) - 1.0f) * 2.0f * xyScale;
            const f32 y = (slot < 3u ? 1.0f : -1.0f) * xyScale;
            const f32 halfWidth = 0.7f * xyScale;
            const Box box{ Float3U(x - halfWidth, y - halfWidth, -7.0f), Float3U(x + halfWidth, y + halfWidth, -5.0f) };
            const bool hole = slot == 0u || slot == 3u;
            const f32 holeHalfWidth = m_pointLight ? 0.36f : 0.24f;
            Box cutter = hole
                ? Box{ Float3U(x + 0.3f * xyScale - holeHalfWidth, y - 0.4f * xyScale, -7.2f),
                    Float3U(x + 0.3f * xyScale + holeHalfWidth, y + 0.4f * xyScale, -4.8f) }
                : Box{ Float3U(x - 1.0f, y - 1.0f, -6.0f), Float3U(x + 1.0f, y + 1.0f, -4.8f) };
            if(slot == 5u){
                cutter.minimum.x += 20.0f;
                cutter.maximum.x += 20.0f;
            }
            const u32 groupIndex = slot == 5u ? 6u : slot;
            addCaster(box, cutter, groupIndex, slot >= 2u, hole);
            if(slot == 4u){
                Box second = box;
                second.minimum.z -= 0.5f;
                second.maximum.z -= 0.5f;
                cutter.minimum.z -= 0.5f;
                cutter.maximum.z -= 0.5f;
                addCaster(second, cutter, 5u, true, false);
            }
        }
    }

    template<typename ParameterT>
    void addAnalyticCutter(
        const u32 groupIndex,
        const Name shapeType,
        const ParameterT& parameters,
        const SIMDMatrix& shapeToWorld){
        auto cutterEntity = m_world->createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_AnalyticGroups[groupIndex];
        cutter.shapeType = shapeType;
        cutter.active = m_arm == Arm::Cut;
        AssignCsgCutterParameters(cutter, parameters);
        SIMDVector determinant;
        StoreFloat(MatrixInverse(&determinant, shapeToWorld), cutter.worldToShape);
        StoreFloat(shapeToWorld, cutter.shapeToWorld);
    }

    void createAnalyticAtlas(){
        for(u32 slot = 0u; slot < 6u; ++slot){
            const f32 x = (static_cast<f32>(slot % 3u) - 1.0f) * 2.0f;
            const f32 y = slot < 3u ? 1.0f : -1.0f;
            const Box box{ Float3U(x - 0.7f, y - 0.7f, -7.0f), Float3U(x + 0.7f, y + 0.7f, -5.0f) };
            const auto entity = createBox(box, true);
            AddStaticCsgMeshReceiver(*m_world, entity, s_AnalyticGroups[slot], false, true);
        }

        NWB::Impl::CsgPlaneShapeParameters plane;
        plane.normalDistance = Float4(0.5f, 0.0f, 1.0f, 0.0f);
        addAnalyticCutter(0u, Name("engine/csg/plane"), plane, MatrixTranslation(-2.0f, 1.0f, -6.0f));

        NWB::Impl::CsgSphereShapeParameters sphere;
        sphere.radius = Float4(1.0f, 0.0f, 0.0f, 0.0f);
        const SIMDMatrix ellipsoid = MatrixAffineTransformation(
            VectorSet(0.5f, 0.45f, 0.75f, 0.0f), VectorZero(), QuaternionIdentity(), VectorSet(0.0f, 1.0f, -6.0f, 0.0f)
        );
        addAnalyticCutter(1u, Name("engine/csg/sphere"), sphere, ellipsoid);

        NWB::Impl::CsgCapsuleShapeParameters capsule;
        capsule.radiusHalfHeight = Float4(0.35f, 0.25f, 0.0f, 0.0f);
        addAnalyticCutter(2u, Name("engine/csg/capsule"), capsule, MatrixTranslation(2.0f, 1.0f, -6.0f));
        capsule.radiusHalfHeight = Float4(0.3f, 0.4f, 0.0f, 0.0f);
        // An exact axis permutation exercises the capsule's zero radial-speed branch.
        const SIMDMatrix alongLight = MatrixSet(
            1.0f, 0.0f, 0.0f, -2.0f,
            0.0f, 0.0f, -1.0f, -1.0f,
            0.0f, 1.0f, 0.0f, -6.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        addAnalyticCutter(3u, Name("engine/csg/capsule"), capsule, alongLight);

        sphere.radius = Float4(0.55f, 0.0f, 0.0f, 0.0f);
        addAnalyticCutter(4u, Name("engine/csg/sphere"), sphere, MatrixTranslation(0.0f, -1.0f, -6.25f));
        addAnalyticCutter(4u, Name("engine/csg/sphere"), sphere, MatrixTranslation(0.0f, -1.0f, -5.75f));
        // The infinite plane keeps the control in the CSG route while removing only z <= -8, outside its box.
        plane.normalDistance = Float4(0.0f, 0.0f, 1.0f, 8.0f);
        addAnalyticCutter(5u, Name("engine/csg/plane"), plane, MatrixIdentity());
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: analytic atlas tiles=6 shapes=plane,sphere,capsule"));
    }

    void destroyWorld(){
        if(m_capture){
            m_capture->stop();
            m_capture.reset();
        }
        DestroySmokeRenderWorld(m_context, m_world);
    }

public:
    explicit CsgShadowSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorld(context))
    {}

    virtual ~CsgShadowSmokeProject()override{ destroyWorld(); }

    virtual bool onStartup()override{
        if(!readMode())
            return false;
        const auto camera = CreateSmokeCamera(*m_world, 0.0f, -s_CameraDepth, 0.0f);
        auto* cameraTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(camera);
        NWB_FATAL_ASSERT_MSG(cameraTransform, NWB_TEXT("CsgShadowSmokeProject: camera creation failed"));
        const f32 cameraX = m_arm == Arm::CameraShift ? 0.3f : 0.0f;
        cameraTransform->position.x = cameraX;
        const auto receiver = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, s_PlaneMesh, s_OpaqueMaterial, s_MaterialInterface,
            Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(4.0f, 1.0f, 3.0f, 0.0f)
        );
        auto* receiverTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(receiver);
        NWB_FATAL_ASSERT_MSG(camera.valid() && receiverTransform, NWB_TEXT("CsgShadowSmokeProject: camera/receiver creation failed"));
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), receiverTransform->rotation);
        const auto light = m_pointLight
            ? NWB::Impl::Scene::CreatePointLightEntity(*m_world, Float4(0.0f, 0.0f, -12.0f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 30.0f)
            : NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.0f, 0.0f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        auto* lightComponent = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(light);
        NWB_FATAL_ASSERT_MSG(lightComponent, NWB_TEXT("CsgShadowSmokeProject: light creation failed"));
        lightComponent->angularRadius = 0.0f;
        lightComponent->sourceRadius = 0.0f;
        lightComponent->enableCaustics = false;
        if(m_analyticAtlas)
            createAnalyticAtlas();
        else
            createAtlas();
        const bool hardware = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery)
            && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: atlas arm={} light={} hardware={} camera_x={} caster_z_max=-5 receiver_z=0")
            , static_cast<u32>(m_arm), m_pointLight ? NWB_TEXT("point") : NWB_TEXT("directional"), hardware ? 1u : 0u
            , static_cast<f64>(cameraX)
        );
        return ConfigureSmokeFramebufferCapture(m_context, NWB_TEXT("CsgShadowSmokeProject"), 120u, m_capture);
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_capture)
            m_capture->update();
        ++m_update;
        if(m_arm == Arm::Moved && m_update == s_MoveUpdate){
            for(u32 index = 0u; index < m_movingCount; ++index){
                auto* cutter = m_world->tryGetComponent<NWB::Impl::CsgCutterComponent>(m_movingCutters[index]);
                NWB_ASSERT(cutter);
                AssignCsgCutterTransform(*cutter, LoadFloat(m_finalCutterCenters[index]), QuaternionIdentity());
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: cutters moved at update40"));
        }
        const f32 fixedDelta = RendererBaselineFixedDelta();
        m_world->tick(fixedDelta > 0.0f ? fixedDelta : delta);
        return true;
    }

private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_capture;
    EntityID m_movingCutters[2] = {};
    Float4 m_finalCutterCenters[2] = {};
    u32 m_movingCount = 0u;
    u32 m_update = 0u;
    Arm::Enum m_arm = Arm::Cut;
    bool m_pointLight = false;
    bool m_analyticAtlas = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960, 720 }; }
const tchar* NWB::QueryProjectWindowTitle(){ return NWB_TEXT("NWB CSG Shadow Smoke"); }
UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_shadow_smoke::CsgShadowSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

