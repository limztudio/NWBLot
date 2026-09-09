// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_gallery_scene.h"

#include "smoke_project_helpers.h"

#include <global/math/constant.h>
#include <global/math/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::Smoke{
namespace __hidden_refraction_gallery_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr SmokeMeshRef s_Plane{"project/meshes/shadow_plane"};
constexpr SmokeMeshRef s_Sphere{"project/meshes/caustic_sphere"};
constexpr SmokeMeshRef s_Torus{"project/meshes/refraction_torus"};
constexpr SmokeMeshRef s_Shells{"project/meshes/refraction_two_shells"};
constexpr SmokeMeshRef s_Prism{"project/meshes/refraction_prism"};
constexpr SmokeMaterialRef s_Glass{"project/smoke/refraction/materials/gallery_glass"};
constexpr SmokeMaterialRef s_Preview{"project/smoke/refraction/materials/gallery_preview"};
constexpr SmokeMaterialRef s_Opaque{"project/smoke/refraction/materials/opaque"};
constexpr SmokeMaterialRef s_Transparent{"project/smoke/refraction/materials/transparent"};
constexpr AStringView s_Interface = "project/shaders/smoke_surface";


class GalleryBuilder final{
public:
    GalleryBuilder(ProjectRuntimeContext& context, Core::ECS::World& world, const bool preview)
        : m_context(context)
        , m_world(world)
        , m_preview(preview)
    {}


public:
    void object(
        const SmokeMeshRef& mesh,
        const Float4& position,
        const Float4& scale,
        const bool second = false,
        const f32 coverage = 0.0f,
        const f32 yaw = 0.0f,
        const f32 roll = 0.0f,
        const f32 pitch = 0.0f){
        const Float4 firstTint = m_preview ? Float4(0.12f, 0.72f, 1.0f, 0.48f) : Float4(0.90f, 0.98f, 1.0f, coverage);
        const Float4 secondTint = m_preview ? Float4(1.0f, 0.33f, 0.13f, 0.48f) : Float4(1.0f, 0.94f, 0.88f, coverage);
        const Float4 tint = second ? secondTint : firstTint;
        const auto entity = CreateTintedStaticMeshEntity(
            m_world, m_context.objectArena, mesh, m_preview ? s_Preview : s_Glass, s_Interface, tint, position, scale
        );

        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: gallery object creation failed"));
        auto* transform = m_world.tryGetComponent<Impl::Scene::TransformComponent>(entity);
        StoreFloat(QuaternionRotationRollPitchYaw(pitch, yaw, roll), &transform->rotation);
    }

    void sphere(const f32 x, const f32 z, const f32 radius, const bool second = false, const f32 coverage = 0.0f){
        object(s_Sphere, Float4(x, 1.4f, z, 0.0f), Float4(radius, radius, radius, 0.0f), second, coverage);
    }

    void panel(
        const SmokeMaterialRef& material,
        const Float4& tint,
        const f32 x,
        const f32 y,
        const f32 z,
        const f32 halfWidth,
        const f32 halfHeight){
        const auto entity = CreateTintedStaticMeshEntity(
            m_world, m_context.objectArena, s_Plane, material, s_Interface, tint,
            Float4(x, y, z, 0.0f), Float4(halfWidth, 1.0f, halfHeight, 0.0f)
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: gallery backdrop creation failed"));
        auto* transform = m_world.tryGetComponent<Impl::Scene::TransformComponent>(entity);
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), &transform->rotation);
    }

    void backdrop(){
        if(m_preview){
            // Translucent, nonrefractive geometry preview makes nested/overlapping surfaces visible.
            panel(s_Opaque, Float4(0.075f, 0.085f, 0.11f, 1.0f), 0.0f, 1.4f, 4.5f, 6.0f, 5.0f);
            return;
        }
        for(u32 stripe = 0u; stripe < 36u; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.9f : 0.045f;
            panel(
                s_Opaque, Float4(value, value, value, 1.0f),
                -5.25f + static_cast<f32>(stripe) * 0.3f, 1.4f, 4.5f, 0.15f, 5.0f
            );
        }
        // A second direction and colored depth markers make ray displacement legible.
        for(u32 row = 0u; row < 9u; ++row)
            panel(
                s_Opaque, Float4(0.2f, 0.24f, 0.28f, 1.0f),
                0.0f, -1.4f + static_cast<f32>(row) * 0.7f, 4.48f, 5.4f, 0.015f
            );
        panel(s_Transparent, Float4(0.04f, 0.27f, 1.0f, 0.70f), 0.0f, 1.93f, 3.0f, 3.1f, 0.10f);
        panel(s_Transparent, Float4(1.0f, 0.035f, 0.025f, 0.70f), 0.0f, 1.05f, -2.5f, 1.55f, 0.04f);
    }


private:
    ProjectRuntimeContext& m_context;
    Core::ECS::World& m_world;
    bool m_preview;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateRefractionGalleryScene(
    ProjectRuntimeContext& context,
    Core::ECS::World& world,
    const AStringView caseName,
    const bool geometryPreview){
    const auto camera = CreateSmokeCamera(world, 1.4f, 6.0f, 0.0f);
    const auto light = Impl::Scene::CreateDirectionalLightEntity(
        world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f
    );
    if(!camera.valid() || !light.valid())
        return false;
    // Tighter framing exposes overlap artifacts while preserving the same camera for every variant.
    world.tryGetComponent<Impl::Scene::CameraComponent>(camera)->setVerticalFovRadians(s_PI * (40.0f / 180.0f));
    __hidden_refraction_gallery_scene::GalleryBuilder scene(context, world, geometryPreview);
    if(caseName == "single")
        scene.sphere(0.0f, 0.0f, 1.15f);
    else if(caseName == "separate"){
        scene.sphere(-1.15f, 0.0f, 0.85f);
        scene.sphere(1.15f, 0.0f, 0.85f, true);
    }
    else if(caseName == "stacked"){
        scene.sphere(-0.25f, -0.95f, 0.8f);
        scene.sphere(0.25f, 0.95f, 0.8f, true);
    }
    else if(caseName == "intersecting"){
        scene.sphere(-0.45f, -0.3f, 1.0f);
        scene.sphere(0.45f, 0.3f, 1.0f, true);
    }
    else if(caseName == "nested"){
        scene.sphere(0.0f, 0.0f, 1.2f);
        scene.sphere(0.2f, 0.15f, 0.62f, true);
    }
    else if(caseName == "coincident" || caseName == "coincident_tinted"){
        const f32 coverage = caseName == "coincident_tinted" ? 0.3f : 0.0f;
        scene.sphere(0.0f, 0.0f, 1.1f, false, coverage);
        scene.sphere(0.0f, 0.0f, 1.1f, true, coverage);
    }
    else if(caseName == "torus")
        scene.object(
            __hidden_refraction_gallery_scene::s_Torus,
            Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(1.2f, 1.2f, 1.2f, 0.0f), false, 0.0f, 1.2f, 0.23f
        );
    else if(caseName == "same_mesh")
        scene.object(
            __hidden_refraction_gallery_scene::s_Shells,
            Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 0.0f), false, 0.0f, -0.20f
        );
    else if(caseName == "prism")
        scene.object(
            __hidden_refraction_gallery_scene::s_Prism,
            Float4(0.08f, 1.4f, 0.0f, 0.0f), Float4(1.15f, 1.15f, 1.15f, 0.0f), false, 0.0f, 0.95f, 0.1f, 0.2f
        );
    else{
        NWB_LOGGER_WARNING(NWB_TEXT("RefractionSmokeProject: unknown gallery case"));
        return false;
    }
    scene.backdrop();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: gallery case {} created"), StringConvert(caseName));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

