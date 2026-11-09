// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Optional gallery cases use the same renderer and readback path as the original regression fixture.
[[nodiscard]] b8 CreateRefractionGalleryScene(AStringView caseName, b8 geometryPreview, Core::ECS::World& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_project_helpers.h"
#include "csg_smoke_helpers.h"

#include <tests/smoke/presentation_types.adhd.h>
#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_csg/shape_registry.h>

#include <global/expected.h>
#include <global/simd/constant.h>
#include <global/simd/convert.h>
#include <global/simd/frame.h>


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
constexpr SmokeMeshRef s_Cube{"project/meshes/cube_hard_edges"};
constexpr SmokeMaterialRef s_Glass{"project/smoke/refraction/materials/gallery_glass"};
constexpr SmokeMaterialRef s_OpticalVolume{"project/smoke/reflection/materials/optical_volume"};
constexpr SmokeMaterialRef s_Preview{"project/smoke/refraction/materials/gallery_preview"};
constexpr SmokeMaterialRef s_Opaque{"project/smoke/refraction/materials/opaque"};
constexpr SmokeMaterialRef s_Transparent{"project/smoke/refraction/materials/transparent"};
constexpr AStringView s_Interface = ::NWB::Tests::Smoke::s_RefractionGalleryInterface;
constexpr AStringView s_OpticalInterface = ::NWB::Tests::Smoke::s_RefractionGalleryOpticalInterface;
constexpr Name s_DuplicateOpticalGroup = NWB::Tests::Smoke::s_SmokeNameProjectSmokeRefractionGalleryDuplicateVolume;
constexpr f32 s_GalleryCameraDistance = ::NWB::Tests::Smoke::s_RefractionGalleryCameraDistance;
constexpr f32 s_IdentityForeignFront = ::NWB::Tests::Smoke::s_RefractionGalleryIdentityForeignFront;
// Place the near plane between the foreign entry and the primary z=0 entry.
constexpr f32 s_IdentityNearPlane = ::NWB::Tests::Smoke::s_RefractionGalleryIdentityNearPlane;
// Camera, objects and backdrop share this height.
constexpr f32 s_GalleryCenterHeight = ::NWB::Tests::Smoke::s_RefractionGalleryCenterHeight;
constexpr f32 s_SlabFaceScale = ::NWB::Tests::Smoke::s_RefractionGallerySlabFaceScale;
constexpr f32 s_PreviewAlpha = ::NWB::Tests::Smoke::s_RefractionGalleryPreviewAlpha;
constexpr f32 s_OpticalIor = ::NWB::Tests::Smoke::s_RefractionGalleryOpticalIor;
// The slab tint and its unit transmission use the same color.
constexpr Float4 s_OpticalTransmission(0.45f, 0.8f, 0.95f, 0.f);
constexpr f32 s_TintedCoverage = ::NWB::Tests::Smoke::s_RefractionGalleryTintedCoverage;
constexpr f32 s_DuplicateSphereRadius = ::NWB::Tests::Smoke::s_RefractionGalleryDuplicateSphereRadius;
constexpr i32 s_WinningOpticalPriority = ::NWB::Tests::Smoke::s_RefractionGalleryWinningOpticalPriority;
constexpr f32 s_BackdropDepth = ::NWB::Tests::Smoke::s_RefractionGalleryBackdropDepth;
constexpr f32 s_BackdropHalfHeight = ::NWB::Tests::Smoke::s_RefractionGalleryBackdropHalfHeight;
constexpr f32 s_DepthMarkerAlpha = ::NWB::Tests::Smoke::s_RefractionGalleryDepthMarkerAlpha;

constexpr AStringView s_CaseCsgReference = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCsgReference;
constexpr AStringView s_CaseCsgCap = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCsgCap;
constexpr AStringView s_CaseCsgMiddle = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCsgMiddle;
constexpr AStringView s_CaseCsgUncut = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCsgUncut;
constexpr AStringView s_CaseIdentityReference = ::NWB::Tests::Smoke::s_RefractionGalleryCaseIdentityReference;
constexpr AStringView s_CaseIdentitySameIor = ::NWB::Tests::Smoke::s_RefractionGalleryCaseIdentitySameIor;
constexpr AStringView s_CaseIdentityDifferentIor = ::NWB::Tests::Smoke::s_RefractionGalleryCaseIdentityDifferentIor;
constexpr AStringView s_CaseCrossingOverflow = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCrossingOverflow;
constexpr AStringView s_CaseNearAirOrdinary = ::NWB::Tests::Smoke::s_RefractionGalleryCaseNearAirOrdinary;
constexpr AStringView s_CaseNearAirCsg = ::NWB::Tests::Smoke::s_RefractionGalleryCaseNearAirCsg;
constexpr AStringView s_CaseSingle = ::NWB::Tests::Smoke::s_RefractionGalleryCaseSingle;
constexpr AStringView s_CaseSeparate = ::NWB::Tests::Smoke::s_RefractionGalleryCaseSeparate;
constexpr AStringView s_CaseStacked = ::NWB::Tests::Smoke::s_RefractionGalleryCaseStacked;
constexpr AStringView s_CaseIntersecting = ::NWB::Tests::Smoke::s_RefractionGalleryCaseIntersecting;
constexpr AStringView s_CaseNested = ::NWB::Tests::Smoke::s_RefractionGalleryCaseNested;
constexpr AStringView s_CaseDuplicateSingleCool = ::NWB::Tests::Smoke::s_RefractionGalleryCaseDuplicateSingleCool;
constexpr AStringView s_CaseDuplicateSingleWarm = ::NWB::Tests::Smoke::s_RefractionGalleryCaseDuplicateSingleWarm;
constexpr AStringView s_CaseDuplicateSingleCoolTinted = ::NWB::Tests::Smoke::s_RefractionGalleryCaseDuplicateSingleCoolTinted;
constexpr AStringView s_CaseDuplicateSingleWarmTinted = ::NWB::Tests::Smoke::s_RefractionGalleryCaseDuplicateSingleWarmTinted;
constexpr AStringView s_CaseCoincident = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincident;
constexpr AStringView s_CaseCoincidentTinted = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentTinted;
constexpr AStringView s_CaseCoincidentReversed = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentReversed;
constexpr AStringView s_CaseCoincidentTintedReversed = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentTintedReversed;
constexpr AStringView s_CaseCoincidentPrioritySwap = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentPrioritySwap;
constexpr AStringView s_CaseCoincidentTintedPrioritySwap = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentTintedPrioritySwap;
constexpr AStringView s_CaseCoincidentIdentical = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentIdentical;
constexpr AStringView s_CaseCoincidentTintedIdentical = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentTintedIdentical;
constexpr AStringView s_CaseNearCoincident = ::NWB::Tests::Smoke::s_RefractionGalleryCaseNearCoincident;
constexpr AStringView s_CaseCoincidentPreserved = ::NWB::Tests::Smoke::s_RefractionGalleryCaseCoincidentPreserved;
constexpr AStringView s_CaseTorus = ::NWB::Tests::Smoke::s_RefractionGalleryCaseTorus;
constexpr AStringView s_CaseSameMesh = ::NWB::Tests::Smoke::s_RefractionGalleryCaseSameMesh;
constexpr AStringView s_CasePrism = ::NWB::Tests::Smoke::s_RefractionGalleryCasePrism;


class GalleryBuilder final{
public:
    GalleryBuilder(const b8 preview, Core::ECS::World& world)noexcept
        : m_world(world)
        , m_preview(preview)
    {}


public:
    void object(
        const SmokeMeshRef& mesh,
        const Float4& position,
        const Float4& scale,
        const b8 second = false,
        const f32 coverage = 0.0f,
        const f32 yaw = 0.0f,
        const f32 roll = 0.0f,
        const f32 pitch = 0.0f,
        const Name opticalGroup = s_NameNone,
        const i32 opticalPriority = 0,
        const Impl::OpticalVolumeCoincidence::Enum opticalCoincidence = Impl::OpticalVolumeCoincidence::Independent
    ){
        const Float4 firstTint = m_preview ? Float4(0.12f, 0.72f, 1.0f, s_PreviewAlpha) : Float4(0.90f, 0.98f, 1.0f, coverage);
        const Float4 secondTint = m_preview ? Float4(1.0f, 0.33f, 0.13f, s_PreviewAlpha) : Float4(1.0f, 0.94f, 0.88f, coverage);
        const Float4 tint = second ? secondTint : firstTint;
        const auto entity = CreateTintedStaticMeshEntity(mesh, m_preview ? s_Preview : s_Glass, s_Interface, tint, position, scale, m_world);

        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: gallery object creation failed"));
        auto& transform = *MakeNotNull(m_world.tryGetComponent<Impl::Scene::TransformComponent>(entity));
        StoreFloat(QuaternionRotationRollPitchYaw(pitch, yaw, roll), transform.rotation);
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
        const b8 second = false,
        const f32 coverage = 0.0f,
        const Name opticalGroup = s_NameNone,
        const i32 opticalPriority = 0,
        const Impl::OpticalVolumeCoincidence::Enum opticalCoincidence = Impl::OpticalVolumeCoincidence::Independent
    ){
        object(s_Sphere, Float4(x, s_GalleryCenterHeight, z, 0.0f), Float4(radius, radius, radius, 0.0f), second, coverage, 0.0f, 0.0f, 0.0f, opticalGroup, opticalPriority, opticalCoincidence);
    }

    Expected<void> opticalParameters(const Core::ECS::EntityID entity, const f32 ior, const Float4& transmission){
        if(m_preview)
            return {};
        const Half packedIor = ConvertFloatToHalf(ior);
        const Half4U packedTransmission = MakeHalf4U(transmission.x, transmission.y, transmission.z, 0.f);
        if(!Impl::SetMaterialMutableParameter(m_world, entity, Name(s_OpticalInterface), "runtime.ior", Impl::MaterialLayoutFieldType::f16, Impl::PackMaterialInstanceValue(packedIor)))
            return MakeUnexpected(Failure{});
        if(!Impl::SetMaterialMutableParameter(m_world, entity, Name(s_OpticalInterface), "runtime.unit_transmission", Impl::MaterialLayoutFieldType::f16x3, Impl::PackMaterialInstanceBytes(Span<const Half>(packedTransmission.raw, 3u))))
            return MakeUnexpected(Failure{});
        return {};
    }

    Expected<Core::ECS::EntityID> csgSlab(const AStringView caseName, const Float4& tint, const SmokeMaterialRef& material, const NameText<char> surfaceInterface){
        const b8 reference = caseName == s_CaseCsgReference;
        const b8 middle = caseName == s_CaseCsgMiddle;
        const auto entity = CreateTintedStaticMeshEntity(
            s_Cube,
            m_preview ? s_Preview : material, m_preview ? NameText<char>{s_Interface} : surfaceInterface, tint,
            Float4(0.f, s_GalleryCenterHeight, reference || middle ? 0.5f : 0.f, 0.f), Float4(s_SlabFaceScale, s_SlabFaceScale, reference ? 1.f : (middle ? 3.f : 2.f), 0.f),
            m_world
        );
        if(!entity.valid())
            return MakeUnexpected(Failure{});
        if(caseName != s_CaseCsgCap && !middle)
            return entity;
        const Name group = NWB::Tests::Smoke::s_SmokeNameSmokeRefractionCsgOptics;
        AddStaticCsgMeshReceiver(entity, group, false, true, m_world);
        // The middle slab has no camera-facing receiver triangle: both its entry and exit are generated walls.
        for(u32 index = 0u; index < (middle ? 2u : 1u); ++index){
            auto cutterEntity = m_world.createEntity();
            if(!cutterEntity.id().valid())
                return MakeUnexpected(Failure{});
            auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>();
            cutter.receiverGroup = group;
            cutter.shapeType = Impl::s_CsgPlaneShapeName;
            Impl::CsgPlaneShapeParameters parameters;
            parameters.normalDistance = index == 0u ? Float4(0.f, 0.f, 1.f, 0.f) : Float4(0.f, 0.f, -1.f, 1.f);
            AssignCsgCutterParameters(parameters, cutter);
            AssignCsgCutterTransform(VectorZero(), QuaternionIdentity(), cutter);
        }
        return entity;
    }

    b8 identitySlab(const AStringView caseName){
        if(caseName != s_CaseIdentityReference){
            // Raster clips this distinct entry, while its ray distance stays inside the primary hardware association band.
            constexpr f32 s_ForeignExit = ::NWB::Tests::Smoke::s_RefractionGalleryForeignExit;
            const auto foreign = CreateTintedStaticMeshEntity(
                s_Cube,
                m_preview ? s_Preview : s_OpticalVolume, m_preview ? NameText<char>{s_Interface} : NameText<char>{s_OpticalInterface},
                Float4(s_OpticalTransmission.x, s_OpticalTransmission.y, s_OpticalTransmission.z, m_preview ? s_PreviewAlpha : 0.f),
                Float4(0.f, s_GalleryCenterHeight, (s_ForeignExit + s_IdentityForeignFront) * 0.5f, 0.f),
                Float4(s_SlabFaceScale, s_SlabFaceScale, s_ForeignExit - s_IdentityForeignFront, 0.f),
                m_world
            );
            if(!foreign.valid())
                return false;
            if(!opticalParameters(foreign, caseName == s_CaseIdentityDifferentIor ? 3.8f : s_OpticalIor, s_OpticalTransmission))
                return false;
        }
        // Reusing an actual ECS slot exercises generation bits beyond float32's exact integer range.
        for(u32 generation = 0u; generation < 32u; ++generation){
            const auto recycled = m_world.createEntity();
            if(!recycled.id().valid())
                return false;
            m_world.destroyEntity(recycled.id());
        }
        const auto primary = csgSlab(s_CaseCsgReference, Float4(s_OpticalTransmission.x, s_OpticalTransmission.y, s_OpticalTransmission.z, m_preview ? s_PreviewAlpha : 0.f), s_OpticalVolume, s_OpticalInterface);
        if(!primary || primary->id <= (1u << 24u))
            return false;
        if(!opticalParameters(*primary, s_OpticalIor, s_OpticalTransmission))
            return false;
        const Name group = NWB::Tests::Smoke::s_SmokeNameSmokeRefractionIdentity;
        if(!farPlaneReceiver(*primary, group))
            return false;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: full primary entity identity {}"), primary->id);
        return true;
    }

    [[nodiscard]] b8 farPlaneReceiver(const Core::ECS::EntityID entity, const Name group){
        AddStaticCsgMeshReceiver(entity, group, false, true, m_world);
        auto cutterEntity = m_world.createEntity();
        if(!cutterEntity.id().valid())
            return false;
        auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>();
        cutter.receiverGroup = group;
        cutter.shapeType = Impl::s_CsgPlaneShapeName;
        Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(-1.f, 0.f, 0.f, 10.f);
        AssignCsgCutterParameters(parameters, cutter);
        AssignCsgCutterTransform(VectorZero(), QuaternionIdentity(), cutter);
        return true;
    }

    b8 crossingOverflow(){
        const auto entity = CreateTintedStaticMeshEntity(
            SmokeMeshRef("project/meshes/refraction_crossing_overflow"),
            m_preview ? s_Preview : s_Glass, s_Interface, Float4(s_OpticalTransmission.x, s_OpticalTransmission.y, s_OpticalTransmission.z, m_preview ? s_PreviewAlpha : 0.f),
            Float4(0.f, s_GalleryCenterHeight, 0.f, 0.f), Float4(0.1f, 0.1f, 1.f, 0.f),
            m_world
        );
        if(!entity.valid())
            return false;
        const Name group = NWB::Tests::Smoke::s_SmokeNameSmokeRefractionCsgCrossingOverflow;
        if(!farPlaneReceiver(entity, group))
            return false;
        return true;
    }

    b8 nearAirSlab(const b8 csg){
        const auto entity = CreateTintedStaticMeshEntity(
            s_Cube,
            m_preview ? s_Preview : s_OpticalVolume, m_preview ? NameText<char>{s_Interface} : NameText<char>{s_OpticalInterface},
            Float4(1.f, 1.f, 1.f, m_preview ? s_PreviewAlpha : 0.f),
            Float4(0.f, s_GalleryCenterHeight, 1.25f, 0.f), Float4(s_SlabFaceScale, s_SlabFaceScale, 3.f, 0.f),
            m_world
        );
        if(!entity.valid())
            return false;
        // The first half IOR above air still has a three-unit Beer path, even with negligible bending.
        if(!opticalParameters(entity, 1.f + 0x1p-10f, Float4(0.55f, 0.8f, 1.f, 0.f)))
            return false;
        if(!csg)
            return true;
        const Name group = NWB::Tests::Smoke::s_SmokeNameSmokeRefractionNearAir;
        if(!farPlaneReceiver(entity, group))
            return false;
        return true;
    }

    void panel(const SmokeMaterialRef& material, const Float4& tint, const f32 x, const f32 y, const f32 z, const f32 halfWidth, const f32 halfHeight){
        const auto entity = CreateTintedStaticMeshEntity(s_Plane, material, s_Interface, tint, Float4(x, y, z, 0.0f), Float4(halfWidth, 1.0f, halfHeight, 0.0f), m_world);
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: gallery backdrop creation failed"));
        auto& transform = *MakeNotNull(m_world.tryGetComponent<Impl::Scene::TransformComponent>(entity));
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform.rotation);
    }

    void backdrop(){
        if(m_preview){
            // Translucent, nonrefractive geometry preview makes nested/overlapping surfaces visible.
            panel(s_Opaque, Float4(0.075f, 0.085f, 0.11f, 1.0f), 0.0f, s_GalleryCenterHeight, s_BackdropDepth, 6.0f, s_BackdropHalfHeight);
            return;
        }
        for(u32 stripe = 0u; stripe < 36u; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.9f : 0.045f;
            panel(s_Opaque, Float4(value, value, value, 1.0f), static_cast<f32>(stripe) * 0.3f + -5.25f, s_GalleryCenterHeight, s_BackdropDepth, 0.15f, s_BackdropHalfHeight);
        }
        // A second direction and colored depth markers make ray displacement legible.
        for(u32 row = 0u; row < 9u; ++row)
            panel(s_Opaque, Float4(0.2f, 0.24f, 0.28f, 1.0f), 0.0f, static_cast<f32>(row) * 0.7f + -1.4f, 4.48f, 5.4f, 0.015f);
        panel(s_Transparent, Float4(0.04f, 0.27f, 1.0f, s_DepthMarkerAlpha), 0.0f, 1.93f, 3.0f, 3.1f, 0.10f);
        panel(s_Transparent, Float4(1.0f, 0.035f, 0.025f, s_DepthMarkerAlpha), 0.0f, 1.05f, -2.5f, 1.55f, 0.04f);
    }


private:
    Core::ECS::World& m_world;
    b8 m_preview;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CreateRefractionGalleryScene(const AStringView caseName, const b8 geometryPreview, Core::ECS::World& world){
    using namespace __hidden_refraction_gallery_scene;

    const auto camera = CreateSmokeCamera(s_GalleryCenterHeight, s_GalleryCameraDistance, 0.0f, world);
    const auto light = Impl::Scene::CreateDirectionalLightEntity(world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
    if(!camera.valid() || !light.valid())
        return false;
    world.entity(light).getComponent<Impl::Scene::LightComponent>().enableCaustics = false;
    // Tighter framing exposes overlap artifacts while preserving the same camera for every variant.
    auto& cameraComponent = world.entity(camera).getComponent<Impl::Scene::CameraComponent>();
    cameraComponent.setVerticalFovRadians(s_PI * (40.0f / 180.0f));
    if(caseName == s_CaseIdentityReference || caseName == s_CaseIdentitySameIor || caseName == s_CaseIdentityDifferentIor)
        cameraComponent.setNearPlane(s_IdentityNearPlane);
    GalleryBuilder scene(geometryPreview, world);
    if(caseName == s_CaseCsgReference || caseName == s_CaseCsgCap || caseName == s_CaseCsgMiddle || caseName == s_CaseCsgUncut){
        const auto slab = scene.csgSlab(caseName, Float4(0.9f, 0.98f, 1.f, geometryPreview ? 0.48f : 0.f), s_Glass, s_Interface);
        if(!slab)
            return false;
    }
    else if(caseName == s_CaseIdentityReference || caseName == s_CaseIdentitySameIor || caseName == s_CaseIdentityDifferentIor){
        if(!scene.identitySlab(caseName))
            return false;
    }
    else if(caseName == s_CaseCrossingOverflow){
        if(!scene.crossingOverflow())
            return false;
    }
    else if(caseName == s_CaseNearAirOrdinary || caseName == s_CaseNearAirCsg){
        if(!scene.nearAirSlab(caseName == s_CaseNearAirCsg))
            return false;
    }
    else if(caseName == s_CaseSingle)
        scene.sphere(0.0f, 0.0f, 1.15f);
    else if(caseName == s_CaseSeparate){
        scene.sphere(-1.15f, 0.0f, 0.85f);
        scene.sphere(1.15f, 0.0f, 0.85f, true);
    }
    else if(caseName == s_CaseStacked){
        scene.sphere(-0.25f, -0.95f, 0.8f);
        scene.sphere(0.25f, 0.95f, 0.8f, true);
    }
    else if(caseName == s_CaseIntersecting){
        scene.sphere(-0.45f, -0.3f, 1.0f);
        scene.sphere(0.45f, 0.3f, 1.0f, true);
    }
    else if(caseName == s_CaseNested){
        scene.sphere(0.0f, 0.0f, 1.2f);
        scene.sphere(0.2f, 0.15f, 0.62f, true);
    }
    else if(caseName == s_CaseDuplicateSingleCool || caseName == s_CaseDuplicateSingleWarm || caseName == s_CaseDuplicateSingleCoolTinted || caseName == s_CaseDuplicateSingleWarmTinted){
        const b8 warm = caseName == s_CaseDuplicateSingleWarm || caseName == s_CaseDuplicateSingleWarmTinted;
        const b8 tinted = caseName == s_CaseDuplicateSingleCoolTinted || caseName == s_CaseDuplicateSingleWarmTinted;
        const f32 coverage = tinted ? s_TintedCoverage : 0.0f;
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, warm, coverage);
    }
    else if(caseName == s_CaseCoincident || caseName == s_CaseCoincidentTinted || caseName == s_CaseCoincidentReversed || caseName == s_CaseCoincidentTintedReversed || caseName == s_CaseCoincidentPrioritySwap || caseName == s_CaseCoincidentTintedPrioritySwap){
        const b8 tinted = caseName == s_CaseCoincidentTinted || caseName == s_CaseCoincidentTintedReversed || caseName == s_CaseCoincidentTintedPrioritySwap;
        const b8 reversed = caseName == s_CaseCoincidentReversed || caseName == s_CaseCoincidentTintedReversed;
        const b8 warmWins = caseName == s_CaseCoincidentPrioritySwap || caseName == s_CaseCoincidentTintedPrioritySwap;
        const f32 coverage = tinted ? s_TintedCoverage : 0.0f;
        const i32 coolPriority = warmWins ? 0 : s_WinningOpticalPriority;
        const i32 warmPriority = warmWins ? s_WinningOpticalPriority : 0;
        const Name opticalGroup = s_DuplicateOpticalGroup;
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, reversed, coverage, opticalGroup, reversed ? warmPriority : coolPriority, Impl::OpticalVolumeCoincidence::SharedGroup);
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, !reversed, coverage, opticalGroup, reversed ? coolPriority : warmPriority, Impl::OpticalVolumeCoincidence::SharedGroup);
    }
    else if(caseName == s_CaseCoincidentIdentical || caseName == s_CaseCoincidentTintedIdentical){
        const f32 coverage = caseName == s_CaseCoincidentTintedIdentical ? s_TintedCoverage : 0.0f;
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, false, coverage, s_NameNone, 0, Impl::OpticalVolumeCoincidence::IdenticalMaterial);
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, false, coverage, s_NameNone, 0, Impl::OpticalVolumeCoincidence::IdenticalMaterial);
    }
    else if(caseName == s_CaseNearCoincident){
        const Name opticalGroup = s_DuplicateOpticalGroup;
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, false, s_TintedCoverage, opticalGroup, s_WinningOpticalPriority, Impl::OpticalVolumeCoincidence::SharedGroup);
        scene.sphere(0.04f, 0.0f, s_DuplicateSphereRadius, true, s_TintedCoverage, opticalGroup, 0, Impl::OpticalVolumeCoincidence::SharedGroup);
    }
    else if(caseName == s_CaseCoincidentPreserved){
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, false, s_TintedCoverage);
        scene.sphere(0.0f, 0.0f, s_DuplicateSphereRadius, false, s_TintedCoverage);
    }
    else if(caseName == s_CaseTorus)
        scene.object(s_Torus, Float4(0.0f, s_GalleryCenterHeight, 0.0f, 0.0f), Float4(1.2f, 1.2f, 1.2f, 0.0f), false, 0.0f, 1.2f, 0.23f);
    else if(caseName == s_CaseSameMesh)
        scene.object(s_Shells, Float4(0.0f, s_GalleryCenterHeight, 0.0f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 0.0f), false, 0.0f, -0.20f);
    else if(caseName == s_CasePrism)
        scene.object(s_Prism, Float4(0.08f, s_GalleryCenterHeight, 0.0f, 0.0f), Float4(1.15f, 1.15f, 1.15f, 0.0f), false, 0.0f, 0.95f, 0.1f, 0.2f);
    else{
        NWB_LOGGER_WARNING(NWB_TEXT("RefractionSmokeProject: unknown gallery case"));
        return false;
    }
    scene.backdrop();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: gallery case {} created"), ConvertString(caseName));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/world.h>
#include <loader/project_entry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RefractionQueryDiagnostics final{
public:
    RefractionQueryDiagnostics(ProjectRuntimeContext& context, Core::ECS::World& world)noexcept
        : m_context(context)
        , m_world(world)
    {}


public:
    [[nodiscard]] b8 configure(Core::Alloc::ScratchArena& arena, b8 densityEnabled, b8 lifecycleEnabled, b8 timingEnabled);
    void update();
    [[nodiscard]] b8 enabled()const noexcept{ return m_enabled; }
    [[nodiscard]] b8 shouldCapture(u64 graphicsFrame)const noexcept{ return m_reported && graphicsFrame > m_sourceFrame; }


private:
    ProjectRuntimeContext& m_context;
    Core::ECS::World& m_world;
    Optional<u64> m_targetStart;
    u64 m_sourceFrame = 0u;
    b8 m_enabled = false;
    b8 m_reported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_environment.h"

#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_render/module.h>
#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionQueryDiagnostics::configure(Core::Alloc::ScratchArena& arena, const b8 densityEnabled, const b8 lifecycleEnabled, const b8 timingEnabled){
    const auto configured = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_DIAGNOSTICS");
    if(!configured || (*configured && **configured != "0" && **configured != "1")){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: diagnostics must be absent, 0, or 1"));
        return false;
    }
    m_enabled = *configured && **configured == "1";
    if(!m_enabled)
        return true;
    const auto freeze = ReadEnvironmentVariable(arena, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
    const auto readback = ReadSmokeEnvironmentText(arena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
    const auto hardware = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_HARDWARE");
    if(!freeze || !hardware || *freeze || !readback || !densityEnabled || lifecycleEnabled || timingEnabled || (*hardware && **hardware != "1")){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: query diagnostics require a hardware density framebuffer capture with timing, lifecycle, and frame freeze disabled"));
        return false;
    }
    auto& renderer = *MakeNotNull(m_world.getSystem<NWB::Impl::RendererSystem>());
    renderer.setRefractionDiagnosticsEnabled(true);
    return true;
}


void RefractionQueryDiagnostics::update(){
    if(!m_enabled || m_reported)
        return;
    i32 width = 0;
    i32 height = 0;
    m_context.graphics.getWindowDimensions(width, height);
    const auto target = NWB::QueryProjectFrameClientSize();
    const u64 currentFrame = m_context.graphics.getFrameIndex();
    if(width != target.width || height != target.height || (m_targetStart && currentFrame < *m_targetStart)){
        m_targetStart.reset();
        return;
    }
    if(!m_targetStart){
        m_targetStart = currentFrame;
        return;
    }
    constexpr u64 s_RequiredTargetFrames = s_RefractionDiagnosticsRequiredTargetFrames;
    if(currentFrame - *m_targetStart < s_RequiredTargetFrames)
        return;
    const auto& renderer = *MakeNotNull(m_world.getSystem<NWB::Impl::RendererSystem>());
    const auto result = renderer.tryGetLatestRefractionStatistics();
    if(!result)
        return;
    const auto& statistics = *result;
    if(
        statistics.width != target.width
        || statistics.height != target.height
        || statistics.graphicsFrameIndex < *m_targetStart
        || statistics.graphicsFrameIndex - *m_targetStart < s_RequiredTargetFrames
        || statistics.graphicsFrameIndex > currentFrame
    )
        return;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryDiagnostics: sequence={} generation={} graphics_frame={} observed_frame={} target_start_frame={} completed_target_frames={} frame_index={} width={} height={}")
        NWB_TEXT(" hardware_requested={} hardware_available={} hardware_ready={} csg_enabled={} token_valid={} physical_valid={} token_queue={} token_value={} physical_queue={} device_generation={}")
        , statistics.sequence
        , statistics.generation
        , statistics.graphicsFrameIndex
        , currentFrame
        , *m_targetStart
        , statistics.graphicsFrameIndex - *m_targetStart
        , statistics.frameIndex
        , statistics.width
        , statistics.height
        , statistics.hardwareRequested ? 1u : 0u
        , statistics.hardwareAvailable ? 1u : 0u
        , statistics.hardwareReady ? 1u : 0u
        , statistics.csgHardwareEnabled ? 1u : 0u
        , statistics.acceptedToken.valid() ? 1u : 0u
        , statistics.acceptedToken.hasPhysicalQueueIdentity() ? 1u : 0u
        , static_cast<u32>(statistics.acceptedToken.queue)
        , statistics.acceptedToken.value
        , statistics.acceptedToken.physicalQueueIndex
        , statistics.acceptedToken.deviceGeneration
    );
#define NWB_SMOKE_REFRACTION_LOG_COUNTER(name, offset) \
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryCounter: sequence={} generation={} name={} value={}"), statistics.sequence, statistics.generation, NWB_TEXT(#name), statistics.name);
    NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_SMOKE_REFRACTION_LOG_COUNTER)
#undef NWB_SMOKE_REFRACTION_LOG_COUNTER
#define NWB_SMOKE_REFRACTION_COUNT_COUNTER(name, offset) + 1u
    constexpr u32 s_CounterCount = 0u NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_SMOKE_REFRACTION_COUNT_COUNTER);
#undef NWB_SMOKE_REFRACTION_COUNT_COUNTER
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryDiagnostics: complete sequence={} generation={} counters={}"), statistics.sequence, statistics.generation, s_CounterCount);
    m_sourceFrame = statistics.graphicsFrameIndex;
    m_reported = true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_scene_helpers.h"

#include <tests/smoke/scene_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RefractionScene final{
public:
    static constexpr SmokeMaterialRef s_ClearMaterial{"project/smoke/refraction/materials/clear"};
    static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/refraction/materials/opaque"};
    static constexpr AStringView s_MaterialInterface = s_SmokeSurfaceMaterialInterface;


public:
    static Core::ECS::EntityID CreatePanel(const SmokeMaterialRef& material, const Float4& tint, const Float4& position, const Float4& scale, Core::ECS::World& world);
    [[nodiscard]] static b8 CreateDensity(u32 percent, Core::Alloc::ScratchArena& arena, Core::ECS::World& world, ProjectRuntimeContext& context, Core::ECS::EntityID& glass);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


#include "framebuffer_capture.h"
#include "avboit_timing_render_pass.h"
#include <tests/smoke/presentation_types.adhd.h>
#include "gpu_pass_timing_probe.h"
#include "smoke_scene_helpers.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "stress_frame_graph_snapshot.h"

#include <loader/project_entry.h>

#include <impl/ecs_scene/module.h>
#include <impl/ecs_render/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/constant.h>
#include <global/simd/frame.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;

struct LifecyclePhase{
    AStringView name;
    f32 glassX;
    b8 enabled;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("RefractionSmokeProject");

static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
static constexpr SmokeMaterialRef s_TransparentMaterial{"project/smoke/refraction/materials/transparent"};

static constexpr Array<LifecyclePhase, 7> s_LifecyclePhases = {{
    { "baseline", 0.0f, false },
    { "active", 0.0f, true },
    { "moved", 1.9f, true },
    { "edge", 4.2f, true },
    { "empty", 100.0f, true },
    { "disabled", 0.0f, false },
    { "reenabled", 0.0f, true },
}};

[[nodiscard]] static Expected<Optional<u32>> ReadDensityPercent(NWB::Core::Alloc::ScratchArena& arena){
    const auto configured = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_DENSITY");
    if(!configured){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: failed to read density fixture selection"));
        return MakeUnexpected(Failure{});
    }
    if(!*configured)
        return Optional<u32>{};
    const AStringView text(**configured);
    if(text == "0")
        return Optional<u32>{0u};
    if(text == "1")
        return Optional<u32>{1u};
    if(text == "10")
        return Optional<u32>{10u};
    if(text == "50")
        return Optional<u32>{50u};
    if(text == "100")
        return Optional<u32>{100u};
    NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: density must be exactly 0, 1, 10, 50, or 100"));
    return MakeUnexpected(Failure{});
}

class RefractionSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(s_ProjectName, context);
        auto& renderer = AddSmokeRenderSystems(*world, context);
        // Isolate transmission by disabling reflection.
        NWB::Impl::ReflectionSettings reflection;
        reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("RefractionSmokeProject: invalid reflection settings"));
        const auto enabled = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_ENABLED");
        const b8 refractionEnabled = !enabled || *enabled != 0.0f;
        renderer.setRefractionEnabled(refractionEnabled);
        const auto hardware = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_HARDWARE");
        const b8 hardwareEnabled = !hardware || *hardware != 0.0f;
        renderer.setRefractionHardwareTracingEnabled(hardwareEnabled);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: refraction {}"), refractionEnabled ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
        return world;
    }


public:
    explicit RefractionSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context), m_world(CreateWorldOrDie(context)), m_lifecyclePaths(context.objectArena){}
    virtual ~RefractionSmokeProject()noexcept override{
        m_timingRenderPass.stop();
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
    }


public:
    virtual b8 onStartup()override{
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        m_lifecycleEnabled = ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_LIFECYCLE");
        m_timingEnabled = ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_TIMING");
        const auto density = ReadDensityPercent(scratchArena);
        if(!density)
            return false;
        m_densityEnabled = density->has_value();
        if(!m_queryDiagnostics.configure(scratchArena, m_densityEnabled, m_lifecycleEnabled, m_timingEnabled))
            return false;
        if(!configureFrameGraphSnapshot(scratchArena))
            return false;
        if(m_timingEnabled){
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
            if(!m_timingRenderPass.start())
                return false;
        }
        if(!configureFramebufferCapture(scratchArena))
            return false;

        const auto galleryCase = ReadSmokeEnvironmentText(scratchArena, "NWB_REFRACTION_SMOKE_CASE");
        if((galleryCase && m_lifecycleEnabled) || (m_densityEnabled && (galleryCase || m_lifecycleEnabled))){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: lifecycle, gallery, and density cases are mutually exclusive"));
            return false;
        }
        if(m_densityEnabled)
            return RefractionScene::CreateDensity(**density, scratchArena, *m_world, m_context, m_glass);
        if(galleryCase)
            return CreateRefractionGalleryScene(AStringView(galleryCase->data(), galleryCase->size()), ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_GEOMETRY"), *m_world);

        const auto camera = CreateSmokeCamera(1.4f, 6.0f, 0.0f, *m_world);
        // Include a light so shared shadow and TLAS preparation runs.
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        m_glass = CreateTintedStaticMeshEntity(s_SphereMesh, RefractionScene::s_ClearMaterial, RefractionScene::s_MaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(1.15f, 1.15f, 1.15f, 0.0f), *m_world);
        NWB_FATAL_ASSERT_MSG(camera.valid() && m_glass.valid() && light.valid(), NWB_TEXT("RefractionSmokeProject: camera/glass/light creation failed"));
        m_world->entity(light).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;

        // Unlit opaque stripes provide a static background for measuring refraction displacement.
        const u32 stripeCount = m_lifecycleEnabled ? 48u : 20u;
        const f32 stripeStart = m_lifecycleEnabled ? -7.05f : -2.85f;
        for(u32 stripe = 0u; stripe < stripeCount; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.92f : 0.04f;
            const f32 x = static_cast<f32>(stripe) * 0.3f + stripeStart;
            RefractionScene::CreatePanel(RefractionScene::s_OpaqueMaterial, Float4(value, value, value, 1.0f), Float4(x, 1.4f, 2.8f, 0.0f), Float4(0.15f, 1.0f, 2.0f, 0.0f), *m_world);
        }
        // Place the red strip in front of the glass and the blue strip behind it.
        RefractionScene::CreatePanel(s_TransparentMaterial, Float4(1.0f, 0.025f, 0.025f, 0.75f), Float4(0.0f, 0.85f, -1.7f, 0.0f), Float4(1.6f, 1.0f, 0.08f, 0.0f), *m_world);
        RefractionScene::CreatePanel(s_TransparentMaterial, Float4(0.025f, 0.1f, 1.0f, 0.70f), Float4(0.0f, 1.9f, 1.6f, 0.0f), Float4(2.0f, 1.0f, 0.12f, 0.0f), *m_world);

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: zero-coverage clear sphere + opaque stripes + foreground/background AVBOIT panels created"));
        if(m_lifecycleEnabled)
            applyLifecyclePhase();
        return true;
    }

    virtual void onShutdown()override{
        m_timingRenderPass.stop();
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        m_queryDiagnostics.update();
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        if(!advanceLifecycleCapture())
            return false;
        if(!updateFrameGraphSnapshot())
            return false;
        if(m_frameGraphSnapshotCompleted)
            return true;
        if(m_timingEnabled){
            m_fpsProbe.recordFrame(delta);
            m_gpuPassTimingProbe.recordFrame(delta, m_context.gpuTimingView());
        }
        const f32 fixedDelta = m_densityEnabled ? 1.f / 60.f : RendererBaselineFixedDelta();
        return TickSmokeWorld(fixedDelta > 0.0f ? fixedDelta : delta, *m_world);
    }


private:
    b8 configureFrameGraphSnapshot(NWB::Core::Alloc::ScratchArena& arena){
        const auto path = ReadEnvironmentVariable(arena, "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH");
        if(!path){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: failed to read frame-graph snapshot path"));
            return false;
        }
        if(!*path)
            return true;
        const auto readback = ReadEnvironmentVariable(arena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
        const auto freeze = ReadEnvironmentVariable(arena, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
        if(!readback || !freeze || (**path).empty() || m_timingEnabled || m_lifecycleEnabled || m_queryDiagnostics.enabled() || *readback || *freeze){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: a nonempty frame-graph snapshot path requires timing, lifecycle, framebuffer readback, and frame freeze disabled"));
            return false;
        }
        m_frameGraphSnapshotPath = SmokeEnvironmentString(**path, m_context.objectArena);
        return true;
    }

    b8 updateFrameGraphSnapshot(){
        if(m_frameGraphSnapshotPath.empty() || m_frameGraphSnapshotCompleted)
            return true;
        i32 width = 0;
        i32 height = 0;
        m_context.graphics.getWindowDimensions(width, height);
        const auto target = NWB::QueryProjectFrameClientSize();
        const u64 currentFrame = m_context.graphics.getFrameIndex();
        if(width != target.width || height != target.height || (m_frameGraphTargetStart && currentFrame < *m_frameGraphTargetStart)){
            m_frameGraphTargetStart.reset();
            return true;
        }
        if(!m_frameGraphTargetStart){
            m_frameGraphTargetStart = currentFrame;
            return true;
        }
        const u64 completedFrames = currentFrame - *m_frameGraphTargetStart;
        if(completedFrames < 360u || currentFrame == 0u)
            return true;
        if(!CaptureStressFrameGraphSnapshot(AStringView(m_frameGraphSnapshotPath), completedFrames, m_context))
            return false;
        m_frameGraphSnapshotCompleted = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionFrameGraphSnapshot: target_extent {}x{} completed_target_frames {}"), target.width, target.height, completedFrames);
        m_context.requestQuit();
        return true;
    }

    b8 configureFramebufferCapture(NWB::Core::Alloc::ScratchArena& scratchArena){
        constexpr u32 s_DefaultCaptureFrameCount = 16u;
        if(m_lifecycleEnabled){
            const auto output = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
            if(!output){
                NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: lifecycle requires application framebuffer capture"));
                return false;
            }
            u32 captureFrames = s_DefaultCaptureFrameCount;
            const auto frames = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT");
            if(frames){
                const auto parsed = ParseU64(AStringView(*frames));
                if(!parsed || *parsed == 0u || *parsed > Limit<u32>::s_Max){
                    NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: lifecycle frame count must be a positive u32"));
                    return false;
                }
                captureFrames = static_cast<u32>(*parsed);
            }
            for(usize phase = 0u; phase < s_LifecyclePhases.size(); ++phase){
                const auto path = phase + 1u == s_LifecyclePhases.size() ? SmokeEnvironmentString(*output, m_context.objectArena) : StringFormat(m_context.objectArena, "{}.{}.bmp", *output, s_LifecyclePhases[phase].name);
                m_lifecyclePaths.emplace_back(m_context.objectArena, AStringView(path));
            }
            const auto extent = NWB::QueryProjectFrameClientSize();
            const FramebufferCaptureOptions options{ .requiredWidth = extent.width, .requiredHeight = extent.height, .quitWhenReady = false, };
            m_framebufferCapture = MakeUnique<FramebufferCapture>(Move(m_lifecyclePaths.front()), captureFrames, m_context, options);
            return m_framebufferCapture && m_framebufferCapture->start();
        }
        FramebufferCaptureOptions options;
        if(m_densityEnabled){
            const auto extent = NWB::QueryProjectFrameClientSize();
            options.requiredWidth = extent.width;
            options.requiredHeight = extent.height;
            // The density harness confirms the exact client/framebuffer extent and readback, then closes normally.
            options.quitWhenReady = false;
        }
        if(m_queryDiagnostics.enabled()){
            options.predicateContext = &m_queryDiagnostics;
            options.shouldCapture = [](void* owner, const u64 graphicsFrame)noexcept{
                const auto& diagnostics = *MakeNotNull(static_cast<RefractionQueryDiagnostics*>(owner));
                return diagnostics.shouldCapture(graphicsFrame);
            };
        }
        auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(s_ProjectName, s_DefaultCaptureFrameCount, options, scratchArena);
        if(!capture)
            return false;
        m_framebufferCapture = Move(*capture);
        return true;
    }

    void applyLifecyclePhase(){
        const auto& phase = s_LifecyclePhases[m_lifecyclePhase];
        auto& transform = *MakeNotNull(m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_glass));
        auto& renderer = *MakeNotNull(m_world->getSystem<NWB::Impl::RendererSystem>());
        transform.position = Float4(phase.glassX, 1.4f, 0.0f, 0.0f);
        renderer.setRefractionEnabled(phase.enabled);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: phase {} applied"), ConvertString(phase.name));
    }

    b8 advanceLifecycleCapture(){
        if(!m_lifecycleEnabled || !m_framebufferCapture->captureReady() || m_lifecyclePhase == s_LifecyclePhases.size())
            return true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: captured {} graphics frame {}"), ConvertString(s_LifecyclePhases[m_lifecyclePhase].name), m_framebufferCapture->capturedGraphicsFrameIndex());
        ++m_lifecyclePhase;
        if(m_lifecyclePhase == s_LifecyclePhases.size()){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: complete samples=7"));
            m_framebufferCapture->finish();
            return true;
        }
        applyLifecyclePhase();
        if(!m_framebufferCapture->rearm(Move(m_lifecyclePaths[m_lifecyclePhase]))){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: failed to advance lifecycle capture"));
            return false;
        }
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    RefractionQueryDiagnostics m_queryDiagnostics{ m_context, *m_world };
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    AvboitTimingRenderPass m_timingRenderPass{ m_context.graphics };
    FpsProbe m_fpsProbe{ s_ProjectName };
    GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    Vector<::Path<NWB::Core::Alloc::GlobalArena>, NWB::Core::Alloc::GlobalArena> m_lifecyclePaths;
    SmokeEnvironmentString m_frameGraphSnapshotPath{ m_context.objectArena };
    Optional<u64> m_frameGraphTargetStart;
    usize m_lifecyclePhase = 0u;
    NWB::Core::ECS::EntityID m_glass = {};
    b8 m_lifecycleEnabled = false;
    b8 m_timingEnabled = false;
    b8 m_densityEnabled = false;
    b8 m_frameGraphSnapshotCompleted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    NWB::Core::Alloc::ScratchArena arena(Tests::Smoke::s_SmokeEnvironmentArena);
    const auto extent = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_EXTENT");
    NWB_FATAL_ASSERT_MSG(extent.has_value(), NWB_TEXT("RefractionSmokeProject: failed to read extent"));
    if(!*extent || **extent == "native" || **extent == "960x720")
        return { 960, 720 };
    if(**extent == "1600x900")
        return { 1600, 900 };
    if(**extent == "1920x1080")
        return { 1920, 1080 };
    NWB_FATAL_ASSERT_MSG(**extent == "npot", NWB_TEXT("RefractionSmokeProject: extent must be native, npot, 960x720, 1600x900, or 1920x1080"));
    return { 967, 727 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{ return NWB_TEXT("NWB Refraction Smoke"); }

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_refraction_smoke::RefractionSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
