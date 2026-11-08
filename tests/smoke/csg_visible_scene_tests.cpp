// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_visible_scene.h"

#include <tests/common/ecs_test_world.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


Core::ECS::EntityID CreateTintedStaticMeshEntity(
    [[maybe_unused]] const SmokeMeshRef& mesh,
    [[maybe_unused]] const SmokeMaterialRef& material,
    [[maybe_unused]] const NameText<char> materialInterfacePath,
    [[maybe_unused]] const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    Core::ECS::World& world
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<Impl::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = scale;
    return entity.id();
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_visible_scene_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CsgVisibleScene, MissingReceiverDoesNotStopOtherPoseUpdates){
    NWB::Tests::EcsTestWorld testWorld;
    auto& world = testWorld.world;
    const auto camera = world.createEntity().id();
    const auto light = world.createEntity().id();
    NWB::Tests::Smoke::CsgVisibleScene scene;
    scene.create(NWB::Tests::Smoke::SmokeMaterialRef{"project/smoke/csg_visible/materials/solid"}, false, camera, light, world);

    NWB::Core::ECS::EntityID missingReceiver = {};
    NWB::Core::ECS::EntityID remainingReceiver = {};
    NWB::Core::ECS::EntityID remainingCutter = {};
    world.view<NWB::Impl::StaticCsgMeshComponent>().each([&](const NWB::Core::ECS::EntityID entity, NWB::Impl::StaticCsgMeshComponent& receiver){
        if(receiver.receiverGroup == Name("project/smoke/csg_visible/plane_receiver"))
            missingReceiver = entity;
        if(receiver.receiverGroup == Name("project/smoke/csg_visible/sphere_receiver"))
            remainingReceiver = entity;
    });
    world.view<NWB::Impl::CsgCutterComponent>().each([&](const NWB::Core::ECS::EntityID entity, NWB::Impl::CsgCutterComponent& cutter){
        if(cutter.receiverGroup == Name("project/smoke/csg_visible/sphere_receiver"))
            remainingCutter = entity;
    });
    ASSERT_TRUE(missingReceiver.valid());
    ASSERT_TRUE(remainingReceiver.valid());
    ASSERT_TRUE(remainingCutter.valid());
    world.destroyEntity(missingReceiver);

    const auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(remainingReceiver);
    const auto* cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(remainingCutter);
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(cutter, nullptr);
    const Float4 oldRotation = transform->rotation;
    const Float34 oldCutterTransform = cutter->shapeToWorld;
    scene.update(1.0f, world);
    transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(remainingReceiver);
    cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(remainingCutter);
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(cutter, nullptr);
    EXPECT_NE(transform->rotation.y, oldRotation.y);
    b8 cutterChanged = false;
    for(usize index = 0u; index < 12u; ++index)
        cutterChanged = cutterChanged || cutter->shapeToWorld.raw[index] != oldCutterTransform.raw[index];
    EXPECT_TRUE(cutterChanged);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

