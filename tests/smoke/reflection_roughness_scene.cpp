// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_roughness_scene.h"

#include "smoke_skinned_scene_helpers.h"

#include <core/common/log.h>
#include <core/assets/manager.h>
#include <global/math/constant.h>
#include <global/math/convert.h>
#include <global/math/frame.h>
#include <impl/assets_skeleton/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_roughness_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Tests::Smoke;
static constexpr SmokeMeshRef s_Plane("project/meshes/shadow_plane");
static constexpr SmokeMaterialRef s_Opaque("project/smoke/reflection/materials/opaque");
static constexpr SmokeMaterialRef s_Lit("project/smoke/reflection/materials/roughness_lit");
static constexpr SmokeModelRef s_Model("project/characters/body/model");
static constexpr AStringView s_Interface = "project/shaders/smoke_surface";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionRoughnessScene::ReflectionRoughnessScene(ProjectRuntimeContext& context, Core::ECS::World& world,
    const Core::ECS::EntityID camera, const Core::ECS::EntityID light, const f32 roughness)
    : m_context(context)
    , m_world(world)
    , m_camera(camera)
    , m_light(light)
    , m_bindJoints(context.objectArena)
    , m_roughness(roughness){}

bool ReflectionRoughnessScene::create(const AStringView caseName, const bool finalState){
    using namespace __hidden_reflection_roughness_scene;
    if(caseName == "rough_furnace")
        m_case = ReflectionRoughnessCase::Furnace;
    else if(caseName == "temporal_camera")
        m_case = ReflectionRoughnessCase::Camera;
    else if(caseName == "temporal_transform")
        m_case = ReflectionRoughnessCase::Transform;
    else if(caseName == "temporal_material")
        m_case = ReflectionRoughnessCase::Material;
    else if(caseName == "temporal_light")
        m_case = ReflectionRoughnessCase::Light;
    else if(caseName == "temporal_deform")
        m_case = ReflectionRoughnessCase::Deform;
    else if(caseName == "rough_deform")
        m_case = ReflectionRoughnessCase::StaticDeform;
    else if(caseName != "rough")
        return false;
    const auto backdrop = createPanel(
        s_Opaque, Float4(0.12f, 0.12f, 0.12f, 1.f),
        Float4(0.f, 1.4f, 2.f, 0.f), Float4(6.f, 1.f, 4.5f, 0.f)
    );
    const auto mirror = createPanel(
        s_Opaque, Float4(0.f, 0.f, 0.f, 1.f),
        Float4(0.f, 1.4f, 0.f, 0.f), Float4(2.8f, 1.f, 1.8f, 0.f),
        m_case == ReflectionRoughnessCase::Furnace ? 1.f : 0.95f, m_roughness
    );
    if(!backdrop.valid() || !mirror.valid())
        return false;
    if(m_case == ReflectionRoughnessCase::Furnace)
        return true;
    const auto green = createPanel(
        s_Opaque, Float4(0.f, 1.f, 0.f, 1.f),
        Float4(1.6f, 1.4f, -8.f, 0.f), Float4(0.75f, 1.f, 0.75f, 0.f)
    );
    if(!green.valid())
        return false;
    if(deforming()){
        if(!createDeformingSource())
            return false;
    }
    else{
        m_red = createPanel(
            m_case == ReflectionRoughnessCase::Light ? s_Lit : s_Opaque,
            Float4(1.f, 0.f, 0.f, 1.f), Float4(-1.6f, 1.4f, -8.f, 0.f), Float4(0.75f, 1.f, 0.75f, 0.f)
        );
        if(!m_red.valid())
            return false;
    }
    return !finalState || applyMutation();
}

bool ReflectionRoughnessScene::applyMutation(){
    using namespace __hidden_reflection_roughness_scene;
    if(m_finalState)
        return true;
    switch(m_case){
    case ReflectionRoughnessCase::Camera:
        m_world.entity(m_camera).getComponent<Impl::Scene::TransformComponent>().position.x = 0.6f;
        break;
    case ReflectionRoughnessCase::Transform:
        m_world.entity(m_red).getComponent<Impl::Scene::TransformComponent>().position.z = -150.f;
        break;
    case ReflectionRoughnessCase::Material:
        if(!Impl::SetMaterialMutableHalf4(m_world, m_red, Name(s_Interface), "runtime.color_tint", Float4(0.f, 0.f, 0.f, 1.f)))
            return false;
        break;
    case ReflectionRoughnessCase::Light:
        // Keep a positive-intensity authored light present so scene gathering does not insert its default light.
        m_world.entity(m_light).getComponent<Impl::Scene::LightComponent>().setColor(Float4(0.f, 0.f, 0.f, 0.f));
        break;
    case ReflectionRoughnessCase::Deform:{
        auto& pose = m_world.entity(m_skeleton).getComponent<Impl::SkeletonPoseComponent>();
        if(pose.localJoints.size() != m_bindJoints.size())
            return false;
        for(u32 joint = 1u; joint < pose.localJoints.size(); ++joint){
            const f32 angle = (joint & 1u) != 0u ? 0.32f : -0.32f;
            StoreFloat(MatrixMultiply(LoadFloat(m_bindJoints[joint]), MatrixRotationRollPitchYaw(0.15f, angle, 0.12f)), pose.localJoints[joint]);
        }
        break;
    }
    default:
        return false;
    }
    m_finalState = true;
    return true;
}

Core::ECS::EntityID ReflectionRoughnessScene::createPanel(const SmokeMaterialRef& material, const Float4& color,
    const Float4& position, const Float4& scale, const f32 f0, const f32 roughness){
    using namespace __hidden_reflection_roughness_scene;
    const auto entity = CreateTintedStaticMeshEntity(m_world, m_context.objectArena, s_Plane, material, s_Interface, color, position, scale);
    if(!entity.valid())
        return entity;
    auto& transform = m_world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
    StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
    const Half4U packedF0 = MakeHalf4U(f0, f0, f0, 0.f);
    const Half packedRoughness = ConvertFloatToHalf(roughness);
    if(!Impl::SetMaterialMutableParameter(
        m_world, entity, Name(s_Interface), "runtime.specular_f0",
        Impl::MaterialLayoutFieldType::Half3, Impl::PackMaterialInstanceBytes(packedF0.raw, sizeof(Half) * 3u)
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    if(!Impl::SetMaterialMutableParameter(
        m_world, entity, Name(s_Interface), "runtime.perceptual_roughness",
        Impl::MaterialLayoutFieldType::Half, Impl::PackMaterialInstanceBytes(&packedRoughness, sizeof(packedRoughness))
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    return entity;
}

bool ReflectionRoughnessScene::createDeformingSource(){
    using namespace __hidden_reflection_roughness_scene;
    UniquePtr<Core::Assets::IAsset> modelAsset;
    if(!m_context.assetManager.loadSync(Impl::Model::AssetTypeName(), s_Model.name(), modelAsset) || !modelAsset)
        return false;
    const auto& model = *checked_cast<const Impl::Model*>(modelAsset.get());
    if(model.skeletonObjects().empty())
        return false;
    UniquePtr<Core::Assets::IAsset> skeletonAsset;
    if(!m_context.assetManager.loadSync(Impl::Skeleton::AssetTypeName(), model.skeletonObjects().front().skeleton.name(), skeletonAsset) || !skeletonAsset)
        return false;
    const auto& skeleton = *checked_cast<const Impl::Skeleton*>(skeletonAsset.get());
    if(skeleton.joints().size() < 2u)
        return false;
    m_bindJoints.reserve(skeleton.joints().size());
    for(const Impl::SkeletonJoint& joint : skeleton.joints())
        m_bindJoints.push_back(joint.localBindPose);
    bool tintApplied = false;
    m_red = CreateTintedModelEntity(
        m_world, m_context.objectArena, s_Model, s_Opaque, s_Interface,
        Float4(1.f, 0.f, 0.f, 1.f), Float4(-1.6f, 0.55f, -8.f, 0.f), Float4(0.85f, 0.85f, 0.85f, 0.f), &tintApplied
    );
    if(!m_red.valid() || !tintApplied)
        return false;
    SyncSmokeModelRuntimes(m_world);
    m_skeleton = FindSpawnedModelObject(m_world, m_red, Name("skeleton"), Impl::ModelObjectKind::Skeleton);
    return m_skeleton.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

