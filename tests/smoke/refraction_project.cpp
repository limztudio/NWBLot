// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <global/math/constant.h>
#include <global/math/frame.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_render/module.h>

#include "framebuffer_capture.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;

static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
static constexpr SmokeMaterialRef s_ClearMaterial{"project/smoke/refraction/materials/clear"};
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/refraction/materials/opaque"};
static constexpr SmokeMaterialRef s_TransparentMaterial{"project/smoke/refraction/materials/transparent"};
static constexpr AStringView s_MaterialInterface = "project/shaders/smoke_surface";

class RefractionSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("RefractionSmokeProject"));
        auto& renderer = AddSmokeRenderSystems(*world, context);
        f32 enabled = 1.0f;
        static_cast<void>(ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_ENABLED", enabled));
        renderer.setRefractionEnabled(enabled != 0.0f);
        f32 hardware = 1.0f;
        static_cast<void>(ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_HARDWARE", hardware));
        renderer.setRefractionHardwareTracingEnabled(hardware != 0.0f);
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RefractionSmokeProject: refraction {}"), enabled != 0.0f ? NWB_TEXT("enabled") : NWB_TEXT("disabled")
        );
        return world;
    }

    bool configureFramebufferCapture(){
        SmokeEnvironmentString outputPath(m_context.objectArena);
        if(!ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", outputPath))
            return true;

        u32 captureFrameCount = 16u;
        SmokeEnvironmentString frameCountText(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", frameCountText)){
            u64 parsedFrameCount = 0u;
            if(!ParseU64(AStringView(frameCountText.data(), frameCountText.size()), parsedFrameCount)
                || parsedFrameCount == 0u || parsedFrameCount > static_cast<u64>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: capture frame count must be a positive u32"));
                return false;
            }
            captureFrameCount = static_cast<u32>(parsedFrameCount);
        }
        auto capture = MakeUnique<FramebufferCapture>(
            m_context, AStringView(outputPath.data(), outputPath.size()), captureFrameCount
        );
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

    NWB::Core::ECS::EntityID createPanel(const SmokeMaterialRef& material, const Float4& tint,
        const Float4& position, const Float4& scale){
        const auto entity = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, s_PlaneMesh, material, s_MaterialInterface, tint, position, scale
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: panel creation failed"));
        auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        // The plane starts in XZ with a +Y normal; face the camera along -Z.
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), &transform->rotation);
        return entity;
    }

public:
    explicit RefractionSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context), m_world(createWorldOrDie(context)){}

    virtual ~RefractionSmokeProject()override{ destroyWorld(); }

    virtual bool onStartup()override{
        if(!configureFramebufferCapture())
            return false;
        const auto camera = CreateSmokeCamera(*m_world, 1.4f, 6.0f, 0.0f);
        // The production TLAS is shared with shadow preparation. A light exercises
        // that normal preparation path; the unlit material ignores its radiance.
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f
        );
        const auto glass = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, s_SphereMesh, s_ClearMaterial, s_MaterialInterface,
            Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 1.4f, 0.0f, 0.0f),
            Float4(1.15f, 1.15f, 1.15f, 0.0f)
        );
        NWB_FATAL_ASSERT_MSG(camera.valid() && glass.valid() && light.valid(), NWB_TEXT("RefractionSmokeProject: camera/glass/light creation failed"));

        // Fixed opaque stripes make displaced edges measurable. Unlit materials
        // keep the comparison independent of GI, shadows, and caustic histories.
        for(u32 stripe = 0u; stripe < 20u; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.92f : 0.04f;
            createPanel(s_OpaqueMaterial, Float4(value, value, value, 1.0f),
                Float4(-2.85f + static_cast<f32>(stripe) * 0.3f, 1.4f, 2.8f, 0.0f),
                Float4(0.15f, 1.0f, 2.0f, 0.0f));
        }
        // Red foreground transparency must stay straight across the glass. The
        // blue strip is behind it and participates in the transmitted background.
        createPanel(s_TransparentMaterial, Float4(1.0f, 0.025f, 0.025f, 0.75f),
            Float4(0.0f, 0.85f, -1.7f, 0.0f), Float4(1.6f, 1.0f, 0.08f, 0.0f));
        createPanel(s_TransparentMaterial, Float4(0.025f, 0.1f, 1.0f, 0.70f),
            Float4(0.0f, 1.9f, 1.6f, 0.0f), Float4(2.0f, 1.0f, 0.12f, 0.0f));

        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RefractionSmokeProject: zero-coverage clear sphere + opaque stripes + foreground/background AVBOIT panels created")
        );
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        const f32 fixedDelta = RendererBaselineFixedDelta();
        m_world->tick(fixedDelta > 0.0f ? fixedDelta : delta);
        return true;
    }

private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960, 720 }; }
const tchar* NWB::QueryProjectWindowTitle(){ return NWB_TEXT("NWB Refraction Smoke"); }
UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_refraction_smoke::RefractionSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

