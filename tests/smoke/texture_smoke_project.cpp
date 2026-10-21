// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "framebuffer_capture.h"
#include "smoke_capture_teardown.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>

#include <global/bit.h>
#include <global/scope_exit.h>
#include <global/simd/frame.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ReadbackResourcePath = "tests.smoke.texture.srgb_edges.readback";
constexpr TStringView s_ProjectName = NWB_TEXT("TextureSmokeProject");

using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroyCapturedSmokeRenderWorld;
using NWB::Tests::Smoke::FramebufferCapture;
using NWB::Tests::Smoke::ConfigureSmokeFramebufferCapture;
using NWB::Tests::Smoke::ReadSmokeEnvironmentText;
using NWB::Tests::Smoke::SmokeEnvironmentString;

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
// A low, bright white key makes the opaque sphere cast a long direct shadow.
// The nearby part of that shadow is the unambiguous receiver-only location where the texture's red-dominant diffuse bounce is visible.
static constexpr f32 s_DirectionalLightPitch = 0.48f;
// Aim the shadow toward the foreground rather than hiding the receiver spill behind the sphere from the camera.
static constexpr f32 s_DirectionalLightYaw = 2.2f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;
static constexpr u32 s_FramebufferCaptureDefaultFrameCount = 360u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_SRGBClearEdgeWidth = 6u;
static constexpr u32 s_SRGBClearEdgeHeight = 4u;
static constexpr u32 s_SRGBClearEdgeBytesPerPixel = 4u;
static constexpr NWB::Core::Color s_SRGBClearEdgeColors[]{
    { BitCast<f32>(BitCast<u32>(0.0031308f) - 1u), 0.0031308f, BitCast<f32>(BitCast<u32>(0.0031308f) + 1u), 0.25f },
    { BitCast<f32>(BitCast<u32>(0.21404114f) - 1u), 0.21404114f, BitCast<f32>(BitCast<u32>(0.21404114f) + 1u), 0.5f },
    { 0.25f, 0.5f, 0.75f, 1.0f },
    { -0.25f, 1.25f, 0.0f, -0.5f },
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u8 ExpectedSRGBClearByte(const f32 value, const bool srgb)noexcept{
    const f32 clamped = Clamp(value, 0.0f, 1.0f);
    f32 encoded = clamped;
    if(srgb){
        // Keep the scalar oracle's rounded steps separate from the packed implementation.
        volatile f32 power = Pow(clamped, 1.0f / 2.4f);
        volatile f32 scaled = 1.055f * power;
        volatile f32 nonlinear = scaled - 0.055f;
        encoded = clamped <= 0.0031308f ? clamped * 12.92f : nonlinear;
    }
    volatile f32 quantized = Clamp(encoded, 0.0f, 1.0f) * 255.0f;
    return static_cast<u8>(Floor(quantized + 0.5f));
}

struct SRGBClearEdgeTask{
    static constexpr NWB::Core::GpuTaskCommandRequirements s_CommandRequirements{ .alternativeCapabilities = NWB::Core::GpuQueueCapability::Compute | NWB::Core::GpuQueueCapability::Graphics, };


public:
    struct Payload{
        NWB::Core::TextureHandle destination;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, NWB::Core::CommandList& commands, const NWB::Core::GpuTaskRecordContext& context){
        if(!payload.destination || context.commandIrCapture)
            return false;
        for(u32 index = 0u; index < LengthOf(s_SRGBClearEdgeColors); ++index)
            commands.clearTextureRectFloat(*payload.destination, NWB::Core::TextureSubresourceSet(0u, 1u, 0u, 1u), NWB::Core::Rect(static_cast<i32>(index + 1u), static_cast<i32>(index + 2u), 1, 3), s_SRGBClearEdgeColors[index]);
        return !commands.commandRecordingFailed();
    }
};

struct SRGBClearEdgeReadbackTask{
    static constexpr NWB::Core::GpuTaskCommandRequirements s_CommandRequirements{ NWB::Core::GpuQueueCapability::Transfer };


public:
    struct Payload{
        NWB::Core::TextureHandle source;
        NWB::Core::StagingTextureHandle destination;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, NWB::Core::CommandList& commands, const NWB::Core::GpuTaskRecordContext& context){
        if(!payload.source || !payload.destination || context.commandIrCapture)
            return false;
        commands.copyTexture(*payload.destination, NWB::Core::TextureSlice{}, *payload.source, NWB::Core::TextureSlice{});
        return !commands.commandRecordingFailed();
    }
};

struct SRGBClearEdgeInputs{
    NWB::Core::TextureHandle texture;
    NWB::Core::StagingTextureHandle readback;
};

[[nodiscard]] static Expected<NWB::Core::GpuTaskId> DeclareSRGBClearEdges(void* const userData, NWB::Core::GpuTaskGraph& graph){
    using namespace NWB::Core;
    const auto& inputs = *static_cast<const SRGBClearEdgeInputs*>(userData);
    const auto texture = graph.importTexture(inputs.texture, GpuGraphResourceDesc{}.setIdentity(Name("tests.smoke.texture.srgb_edges")).setMarkerLabel("sRGB clear edge texture").setType(GpuGraphResourceType::Texture).setInitialState(ResourceStates::Unknown));
    const auto readback = graph.importHazardDomain(GpuGraphResourceDesc{}.setIdentity(Name(s_ReadbackResourcePath)).setMarkerLabel("sRGB clear edge staging writes").setType(GpuGraphResourceType::HazardDomain));
    if(!texture || !readback)
        return MakeUnexpected(Failure{});

    GpuClearTextureTaskDesc clear;
    clear.destination = *texture;
    clear.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    clear.valueType = GpuClearTextureTaskValueType::Float;
    clear.floatValue = Color(0.0f, 0.0f, 0.0f, 1.0f);
    const auto initialized = graph.addClearTextureTask(GpuTaskDesc{}.setIdentity(Name("tests.smoke.texture.srgb_edges.initialize")).setMarkerLabel("Initialize sRGB clear edge border"), clear);
    if(!initialized)
        return MakeUnexpected(Failure{});

    const GpuTaskResourceUse clearUse[]{
        { .resource = *texture, .range = { .textureSubresources = TextureSubresourceSet(0u, 1u, 0u, 1u) }, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write },
    };
    const auto bounded = graph.addTask<SRGBClearEdgeTask>(
        GpuTaskDesc{}.setIdentity(Name("tests.smoke.texture.srgb_edges.bounded")).setMarkerLabel("Clear bounded sRGB edge strips").setDependencies(&*initialized, 1u).setResourceUses(clearUse, LengthOf(clearUse)),
        SRGBClearEdgeTask::Payload{ inputs.texture }
    );
    if(!bounded)
        return MakeUnexpected(Failure{});

    const GpuTaskResourceUse readbackUses[]{
        { .resource = *texture, .range = { .textureSubresources = TextureSubresourceSet(0u, 1u, 0u, 1u) }, .requiredState = ResourceStates::CopySource, .access = GpuTaskResourceAccess::Read },
        { .resource = *readback, .range = {}, .requiredState = ResourceStates::Unknown, .access = GpuTaskResourceAccess::Write },
    };
    return graph.addTask<SRGBClearEdgeReadbackTask>(
        GpuTaskDesc{}.setIdentity(Name(s_ReadbackResourcePath)).setMarkerLabel("Read back sRGB clear edge pixels").setDependencies(&*bounded, 1u).setResourceUses(readbackUses, LengthOf(readbackUses)),
        SRGBClearEdgeReadbackTask::Payload{ inputs.texture, inputs.readback }
    );
}

[[nodiscard]] static bool VerifySRGBClearEdges(NWB::Core::GraphicsRuntime& graphics){
    using namespace NWB::Core;
    auto& device = graphics.getDevice();
    for(const Format::Enum format : { Format::RGBA8_UNORM_SRGB, Format::BGRA8_UNORM_SRGB }){
        TextureDesc description;
        description.width = s_SRGBClearEdgeWidth;
        description.height = s_SRGBClearEdgeHeight;
        description.format = format;
        description.isShaderResource = false;
        const auto texture = graphics.createTexture(description);
        const auto readback = device.createStagingTexture(description, CpuAccessMode::Read);
        if(!texture || !readback){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureSmokeProject: failed to allocate the sRGB clear edge resources"));
            return false;
        }
        SRGBClearEdgeInputs inputs{ *texture, *readback };
        const auto token = graphics.submitStandaloneTaskGraph(&inputs, DeclareSRGBClearEdges);
        if(!token || !device.waitForSubmissionToken(*token)){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureSmokeProject: sRGB clear edge graph did not complete"));
            return false;
        }
        const auto mapping = device.mapStagingTexture(**readback, TextureSlice{}, CpuAccessMode::Read);
        if(!mapping){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureSmokeProject: failed to map the sRGB clear edge readback"));
            return false;
        }
        ScopeExit unmap([&]()noexcept{ device.unmapStagingTexture(**readback); });
        if(!mapping->data || mapping->rowPitch < s_SRGBClearEdgeWidth * s_SRGBClearEdgeBytesPerPixel){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureSmokeProject: sRGB clear edge mapping has an invalid extent"));
            return false;
        }
        const auto bytes = MakeNotNull(static_cast<const u8*>(mapping->data));
        for(u32 y = 0u; y < s_SRGBClearEdgeHeight; ++y){
            for(u32 x = 0u; x < s_SRGBClearEdgeWidth; ++x){
                u8 expected[]{ 0u, 0u, 0u, 255u };
                if(x > 0u && x <= LengthOf(s_SRGBClearEdgeColors) && y > 0u && y < 3u){
                    const Color& color = s_SRGBClearEdgeColors[x - 1u];
                    expected[0] = ExpectedSRGBClearByte(color.r, true);
                    expected[1] = ExpectedSRGBClearByte(color.g, true);
                    expected[2] = ExpectedSRGBClearByte(color.b, true);
                    expected[3] = ExpectedSRGBClearByte(color.a, false);
                    if(format == Format::BGRA8_UNORM_SRGB)
                        Swap(expected[0], expected[2]);
                }
                for(u32 channel = 0u; channel < s_SRGBClearEdgeBytesPerPixel; ++channel){
                    const u8 actual = bytes.get()[static_cast<usize>(y) * mapping->rowPitch + static_cast<usize>(x) * s_SRGBClearEdgeBytesPerPixel + channel];
                    if(actual != expected[channel]){
                        NWB_LOGGER_ERROR(NWB_TEXT("TextureSmokeProject: sRGB bounded clear changed format {} pixel ({}, {}) channel {}: {} != {}"), static_cast<u32>(format), x, y, channel, actual, expected[channel]);
                        return false;
                    }
                }
            }
        }
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TextureSmokeProject: bounded sRGB clear edges preserve alpha and neighboring pixels"));
    return true;
}


class TextureSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, s_ProjectName);
        AddSmokeRenderSystems(*world, context);
        return world;
    }


private:
    bool configureFramebufferCapture(){
        auto capture = ConfigureSmokeFramebufferCapture(m_context, s_ProjectName, s_FramebufferCaptureDefaultFrameCount);
        if(!capture)
            return false;
        m_framebufferCapture = Move(*capture);
        return true;
    }


public:
    explicit TextureSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
    {
        m_captureConfigurationValid = configureFramebufferCapture();
    }
    virtual ~TextureSmokeProject()noexcept override{
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
    }


public:
    virtual bool onStartup()override{
        if(!m_captureConfigurationValid)
            return false;
        if(!VerifySRGBClearEdges(m_context.graphics))
            return false;

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraHeight, s_CameraDistance, s_CameraPitch);
        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, s_DirectionalLightPitch, s_DirectionalLightYaw, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), s_DirectionalLightIntensity);

        // A white diffuse receiver makes the textured sphere's direct shadow and colored indirect bounce readable.
        // Keep the plane separate from the texture material so no pattern leaks onto the GI receiver itself.
        m_whiteGround = CreateTintedStaticMeshEntity(*m_world, m_context.objectArena, s_GroundPlaneMesh, s_WhiteGroundMaterial, s_WhiteGroundMaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(3.25f, 1.0f, 3.25f, 0.0f));
        // caustic_sphere is the smoke asset's dense, smooth, UV-mapped sphere.
        // It rests just above the receiver, where the white plane provides a clean read of the texture's colored irradiance after GI converges.
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
        NWB_FATAL_ASSERT_MSG(activeCamera.valid() && directionalLight.valid() && m_whiteGround.valid() && m_texturedSphere.valid(), NWB_TEXT("TextureSmokeProject failed to create the white receiver and textured sphere"));

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TextureSmokeProject: white receiver + UV-mapped authored Texture2D sphere created"));
        return true;
    }

    virtual void onShutdown()override{
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TextureSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        const auto updated = m_world->tick(delta);
        if(!updated){
            NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
            return false;
        }
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    NWB::Core::ECS::EntityID m_whiteGround = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_texturedSphere = NWB::Core::ECS::s_InvalidEntityId;
    bool m_captureConfigurationValid = true;
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

