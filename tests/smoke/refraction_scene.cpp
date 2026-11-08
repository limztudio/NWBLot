// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_scene.h"
#include "smoke_environment.h"

#include <global/simd/constant.h>
#include <global/simd/frame.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_scene{

static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::ECS::EntityID RefractionScene::CreatePanel(const SmokeMaterialRef& material, const Float4& tint, const Float4& position, const Float4& scale, Core::ECS::World& world){
    using namespace __hidden_refraction_scene;
    const auto entity = CreateTintedStaticMeshEntity(s_PlaneMesh, material, s_MaterialInterface, tint, position, scale, world);
    NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: panel creation failed"));
    auto& transform = *MakeNotNull(world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity));
    // Rotate the XZ plane to face the camera along -Z.
    StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform.rotation);
    return entity;
}


b8 RefractionScene::CreateDensity(const u32 percent, Core::Alloc::ScratchArena& arena, Core::ECS::World& world, ProjectRuntimeContext& context, Core::ECS::EntityID& glass){
    using namespace __hidden_refraction_scene;
    const auto configuredExtent = ReadSmokeEnvironmentText(arena, "NWB_REFRACTION_SMOKE_EXTENT");
    if(!configuredExtent || (*configuredExtent != "960x720" && *configuredExtent != "1600x900" && *configuredExtent != "1920x1080")){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: density requires explicit extent 960x720, 1600x900, or 1920x1080"));
        return false;
    }
    const auto enabled = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_ENABLED");
    const auto hardware = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_HARDWARE");
    if(!enabled || !hardware || (*enabled && **enabled != "1") || (*hardware && **hardware != "0" && **hardware != "1")){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: density requires refraction enabled and hardware selection absent, 0, or 1"));
        return false;
    }
    const b8 hardwareRequested = !*hardware || **hardware == "1";
    const auto extent = NWB::QueryProjectFrameClientSize();
    const u32 frameWidth = extent.width;
    const u32 frameHeight = extent.height;
    const f32 frameWidthFloat = static_cast<f32>(frameWidth);
    const f32 frameHeightFloat = static_cast<f32>(frameHeight);
    const f32 aspect = frameWidthFloat / frameHeightFloat;
    const f32 tangent = Tan(s_PI / 6.f);
    const f32 fractionEdge = Sqrt(static_cast<f32>(percent) * 0.01f);
    const u32 coveredWidth = Min(frameWidth, 2u * static_cast<u32>(frameWidthFloat * fractionEdge * 0.5f + 0.5f));
    const u32 coveredHeight = Min(frameHeight, 2u * static_cast<u32>(frameHeightFloat * fractionEdge * 0.5f + 0.5f));
    const u32 left = percent == 0u ? 0u : (frameWidth - coveredWidth) / 2u;
    const u32 top = percent == 0u ? 0u : (frameHeight - coveredHeight) / 2u;
    const u32 right = left + coveredWidth;
    const u32 bottom = top + coveredHeight;
    const u64 admittedPixels = static_cast<u64>(coveredWidth) * coveredHeight;
    const u64 totalPixels = static_cast<u64>(extent.width) * extent.height;
    constexpr u32 s_TileEdge = s_RefractionDensityTileEdge;
    const u32 activeTiles = percent == 0u ? 0u : (DivideUp(right, s_TileEdge) - left / s_TileEdge) * (DivideUp(bottom, s_TileEdge) - top / s_TileEdge);
    const u32 totalTiles = DivideUp(frameWidth, s_TileEdge) * DivideUp(frameHeight, s_TileEdge);

    const auto camera = CreateSmokeCamera(1.4f, 6.f, 0.f, world);
    if(!camera.valid())
        return false;
    auto& cameraComponent = world.entity(camera).getComponent<NWB::Impl::Scene::CameraComponent>();
    cameraComponent.setVerticalFovRadians(s_PI / 3.f);
    cameraComponent.setAspectRatio(aspect);
    const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(world, 0.f, 0.f, 0.f, Float4(1.f, 1.f, 1.f, 1.f), 1.f);
    if(!light.valid())
        return false;
    world.entity(light).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;

    // Shared opaque backdrop that occludes the behind-wall slab.
    constexpr f32 s_BackdropDepth = s_RefractionDensityBackdropDepth;
    const f32 backdropHalfHeight = tangent * (6.f + s_BackdropDepth);
    const f32 backdropHalfWidth = backdropHalfHeight * aspect;
    const f32 backdropPanelHalfWidth = backdropHalfWidth * 1.05f;
    const f32 backdropPanelHalfHeight = backdropHalfHeight * 1.05f;
    CreatePanel(s_OpaqueMaterial, Float4(0.04f, 0.04f, 0.04f, 1.f), Float4(0.f, 1.4f, s_BackdropDepth, 0.f), Float4(backdropPanelHalfWidth, 1.f, backdropPanelHalfHeight, 0.f), world);
    constexpr u32 s_StripeCount = s_RefractionDensityStripeCount;
    const f32 stripeHalfWidth = backdropHalfWidth / static_cast<f32>(s_StripeCount);
    const f32 stripeStart = -backdropHalfWidth;
    for(u32 stripe = 1u; stripe < s_StripeCount; stripe += 2u){
        const f32 stripeSlot = 2.f * static_cast<f32>(stripe) + 1.f;
        const f32 x = stripeSlot * stripeHalfWidth + stripeStart;
        CreatePanel(s_OpaqueMaterial, Float4(0.92f, 0.92f, 0.92f, 1.f), Float4(x, 1.4f, s_BackdropDepth - 0.01f, 0.f), Float4(stripeHalfWidth, 1.f, backdropPanelHalfHeight, 0.f), world);
    }

    // Full coverage extends past the viewport while empty coverage hides the refractor behind the wall.
    const f32 frontDepthValue = percent == 0u ? 4.f : 0.f;
    const f32 projectedWidth = percent == 0u || percent == 100u ? frameWidthFloat + 2.f : static_cast<f32>(coveredWidth);
    const f32 projectedHeight = percent == 0u || percent == 100u ? frameHeightFloat + 2.f : static_cast<f32>(coveredHeight);
    const f32 halfHeight = (6.f + frontDepthValue) * tangent;
    const Float4 scale((halfHeight * 2.f * aspect * projectedWidth) / frameWidthFloat, (halfHeight * 2.f * projectedHeight) / frameHeightFloat, 0.2f, 0.f);
    glass = CreateTintedStaticMeshEntity(s_CubeMesh, s_ClearMaterial, s_MaterialInterface, Float4(1.f, 1.f, 1.f, 0.f), Float4(0.f, 1.4f, frontDepthValue + 0.1f, 0.f), scale, world);
    if(!glass.valid())
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionDensity: extent {}x{} requested_percent {} admitted_pixels {} total_pixels {} active_tiles {} total_tiles {} glass_instances 1 capture_mode {}")
        , extent.width
        , extent.height
        , percent
        , admittedPixels
        , totalPixels
        , activeTiles
        , totalTiles
        , percent == 0u ? NWB_TEXT("occluded") : NWB_TEXT("visible")
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionDensity: pixel_bounds {} {} {} {}"), left, top, right, bottom);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionDensity: tracing requested {} ray_query_supported {} refraction_enabled 1")
        , hardwareRequested ? NWB_TEXT("hardware") : NWB_TEXT("screen-space")
        , context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery) ? 1u : 0u
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

