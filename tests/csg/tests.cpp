// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>
#include <core/ecs/module.h>
#include <impl/ecs_csg/module.h>

#include <tests/ecs_test_world.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
using TestWorld = NWB::Tests::EcsTestWorld;

inline constexpr Name s_ScratchArena("tests/csg/scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestCsgReceiverComponents(){
    TestWorld testWorld;

    auto staticReceiverEntity = testWorld.world.createEntity();
    auto& staticReceiver = staticReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
    staticReceiver.receiverGroup = Name("project/csg/receiver_group_a");

    EXPECT_TRUE((staticReceiverEntity.hasComponent<NWB::Impl::StaticCsgMeshComponent>()));
    EXPECT_TRUE((staticReceiver.receiverGroup == Name("project/csg/receiver_group_a")));
    EXPECT_TRUE((staticReceiver.enabled));
    EXPECT_TRUE((staticReceiver.affectOpaquePass));
    EXPECT_TRUE((staticReceiver.affectTransparentPass));

    auto skinnedReceiverEntity = testWorld.world.createEntity();
    auto& skinnedReceiver = skinnedReceiverEntity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
    skinnedReceiver.affectTransparentPass = false;

    EXPECT_TRUE((skinnedReceiverEntity.hasComponent<NWB::Impl::SkinnedCsgMeshComponent>()));
    EXPECT_TRUE((skinnedReceiver.receiverGroup == NAME_NONE));
    EXPECT_TRUE((skinnedReceiver.enabled));
    EXPECT_TRUE((skinnedReceiver.affectOpaquePass));
    EXPECT_TRUE((!skinnedReceiver.affectTransparentPass));

    usize staticReceiverCount = 0u;
    testWorld.world.view<NWB::Impl::StaticCsgMeshComponent>().each(
        [&staticReceiverCount](NWB::Core::ECS::EntityID entityId, NWB::Impl::StaticCsgMeshComponent& receiver){
            ++staticReceiverCount;
            EXPECT_TRUE((entityId.valid()));
            EXPECT_TRUE((receiver.receiverGroup == Name("project/csg/receiver_group_a")));
        }
    );
    EXPECT_TRUE((staticReceiverCount == 1u));
}

static void TestCsgCutterComponent(){
    TestWorld testWorld;

    auto cutterEntity = testWorld.world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);

    EXPECT_TRUE((cutterEntity.hasComponent<NWB::Impl::CsgCutterComponent>()));
    EXPECT_TRUE((cutter.receiverGroup == NAME_NONE));
    EXPECT_TRUE((cutter.shapeType == NAME_NONE));
    EXPECT_TRUE((cutter.active));
    EXPECT_TRUE((cutter.parameterBytes.empty()));

    EXPECT_TRUE((cutter.worldToShape._11 == 1.0f));
    EXPECT_TRUE((cutter.worldToShape._22 == 1.0f));
    EXPECT_TRUE((cutter.worldToShape._33 == 1.0f));
    EXPECT_TRUE((cutter.shapeToWorld._11 == 1.0f));
    EXPECT_TRUE((cutter.shapeToWorld._22 == 1.0f));
    EXPECT_TRUE((cutter.shapeToWorld._33 == 1.0f));

    cutter.receiverGroup = Name("project/csg/receiver_group_a");
    cutter.shapeType = Name("engine/csg/box");
    cutter.parameterBytes.push_back(0xAu);
    cutter.parameterBytes.push_back(0xBu);

    usize cutterCount = 0u;
    testWorld.world.view<NWB::Impl::CsgCutterComponent>().each(
        [&cutterCount](NWB::Core::ECS::EntityID entityId, NWB::Impl::CsgCutterComponent& viewCutter){
            ++cutterCount;
            EXPECT_TRUE((entityId.valid()));
            EXPECT_TRUE((viewCutter.receiverGroup == Name("project/csg/receiver_group_a")));
            EXPECT_TRUE((viewCutter.shapeType == Name("engine/csg/box")));
            EXPECT_TRUE((viewCutter.parameterBytes.size() == 2u));
            EXPECT_TRUE((viewCutter.parameterBytes[0u] == 0xAu));
            EXPECT_TRUE((viewCutter.parameterBytes[1u] == 0xBu));
        }
    );
    EXPECT_TRUE((cutterCount == 1u));
}

static NWB::Impl::CsgFrameState BuildTestCsgFrameState(
    TestWorld& testWorld,
    const NWB::Impl::CsgFrameBuildDesc& desc = NWB::Impl::CsgFrameBuildDesc{}
){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    return NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena, desc);
}

static bool ResolveTestCsgReceiverDrawState(
    TestWorld& testWorld,
    const NWB::Core::ECS::EntityID entity,
    const NWB::Impl::CsgReceiverPass::Enum receiverPass,
    NWB::Impl::CsgReceiverDrawState& outState
){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    const NWB::Impl::CsgFrameReceiverLookup receiverLookup(testWorld.world, scratchArena);
    return receiverLookup.resolveReceiverDrawState(entity, receiverPass, outState);
}

struct TestCsgVisibilityFilter{
    NWB::Core::ECS::EntityID hiddenEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
};

static bool TestCsgReceiverVisible(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID entity,
    const NWB::Impl::CsgReceiverKind::Enum receiverKind,
    const NWB::Impl::CsgReceiverComponent& receiver,
    void* userData
){
    static_cast<void>(world);
    static_cast<void>(receiverKind);
    static_cast<void>(receiver);

    const auto* filter = static_cast<const TestCsgVisibilityFilter*>(userData);
    return !filter || entity != filter->hiddenEntity;
}

static void TestCsgFrameStateKillSwitch(){
    {
        TestWorld testWorld;
        EXPECT_TRUE((!NWB::Impl::HasCsgFrameCandidates(testWorld.world)));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE((state.empty()));
        EXPECT_TRUE((!state.hasAnyWork));
        EXPECT_TRUE((state.receiverCount == 0u));
        EXPECT_TRUE((state.cutterCount == 0u));
    }

    {
        TestWorld testWorld;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/box");

        EXPECT_TRUE((!NWB::Impl::HasCsgFrameCandidates(testWorld.world)));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE((state.empty()));
        EXPECT_TRUE((!state.hasAnyWork));
        EXPECT_TRUE((state.receiverCount == 0u));
        EXPECT_TRUE((state.cutterCount == 0u));
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");

        EXPECT_TRUE((!NWB::Impl::HasCsgFrameCandidates(testWorld.world)));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE((state.empty()));
        EXPECT_TRUE((!state.hasAnyWork));
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/box");

        EXPECT_TRUE((NWB::Impl::HasCsgFrameCandidates(testWorld.world)));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE((!state.empty()));
        EXPECT_TRUE((state.hasAnyWork));
        EXPECT_TRUE((state.hasOpaqueStaticWork));
        EXPECT_TRUE((!state.hasOpaqueSkinnedWork));
        EXPECT_TRUE((state.hasTransparentStaticWork));
        EXPECT_TRUE((!state.hasTransparentSkinnedWork));
        EXPECT_TRUE((state.receiverCount == 1u));
        EXPECT_TRUE((state.cutterCount == 1u));
    }

    {
        TestWorld testWorld;

        auto disabledReceiverEntity = testWorld.world.createEntity();
        auto& disabledReceiver = disabledReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        disabledReceiver.receiverGroup = Name("project/csg/group_a");
        disabledReceiver.enabled = false;

        auto nonMatchingReceiverEntity = testWorld.world.createEntity();
        auto& nonMatchingReceiver = nonMatchingReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        nonMatchingReceiver.receiverGroup = Name("project/csg/group_b");

        auto inactiveCutterEntity = testWorld.world.createEntity();
        auto& inactiveCutter = inactiveCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        inactiveCutter.receiverGroup = Name("project/csg/group_b");
        inactiveCutter.shapeType = Name("engine/csg/box");
        inactiveCutter.active = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/box");

        EXPECT_TRUE((NWB::Impl::HasCsgFrameCandidates(testWorld.world)));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE((state.empty()));
        EXPECT_TRUE((!state.hasAnyWork));
        EXPECT_TRUE((state.receiverCount == 0u));
        EXPECT_TRUE((state.cutterCount == 0u));
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");
        receiver.affectOpaquePass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/sphere");

        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE((!state.empty()));
        EXPECT_TRUE((!state.hasOpaqueStaticWork));
        EXPECT_TRUE((!state.hasOpaqueSkinnedWork));
        EXPECT_TRUE((!state.hasTransparentStaticWork));
        EXPECT_TRUE((state.hasTransparentSkinnedWork));
        EXPECT_TRUE((state.receiverCount == 1u));
        EXPECT_TRUE((state.cutterCount == 1u));
    }

    {
        TestWorld testWorld;

        auto hiddenReceiverEntity = testWorld.world.createEntity();
        auto& hiddenReceiver = hiddenReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        hiddenReceiver.receiverGroup = Name("project/csg/group_a");

        auto visibleReceiverEntity = testWorld.world.createEntity();
        auto& visibleReceiver = visibleReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        visibleReceiver.receiverGroup = Name("project/csg/group_a");

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/capsule");

        TestCsgVisibilityFilter filter;
        filter.hiddenEntity = hiddenReceiverEntity.id();

        NWB::Impl::CsgFrameBuildDesc desc;
        desc.receiverVisible = &TestCsgReceiverVisible;
        desc.receiverVisibleUserData = &filter;

        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld, desc);

        EXPECT_TRUE((!state.empty()));
        EXPECT_TRUE((state.hasOpaqueStaticWork));
        EXPECT_TRUE((state.hasTransparentStaticWork));
        EXPECT_TRUE((state.receiverCount == 1u));
        EXPECT_TRUE((state.cutterCount == 1u));
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");
        receiver.affectTransparentPass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/box");

        NWB::Impl::CsgFrameBuildDesc desc;
        desc.includeOpaquePass = false;

        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld, desc);

        EXPECT_TRUE((state.empty()));
        EXPECT_TRUE((!state.hasAnyWork));
        EXPECT_TRUE((state.receiverCount == 0u));
        EXPECT_TRUE((state.cutterCount == 0u));
    }
}

static void TestCsgFrameReceiverLookup(){
    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");

        NWB::Impl::CsgReceiverDrawState drawState;
        EXPECT_TRUE((!ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            drawState
        )));
        EXPECT_TRUE((!drawState.active));
        EXPECT_TRUE((drawState.cutterCount == 0u));
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");

        auto boxCutterEntity = testWorld.world.createEntity();
        auto& boxCutter = boxCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        boxCutter.receiverGroup = Name("project/csg/group_a");
        boxCutter.shapeType = Name("engine/csg/box");

        auto sphereCutterEntity = testWorld.world.createEntity();
        auto& sphereCutter = sphereCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        sphereCutter.receiverGroup = Name("project/csg/group_a");
        sphereCutter.shapeType = Name("engine/csg/sphere");

        auto inactiveCutterEntity = testWorld.world.createEntity();
        auto& inactiveCutter = inactiveCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        inactiveCutter.receiverGroup = Name("project/csg/group_a");
        inactiveCutter.shapeType = Name("engine/csg/capsule");
        inactiveCutter.active = false;

        auto untypedCutterEntity = testWorld.world.createEntity();
        auto& untypedCutter = untypedCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        untypedCutter.receiverGroup = Name("project/csg/group_a");

        auto otherGroupCutterEntity = testWorld.world.createEntity();
        auto& otherGroupCutter = otherGroupCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        otherGroupCutter.receiverGroup = Name("project/csg/group_b");
        otherGroupCutter.shapeType = Name("engine/csg/box");

        NWB::Impl::CsgReceiverDrawState opaqueDrawState;
        EXPECT_TRUE((ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            opaqueDrawState
        )));
        EXPECT_TRUE((opaqueDrawState.active));
        EXPECT_TRUE((opaqueDrawState.receiverKind == NWB::Impl::CsgReceiverKind::Static));
        EXPECT_TRUE((opaqueDrawState.cutterCount == 2u));

        NWB::Impl::CsgReceiverDrawState transparentDrawState;
        EXPECT_TRUE((ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Transparent,
            transparentDrawState
        )));
        EXPECT_TRUE((transparentDrawState.active));
        EXPECT_TRUE((transparentDrawState.receiverKind == NWB::Impl::CsgReceiverKind::Static));
        EXPECT_TRUE((transparentDrawState.cutterCount == 2u));
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");
        receiver.affectTransparentPass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/box");

        NWB::Impl::CsgReceiverDrawState opaqueDrawState;
        EXPECT_TRUE((ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            opaqueDrawState
        )));
        EXPECT_TRUE((opaqueDrawState.active));

        NWB::Impl::CsgReceiverDrawState transparentDrawState;
        EXPECT_TRUE((!ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Transparent,
            transparentDrawState
        )));
        EXPECT_TRUE((!transparentDrawState.active));
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");
        receiver.affectOpaquePass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/capsule");

        NWB::Impl::CsgReceiverDrawState opaqueDrawState;
        EXPECT_TRUE((!ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            opaqueDrawState
        )));
        EXPECT_TRUE((!opaqueDrawState.active));

        NWB::Impl::CsgReceiverDrawState transparentDrawState;
        EXPECT_TRUE((ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Transparent,
            transparentDrawState
        )));
        EXPECT_TRUE((transparentDrawState.active));
        EXPECT_TRUE((transparentDrawState.receiverKind == NWB::Impl::CsgReceiverKind::Skinned));
        EXPECT_TRUE((transparentDrawState.cutterCount == 1u));
    }
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

static void TestCsgShapeRegistryBuiltIns(){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);

    EXPECT_TRUE((registry.shapeTypeCount() == 0u));
    EXPECT_TRUE((NWB::Impl::RegisterBuiltInCsgShapeTypes(registry)));
    EXPECT_TRUE((registry.shapeTypeCount() == 4u));

    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(Name("engine/csg/box"));
    EXPECT_TRUE((boxId != NWB::Impl::s_InvalidCsgShapeTypeId));

    NWB::Impl::CsgShapeTypeInfo boxShape;
    EXPECT_TRUE((registry.findShapeType(boxId, boxShape)));
    EXPECT_TRUE((boxShape.desc.name == Name("engine/csg/box")));
    EXPECT_TRUE((!boxShape.desc.shaderModule));
    EXPECT_TRUE((boxShape.desc.parameterByteSize == sizeof(NWB::Impl::CsgBoxShapeParameters)));

    EXPECT_TRUE((NWB::Impl::RegisterBuiltInCsgShapeTypes(registry)));
    EXPECT_TRUE((registry.shapeTypeCount() == 4u));
    EXPECT_TRUE((registry.findShapeTypeId(Name("engine/csg/box")) == boxId));
}

static void TestCsgShapeRegistryBounds(){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    EXPECT_TRUE((NWB::Impl::RegisterBuiltInCsgShapeTypes(registry)));

    Float34 shapeToWorld = NWB::Impl::CsgIdentityTransform();
    shapeToWorld._14 = 10.0f;
    shapeToWorld._24 = -5.0f;
    shapeToWorld._34 = 1.0f;
    const SIMDMatrix shapeToWorldMatrix = LoadFloat(shapeToWorld);

    NWB::Impl::CsgBoxShapeParameters boxParameters;
    boxParameters.halfExtents = Float4(2.0f, 3.0f, 4.0f, 0.0f);

    SIMDVector minBounds;
    SIMDVector maxBounds;
    bool finiteBounds = false;
    EXPECT_TRUE((registry.buildShapeBounds(
        Name("engine/csg/box"),
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters),
        minBounds,
        maxBounds,
        finiteBounds
    )));
    EXPECT_TRUE((finiteBounds));
    EXPECT_TRUE((VectorGetX(minBounds) == 8.0f));
    EXPECT_TRUE((VectorGetY(minBounds) == -8.0f));
    EXPECT_TRUE((VectorGetZ(minBounds) == -3.0f));
    EXPECT_TRUE((VectorGetX(maxBounds) == 12.0f));
    EXPECT_TRUE((VectorGetY(maxBounds) == -2.0f));
    EXPECT_TRUE((VectorGetZ(maxBounds) == 5.0f));

    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(Name("engine/csg/box"));
    EXPECT_TRUE((registry.buildShapeBounds(
        boxId,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters),
        minBounds,
        maxBounds,
        finiteBounds
    )));
    EXPECT_TRUE((finiteBounds));
    EXPECT_TRUE((VectorGetX(minBounds) == 8.0f));
    EXPECT_TRUE((VectorGetX(maxBounds) == 12.0f));

    NWB::Impl::CsgPlaneShapeParameters planeParameters;
    EXPECT_TRUE((registry.buildShapeBounds(
        Name("engine/csg/plane"),
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&planeParameters),
        sizeof(planeParameters),
        minBounds,
        maxBounds,
        finiteBounds
    )));
    EXPECT_TRUE((!finiteBounds));

    EXPECT_TRUE((!registry.buildShapeBounds(
        Name("engine/csg/box"),
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters) - 1u,
        minBounds,
        maxBounds,
        finiteBounds
    )));
}

static void TestCsgShapeRegistryProjectShape(){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);

    NWB::Impl::CsgShapeTypeDesc desc;
    desc.name = Name("project/csg/noise_blob");
    desc.shaderModule = Name("project/shaders/csg/noise_blob");
    desc.shaderModuleInclude = ACompactString("project/shaders/csg/noise_blob.slangi");
    desc.parameterByteSize = sizeof(TestProjectShapeParameters);
    desc.boundsCallback = &TestProjectShapeBounds;

    NWB::Impl::CsgShapeTypeId shapeTypeId = NWB::Impl::s_InvalidCsgShapeTypeId;
    EXPECT_TRUE((registry.registerShapeType(desc, shapeTypeId)));
    EXPECT_TRUE((shapeTypeId != NWB::Impl::s_InvalidCsgShapeTypeId));
    EXPECT_TRUE((registry.findShapeTypeId(desc.name) == shapeTypeId));

    NWB::Impl::CsgShapeTypeInfo shapeType;
    EXPECT_TRUE((registry.findShapeType(desc.name, shapeType)));
    EXPECT_TRUE((shapeType.desc.shaderModule == desc.shaderModule));
    EXPECT_TRUE((shapeType.desc.shaderModuleInclude == desc.shaderModuleInclude));
    EXPECT_TRUE((shapeType.desc.boundsCallback == &TestProjectShapeBounds));

    ACompactString shaderModuleInclude;
    EXPECT_TRUE((registry.findShaderModuleInclude(desc.shaderModule, shaderModuleInclude)));
    EXPECT_TRUE((shaderModuleInclude == desc.shaderModuleInclude));

    TestProjectShapeParameters parameters;
    SIMDVector minBounds;
    SIMDVector maxBounds;
    bool finiteBounds = false;
    const SIMDMatrix shapeToWorldMatrix = LoadFloat(NWB::Impl::CsgIdentityTransform());
    EXPECT_TRUE((registry.buildShapeBounds(
        desc.name,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&parameters),
        sizeof(parameters),
        minBounds,
        maxBounds,
        finiteBounds
    )));
    EXPECT_TRUE((finiteBounds));
    EXPECT_TRUE((VectorGetX(minBounds) == -2.0f));
    EXPECT_TRUE((VectorGetY(minBounds) == -3.0f));
    EXPECT_TRUE((VectorGetZ(minBounds) == -4.0f));
    EXPECT_TRUE((VectorGetX(maxBounds) == 2.0f));
    EXPECT_TRUE((VectorGetY(maxBounds) == 3.0f));
    EXPECT_TRUE((VectorGetZ(maxBounds) == 4.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Csg, CsgReceiverComponents){
    __hidden_tests::TestCsgReceiverComponents();
}

TEST(Csg, CsgCutterComponent){
    __hidden_tests::TestCsgCutterComponent();
}

TEST(Csg, CsgFrameStateKillSwitch){
    __hidden_tests::TestCsgFrameStateKillSwitch();
}

TEST(Csg, CsgFrameReceiverLookup){
    __hidden_tests::TestCsgFrameReceiverLookup();
}

TEST(Csg, CsgShapeRegistryBuiltIns){
    __hidden_tests::TestCsgShapeRegistryBuiltIns();
}

TEST(Csg, CsgShapeRegistryBounds){
    __hidden_tests::TestCsgShapeRegistryBounds();
}

TEST(Csg, CsgShapeRegistryProjectShape){
    __hidden_tests::TestCsgShapeRegistryProjectShape();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

