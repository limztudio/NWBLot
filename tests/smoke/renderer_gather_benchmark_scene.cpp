// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_gather_benchmark_scene.h"

#include "smoke_project_helpers.h"

#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_render/module.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


namespace __hidden_renderer_gather_scene{
constexpr AStringView s_OpaqueWorkload = s_GatherBenchmarkOpaqueWorkload;
constexpr AStringView s_HybridWorkload = s_GatherBenchmarkHybridWorkload;
constexpr AStringView s_UniqueWorkload = s_GatherBenchmarkUniqueWorkload;
constexpr AStringView s_OverridesWorkload = s_GatherBenchmarkOverridesWorkload;
static constexpr SmokeMeshRef s_Mesh("project/meshes/octahedron");
static constexpr SmokeMeshRef s_Plane("project/meshes/shadow_plane");
static constexpr SmokeMaterialRef s_Opaque("project/smoke/reflection/materials/opaque");
static constexpr SmokeMaterialRef s_Glass("project/smoke/gather_benchmark/materials/glass_00");
static constexpr SmokeModelRef s_Model("project/characters/body/model");
static constexpr AStringView s_Interface = s_SmokeSurfaceMaterialInterface;
static constexpr u32 s_ObjectCount = s_GatherBenchmarkObjectCount;
};

using namespace __hidden_renderer_gather_scene;


b8 RendererGatherBenchmarkScene::build(const AStringView workload, const b8 runtime, Core::Alloc::ScratchArena& scratchArena){
    m_runtime = runtime;
    const auto cameraEntity = CreateSmokeCamera(2.6f, 9.0f, 0.0f, m_world);
    if(!cameraEntity.valid())
        return false;
    const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(m_world, 0.9f, 0.65f, 0.0f, Float4(1.0f, 0.96f, 0.88f), 2.0f);
    m_world.entity(light).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;
    if(!createBackdrop())
        return false;
    for(u32 index = 0u; index < s_ObjectCount; ++index){
        const SIMDVectorSingle grid = VectorSubtract(VectorSet(static_cast<f32>(index % 8u), static_cast<f32>(index / 8u), 0.0f, 0.0f), VectorSet(3.5f, 0.0f, 0.0f, 0.0f));
        const SIMDVectorSingle spacing = VectorMultiply(grid, VectorSet(0.75f, 0.58f, 0.0f, 0.0f));
        Float4 position;
        StoreFloat(VectorAdd(VectorSet(0.0f, 0.7f, 0.0f, 0.0f), spacing), position);
        if(m_runtime && index % 8u == 0u){
            if(!createRuntime(index, position))
                return false;
            continue;
        }
        const b8 opaque = workload == s_OpaqueWorkload || (workload == s_HybridWorkload && index % 2u == 0u);
        SmokeMeshRef mesh = s_Mesh;
        SmokeMaterialRef material = opaque ? s_Opaque : s_Glass;
        if(workload == s_UniqueWorkload){
            const auto meshPath = StringFormat(scratchArena, "project/smoke/gather_benchmark/meshes/mesh_{:02}", index);
            const auto materialPath = StringFormat(scratchArena, "project/smoke/gather_benchmark/materials/glass_{:02}", index);
            const auto meshReference = SmokeMeshRef::FromText(AStringView(meshPath));
            if(!meshReference)
                return false;
            mesh = *meshReference;
            const auto materialReference = SmokeMaterialRef::FromText(AStringView(materialPath));
            if(!materialReference)
                return false;
            material = *materialReference;
        }
        auto entity = m_world.createEntity();
        auto& transform = entity.addComponent<NWB::Impl::Scene::TransformComponent>();
        transform.position = position;
        transform.scale = Float4(0.43f, 0.43f, 0.43f, 0.0f);
        entity.addComponent<NWB::Impl::MeshComponent>().mesh = mesh;
        auto& component = entity.addComponent<NWB::Impl::RendererComponent>();
        component.material = material;
        if(!opaque){
            component.opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;
            ++m_counts.transparentRenderers;
        }
        ++m_counts.renderers;
        if(workload == s_OverridesWorkload){
            entity.addComponent<NWB::Impl::MaterialInstanceComponent>(Name(s_Interface));
            const Float4 tint(0.55f + static_cast<f32>(index) / 256.0f, 0.85f, 1.0f, 1.0f);
            if(!NWB::Impl::SetMaterialMutableHalf4(m_world, entity.id(), Name(s_Interface), "runtime.color_tint", tint))
                return false;
        }
    }
    if(m_runtime)
        SyncSmokeModelRuntimes(m_world);
    return true;
}


b8 RendererGatherBenchmarkScene::createBackdrop(){
    const auto backdrop = CreateTintedStaticMeshEntity(s_Plane, s_Opaque, s_Interface, Float4(0.1f, 0.1f, 0.1f, 1.0f), Float4(0.0f, 2.6f, 2.0f, 0.0f), Float4(5.0f, 1.0f, 3.5f, 0.0f), m_world);
    if(!backdrop.valid())
        return false;
    auto& transform = m_world.entity(backdrop).getComponent<NWB::Impl::Scene::TransformComponent>();
    StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform.rotation);
    const Half4U f0 = MakeHalf4U(0.35f, 0.35f, 0.35f, 0.0f);
    const Half roughness = ConvertFloatToHalf(0.0f);
    if(!NWB::Impl::SetMaterialMutableParameter(m_world, backdrop, Name(s_Interface), "runtime.specular_f0", NWB::Impl::MaterialLayoutFieldType::f16x3, NWB::Impl::PackMaterialInstanceBytes(Span<const Half>(f0.raw, 3u))))
        return false;
    if(!NWB::Impl::SetMaterialMutableParameter(m_world, backdrop, Name(s_Interface), "runtime.perceptual_roughness", NWB::Impl::MaterialLayoutFieldType::f16, NWB::Impl::PackMaterialInstanceValue(roughness)))
        return false;
    ++m_counts.renderers;
    return true;
}

b8 RendererGatherBenchmarkScene::createRuntime(const u32 index, const Float4& position){
    const auto modelResult = CreateTintedModelEntity(s_Model, s_Opaque, s_Interface, Float4(0.9f, 0.45f, 0.2f, 1.0f), position, Float4(0.22f, 0.22f, 0.22f, 0.0f), m_world);
    const auto owner = modelResult.entity;
    if(!owner.valid() || !modelResult.tintApplied)
        return false;
    SyncSmokeModelRuntimes(m_world);
    const auto skeleton = FindSpawnedModelObject(owner, Name(NWB::Tests::Smoke::s_SmokeNameSkeleton), NWB::Impl::ModelObjectKind::Skeleton, m_world);
    if(!skeleton.valid())
        return false;
    auto& pose = m_world.entity(skeleton).getComponent<NWB::Impl::SkeletonPoseComponent>();
    if(pose.localJoints.size() < 2u)
        return false;
    const f32 yaw = (index & 8u) != 0u ? 0.08f : -0.08f;
    StoreFloat(MatrixMultiply(LoadFloat(pose.localJoints[1u]), MatrixRotationRollPitchYaw(0.0f, yaw, 0.0f)), pose.localJoints[1u]);
    // Held real skeletal pose: runtime preparation stays live, with no animation-phase difference between arms.
    ++m_counts.runtimeOwners;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

