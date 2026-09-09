// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/graphics/runtime/runtime.h>
#include <global/math/constant.h>
#include <global/math/convert.h>
#include <global/math/frame.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>

#include "framebuffer_capture.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;

static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/reflection/materials/opaque"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/reflection/materials/glass"};
static constexpr SmokeMaterialRef s_TransparentMaterial{"project/smoke/refraction/materials/transparent"};
static constexpr AStringView s_MaterialInterface = "project/shaders/smoke_surface";
static constexpr f32 s_GlassIor = 3.8f;
static constexpr f32 s_GlassF0 = ((s_GlassIor - 1.0f) * (s_GlassIor - 1.0f)) / ((s_GlassIor + 1.0f) * (s_GlassIor + 1.0f));

class ReflectionSmokeProject final : public NWB::IProjectEntryCallbacks{
public:
    explicit ReflectionSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeWorldOrDie(context, NWB_TEXT("ReflectionSmokeProject")))
        , m_renderer(AddSmokeRenderSystems(*m_world, context)){}
    virtual ~ReflectionSmokeProject()override{ destroyWorld(); }

    virtual bool onStartup()override{
        if(!configureRenderer() || !configureFramebufferCapture())
            return false;

        SmokeEnvironmentString caseText(m_context.objectArena);
        const bool hasCase = ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_CASE", caseText);
        const AStringView caseName = hasCase ? AStringView(caseText.data(), caseText.size()) : AStringView("offscreen");
        if(caseName != "offscreen" && caseName != "moved" && caseName != "opaque_glass"){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown case '{}'"), StringConvert(caseName));
            return false;
        }
        const auto cameraId = CreateSmokeCamera(*m_world, 1.4f, 6.0f, 0.0f);
        auto& camera = m_world->entity(cameraId).getComponent<NWB::Impl::Scene::CameraComponent>();
        camera.setVerticalFovRadians(s_PI / 3.0f);
        camera.setAspectRatio(4.0f / 3.0f);
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f
        );
        NWB_FATAL_ASSERT_MSG(cameraId.valid() && light.valid(), NWB_TEXT("ReflectionSmokeProject: camera/light creation failed"));

        if(caseName == "opaque_glass")
            createOpaqueGlassScene();
        else
            createMirrorScene(caseName == "moved");
        if(ReadSmokeEnvironmentFlag("NWB_REFLECTION_SMOKE_TIMING"))
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: case {} created"), StringConvert(caseName));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        m_fpsProbe.recordFrame(delta);
        m_gpuPassTimingProbe.recordFrame(delta, m_context.gpuTimingView());
        const f32 fixedDelta = RendererBaselineFixedDelta();
        m_world->tick(fixedDelta > 0.0f ? fixedDelta : delta);
        return true;
    }


private:
    bool configureRenderer(){
        NWB::Impl::ReflectionSettings settings;
        settings.traceMode = NWB::Impl::ReflectionTraceMode::Hardware;
        settings.environmentTop = Float3U(0.0f, 0.0f, 0.0f);
        settings.environmentBottom = Float3U(0.0f, 0.0f, 0.0f);
        settings.maxHardwareRaysPerFrame = 2u * 960u * 720u;
        SmokeEnvironmentString routeText(m_context.objectArena);
        const bool hasRoute = ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_MODE", routeText);
        const AStringView route = hasRoute ? AStringView(routeText.data(), routeText.size()) : AStringView("hardware");
        if(route == "disabled")
            settings.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        else if(route == "screen")
            settings.traceMode = NWB::Impl::ReflectionTraceMode::ScreenSpace;
        else if(route == "hybrid")
            settings.traceMode = NWB::Impl::ReflectionTraceMode::Hybrid;
        else if(route != "hardware"){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown reflection mode '{}'"), StringConvert(route));
            return false;
        }
        if(!m_renderer.setReflectionSettings(settings))
            return false;
        m_renderer.setRefractionEnabled(true);
        m_renderer.setRefractionHardwareTracingEnabled(true);
        const bool hardwareAvailable = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)
            && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: hardware {}")
            , hardwareAvailable ? NWB_TEXT("available") : NWB_TEXT("unavailable")
        );
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: reflection mode {}"), StringConvert(route));
        return true;
    }

    bool configureFramebufferCapture(){
        SmokeEnvironmentString outputPath(m_context.objectArena);
        if(!ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", outputPath))
            return true;
        u32 frameCount = 16u;
        SmokeEnvironmentString frameText(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", frameText)){
            u64 parsed = 0u;
            if(!ParseU64(AStringView(frameText.data(), frameText.size()), parsed) || parsed == 0u || parsed > Limit<u32>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: capture frame count must be a positive u32"));
                return false;
            }
            frameCount = static_cast<u32>(parsed);
        }
        auto capture = MakeUnique<FramebufferCapture>(m_context, AStringView(outputPath.data(), outputPath.size()), frameCount);
        if(!capture || !capture->start())
            return false;
        m_framebufferCapture = Move(capture);
        return true;
    }

    void destroyWorld(){
        DestroySmokeRenderWorld(m_context, m_world);
        if(m_framebufferCapture){
            m_framebufferCapture->stop();
            m_framebufferCapture.reset();
        }
    }

    NWB::Core::ECS::EntityID createMesh(const SmokeMeshRef& mesh, const SmokeMaterialRef& material,
        const Float4& color, const Float4& position, const Float4& scale, const f32 specularF0 = 0.0f){
        const auto entity = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, mesh, material, s_MaterialInterface, color, position, scale
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("ReflectionSmokeProject: mesh creation failed"));
        const Name materialInterface(s_MaterialInterface);
        const Half4U f0 = MakeHalf4U(specularF0, specularF0, specularF0, 0.0f);
        const Half roughness = ConvertFloatToHalf(0.0f);
        NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
            *m_world, entity, materialInterface, "runtime.specular_f0", NWB::Impl::MaterialLayoutFieldType::Half3,
            NWB::Impl::PackMaterialInstanceBytes(f0.raw, sizeof(Half) * 3u)
        ), NWB_TEXT("ReflectionSmokeProject: F0 override failed"));
        NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
            *m_world, entity, materialInterface, "runtime.perceptual_roughness", NWB::Impl::MaterialLayoutFieldType::Half,
            NWB::Impl::PackMaterialInstanceBytes(&roughness, sizeof(roughness))
        ), NWB_TEXT("ReflectionSmokeProject: roughness override failed"));
        return entity;
    }

    void createPanel(const SmokeMaterialRef& material, const Float4& color, const Float4& position,
        const Float4& scale, const f32 specularF0 = 0.0f){
        const auto entity = createMesh(s_PlaneMesh, material, color, position, scale, specularF0);
        auto& transform = m_world->entity(entity).getComponent<NWB::Impl::Scene::TransformComponent>();
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), &transform.rotation);
    }

    void createMirrorScene(const bool moved){
        createPanel(s_OpaqueMaterial, Float4(0.12f, 0.12f, 0.12f, 1.0f),
            Float4(0.0f, 1.4f, 2.0f, 0.0f), Float4(6.0f, 1.0f, 4.5f, 0.0f));
        createPanel(s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f),
            Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(2.8f, 1.0f, 1.8f, 0.0f), 0.95f);
        const f32 shift = moved ? 0.9f : 0.0f;
        // Both marker spheres are behind the camera. Reflecting them through z=0 gives virtual images at z=+8.
        const auto red = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f),
            Float4(-1.6f + shift, 1.0f, -8.0f, 0.0f), Float4(0.65f, 0.65f, 0.65f, 0.0f));
        const auto green = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f),
            Float4(1.6f + shift, 2.0f, -8.0f, 0.0f), Float4(0.65f, 0.65f, 0.65f, 0.0f));
        NWB_FATAL_ASSERT_MSG(red.valid() && green.valid(), NWB_TEXT("ReflectionSmokeProject: offscreen markers failed"));
    }

    void createOpaqueGlassScene(){
        for(u32 stripe = 0u; stripe < 24u; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.55f : 0.015f;
            createPanel(s_OpaqueMaterial, Float4(value, value, value, 1.0f),
                Float4(-3.45f + static_cast<f32>(stripe) * 0.3f, 1.4f, 2.8f, 0.0f),
                Float4(0.15f, 1.0f, 3.0f, 0.0f));
        }
        const auto opaque = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f),
            Float4(-1.3f, 1.5f, 0.0f, 0.0f), Float4(0.95f, 0.95f, 0.95f, 0.0f), 0.95f);
        const auto glass = createMesh(s_SphereMesh, s_GlassMaterial, Float4(1.0f, 1.0f, 1.0f, 0.0f),
            Float4(1.3f, 1.5f, 0.0f, 0.0f), Float4(0.95f, 0.95f, 0.95f, 0.0f), s_GlassF0);
        NWB_FATAL_ASSERT_MSG(opaque.valid() && glass.valid(), NWB_TEXT("ReflectionSmokeProject: opaque/glass spheres failed"));
        createPanel(s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f),
            Float4(-4.0f, 1.4f, -8.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f));
        createPanel(s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f),
            Float4(4.0f, 1.4f, -8.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f));
        createPanel(s_TransparentMaterial, Float4(0.015f, 0.025f, 1.0f, 0.90f),
            Float4(0.0f, 0.85f, -1.7f, 0.0f), Float4(2.0f, 1.0f, 0.08f, 0.0f));
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Impl::RendererSystem& m_renderer;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    FpsProbe m_fpsProbe{ NWB_TEXT("ReflectionSmokeProject") };
    GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("ReflectionSmokeProject") };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960, 720 }; }
const tchar* NWB::QueryProjectWindowTitle(){ return NWB_TEXT("NWB Reflection Smoke"); }
UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_reflection_smoke::ReflectionSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

