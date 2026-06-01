// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>
#include <core/ecs/module.h>
#include <impl/ecs_csg/module.h>

#include <tests/ecs_test_world.h>
#include <tests/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestWorld = NWB::Tests::EcsTestWorld;


#define NWB_CSG_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestCsgReceiverComponents(TestContext& context){
    TestWorld testWorld;

    auto staticReceiverEntity = testWorld.world.createEntity();
    auto& staticReceiver = staticReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
    staticReceiver.receiverGroup = Name("project/csg/receiver_group_a");

    NWB_CSG_TEST_CHECK(context, staticReceiverEntity.hasComponent<NWB::Impl::StaticCsgMeshComponent>());
    NWB_CSG_TEST_CHECK(context, staticReceiver.receiverGroup == Name("project/csg/receiver_group_a"));
    NWB_CSG_TEST_CHECK(context, staticReceiver.enabled);
    NWB_CSG_TEST_CHECK(context, staticReceiver.generateCaps);
    NWB_CSG_TEST_CHECK(context, staticReceiver.affectOpaquePass);
    NWB_CSG_TEST_CHECK(context, staticReceiver.affectTransparentPass);

    auto skinnedReceiverEntity = testWorld.world.createEntity();
    auto& skinnedReceiver = skinnedReceiverEntity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
    skinnedReceiver.generateCaps = false;
    skinnedReceiver.affectTransparentPass = false;

    NWB_CSG_TEST_CHECK(context, skinnedReceiverEntity.hasComponent<NWB::Impl::SkinnedCsgMeshComponent>());
    NWB_CSG_TEST_CHECK(context, skinnedReceiver.receiverGroup == NAME_NONE);
    NWB_CSG_TEST_CHECK(context, skinnedReceiver.enabled);
    NWB_CSG_TEST_CHECK(context, !skinnedReceiver.generateCaps);
    NWB_CSG_TEST_CHECK(context, skinnedReceiver.affectOpaquePass);
    NWB_CSG_TEST_CHECK(context, !skinnedReceiver.affectTransparentPass);

    usize staticReceiverCount = 0u;
    testWorld.world.view<NWB::Impl::StaticCsgMeshComponent>().each(
        [&context, &staticReceiverCount](NWB::Core::ECS::EntityID entityId, NWB::Impl::StaticCsgMeshComponent& receiver){
            ++staticReceiverCount;
            NWB_CSG_TEST_CHECK(context, entityId.valid());
            NWB_CSG_TEST_CHECK(context, receiver.receiverGroup == Name("project/csg/receiver_group_a"));
        }
    );
    NWB_CSG_TEST_CHECK(context, staticReceiverCount == 1u);
}

static void TestCsgCutterComponent(TestContext& context){
    TestWorld testWorld;

    auto cutterEntity = testWorld.world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);

    NWB_CSG_TEST_CHECK(context, cutterEntity.hasComponent<NWB::Impl::CsgCutterComponent>());
    NWB_CSG_TEST_CHECK(context, cutter.receiverGroup == NAME_NONE);
    NWB_CSG_TEST_CHECK(context, cutter.shapeType == NAME_NONE);
    NWB_CSG_TEST_CHECK(context, cutter.operation == NWB::Impl::CsgOperation::Subtract);
    NWB_CSG_TEST_CHECK(context, cutter.active);
    NWB_CSG_TEST_CHECK(context, !cutter.previewVisible);
    NWB_CSG_TEST_CHECK(context, cutter.parameterBytes.empty());

    NWB_CSG_TEST_CHECK(context, cutter.worldToShape._11 == 1.0f);
    NWB_CSG_TEST_CHECK(context, cutter.worldToShape._22 == 1.0f);
    NWB_CSG_TEST_CHECK(context, cutter.worldToShape._33 == 1.0f);
    NWB_CSG_TEST_CHECK(context, cutter.worldToShape._44 == 1.0f);
    NWB_CSG_TEST_CHECK(context, cutter.shapeToWorld._11 == 1.0f);
    NWB_CSG_TEST_CHECK(context, cutter.shapeToWorld._22 == 1.0f);
    NWB_CSG_TEST_CHECK(context, cutter.shapeToWorld._33 == 1.0f);
    NWB_CSG_TEST_CHECK(context, cutter.shapeToWorld._44 == 1.0f);

    cutter.receiverGroup = Name("project/csg/receiver_group_a");
    cutter.shapeType = Name("engine/csg/box");
    cutter.parameterBytes.push_back(0xAu);
    cutter.parameterBytes.push_back(0xBu);

    usize cutterCount = 0u;
    testWorld.world.view<NWB::Impl::CsgCutterComponent>().each(
        [&context, &cutterCount](NWB::Core::ECS::EntityID entityId, NWB::Impl::CsgCutterComponent& viewCutter){
            ++cutterCount;
            NWB_CSG_TEST_CHECK(context, entityId.valid());
            NWB_CSG_TEST_CHECK(context, viewCutter.receiverGroup == Name("project/csg/receiver_group_a"));
            NWB_CSG_TEST_CHECK(context, viewCutter.shapeType == Name("engine/csg/box"));
            NWB_CSG_TEST_CHECK(context, viewCutter.parameterBytes.size() == 2u);
            NWB_CSG_TEST_CHECK(context, viewCutter.parameterBytes[0u] == 0xAu);
            NWB_CSG_TEST_CHECK(context, viewCutter.parameterBytes[1u] == 0xBu);
        }
    );
    NWB_CSG_TEST_CHECK(context, cutterCount == 1u);
}

struct TestProjectShapeParameters{
    Float4 minExtent = Float4(-2.0f, -3.0f, -4.0f, 0.0f);
    Float4 maxExtent = Float4(2.0f, 3.0f, 4.0f, 0.0f);
};

static bool TestProjectShapeBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    static_cast<void>(shapeToWorld);
    outMinBounds = VectorZero();
    outMaxBounds = VectorZero();
    outFiniteBounds = false;

    if(parameterByteSize != sizeof(TestProjectShapeParameters) || !parameterBytes)
        return false;

    TestProjectShapeParameters parameters;
    NWB_MEMCPY(&parameters, sizeof(parameters), parameterBytes, sizeof(parameters));

    outMinBounds = LoadFloat(parameters.minExtent);
    outMaxBounds = LoadFloat(parameters.maxExtent);
    outFiniteBounds = true;
    return true;
}

static void TestCsgShapeRegistryBuiltIns(TestContext& context){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);

    NWB_CSG_TEST_CHECK(context, registry.shapeTypeCount() == 0u);
    NWB_CSG_TEST_CHECK(context, NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    NWB_CSG_TEST_CHECK(context, registry.shapeTypeCount() == 4u);

    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(NWB::Impl::s_CsgBoxShapeName);
    NWB_CSG_TEST_CHECK(context, boxId != NWB::Impl::s_InvalidCsgShapeTypeId);

    NWB::Impl::CsgShapeTypeInfo boxShape;
    NWB_CSG_TEST_CHECK(context, registry.findShapeType(boxId, boxShape));
    NWB_CSG_TEST_CHECK(context, boxShape.desc.name == NWB::Impl::s_CsgBoxShapeName);
    NWB_CSG_TEST_CHECK(context, boxShape.desc.shaderModule == NWB::Impl::s_CsgBuiltInShapeShaderModuleName);
    NWB_CSG_TEST_CHECK(context, boxShape.desc.parameterByteSize == sizeof(NWB::Impl::CsgBoxShapeParameters));
    NWB_CSG_TEST_CHECK(context, boxShape.desc.supportsAnalyticGradient);
    NWB_CSG_TEST_CHECK(context, boxShape.desc.supportsCapGeneration);

    NWB_CSG_TEST_CHECK(context, NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    NWB_CSG_TEST_CHECK(context, registry.shapeTypeCount() == 4u);
    NWB_CSG_TEST_CHECK(context, registry.findShapeTypeId(NWB::Impl::s_CsgBoxShapeName) == boxId);
}

static void TestCsgShapeRegistryBounds(TestContext& context){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    NWB_CSG_TEST_CHECK(context, NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));

    Float44 shapeToWorld = NWB::Impl::CsgIdentityTransform();
    shapeToWorld._14 = 10.0f;
    shapeToWorld._24 = -5.0f;
    shapeToWorld._34 = 1.0f;
    const SIMDMatrix shapeToWorldMatrix = LoadFloat(shapeToWorld);

    NWB::Impl::CsgBoxShapeParameters boxParameters;
    boxParameters.halfExtents = Float4(2.0f, 3.0f, 4.0f, 0.0f);

    SIMDVector minBounds;
    SIMDVector maxBounds;
    bool finiteBounds = false;
    NWB_CSG_TEST_CHECK(context, registry.buildShapeBounds(
        NWB::Impl::s_CsgBoxShapeName,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    NWB_CSG_TEST_CHECK(context, finiteBounds);
    NWB_CSG_TEST_CHECK(context, VectorGetX(minBounds) == 8.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetY(minBounds) == -8.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetZ(minBounds) == -3.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetX(maxBounds) == 12.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetY(maxBounds) == -2.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetZ(maxBounds) == 5.0f);

    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(NWB::Impl::s_CsgBoxShapeName);
    NWB_CSG_TEST_CHECK(context, registry.buildShapeBounds(
        boxId,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    NWB_CSG_TEST_CHECK(context, finiteBounds);
    NWB_CSG_TEST_CHECK(context, VectorGetX(minBounds) == 8.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetX(maxBounds) == 12.0f);

    NWB::Impl::CsgPlaneShapeParameters planeParameters;
    NWB_CSG_TEST_CHECK(context, registry.buildShapeBounds(
        NWB::Impl::s_CsgPlaneShapeName,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&planeParameters),
        sizeof(planeParameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    NWB_CSG_TEST_CHECK(context, !finiteBounds);

    NWB_CSG_TEST_CHECK(context, !registry.buildShapeBounds(
        NWB::Impl::s_CsgBoxShapeName,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters) - 1u,
        minBounds,
        maxBounds,
        finiteBounds
    ));
}

static void TestCsgShapeRegistryProjectShape(TestContext& context){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);

    NWB::Impl::CsgShapeTypeDesc desc;
    desc.name = Name("project/csg/noise_blob");
    desc.shaderModule = Name("project/shaders/csg/noise_blob");
    desc.parameterByteSize = sizeof(TestProjectShapeParameters);
    desc.boundsCallback = &TestProjectShapeBounds;
    desc.supportsAnalyticGradient = false;
    desc.supportsCapGeneration = false;

    NWB::Impl::CsgShapeTypeId shapeTypeId = NWB::Impl::s_InvalidCsgShapeTypeId;
    NWB_CSG_TEST_CHECK(context, registry.registerShapeType(desc, shapeTypeId));
    NWB_CSG_TEST_CHECK(context, shapeTypeId != NWB::Impl::s_InvalidCsgShapeTypeId);
    NWB_CSG_TEST_CHECK(context, registry.findShapeTypeId(desc.name) == shapeTypeId);

    NWB::Impl::CsgShapeTypeInfo shapeType;
    NWB_CSG_TEST_CHECK(context, registry.findShapeType(desc.name, shapeType));
    NWB_CSG_TEST_CHECK(context, shapeType.desc.shaderModule == desc.shaderModule);
    NWB_CSG_TEST_CHECK(context, shapeType.desc.boundsCallback == &TestProjectShapeBounds);

    TestProjectShapeParameters parameters;
    SIMDVector minBounds;
    SIMDVector maxBounds;
    bool finiteBounds = false;
    const SIMDMatrix shapeToWorldMatrix = LoadFloat(NWB::Impl::CsgIdentityTransform());
    NWB_CSG_TEST_CHECK(context, registry.buildShapeBounds(
        desc.name,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&parameters),
        sizeof(parameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    NWB_CSG_TEST_CHECK(context, finiteBounds);
    NWB_CSG_TEST_CHECK(context, VectorGetX(minBounds) == -2.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetY(minBounds) == -3.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetZ(minBounds) == -4.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetX(maxBounds) == 2.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetY(maxBounds) == 3.0f);
    NWB_CSG_TEST_CHECK(context, VectorGetZ(maxBounds) == 4.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_CSG_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("csg", [](NWB::Tests::TestContext& context){
    __hidden_tests::TestCsgReceiverComponents(context);
    __hidden_tests::TestCsgCutterComponent(context);
    __hidden_tests::TestCsgShapeRegistryBuiltIns(context);
    __hidden_tests::TestCsgShapeRegistryBounds(context);
    __hidden_tests::TestCsgShapeRegistryProjectShape(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

