// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_reference_scene.h"

#include <global/simd/constant.h>
#include <global/simd/convert.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_reference_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Tests::Smoke;
static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/reflection/materials/opaque"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/reflection/materials/glass"};
static constexpr SmokeMaterialRef s_TransparentMaterial{"project/smoke/refraction/materials/transparent"};
static constexpr AStringView s_MaterialInterface = "project/shaders/smoke_surface";
static constexpr f32 s_GlassIor = 3.8f;
static constexpr f32 s_GlassF0 = ((s_GlassIor - 1.0f) * (s_GlassIor - 1.0f)) / ((s_GlassIor + 1.0f) * (s_GlassIor + 1.0f));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace __hidden_reflection_reference_scene;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionReferenceScene::ReflectionReferenceScene(Core::ECS::World& world, const b8 glassRoughnessCase, const f32 authoredRoughness)noexcept
    : m_world(world)
    , m_glassRoughnessCase(glassRoughnessCase)
    , m_authoredRoughness(authoredRoughness)
{}

void ReflectionReferenceScene::createMirrorScene(const b8 moved){
    createMirrorBackdrop();
    const f32 shift = moved ? 0.9f : 0.0f;
    // Marker spheres behind the camera reflect through z=0 to virtual images at z=+8.
    const auto red = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f), Float4(-1.6f + shift, 1.0f, -8.0f, 0.0f), Float4(0.65f, 0.65f, 0.65f, 0.0f));
    const auto green = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f), Float4(1.6f + shift, 2.0f, -8.0f, 0.0f), Float4(0.65f, 0.65f, 0.65f, 0.0f));
    NWB_FATAL_ASSERT_MSG(red.valid() && green.valid(), NWB_TEXT("ReflectionSmokeProject: offscreen markers failed"));
}

void ReflectionReferenceScene::createOnscreenMirrorScene(const b8 moved, const b8 boundary){
    createMirrorBackdrop();
    const f32 redX = moved ? -1.45f : -1.7f;
    const f32 greenX = boundary ? 3.0f : (moved ? 1.45f : 1.7f);
    // Hybrid reflection resolves provisional screen hits against virtual images at z=+3.
    createPanel(s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f), Float4(redX, 1.0f, -3.0f, 0.0f), Float4(0.3f, 1.0f, 0.25f, 0.0f));
    createPanel(s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f), Float4(greenX, 2.0f, -3.0f, 0.0f), Float4(0.3f, 1.0f, 0.35f, 0.0f));
}

void ReflectionReferenceScene::createFloorMirrorScene(){
    createPanel(s_OpaqueMaterial, Float4(0.12f, 0.12f, 0.12f, 1.0f), Float4(0.0f, 1.4f, 5.0f, 0.0f), Float4(6.0f, 1.0f, 4.5f, 0.0f));
    const auto floor = createMesh(s_PlaneMesh, s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f), 0.95f);
    NWB_FATAL_ASSERT_MSG(floor.valid(), NWB_TEXT("ReflectionSmokeProject: floor mirror creation failed"));
    // Reflection through y=0 places virtual marker centers at y=-2.
    createPanel(s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f), Float4(-1.7f, 2.0f, 3.0f, 0.0f), Float4(0.3f, 1.0f, 0.25f, 0.0f));
    createPanel(s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f), Float4(1.7f, 2.0f, 3.0f, 0.0f), Float4(0.3f, 1.0f, 0.35f, 0.0f));
}

void ReflectionReferenceScene::createOpaqueGlassScene(){
    for(u32 stripe = 0u; stripe < 24u; ++stripe){
        const f32 value = (stripe & 1u) != 0u ? 0.55f : 0.015f;
        const f32 x = static_cast<f32>(stripe) * 0.3f - 3.45f;
        createPanel(s_OpaqueMaterial, Float4(value, value, value, 1.0f), Float4(x, 1.4f, 2.8f, 0.0f), Float4(0.15f, 1.0f, 3.0f, 0.0f));
    }
    const auto opaque = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f), Float4(-1.3f, 1.5f, 0.0f, 0.0f), Float4(0.95f, 0.95f, 0.95f, 0.0f), 0.95f);
    const auto glass = createMesh(s_SphereMesh, s_GlassMaterial, Float4(1.0f, 1.0f, 1.0f, 0.0f), Float4(1.3f, 1.5f, 0.0f, 0.0f), Float4(0.95f, 0.95f, 0.95f, 0.0f), s_GlassF0);
    NWB_FATAL_ASSERT_MSG(opaque.valid() && glass.valid(), NWB_TEXT("ReflectionSmokeProject: opaque/glass spheres failed"));
    m_world.entity(glass).getComponent<NWB::Impl::RendererComponent>().opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;
    createPanel(s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f), Float4(-4.0f, 1.4f, -8.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f));
    createPanel(s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f), Float4(4.0f, 1.4f, -8.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f));
    createPanel(s_TransparentMaterial, Float4(0.015f, 0.025f, 1.0f, 0.90f), Float4(0.0f, 0.85f, -1.7f, 0.0f), Float4(2.0f, 1.0f, 0.08f, 0.0f));
}

NWB::Core::ECS::EntityID ReflectionReferenceScene::createMesh(const SmokeMeshRef& mesh, const SmokeMaterialRef& material, const Float4& color, const Float4& position, const Float4& scale, const f32 specularF0){
    const auto entity = CreateTintedStaticMeshEntity(mesh, material, s_MaterialInterface, color, position, scale, m_world);
    NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("ReflectionSmokeProject: mesh creation failed"));
    const Name materialInterface(s_MaterialInterface);
    const Half4U f0 = MakeHalf4U(specularF0, specularF0, specularF0, 0.0f);
    const Half roughness = ConvertFloatToHalf(m_glassRoughnessCase && specularF0 == s_GlassF0 ? m_authoredRoughness : 0.f);
    NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
        m_world, entity, materialInterface, "runtime.specular_f0", NWB::Impl::MaterialLayoutFieldType::f16x3,
        NWB::Impl::PackMaterialInstanceBytes(Span<const Half>(f0.raw, 3u))
    ), NWB_TEXT("ReflectionSmokeProject: F0 override failed")
    );
    NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
        m_world, entity, materialInterface, "runtime.perceptual_roughness", NWB::Impl::MaterialLayoutFieldType::f16,
        NWB::Impl::PackMaterialInstanceValue(roughness)
    ), NWB_TEXT("ReflectionSmokeProject: roughness override failed")
    );
    return entity;
}

void ReflectionReferenceScene::createPanel(const SmokeMaterialRef& material, const Float4& color, const Float4& position, const Float4& scale, const f32 specularF0){
    const auto entity = createMesh(s_PlaneMesh, material, color, position, scale, specularF0);
    auto& transform = m_world.entity(entity).getComponent<NWB::Impl::Scene::TransformComponent>();
    StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform.rotation);
}

void ReflectionReferenceScene::createMirrorBackdrop(){
    createPanel(s_OpaqueMaterial, Float4(0.12f, 0.12f, 0.12f, 1.0f), Float4(0.0f, 1.4f, 2.0f, 0.0f), Float4(6.0f, 1.0f, 4.5f, 0.0f));
    createPanel(s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f), Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(2.8f, 1.0f, 1.8f, 0.0f), 0.95f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

