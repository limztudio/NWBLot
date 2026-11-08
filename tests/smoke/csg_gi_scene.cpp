// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_gi_scene.h"
#include "smoke_capture_factory.h"

#include "csg_smoke_helpers.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"

#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>

#include <core/alloc/scratch.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_gi_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ReferenceArm = "reference";
constexpr AStringView s_BoxShapePath = "engine/csg/box";
constexpr AStringView s_CutArm = "cut";

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
static constexpr Array<Name, 3> s_Groups = { Name("smoke/gi/passage"), Name("smoke/gi/cap"), Name("smoke/gi/mixed") };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Box = SmokeBoxBounds;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::ECS::EntityID CreateBox(const Box& box, const SmokeMaterialRef& material, const Float4& tint, Core::ECS::World& world){
    const SIMDVectorSingle minimum = LoadFloat(box.minimum);
    const SIMDVectorSingle maximum = LoadFloat(box.maximum);
    Float4 center;
    Float4 extent;
    StoreFloat(VectorScale(VectorAdd(minimum, maximum), 0.5f), center);
    StoreFloat(VectorSubtract(maximum, minimum), extent);
    return CreateTintedStaticMeshEntity(s_CubeMesh, material, s_MaterialInterface, tint, center, extent, world);
}

[[nodiscard]] static b8 CreateReferencePieces(const Box& box, const Box& cutter, const SmokeMaterialRef& material, const Float4& tint, Core::ECS::World& world){
    // Ordinary closed pieces provide independent triangle geometry for the same subtraction union.
    Array<Box, 6> pieces{};
    if(!BuildSmokeBoxSubtractionPieces(box, cutter, pieces))
        return false;
    for(const auto& piece : pieces){
        if(!SmokeBoxBoundsValid(piece))
            continue;
        if(!CreateBox(piece, material, tint, world).valid())
            return false;
    }
    return true;
}

[[nodiscard]] static b8 CreateSubtraction(const Box& box, const Box& cutter, const u32 slot, const AStringView arm, Core::ECS::World& world){
    const auto& material = slot == 0u ? s_DiffuseMaterial : s_CapMaterial;
    const Float4& tint = slot == 0u ? s_Black : s_White;
    if(arm == s_ReferenceArm)
        return CreateReferencePieces(box, cutter, material, tint, world);

    const auto receiver = CreateBox(box, material, tint, world);
    if(!receiver.valid())
        return false;
    AddStaticCsgMeshReceiver(receiver, s_Groups[slot], true, false, world);
    // Overlapping cutters form one opening without an internal GI seam.
    const f32 cutterSpan = cutter.maximum.x - cutter.minimum.x;
    const f32 overlapBegin = cutter.minimum.x + cutterSpan * 0.35f;
    const f32 overlapEnd = cutter.minimum.x + cutterSpan * 0.65f;
    const Array<Box, 2> cuts = {{
        { cutter.minimum, Float3U(overlapEnd, cutter.maximum.y, cutter.maximum.z) },
        { Float3U(overlapBegin, cutter.minimum.y, cutter.minimum.z), cutter.maximum },
    }};
    for(const auto& cut : cuts){
        auto entity = world.createEntity();
        auto& component = entity.addComponent<Impl::CsgCutterComponent>();
        component.receiverGroup = s_Groups[slot];
        component.shapeType = Name(s_BoxShapePath);
        component.active = arm == s_CutArm;
        Impl::CsgBoxShapeParameters parameters;
        const SIMDVectorSingle minimum = LoadFloat(cut.minimum);
        const SIMDVectorSingle maximum = LoadFloat(cut.maximum);
        StoreFloat(VectorScale(VectorSubtract(maximum, minimum), 0.5f), parameters.halfExtents);
        AssignCsgCutterParameters(parameters, component);
        AssignCsgCutterTransform(VectorScale(VectorAdd(minimum, maximum), 0.5f), QuaternionIdentity(), component);
    }
    return true;
}

[[nodiscard]] static b8 CreateMixedSubtraction(const AStringView arm, Core::ECS::World& world){
    const Box box{ Float3U(-0.5f, -0.8f, -0.8f), Float3U(0.5f, 0.2f, 0.8f) };
    const Box cutter{ Float3U(-0.25f, -1.0f, -1.2f), Float3U(0.25f, 0.2f, 1.0f) };
    if(arm == s_ReferenceArm){
        if(!CreateReferencePieces(box, cutter, s_MixedMaterial, s_White, world))
            return false;
        const Array<f32, 2> quadCenters = { -0.875f, 0.875f };
        for(const f32 x : quadCenters){
            if(!CreateTintedStaticMeshEntity(s_OpenQuadMesh, s_MixedMaterial, s_MaterialInterface, s_White, Float4(x, -0.3f, -1.0f, 0.0f), Float4(1.25f, 1.0f, 1.0f, 0.0f), world).valid())
                return false;
        }
        return true;
    }

    // One asset contains a closed solid and an independently open two-triangle surface.
    const auto receiver = CreateTintedStaticMeshEntity(s_MixedMesh, s_MixedMaterial, s_MaterialInterface, s_White, Float4(0.0f, -0.3f, 0.0f, 0.0f), Float4(1.0f, 1.0f, 1.6f, 0.0f), world);
    if(!receiver.valid())
        return false;
    AddStaticCsgMeshReceiver(receiver, s_Groups[2], true, false, world);
    auto entity = world.createEntity();
    auto& component = entity.addComponent<Impl::CsgCutterComponent>();
    component.receiverGroup = s_Groups[2];
    component.shapeType = Name(s_BoxShapePath);
    component.active = arm == s_CutArm;
    Impl::CsgBoxShapeParameters parameters;
    parameters.halfExtents = Float4(0.25f, 0.6f, 1.1f, 0.0f);
    AssignCsgCutterParameters(parameters, component);
    AssignCsgCutterTransform(VectorSet(0.0f, -0.4f, -0.1f, 0.0f), QuaternionIdentity(), component);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgGiScene::ShouldCapture(void* context, const u64)noexcept{
    const auto& scene = *MakeNotNull(static_cast<CsgGiScene*>(context));
    return scene.m_liveWarmupComplete && scene.m_context.graphics.getSuccessfulPresentationCount() >= scene.m_requiredPresentations;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgGiScene::CsgGiScene(ProjectRuntimeContext& context, Core::ECS::World& world)noexcept
    : m_context(context)
    , m_world(world)
{}

Expected<b8> CsgGiScene::start(){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
    const auto configuredArm = ReadSmokeEnvironmentText(scratchArena, "NWB_GI_CSG_ARM");
    if(!configuredArm)
        return false;
    const AStringView arm(configuredArm->data(), configuredArm->size());
    if(arm != __hidden_csg_gi_scene::s_ReferenceArm && arm != __hidden_csg_gi_scene::s_CutArm && arm != "uncut"){
        NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: invalid NWB_GI_CSG_ARM"));
        return MakeUnexpected(Failure{});
    }
    if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT")){
        const auto count = ParseU64(AStringView(value->data(), value->size()));
        constexpr u32 s_MinimumWarmupPresentations = 360u;
        if(!count || *count < s_MinimumWarmupPresentations || *count > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: CSG GI warmup requires at least 360 successful presentations"));
            return MakeUnexpected(Failure{});
        }
        m_requiredPresentations = *count;
    }

    Impl::RendererSystem& renderer = *MakeNotNull(m_world.getSystem<Impl::RendererSystem>());
    if(ReadSmokeEnvironmentFlag("NWB_GI_CSG_LAGGED_LIGHTING"))
        renderer.setFrameLaggedAsyncLightingEnabled(true);
    Impl::ReflectionSettings reflection;
    reflection.traceMode = Impl::ReflectionTraceMode::Disabled;
    Impl::PresentationSettings presentation;
    presentation.toneMap = Impl::PresentationToneMap::LinearClamp;
    if(!renderer.setReflectionSettings(reflection) || !renderer.setPresentationSettings(presentation))
        return MakeUnexpected(Failure{});
    renderer.setRefractionEnabled(false);

    const auto camera = CreateSmokeCamera(1.3f, 5.5f, 0.0f, m_world);
    const auto light = Impl::Scene::CreatePointLightEntity(m_world, Float4(0.0f, 1.3f, -3.0f, 0.0f), __hidden_csg_gi_scene::s_White, 20.0f, 15.0f);
    auto* lightComponent = m_world.tryGetComponent<Impl::Scene::LightComponent>(light);
    if(!camera.valid() || !lightComponent)
        return MakeUnexpected(Failure{});
    lightComponent->sourceRadius = 0.0f;
    lightComponent->enableCaustics = false;

    using namespace __hidden_csg_gi_scene;
    if(!CreateBox({ Float3U(-5.0f, -1.0f, -6.6f), Float3U(5.0f, 5.0f, -6.5f) }, s_DiffuseMaterial, Float4(0.02f, 0.02f, 1.0f, 1.0f), m_world).valid())
        return MakeUnexpected(Failure{});
    for(u32 slot = 0u; slot < 2u; ++slot){
        const f32 x = slot == 0u ? -1.3f : 1.3f;
        const Box receiver{ Float3U(x - 0.9f, 0.3f, -0.8f), Float3U(x + 0.9f, 2.3f, 0.8f) };
        const Box cutter{ Float3U(x - 0.65f, 0.55f, -1.0f), Float3U(x + 0.65f, 2.05f, slot == 0u ? 1.0f : 0.4f) };
        if(!CreateSubtraction(receiver, cutter, slot, arm, m_world))
            return MakeUnexpected(Failure{});
    }
    if(!CreateMixedSubtraction(arm, m_world))
        return MakeUnexpected(Failure{});
    const auto passage = CreateBox({ Float3U(-1.9f, 0.65f, 1.0f), Float3U(-0.7f, 1.95f, 1.08f) }, s_IndirectMaterial, s_White, m_world);
    // The cavity receiver sees bounced light from the green generated back cap.
    const auto cavity = CreateBox({ Float3U(0.85f, 0.56f, -0.72f), Float3U(1.75f, 0.61f, 0.06f) }, s_IndirectMaterial, s_White, m_world);
    const auto control = CreateBox({ Float3U(-0.35f, 2.7f, 0.0f), Float3U(0.35f, 3.15f, 0.06f) }, s_IndirectMaterial, s_White, m_world);
    if(!passage.valid() || !cavity.valid() || !control.valid())
        return MakeUnexpected(Failure{});
    const auto mixedPassage = CreateBox({ Float3U(-0.2f, -0.7f, 1.0f), Float3U(0.2f, -0.4f, 1.08f) }, s_IndirectMaterial, s_White, m_world);
    const auto mixedFloor = CreateBox({ Float3U(-1.3f, -0.82f, -1.85f), Float3U(-0.75f, -0.77f, -1.08f) }, s_IndirectMaterial, s_White, m_world);
    if(!mixedPassage.valid() || !mixedFloor.valid())
        return MakeUnexpected(Failure{});

    const FramebufferCaptureOptions options{ .shouldCapture = ShouldCapture, .predicateContext = this, .requiredWidth = 1280u, .requiredHeight = 900u, };
    constexpr u32 s_DefaultCaptureFrameCount = 360u;
    auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(NWB_TEXT("GiTestSmokeProject"), s_DefaultCaptureFrameCount, options, scratchArena);
    if(!capture)
        return MakeUnexpected(Failure{});
    m_capture = Move(*capture);
    m_active = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: CSG GI arm={} overlapping_box_cutters=2 accepted_presentations={}"), ConvertString(arm), m_requiredPresentations);
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
                , frameIndex
                , presentations
                , m_requiredPresentations
                , __hidden_csg_gi_scene::s_LiveWarmupSeconds
            );
        }else{
            const f64 elapsedSeconds = DurationInSeconds<f64>(now, m_liveWarmupStartTime);
            if(elapsedSeconds >= __hidden_csg_gi_scene::s_LiveWarmupSeconds && presentations > m_liveWarmupStartPresentations){
                m_liveWarmupComplete = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: CSG GI live warmup end source_frame={} presentations={} elapsed_seconds={:.6f}"), frameIndex, presentations, elapsedSeconds);
            }
        }
    }

    m_capture->update();
    if(m_capture->captureReady() && !m_captureReceiptLogged){
        m_captureReceiptLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: CSG GI capture source_frame={}"), m_capture->capturedGraphicsFrameIndex());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

