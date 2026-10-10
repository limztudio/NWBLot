// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_gi_scene.h"

#include "csg_smoke_helpers.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"

#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_gi_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_LiveWarmupSeconds = 30.0;
static constexpr SmokeMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
static constexpr SmokeMeshRef s_MixedMesh{"project/meshes/gi_csg_mixed"};
static constexpr SmokeMeshRef s_OpenQuadMesh{"project/meshes/gi_csg_open_quad"};
static constexpr SmokeMaterialRef s_DiffuseMaterial{"project/smoke/gi/materials/diffuse"};
static constexpr SmokeMaterialRef s_IndirectMaterial{"project/smoke/gi/materials/indirect"};
static constexpr SmokeMaterialRef s_CapMaterial{"project/smoke/gi/materials/csg_cap"};
static constexpr SmokeMaterialRef s_MixedMaterial{"project/smoke/gi/materials/csg_mixed"};
static constexpr AStringView s_MaterialInterface = "project/shaders/smoke_surface";
static constexpr Float4 s_Black(0.0f, 0.0f, 0.0f, 1.0f);
static constexpr Float4 s_White(1.0f, 1.0f, 1.0f, 1.0f);
static constexpr Name s_Groups[] = { Name("smoke/gi/passage"), Name("smoke/gi/cap"), Name("smoke/gi/mixed") };

struct Box{
    Float3U minimum;
    Float3U maximum;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::ECS::EntityID CreateBox(
    ProjectRuntimeContext& context,
    Core::ECS::World& world,
    const Box& box,
    const SmokeMaterialRef& material,
    const Float4& tint
){
    const SIMDVector minimum = LoadFloat(box.minimum);
    const SIMDVector maximum = LoadFloat(box.maximum);
    Float4 center;
    Float4 extent;
    StoreFloat(VectorScale(VectorAdd(minimum, maximum), 0.5f), center);
    StoreFloat(VectorSubtract(maximum, minimum), extent);
    return CreateTintedStaticMeshEntity(
        world, context.objectArena, s_CubeMesh, material, s_MaterialInterface, tint,
        center, extent
    );
}

[[nodiscard]] static bool CreateReferencePieces(
    ProjectRuntimeContext& context,
    Core::ECS::World& world,
    const Box& box,
    const Box& cutter,
    const SmokeMaterialRef& material,
    const Float4& tint
){
    const Float3U lower(Max(box.minimum.x, cutter.minimum.x), Max(box.minimum.y, cutter.minimum.y),
        Max(box.minimum.z, cutter.minimum.z));
    const Float3U upper(Min(box.maximum.x, cutter.maximum.x), Min(box.maximum.y, cutter.maximum.y),
        Min(box.maximum.z, cutter.maximum.z));
    // Ordinary closed pieces provide independent triangle geometry for the same subtraction union.
    const Box pieces[] = {
        { box.minimum, Float3U(lower.x, box.maximum.y, box.maximum.z) },
        { Float3U(upper.x, box.minimum.y, box.minimum.z), box.maximum },
        { Float3U(lower.x, box.minimum.y, box.minimum.z), Float3U(upper.x, lower.y, box.maximum.z) },
        { Float3U(lower.x, upper.y, box.minimum.z), Float3U(upper.x, box.maximum.y, box.maximum.z) },
        { Float3U(lower.x, lower.y, box.minimum.z), Float3U(upper.x, upper.y, lower.z) },
        { Float3U(lower.x, lower.y, upper.z), Float3U(upper.x, upper.y, box.maximum.z) },
    };
    for(const auto& piece : pieces){
        if(piece.minimum.x >= piece.maximum.x || piece.minimum.y >= piece.maximum.y || piece.minimum.z >= piece.maximum.z)
            continue;
        if(!CreateBox(context, world, piece, material, tint).valid())
            return false;
    }
    return true;
}

[[nodiscard]] static bool CreateSubtraction(
    ProjectRuntimeContext& context,
    Core::ECS::World& world,
    const Box& box,
    const Box& cutter,
    const u32 slot,
    const AStringView arm
){
    const auto& material = slot == 0u ? s_DiffuseMaterial : s_CapMaterial;
    const Float4& tint = slot == 0u ? s_Black : s_White;
    if(arm == "reference")
        return CreateReferencePieces(context, world, box, cutter, material, tint);

    const auto receiver = CreateBox(context, world, box, material, tint);
    if(!receiver.valid())
        return false;
    AddStaticCsgMeshReceiver(world, receiver, s_Groups[slot], true, false);
    // Two overlapping cutters form one opening; their internal union seam must not become a GI surface.
    const f32 overlapBegin = cutter.minimum.x + (cutter.maximum.x - cutter.minimum.x) * 0.35f;
    const f32 overlapEnd = cutter.minimum.x + (cutter.maximum.x - cutter.minimum.x) * 0.65f;
    const Box cuts[] = {
        { cutter.minimum, Float3U(overlapEnd, cutter.maximum.y, cutter.maximum.z) },
        { Float3U(overlapBegin, cutter.minimum.y, cutter.minimum.z), cutter.maximum },
    };
    for(const auto& cut : cuts){
        auto entity = world.createEntity();
        auto& component = entity.addComponent<Impl::CsgCutterComponent>(context.objectArena);
        component.receiverGroup = s_Groups[slot];
        component.shapeType = Name("engine/csg/box");
        component.active = arm == "cut";
        Impl::CsgBoxShapeParameters parameters;
        const SIMDVector minimum = LoadFloat(cut.minimum);
        const SIMDVector maximum = LoadFloat(cut.maximum);
        StoreFloat(VectorScale(VectorSubtract(maximum, minimum), 0.5f), parameters.halfExtents);
        AssignCsgCutterParameters(component, parameters);
        AssignCsgCutterTransform(component, VectorScale(VectorAdd(minimum, maximum), 0.5f), QuaternionIdentity());
    }
    return true;
}

[[nodiscard]] static bool CreateMixedSubtraction(ProjectRuntimeContext& context, Core::ECS::World& world, const AStringView arm){
    const Box box{ Float3U(-0.5f, -0.8f, -0.8f), Float3U(0.5f, 0.2f, 0.8f) };
    const Box cutter{ Float3U(-0.25f, -1.0f, -1.2f), Float3U(0.25f, 0.2f, 1.0f) };
    if(arm == "reference"){
        if(!CreateReferencePieces(context, world, box, cutter, s_MixedMaterial, s_White))
            return false;
        const f32 quadCenters[] = { -0.875f, 0.875f };
        for(const f32 x : quadCenters){
            if(!CreateTintedStaticMeshEntity(
                world, context.objectArena, s_OpenQuadMesh, s_MixedMaterial, s_MaterialInterface, s_White,
                Float4(x, -0.3f, -1.0f, 0.0f), Float4(1.25f, 1.0f, 1.0f, 0.0f)
            ).valid())
                return false;
        }
        return true;
    }

    // One asset contains a closed solid and an independently open two-triangle surface.
    const auto receiver = CreateTintedStaticMeshEntity(
        world, context.objectArena, s_MixedMesh, s_MixedMaterial, s_MaterialInterface, s_White,
        Float4(0.0f, -0.3f, 0.0f, 0.0f), Float4(1.0f, 1.0f, 1.6f, 0.0f)
    );
    if(!receiver.valid())
        return false;
    AddStaticCsgMeshReceiver(world, receiver, s_Groups[2], true, false);
    auto entity = world.createEntity();
    auto& component = entity.addComponent<Impl::CsgCutterComponent>(context.objectArena);
    component.receiverGroup = s_Groups[2];
    component.shapeType = Name("engine/csg/box");
    component.active = arm == "cut";
    Impl::CsgBoxShapeParameters parameters;
    parameters.halfExtents = Float4(0.25f, 0.6f, 1.1f, 0.0f);
    AssignCsgCutterParameters(component, parameters);
    AssignCsgCutterTransform(component, VectorSet(0.0f, -0.4f, -0.1f, 0.0f), QuaternionIdentity());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CsgGiScene::ShouldCapture(void* context, const u64)noexcept{
    const auto& scene = *static_cast<CsgGiScene*>(context);
    return scene.m_liveWarmupComplete && scene.m_context.graphics.getSuccessfulPresentationCount() >= scene.m_requiredPresentations;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgGiScene::CsgGiScene(ProjectRuntimeContext& context, Core::ECS::World& world)noexcept
    : m_context(context)
    , m_world(world)
{}

Expected<bool> CsgGiScene::start(){
    const auto configuredArm = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_GI_CSG_ARM");
    if(!configuredArm)
        return false;
    const AStringView arm(configuredArm->data(), configuredArm->size());
    if(arm != "reference" && arm != "cut" && arm != "uncut"){
        NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: invalid NWB_GI_CSG_ARM"));
        return MakeUnexpected(Failure{});
    }
    if(const auto value = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT")){
        const auto count = ParseU64(AStringView(value->data(), value->size()));
        if(!count || *count < 360u || *count > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: CSG GI warmup requires at least 360 successful presentations"));
            return MakeUnexpected(Failure{});
        }
        m_requiredPresentations = *count;
    }

    auto* renderer = m_world.getSystem<Impl::RendererSystem>();
    if(!renderer)
        return MakeUnexpected(Failure{});
    if(ReadSmokeEnvironmentFlag("NWB_GI_CSG_LAGGED_LIGHTING"))
        renderer->setFrameLaggedAsyncLightingEnabled(true);
    Impl::ReflectionSettings reflection;
    reflection.traceMode = Impl::ReflectionTraceMode::Disabled;
    Impl::PresentationSettings presentation;
    presentation.toneMap = Impl::PresentationToneMap::LinearClamp;
    if(!renderer->setReflectionSettings(reflection) || !renderer->setPresentationSettings(presentation))
        return MakeUnexpected(Failure{});
    renderer->setRefractionEnabled(false);

    const auto camera = CreateSmokeCamera(m_world, 1.3f, 5.5f, 0.0f);
    const auto light = Impl::Scene::CreatePointLightEntity(
        m_world, Float4(0.0f, 1.3f, -3.0f, 0.0f), __hidden_csg_gi_scene::s_White, 20.0f, 15.0f
    );
    auto* lightComponent = m_world.tryGetComponent<Impl::Scene::LightComponent>(light);
    if(!camera.valid() || !lightComponent)
        return MakeUnexpected(Failure{});
    lightComponent->sourceRadius = 0.0f;
    lightComponent->enableCaustics = false;

    using namespace __hidden_csg_gi_scene;
    if(!CreateBox(m_context, m_world, { Float3U(-5.0f, -1.0f, -6.6f), Float3U(5.0f, 5.0f, -6.5f) },
        s_DiffuseMaterial, Float4(0.02f, 0.02f, 1.0f, 1.0f)).valid())
        return MakeUnexpected(Failure{});
    for(u32 slot = 0u; slot < 2u; ++slot){
        const f32 x = slot == 0u ? -1.3f : 1.3f;
        const Box receiver{ Float3U(x - 0.9f, 0.3f, -0.8f), Float3U(x + 0.9f, 2.3f, 0.8f) };
        const Box cutter{ Float3U(x - 0.65f, 0.55f, -1.0f), Float3U(x + 0.65f, 2.05f, slot == 0u ? 1.0f : 0.4f) };
        if(!CreateSubtraction(m_context, m_world, receiver, cutter, slot, arm))
            return MakeUnexpected(Failure{});
    }
    if(!CreateMixedSubtraction(m_context, m_world, arm))
        return MakeUnexpected(Failure{});
    const auto passage = CreateBox(m_context, m_world,
        { Float3U(-1.9f, 0.65f, 1.0f), Float3U(-0.7f, 1.95f, 1.08f) }, s_IndirectMaterial, s_White);
    // The receiver is inside the removed cavity. Its upward hemisphere reaches the green generated back cap.
    const auto cavity = CreateBox(m_context, m_world,
        { Float3U(0.85f, 0.56f, -0.72f), Float3U(1.75f, 0.61f, 0.06f) }, s_IndirectMaterial, s_White);
    const auto control = CreateBox(m_context, m_world,
        { Float3U(-0.35f, 2.7f, 0.0f), Float3U(0.35f, 3.15f, 0.06f) }, s_IndirectMaterial, s_White);
    if(!passage.valid() || !cavity.valid() || !control.valid())
        return MakeUnexpected(Failure{});
    const auto mixedPassage = CreateBox(m_context, m_world,
        { Float3U(-0.2f, -0.7f, 1.0f), Float3U(0.2f, -0.4f, 1.08f) }, s_IndirectMaterial, s_White);
    const auto mixedFloor = CreateBox(m_context, m_world,
        { Float3U(-1.3f, -0.82f, -1.85f), Float3U(-0.75f, -0.77f, -1.08f) }, s_IndirectMaterial, s_White);
    if(!mixedPassage.valid() || !mixedFloor.valid())
        return MakeUnexpected(Failure{});

    const FramebufferCaptureOptions options{
        .shouldCapture = ShouldCapture,
        .predicateContext = this,
        .requiredWidth = 1280u,
        .requiredHeight = 900u,
    };
    auto capture = ConfigureSmokeFramebufferCapture(m_context, NWB_TEXT("GiTestSmokeProject"), 360u, options);
    if(!capture)
        return MakeUnexpected(Failure{});
    m_capture = Move(*capture);
    m_active = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: CSG GI arm={} overlapping_box_cutters=2 accepted_presentations={}")
        , StringConvert(arm), m_requiredPresentations
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: mixed CSG receiver has 12 closed and 2 open triangles"));
    return true;
}

void CsgGiScene::stop(){
    if(m_capture){
        m_capture->stop();
        m_capture.reset();
    }
    m_active = false;
}

void CsgGiScene::update(){
    if(!m_capture)
        return;

    const u64 presentations = m_context.graphics.getSuccessfulPresentationCount();
    if(presentations >= m_requiredPresentations && !m_liveWarmupComplete){
        const Timer now = TimerNow();
        const u64 frameIndex = m_context.graphics.getFrameIndex();
        if(!m_liveWarmupStarted){
            m_liveWarmupStartTime = now;
            m_liveWarmupStartPresentations = presentations;
            m_liveWarmupStarted = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: CSG GI live warmup start source_frame={} presentations={} required_presentations={} required_seconds={}")
                , frameIndex, presentations, m_requiredPresentations, __hidden_csg_gi_scene::s_LiveWarmupSeconds
            );
        }else{
            const f64 elapsedSeconds = DurationInSeconds<f64>(now, m_liveWarmupStartTime);
            if(elapsedSeconds >= __hidden_csg_gi_scene::s_LiveWarmupSeconds && presentations > m_liveWarmupStartPresentations){
                m_liveWarmupComplete = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: CSG GI live warmup end source_frame={} presentations={} elapsed_seconds={:.6f}")
                    , frameIndex, presentations, elapsedSeconds
                );
            }
        }
    }

    m_capture->update();
    if(m_capture->captureReady() && !m_captureReceiptLogged){
        m_captureReceiptLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: CSG GI capture source_frame={}")
            , m_capture->capturedGraphicsFrameIndex()
        );
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

