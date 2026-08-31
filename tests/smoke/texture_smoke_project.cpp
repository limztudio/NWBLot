// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <global/math/frame.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>

#include "framebuffer_capture.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
using NWB::Tests::Smoke::FramebufferCapture;
using NWB::Tests::Smoke::ReadSmokeEnvironmentText;
using NWB::Tests::Smoke::SmokeEnvironmentString;

using TextureSmokeMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using TextureSmokeMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TextureSmokeMeshRef s_GroundPlaneMesh = []() constexpr{
    TextureSmokeMeshRef result;
    result.virtualPath = Name("project/meshes/shadow_plane");
    return result;
}();
static constexpr TextureSmokeMeshRef s_TexturedSphereMesh = []() constexpr{
    TextureSmokeMeshRef result;
    result.virtualPath = Name("project/meshes/caustic_sphere");
    return result;
}();
static constexpr TextureSmokeMaterialRef s_WhiteGroundMaterial = []() constexpr{
    TextureSmokeMaterialRef result;
    result.virtualPath = Name("project/smoke/texture/materials/white_ground");
    return result;
}();
static constexpr AStringView s_WhiteGroundMaterialInterface = "project/shaders/smoke_surface";
static constexpr TextureSmokeMaterialRef s_TextureMaterial = []() constexpr{
    TextureSmokeMaterialRef result;
    result.virtualPath = Name("project/smoke/texture/materials/pattern");
    return result;
}();
static constexpr AStringView s_TextureMaterialInterface = "project/shaders/texture_smoke_surface";
static constexpr AStringView s_TextureRuntimeTintParameter = "texture_runtime.color_tint";
static constexpr f32 s_CameraHeight = 3.1f;
static constexpr f32 s_CameraDistance = 5.7f;
static constexpr f32 s_CameraPitch = 0.36f;
// A low, bright white key makes the opaque sphere cast a long direct shadow.  The nearby part of that shadow is the
// unambiguous receiver-only location where the texture's red-dominant diffuse bounce is visible.
static constexpr f32 s_DirectionalLightPitch = 0.48f;
// Aim the shadow toward the foreground rather than hiding the receiver spill behind the sphere from the camera.
static constexpr f32 s_DirectionalLightYaw = 2.2f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;
static constexpr u32 s_FramebufferCaptureDefaultFrameCount = 360u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextureSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("TextureSmokeProject"));
        AddSmokeRenderSystems(*world, context);
        return world;
    }

    bool configureFramebufferCapture(){
        SmokeEnvironmentString outputPath(m_context.objectArena);
        if(!ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", outputPath))
            return true;

        u32 captureFrameCount = s_FramebufferCaptureDefaultFrameCount;
        SmokeEnvironmentString frameCountText(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", frameCountText)){
            u64 parsedFrameCount = 0u;
            if(
                !ParseU64(AStringView(frameCountText.data(), frameCountText.size()), parsedFrameCount)
                || parsedFrameCount == 0u
                || parsedFrameCount > static_cast<u64>(Limit<u32>::s_Max)
            ){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("TextureSmokeProject: NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT must be a positive u32")
                );
                return false;
            }
            captureFrameCount = static_cast<u32>(parsedFrameCount);
        }

        auto capture = MakeUnique<FramebufferCapture>(
            m_context,
            AStringView(outputPath.data(), outputPath.size()),
            captureFrameCount
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


public:
    explicit TextureSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {
        m_captureConfigurationValid = configureFramebufferCapture();
    }

    virtual ~TextureSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        if(!m_captureConfigurationValid)
            return false;

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(
            *m_world,
            s_CameraHeight,
            s_CameraDistance,
            s_CameraPitch
        );
        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DirectionalLightPitch,
            s_DirectionalLightYaw,
            0.0f,
            Float4(1.0f, 1.0f, 1.0f, 1.0f),
            s_DirectionalLightIntensity
        );

        // A white diffuse receiver makes the textured sphere's direct shadow and colored indirect bounce readable.
        // Keep the plane separate from the texture material so no pattern leaks onto the GI receiver itself.
        m_whiteGround = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundPlaneMesh,
            s_WhiteGroundMaterial,
            s_WhiteGroundMaterialInterface,
            Float4(1.0f, 1.0f, 1.0f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f),
            Float4(3.25f, 1.0f, 3.25f, 0.0f)
        );
        // caustic_sphere is the smoke asset's dense, smooth, UV-mapped sphere.  It rests just above the receiver,
        // where the white plane provides a clean read of the texture's colored irradiance after GI converges.
        m_texturedSphere = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TexturedSphereMesh,
            s_TextureMaterial,
            s_TextureMaterialInterface,
            Float4(1.0f, 1.0f, 1.0f, 1.0f),
            Float4(0.0f, 1.16f, 0.15f, 0.0f),
            Float4(1.1f, 1.1f, 1.1f, 0.0f),
            s_TextureRuntimeTintParameter
        );
        NWB_FATAL_ASSERT_MSG(
            activeCamera.valid() && directionalLight.valid() && m_whiteGround.valid() && m_texturedSphere.valid(),
            NWB_TEXT("TextureSmokeProject failed to create the white receiver and textured sphere")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TextureSmokeProject: white receiver + UV-mapped authored Texture2D sphere created"));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TextureSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        m_world->tick(delta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    NWB::Core::ECS::EntityID m_whiteGround = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_texturedSphere = NWB::Core::ECS::ENTITY_ID_INVALID;
    bool m_captureConfigurationValid = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

const tchar* NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB Texture Smoke");
}

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_texture_smoke::TextureSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

