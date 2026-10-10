// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_gallery_scene.h"

#include "smoke_project_helpers.h"
#include "csg_smoke_helpers.h"

#include <global/expected.h>
#include <global/math/constant.h>
#include <global/math/convert.h>
#include <global/math/frame.h>
#include <impl/ecs_csg/shape_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_gallery_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr SmokeMeshRef s_Plane{"project/meshes/shadow_plane"};
constexpr SmokeMeshRef s_Sphere{"project/meshes/caustic_sphere"};
constexpr SmokeMeshRef s_Torus{"project/meshes/refraction_torus"};
constexpr SmokeMeshRef s_Shells{"project/meshes/refraction_two_shells"};
constexpr SmokeMeshRef s_Prism{"project/meshes/refraction_prism"};
constexpr SmokeMaterialRef s_Glass{"project/smoke/refraction/materials/gallery_glass"};
constexpr SmokeMaterialRef s_OpticalVolume{"project/smoke/reflection/materials/optical_volume"};
constexpr SmokeMaterialRef s_Preview{"project/smoke/refraction/materials/gallery_preview"};
constexpr SmokeMaterialRef s_Opaque{"project/smoke/refraction/materials/opaque"};
constexpr SmokeMaterialRef s_Transparent{"project/smoke/refraction/materials/transparent"};
constexpr AStringView s_Interface = "project/shaders/smoke_surface";
constexpr AStringView s_OpticalInterface = "project/shaders/reflection_optical";
constexpr Name s_DuplicateOpticalGroup("project/smoke/refraction/gallery_duplicate_volume");
constexpr f32 s_GalleryCameraDistance = 6.f;
constexpr f32 s_IdentityForeignFront = -0.0005f;
// The near plane lies strictly between the foreign entry and the primary z=0 entry; ray queries still start at the camera.
constexpr f32 s_IdentityNearPlane = s_GalleryCameraDistance - 0.00025f;


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
        const f32 pitch = 0.0f,
        const Name opticalGroup = s_NameNone,
        const i32 opticalPriority = 0,
        const Impl::OpticalVolumeCoincidence::Enum opticalCoincidence = Impl::OpticalVolumeCoincidence::Independent
    ){
        const Float4 firstTint = m_preview ? Float4(0.12f, 0.72f, 1.0f, 0.48f) : Float4(0.90f, 0.98f, 1.0f, coverage);
        const Float4 secondTint = m_preview ? Float4(1.0f, 0.33f, 0.13f, 0.48f) : Float4(1.0f, 0.94f, 0.88f, coverage);
        const Float4 tint = second ? secondTint : firstTint;
        const auto entity = CreateTintedStaticMeshEntity(
            m_world, m_context.objectArena, mesh, m_preview ? s_Preview : s_Glass, s_Interface, tint, position, scale
        );

        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: gallery object creation failed"));
        auto* transform = m_world.tryGetComponent<Impl::Scene::TransformComponent>(entity);
        StoreFloat(QuaternionRotationRollPitchYaw(pitch, yaw, roll), transform->rotation);
        if(!m_preview){
            auto& renderer = m_world.entity(entity).getComponent<Impl::RendererComponent>();
            renderer.opticalVolumeGroup = opticalGroup;
            renderer.opticalVolumePriority = opticalPriority;
            renderer.opticalVolumeCoincidence = opticalCoincidence;
        }
    }

    void sphere(
        const f32 x,
        const f32 z,
        const f32 radius,
        const bool second = false,
        const f32 coverage = 0.0f,
        const Name opticalGroup = s_NameNone,
        const i32 opticalPriority = 0,
        const Impl::OpticalVolumeCoincidence::Enum opticalCoincidence = Impl::OpticalVolumeCoincidence::Independent
    ){
        object(
            s_Sphere, Float4(x, 1.4f, z, 0.0f), Float4(radius, radius, radius, 0.0f), second, coverage,
            0.0f, 0.0f, 0.0f, opticalGroup, opticalPriority, opticalCoincidence
        );
    }

    Expected<void> opticalParameters(const Core::ECS::EntityID entity, const f32 ior, const Float4& transmission){
        if(m_preview)
            return {};
        const Half packedIor = ConvertFloatToHalf(ior);
        const Half4U packedTransmission = MakeHalf4U(transmission.x, transmission.y, transmission.z, 0.f);
        if(!Impl::SetMaterialMutableParameter(
            m_world, entity, Name(s_OpticalInterface), "runtime.ior", Impl::MaterialLayoutFieldType::Half,
            Impl::PackMaterialInstanceBytes(&packedIor, sizeof(packedIor))
        ))
            return MakeUnexpected(Failure{});
        if(!Impl::SetMaterialMutableParameter(
            m_world, entity, Name(s_OpticalInterface), "runtime.unit_transmission", Impl::MaterialLayoutFieldType::Half3,
            Impl::PackMaterialInstanceBytes(packedTransmission.raw, sizeof(Half) * 3u)
        ))
            return MakeUnexpected(Failure{});
        return {};
    }

    Expected<Core::ECS::EntityID> csgSlab(
        const AStringView caseName, const Float4& tint, const SmokeMaterialRef& material, const AStringView surfaceInterface
    ){
        const bool reference = caseName == "csg_reference";
        const bool middle = caseName == "csg_middle";
        const auto entity = CreateTintedStaticMeshEntity(
            m_world, m_context.objectArena, SmokeMeshRef("project/meshes/cube_hard_edges"),
            m_preview ? s_Preview : material, m_preview ? s_Interface : surfaceInterface, tint,
            Float4(0.f, 1.4f, reference || middle ? 0.5f : 0.f, 0.f), Float4(2.6f, 2.6f, reference ? 1.f : (middle ? 3.f : 2.f), 0.f)
        );
        if(!entity.valid())
            return MakeUnexpected(Failure{});
        if(caseName != "csg_cap" && !middle)
            return entity;
        const Name group("smoke/refraction/csg_optics");
        AddStaticCsgMeshReceiver(m_world, entity, group, false, true);
        // The middle slab has no camera-facing receiver triangle: both its entry and exit are generated walls.
        for(u32 index = 0u; index < (middle ? 2u : 1u); ++index){
            auto cutterEntity = m_world.createEntity();
            if(!cutterEntity.id().valid())
                return MakeUnexpected(Failure{});
            auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(m_context.objectArena);
            cutter.receiverGroup = group;
            cutter.shapeType = Name("engine/csg/plane");
            Impl::CsgPlaneShapeParameters parameters;
            parameters.normalDistance = index == 0u ? Float4(0.f, 0.f, 1.f, 0.f) : Float4(0.f, 0.f, -1.f, 1.f);
            AssignCsgCutterParameters(cutter, parameters);
            AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
        }
        return entity;
    }

    bool identitySlab(const AStringView caseName){
        if(caseName != "identity_reference"){
            // Raster clips this distinct entry, while its ray distance stays inside the primary hardware association band.
            constexpr f32 s_ForeignExit = 0.25f;
            const auto foreign = CreateTintedStaticMeshEntity(
                m_world, m_context.objectArena, SmokeMeshRef("project/meshes/cube_hard_edges"),
                m_preview ? s_Preview : s_OpticalVolume, m_preview ? s_Interface : s_OpticalInterface,
                Float4(0.45f, 0.8f, 0.95f, m_preview ? 0.48f : 0.f),
                Float4(0.f, 1.4f, (s_ForeignExit + s_IdentityForeignFront) * 0.5f, 0.f),
                Float4(2.6f, 2.6f, s_ForeignExit - s_IdentityForeignFront, 0.f)
            );
            if(!foreign.valid())
                return false;
            if(!opticalParameters(
                foreign, caseName == "identity_different_ior" ? 3.8f : 1.5f, Float4(0.45f, 0.8f, 0.95f, 0.f)
            ))
                return false;
        }
        // Reusing an actual ECS slot exercises generation bits beyond float32's exact integer range.
        for(u32 generation = 0u; generation < 32u; ++generation){
            const auto recycled = m_world.createEntity();
            if(!recycled.id().valid())
                return false;
            m_world.destroyEntity(recycled.id());
        }
        const auto primary = csgSlab(
            "csg_reference", Float4(0.45f, 0.8f, 0.95f, m_preview ? 0.48f : 0.f), s_OpticalVolume, s_OpticalInterface
        );
        if(!primary || primary->id <= (1u << 24u))
            return false;
        if(!opticalParameters(*primary, 1.5f, Float4(0.45f, 0.8f, 0.95f, 0.f)))
            return false;
        const Name group("smoke/refraction/identity");
        AddStaticCsgMeshReceiver(m_world, *primary, group, false, true);
        auto cutterEntity = m_world.createEntity();
        if(!cutterEntity.id().valid())
            return false;
        auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = group;
        cutter.shapeType = Name("engine/csg/plane");
        Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(-1.f, 0.f, 0.f, 10.f);
        AssignCsgCutterParameters(cutter, parameters);
        AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: full primary entity identity {}"), primary->id);
        return true;
    }

    bool crossingOverflow(){
        const auto entity = CreateTintedStaticMeshEntity(
            m_world, m_context.objectArena, SmokeMeshRef("project/meshes/refraction_crossing_overflow"),
            m_preview ? s_Preview : s_Glass, s_Interface, Float4(0.45f, 0.8f, 0.95f, m_preview ? 0.48f : 0.f),
            Float4(0.f, 1.4f, 0.f, 0.f), Float4(0.1f, 0.1f, 1.f, 0.f)
        );
        if(!entity.valid())
            return false;
        const Name group("smoke/refraction/csg_crossing_overflow");
        AddStaticCsgMeshReceiver(m_world, entity, group, false, true);
        auto cutterEntity = m_world.createEntity();
        if(!cutterEntity.id().valid())
            return false;
        auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = group;
        cutter.shapeType = Name("engine/csg/plane");
        Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(-1.f, 0.f, 0.f, 10.f);
        AssignCsgCutterParameters(cutter, parameters);
        AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
        return true;
    }

    bool nearAirSlab(const bool csg){
        const auto entity = CreateTintedStaticMeshEntity(
            m_world, m_context.objectArena, SmokeMeshRef("project/meshes/cube_hard_edges"),
            m_preview ? s_Preview : s_OpticalVolume, m_preview ? s_Interface : s_OpticalInterface,
            Float4(1.f, 1.f, 1.f, m_preview ? 0.48f : 0.f),
            Float4(0.f, 1.4f, 1.25f, 0.f), Float4(2.6f, 2.6f, 3.f, 0.f)
        );
        if(!entity.valid())
            return false;
        // The first half IOR above air still has a three-unit Beer path, even with negligible bending.
        if(!opticalParameters(entity, 1.f + 0x1p-10f, Float4(0.55f, 0.8f, 1.f, 0.f)))
            return false;
        if(!csg)
            return true;
        const Name group("smoke/refraction/near_air");
        AddStaticCsgMeshReceiver(m_world, entity, group, false, true);
        auto cutterEntity = m_world.createEntity();
        if(!cutterEntity.id().valid())
            return false;
        auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = group;
        cutter.shapeType = Name("engine/csg/plane");
        Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(-1.f, 0.f, 0.f, 10.f);
        AssignCsgCutterParameters(cutter, parameters);
        AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
        return true;
    }

    void panel(
        const SmokeMaterialRef& material,
        const Float4& tint,
        const f32 x,
        const f32 y,
        const f32 z,
        const f32 halfWidth,
        const f32 halfHeight
    ){
        const auto entity = CreateTintedStaticMeshEntity(
            m_world, m_context.objectArena, s_Plane, material, s_Interface, tint,
            Float4(x, y, z, 0.0f), Float4(halfWidth, 1.0f, halfHeight, 0.0f)
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: gallery backdrop creation failed"));
        auto* transform = m_world.tryGetComponent<Impl::Scene::TransformComponent>(entity);
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform->rotation);
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
    const bool geometryPreview
){
    const auto camera = CreateSmokeCamera(world, 1.4f, __hidden_refraction_gallery_scene::s_GalleryCameraDistance, 0.0f);
    const auto light = Impl::Scene::CreateDirectionalLightEntity(
        world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f
    );
    if(!camera.valid() || !light.valid())
        return false;
    // Tighter framing exposes overlap artifacts while preserving the same camera for every variant.
    auto& cameraComponent = world.entity(camera).getComponent<Impl::Scene::CameraComponent>();
    cameraComponent.setVerticalFovRadians(s_PI * (40.0f / 180.0f));
    if(caseName == "identity_reference" || caseName == "identity_same_ior" || caseName == "identity_different_ior")
        cameraComponent.setNearPlane(__hidden_refraction_gallery_scene::s_IdentityNearPlane);
    __hidden_refraction_gallery_scene::GalleryBuilder scene(context, world, geometryPreview);
    if(caseName == "csg_reference" || caseName == "csg_cap" || caseName == "csg_middle" || caseName == "csg_uncut"){
        const auto slab = scene.csgSlab(
            caseName, Float4(0.9f, 0.98f, 1.f, geometryPreview ? 0.48f : 0.f),
            __hidden_refraction_gallery_scene::s_Glass, __hidden_refraction_gallery_scene::s_Interface
        );
        if(!slab)
            return false;
    }
    else if(caseName == "identity_reference" || caseName == "identity_same_ior" || caseName == "identity_different_ior"){
        if(!scene.identitySlab(caseName))
            return false;
    }
    else if(caseName == "crossing_overflow"){
        if(!scene.crossingOverflow())
            return false;
    }
    else if(caseName == "near_air_ordinary" || caseName == "near_air_csg"){
        if(!scene.nearAirSlab(caseName == "near_air_csg"))
            return false;
    }
    else if(caseName == "single")
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
    else if(
        caseName == "duplicate_single_cool" || caseName == "duplicate_single_warm"
        || caseName == "duplicate_single_cool_tinted" || caseName == "duplicate_single_warm_tinted"
    ){
        const bool warm = caseName == "duplicate_single_warm" || caseName == "duplicate_single_warm_tinted";
        const bool tinted = caseName == "duplicate_single_cool_tinted" || caseName == "duplicate_single_warm_tinted";
        const f32 coverage = tinted ? 0.3f : 0.0f;
        scene.sphere(0.0f, 0.0f, 1.1f, warm, coverage);
    }
    else if(
        caseName == "coincident" || caseName == "coincident_tinted"
        || caseName == "coincident_reversed" || caseName == "coincident_tinted_reversed"
        || caseName == "coincident_priority_swap" || caseName == "coincident_tinted_priority_swap"
    ){
        const bool tinted = caseName == "coincident_tinted" || caseName == "coincident_tinted_reversed"
            || caseName == "coincident_tinted_priority_swap";
        const bool reversed = caseName == "coincident_reversed" || caseName == "coincident_tinted_reversed";
        const bool warmWins = caseName == "coincident_priority_swap" || caseName == "coincident_tinted_priority_swap";
        const f32 coverage = tinted ? 0.3f : 0.0f;
        const i32 coolPriority = warmWins ? 0 : 10;
        const i32 warmPriority = warmWins ? 10 : 0;
        const Name opticalGroup = __hidden_refraction_gallery_scene::s_DuplicateOpticalGroup;
        scene.sphere(
            0.0f, 0.0f, 1.1f, reversed, coverage, opticalGroup,
            reversed ? warmPriority : coolPriority, Impl::OpticalVolumeCoincidence::SharedGroup
        );
        scene.sphere(
            0.0f, 0.0f, 1.1f, !reversed, coverage, opticalGroup,
            reversed ? coolPriority : warmPriority, Impl::OpticalVolumeCoincidence::SharedGroup
        );
    }
    else if(caseName == "coincident_identical" || caseName == "coincident_tinted_identical"){
        const f32 coverage = caseName == "coincident_tinted_identical" ? 0.3f : 0.0f;
        scene.sphere(0.0f, 0.0f, 1.1f, false, coverage, s_NameNone, 0, Impl::OpticalVolumeCoincidence::IdenticalMaterial);
        scene.sphere(0.0f, 0.0f, 1.1f, false, coverage, s_NameNone, 0, Impl::OpticalVolumeCoincidence::IdenticalMaterial);
    }
    else if(caseName == "near_coincident"){
        const Name opticalGroup = __hidden_refraction_gallery_scene::s_DuplicateOpticalGroup;
        scene.sphere(0.0f, 0.0f, 1.1f, false, 0.3f, opticalGroup, 10, Impl::OpticalVolumeCoincidence::SharedGroup);
        scene.sphere(0.04f, 0.0f, 1.1f, true, 0.3f, opticalGroup, 0, Impl::OpticalVolumeCoincidence::SharedGroup);
    }
    else if(caseName == "coincident_preserved"){
        scene.sphere(0.0f, 0.0f, 1.1f, false, 0.3f);
        scene.sphere(0.0f, 0.0f, 1.1f, false, 0.3f);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

