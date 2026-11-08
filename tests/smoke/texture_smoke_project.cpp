// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_capture_factory.h"
#include "texture_srgb_clear_probe.h"
#include "framebuffer_capture.h"
#include "smoke_capture_teardown.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/module.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/frame.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("TextureSmokeProject");

using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeRenderWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroyCapturedSmokeRenderWorld;
using NWB::Tests::Smoke::FramebufferCapture;

using TextureSmokeMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using TextureSmokeMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TextureSmokeMeshRef s_GroundPlaneMesh{"project/meshes/shadow_plane"};
static constexpr TextureSmokeMeshRef s_TexturedSphereMesh{"project/meshes/caustic_sphere"};
static constexpr TextureSmokeMaterialRef s_WhiteGroundMaterial{"project/smoke/texture/materials/white_ground"};
static constexpr AStringView s_WhiteGroundMaterialInterface = "project/shaders/smoke_surface";
static constexpr TextureSmokeMaterialRef s_TextureMaterial{"project/smoke/texture/materials/pattern"};
static constexpr AStringView s_TextureMaterialInterface = "project/shaders/texture_smoke_surface";
static constexpr AStringView s_TextureRuntimeTintParameter = "texture_runtime.color_tint";
static constexpr f32 s_CameraHeight = 3.1f;
static constexpr f32 s_CameraDistance = 5.7f;
static constexpr f32 s_CameraPitch = 0.36f;
// Cast a foreground shadow on the white floor to expose red indirect bounce from the sphere.
static constexpr f32 s_DirectionalLightPitch = 0.48f;
// Aim the shadow toward the foreground rather than hiding the receiver spill behind the sphere from the camera.
static constexpr f32 s_DirectionalLightYaw = 2.2f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;
static constexpr u32 s_FramebufferCaptureDefaultFrameCount = 360u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextureSmokeProject final : public NWB::IProjectEntryCallbacks{
public:
    explicit TextureSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeRenderWorldOrDie(s_ProjectName, context))
    {
        m_captureConfigurationValid = configureFramebufferCapture();
    }
    virtual ~TextureSmokeProject()noexcept override{
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
    }


public:
    virtual b8 onStartup()override{
        if(!m_captureConfigurationValid)
            return false;
        if(!NWB::Tests::Smoke::TextureSrgbClearProbe::verify(m_context.graphics))
            return false;

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(s_CameraHeight, s_CameraDistance, s_CameraPitch, *m_world);
        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, s_DirectionalLightPitch, s_DirectionalLightYaw, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), s_DirectionalLightIntensity);

        // Use an untextured white floor to isolate indirect color bounce from the sphere.
        m_whiteGround = CreateTintedStaticMeshEntity(s_GroundPlaneMesh, s_WhiteGroundMaterial, s_WhiteGroundMaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(3.25f, 1.0f, 3.25f, 0.0f), *m_world);
        // Place the UV-mapped sphere just above the white floor.
        m_texturedSphere = CreateTintedStaticMeshEntity(
            s_TexturedSphereMesh,
            s_TextureMaterial,
            s_TextureMaterialInterface,
            Float4(1.0f, 1.0f, 1.0f, 1.0f),
            Float4(0.0f, 1.16f, 0.15f, 0.0f),
            Float4(1.1f, 1.1f, 1.1f, 0.0f),
            s_TextureRuntimeTintParameter,
            *m_world
        );
        NWB_FATAL_ASSERT_MSG(activeCamera.valid() && directionalLight.valid() && m_whiteGround.valid() && m_texturedSphere.valid(), NWB_TEXT("TextureSmokeProject failed to create the white receiver and textured sphere"));

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TextureSmokeProject: white receiver + UV-mapped authored Texture2D sphere created"));
        return true;
    }

    virtual void onShutdown()override{
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TextureSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        return NWB::Tests::Smoke::TickSmokeWorldWithCapture(delta, *m_world, m_framebufferCapture);
    }


private:
    b8 configureFramebufferCapture(){
        auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(s_ProjectName, s_FramebufferCaptureDefaultFrameCount, {});
        if(!capture)
            return false;
        m_framebufferCapture = Move(*capture);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    NWB::Core::ECS::EntityID m_whiteGround = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_texturedSphere = NWB::Core::ECS::s_InvalidEntityId;
    b8 m_captureConfigurationValid = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB Texture Smoke");
}

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_texture_smoke::TextureSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

