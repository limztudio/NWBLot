// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <global/math/frame.h>
#include <global/simdmath.h>
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


static constexpr AStringView s_ENGINE_CSG_SPHERE = "engine/csg/sphere";
static constexpr AStringView s_DIRECTIONAL = "directional";
static constexpr AStringView s_POINT = "point";
static constexpr AStringView s_HARD = "hard";
static constexpr AStringView s_FINITE = "finite";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;
using EntityID = NWB::Core::ECS::EntityID;

static constexpr SmokeMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
#if defined(NWB_CSG_CAUSTIC_SMOKE)
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/csg_caustic/materials/receiver"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/csg_caustic/materials/glass"};
#else
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/csg_shadow/materials/opaque"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/csg_shadow/materials/glass"};
#endif
static constexpr AStringView s_MaterialInterface = "project/shaders/csg_shadow";
static constexpr SmokeMaterialRef s_OpenMaterial{"project/smoke/reflection/materials/optical_volume"};
static constexpr AStringView s_OpenInterface = "project/shaders/reflection_optical";
static constexpr Name s_Groups[] = {
    Name("csg_shadow/opaque_hole"), Name("csg_shadow/opaque_cap"), Name("csg_shadow/glass_depth"),
    Name("csg_shadow/glass_hole"), Name("csg_shadow/overlap_a"), Name("csg_shadow/overlap_b"),
    Name("csg_shadow/glass_control"),
};
static constexpr Name s_AnalyticGroups[] = {
    Name("csg_shadow/analytic_plane"), Name("csg_shadow/analytic_sphere"), Name("csg_shadow/analytic_capsule"),
    Name("csg_shadow/analytic_axial_capsule"), Name("csg_shadow/analytic_union"), Name("csg_shadow/analytic_control"),
};
static constexpr AStringView s_OpenCaseNames[] = {
    "reference", "retained", "clipped_reference", "clipped", "boundary64", "terminal65",
};
static constexpr Name s_OpenGroups[] = { Name("csg_shadow/open_five"), Name("csg_shadow/open_six") };
static constexpr SmokeMeshRef s_OpenPairMesh{"project/meshes/csg_shadow_open_2"};
static constexpr SmokeMeshRef s_OpenFiveMesh{"project/meshes/csg_shadow_open_5"};
static constexpr SmokeMeshRef s_OpenSixMesh{"project/meshes/csg_shadow_open_6"};
static constexpr SmokeMeshRef s_OpenBoundaryMesh{"project/meshes/csg_shadow_open_64"};
static constexpr SmokeMeshRef s_OpenOverflowMesh{"project/meshes/csg_shadow_open_65"};
static constexpr f32 s_CameraDepth = -4.0f;
static constexpr u32 s_MoveUpdate = 40u;

namespace Arm{
    enum Enum : u8{ Reference, Cut, Uncut, Moved, CameraShift };
};

namespace OpenCase{
    enum Enum : u8{ Reference, Retained, ClippedReference, Clipped, Boundary64, Terminal65 };
};

struct Box{
    Float3U minimum;
    Float3U maximum;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgShadowSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorld(NWB::ProjectRuntimeContext& context){
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
        if(const auto value = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_CSG_SHADOW_ARM")){
            const AStringView arm(value->data(), value->size());
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
        if(const auto value = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_CSG_SHADOW_LIGHT")){
            const AStringView light(value->data(), value->size());
            if(light != s_DIRECTIONAL && light != s_POINT)
                return false;
            m_pointLight = light == s_POINT;
        }
        if(const auto value = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_CSG_SHADOW_LIGHT_SOURCE")){
            const AStringView source(value->data(), value->size());
            if(source != s_HARD && source != s_FINITE)
                return false;
            m_finiteLightSource = source == s_FINITE;
        }
        if(const auto value = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_CSG_SHADOW_ATLAS")){
            const AStringView atlas(value->data(), value->size());
            if(atlas != "boxes" && atlas != "analytic" && atlas != "open")
                return false;
            m_analyticAtlas = atlas == "analytic";
            m_openAtlas = atlas == "open";
        }
        if(m_openAtlas){
            const auto value = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_CSG_SHADOW_OPEN_CASE");
            if(!value || m_pointLight || m_finiteLightSource || m_arm != Arm::Cut)
                return false;
            const AStringView selected(value->data(), value->size());
            bool found = false;
            for(u32 index = 0u; index < LengthOf(s_OpenCaseNames); ++index){
                if(selected == s_OpenCaseNames[index]){
                    m_openCase = static_cast<OpenCase::Enum>(index);
                    found = true;
                    break;
                }
            }
            if(!found)
                return false;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
            return false;
#endif
        }
        return !m_analyticAtlas || (!m_pointLight && (m_arm == Arm::Cut || m_arm == Arm::Uncut));
    }

    EntityID createBox(const Box& box, const bool transparent, const SmokeMeshRef& mesh = s_CubeMesh){
        const SIMDVector minimum = LoadFloat(box.minimum);
        const SIMDVector maximum = LoadFloat(box.maximum);
        Float4 center;
        Float4 extent;
        StoreFloat(VectorScale(VectorAdd(minimum, maximum), 0.5f), center);
        StoreFloat(VectorSubtract(maximum, minimum), extent);
        const auto entity = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, mesh, transparent ? (m_openAtlas ? s_OpenMaterial : s_GlassMaterial) : s_OpaqueMaterial,
            transparent && m_openAtlas ? s_OpenInterface : s_MaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f),
            center, extent
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("CsgShadowSmokeProject: box creation failed"));
        if(transparent && m_openAtlas){
            const Half packedIor = ConvertFloatToHalf(1.5f);
            const Half4U packedTransmission = MakeHalf4U(0.25f, 0.5f, 0.75f, 0.0f);
            NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
                *m_world, entity, Name(s_OpenInterface), "runtime.ior", NWB::Impl::MaterialLayoutFieldType::Half,
                NWB::Impl::PackMaterialInstanceBytes(&packedIor, sizeof(packedIor))
            ), NWB_TEXT("CsgShadowSmokeProject: open-sheet IOR override failed"));
            NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
                *m_world, entity, Name(s_OpenInterface), "runtime.unit_transmission", NWB::Impl::MaterialLayoutFieldType::Half3,
                NWB::Impl::PackMaterialInstanceBytes(packedTransmission.raw, sizeof(Half) * 3u)
            ), NWB_TEXT("CsgShadowSmokeProject: open-sheet transmission override failed"));
        }
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
        const SIMDVector minimum = LoadFloat(cutterBox.minimum);
        const SIMDVector maximum = LoadFloat(cutterBox.maximum);
        StoreFloat(VectorScale(VectorSubtract(maximum, minimum), 0.5f), parameters.halfExtents);
        AssignCsgCutterParameters(cutter, parameters);
        Float4 finalCenter;
        StoreFloat(VectorScale(VectorAdd(minimum, maximum), 0.5f), finalCenter);
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
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        const Box split{ Float3U(-2.0f, -0.7f, -7.0f), Float3U(-0.6f, 0.7f, -5.0f) };
        const Box cavity{ Float3U(-2.2f, -0.9f, -6.5f), Float3U(-0.4f, 0.9f, -5.5f) };
        if(m_arm == Arm::Reference)
            createBox(split, true, SmokeMeshRef{"project/meshes/csg_caustic_split"});
        else
            addCaster(split, cavity, 2u, true, false);
        const Box control{ Float3U(0.6f, -0.7f, -7.0f), Float3U(2.0f, 0.7f, -5.0f) };
        createBox(control, true);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticSmokeProject: split cavity has four interfaces before the receiver; control has two"));
#else
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
#endif
    }

    template<typename ParameterT>
    void addAnalyticCutter(
        const u32 groupIndex,
        const Name shapeType,
        const ParameterT& parameters,
        const SIMDMatrix& shapeToWorld
    ){
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
        addAnalyticCutter(1u, Name(s_ENGINE_CSG_SPHERE), sphere, ellipsoid);

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
        addAnalyticCutter(4u, Name(s_ENGINE_CSG_SPHERE), sphere, MatrixTranslation(0.0f, -1.0f, -6.25f));
        addAnalyticCutter(4u, Name(s_ENGINE_CSG_SPHERE), sphere, MatrixTranslation(0.0f, -1.0f, -5.75f));
        // The infinite plane keeps the control in the CSG route while removing only z <= -8, outside its box.
        plane.normalDistance = Float4(0.0f, 0.0f, 1.0f, 8.0f);
        addAnalyticCutter(5u, Name("engine/csg/plane"), plane, MatrixIdentity());
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: analytic atlas tiles=6 shapes=plane,sphere,capsule"));
    }

    bool createOpenAtlas(){
        const bool clipped = m_openCase == OpenCase::Clipped || m_openCase == OpenCase::ClippedReference;
        if(m_openCase == OpenCase::Reference){
            if(!createBox({ Float3U(-1.9f, -0.7f, -7.0f), Float3U(-0.5f, 0.7f, -5.0f) }, true, s_OpenFiveMesh).valid())
                return false;
            // Separate ordinary two-sheet instances give an independent exact even-pair reference beyond the ordinary four-event approximation.
            for(u32 pair = 0u; pair < 3u; ++pair){
                const f32 lower = -7.0f + 0.8f * static_cast<f32>(pair);
                if(!createBox({ Float3U(0.5f, -0.7f, lower), Float3U(1.9f, 0.7f, lower + 0.4f) }, true, s_OpenPairMesh).valid())
                    return false;
            }
        }
        else if(m_openCase == OpenCase::ClippedReference){
            for(u32 pair = 0u; pair < 2u; ++pair){
                const f32 lower = -6.5f + static_cast<f32>(pair);
                if(!createBox({ Float3U(-1.9f, -0.7f, lower), Float3U(-0.5f, 0.7f, lower + 0.5f) }, true, s_OpenPairMesh).valid())
                    return false;
            }
            if(!createBox({ Float3U(0.5f, -0.7f, -6.6f), Float3U(1.9f, 0.7f, -5.0f) }, true, s_OpenFiveMesh).valid())
                return false;
        }
        else{
            const SmokeMeshRef firstMesh = m_openCase == OpenCase::Boundary64 ? s_OpenBoundaryMesh
                : m_openCase == OpenCase::Terminal65 ? s_OpenOverflowMesh : s_OpenFiveMesh;
            const SmokeMeshRef meshes[] = { firstMesh, s_OpenSixMesh };
            for(u32 tile = 0u; tile < 2u; ++tile){
                const f32 x = tile == 0u ? -1.2f : 1.2f;
                const auto receiver = createBox({ Float3U(x - 0.7f, -0.7f, -7.0f), Float3U(x + 0.7f, 0.7f, -5.0f) }, true, meshes[tile]);
                AddStaticCsgMeshReceiver(*m_world, receiver, s_OpenGroups[tile], false, true);
                auto entity = m_world->createEntity();
                if(!entity.id().valid())
                    return false;
                auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
                cutter.receiverGroup = s_OpenGroups[tile];
                cutter.shapeType = Name("engine/csg/plane");
                NWB::Impl::CsgPlaneShapeParameters plane;
                plane.normalDistance = Float4(0.0f, 0.0f, 1.0f, clipped ? 6.75f : 8.0f);
                AssignCsgCutterParameters(cutter, plane);
                AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
            }
        }
        const u32 firstCount = m_openCase == OpenCase::Boundary64 ? 64u : m_openCase == OpenCase::Terminal65 ? 65u : clipped ? 4u : 5u;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: open_case={} retained_events={},{} receiver_z=0 sheets_z_min=-7 sheets_z_max=-5 ior=1.5 unit_transmission=0.25,0.5,0.75 coverage=1")
            , StringConvert(s_OpenCaseNames[static_cast<u32>(m_openCase)]), firstCount, clipped ? 5u : 6u
        );
        return true;
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
        , m_world(CreateWorld(context))
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
        lightComponent->angularRadius = m_finiteLightSource && !m_pointLight ? 0.005f : 0.0f;
        lightComponent->sourceRadius = m_finiteLightSource && m_pointLight ? 0.02f : 0.0f;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        lightComponent->enableCaustics = !ReadSmokeEnvironmentFlag("NWB_CSG_CAUSTIC_DISABLED");
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticSmokeProject: photons {}"), lightComponent->enableCaustics ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
#else
        lightComponent->enableCaustics = false;
#endif
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: light_source={} angular_radius={:.3f} source_radius={:.3f}")
            , StringConvert(m_finiteLightSource ? s_FINITE : s_HARD)
            , static_cast<f64>(lightComponent->angularRadius), static_cast<f64>(lightComponent->sourceRadius)
        );
        if(m_openAtlas){
            if(!createOpenAtlas())
                return false;
        }
        else if(m_analyticAtlas)
            createAnalyticAtlas();
        else
            createAtlas();
        const bool hardware = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery)
            && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: atlas arm={} light={} hardware={} camera_x={} caster_z_max=-5 receiver_z=0")
            , static_cast<u32>(m_arm), StringConvert(m_pointLight ? s_POINT : s_DIRECTIONAL), hardware ? 1u : 0u
            , static_cast<f64>(cameraX)
        );
        auto capture = ConfigureSmokeFramebufferCapture(m_context, NWB_TEXT("CsgShadowSmokeProject"), 120u);
        if(!capture)
            return false;
        m_capture = Move(*capture);
        return true;
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
    bool m_finiteLightSource = false;
    bool m_analyticAtlas = false;
    bool m_openAtlas = false;
    OpenCase::Enum m_openCase = OpenCase::Retained;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960, 720 }; }
TStringView NWB::QueryProjectWindowTitle(){ return NWB_TEXT("NWB CSG Shadow Smoke"); }
UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_shadow_smoke::CsgShadowSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

