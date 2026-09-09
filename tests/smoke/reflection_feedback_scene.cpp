// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_feedback_scene.h"

#include <core/assets/manager.h>
#include <global/math/constant.h>
#include <global/math/convert.h>
#include <global/math/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_feedback_scene{

using namespace Tests::Smoke;
static constexpr SmokeMeshRef s_Plane("project/meshes/shadow_plane");
static constexpr SmokeMaterialRef s_Opaque("project/smoke/reflection/materials/opaque");
static constexpr AStringView s_Interface = "project/shaders/smoke_surface";

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionFeedbackScene::ReflectionFeedbackScene(ProjectRuntimeContext& context, Core::ECS::World& world)
    : m_context(context)
    , m_world(world){}

bool ReflectionFeedbackScene::create(const AStringView caseName, const bool finalState){
    if(caseName == "feedback_mutation")
        m_case = ReflectionFeedbackCase::Mutation;
    else if(caseName == "feedback_long_miss")
        m_case = ReflectionFeedbackCase::LongMiss;
    else if(caseName != "feedback_boundary")
        return false;
    if(m_case == ReflectionFeedbackCase::LongMiss){
        if(!createPanel(
            Float4(0.12f, 0.12f, 0.12f, 1.f), Float4(0.f, 1.4f, 8.f, 0.f), Float4(6.f, 1.f, 6.f, 0.f)
        ).valid())
            return false;
        if(!createPanel(
            Float4(0.008f, 0.008f, 0.008f, 1.f), Float4(0.f, 0.f, 2.f, 0.f), Float4(4.f, 1.f, 6.f, 0.f), 0.95f, true
        ).valid())
            return false;
        const auto red = createPanel(
            Float4(1.f, 0.01f, 0.01f, 1.f), Float4(-1.7f, 3.6f, 6.f, 0.f), Float4(0.3f, 1.f, 0.35f, 0.f)
        );
        m_green = createPanel(
            Float4(0.01f, 1.f, 0.01f, 1.f), Float4(1.7f, 3.6f, 6.f, 0.f), Float4(0.3f, 1.f, 0.35f, 0.f)
        );
        return red.valid() && m_green.valid() && !finalState;
    }
    if(!createPanel(
        Float4(0.12f, 0.12f, 0.12f, 1.f), Float4(0.f, 1.4f, 2.f, 0.f), Float4(6.f, 1.f, 4.5f, 0.f)
    ).valid())
        return false;
    if(!createPanel(
        Float4(0.008f, 0.008f, 0.008f, 1.f), Float4(0.f, 1.4f, 0.f, 0.f), Float4(2.8f, 1.f, 1.8f, 0.f), 0.95f
    ).valid())
        return false;
    const auto red = createPanel(
        Float4(1.f, 0.01f, 0.01f, 1.f), Float4(-1.7f, 1.f, -3.f, 0.f), Float4(0.3f, 1.f, 0.25f, 0.f)
    );
    // The initial green panel lies outside the direct viewport but remains visible in the wall's hardware reflection.
    // Moving it into the viewport creates a provisional SSR return; feedback must wake on any return, not only accepted hits.
    m_green = createPanel(
        Float4(0.01f, 1.f, 0.01f, 1.f), Float4(3.f, 2.f, -3.f, 0.f), Float4(0.3f, 1.f, 0.35f, 0.f)
    );
    if(!red.valid() || !m_green.valid())
        return false;
    return !finalState || applyMutation();
}

bool ReflectionFeedbackScene::applyMutation(){
    if(m_case != ReflectionFeedbackCase::Mutation)
        return false;
    if(m_finalState)
        return true;
    m_world.entity(m_green).getComponent<Impl::Scene::TransformComponent>().position.x = 1.7f;
    m_finalState = true;
    return true;
}

Core::ECS::EntityID ReflectionFeedbackScene::createPanel(const Float4& color, const Float4& position, const Float4& scale,
    const f32 f0, const bool horizontal){
    using namespace __hidden_reflection_feedback_scene;
    const auto entity = CreateTintedStaticMeshEntity(m_world, m_context.objectArena, s_Plane, s_Opaque, s_Interface, color, position, scale);
    if(!entity.valid())
        return entity;
    if(!horizontal){
        auto& transform = m_world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
    }
    const Half4U packedF0 = MakeHalf4U(f0, f0, f0, 0.f);
    const Half packedRoughness = ConvertFloatToHalf(0.f);
    if(!Impl::SetMaterialMutableParameter(
        m_world, entity, Name(s_Interface), "runtime.specular_f0", Impl::MaterialLayoutFieldType::Half3,
        Impl::PackMaterialInstanceBytes(packedF0.raw, sizeof(Half) * 3u)
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    if(!Impl::SetMaterialMutableParameter(
        m_world, entity, Name(s_Interface), "runtime.perceptual_roughness", Impl::MaterialLayoutFieldType::Half,
        Impl::PackMaterialInstanceBytes(&packedRoughness, sizeof(packedRoughness))
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    return entity;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

