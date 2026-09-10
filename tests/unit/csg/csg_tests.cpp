// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>
#include <core/ecs/module.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_csg/deform_edit.h>

#include <tests/common/ecs_test_world.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;

inline constexpr Name s_ScratchArena("tests/csg/scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Csg, CsgReceiverComponents){
    TestWorld testWorld;

    auto staticReceiverEntity = testWorld.world.createEntity();
    auto& staticReceiver = staticReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
    staticReceiver.receiverGroup = Name("project/csg/receiver_group_a");

    EXPECT_TRUE(staticReceiverEntity.hasComponent<NWB::Impl::StaticCsgMeshComponent>());
    EXPECT_EQ(staticReceiver.receiverGroup, Name("project/csg/receiver_group_a"));
    EXPECT_TRUE(staticReceiver.enabled);
    EXPECT_TRUE(staticReceiver.affectOpaquePass);
    EXPECT_TRUE(staticReceiver.affectTransparentPass);

    auto skinnedReceiverEntity = testWorld.world.createEntity();
    auto& skinnedReceiver = skinnedReceiverEntity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
    skinnedReceiver.affectTransparentPass = false;

    EXPECT_TRUE(skinnedReceiverEntity.hasComponent<NWB::Impl::SkinnedCsgMeshComponent>());
    EXPECT_EQ(skinnedReceiver.receiverGroup, NAME_NONE);
    EXPECT_TRUE(skinnedReceiver.enabled);
    EXPECT_TRUE(skinnedReceiver.affectOpaquePass);
    EXPECT_FALSE(skinnedReceiver.affectTransparentPass);

    usize staticReceiverCount = 0u;
    testWorld.world.view<NWB::Impl::StaticCsgMeshComponent>().each(
        [&staticReceiverCount](NWB::Core::ECS::EntityID entityId, NWB::Impl::StaticCsgMeshComponent& receiver){
            ++staticReceiverCount;
            EXPECT_TRUE(entityId.valid());
            EXPECT_EQ(receiver.receiverGroup, Name("project/csg/receiver_group_a"));
        }
    );
    EXPECT_EQ(staticReceiverCount, 1u);
}

TEST(Csg, CsgCutterComponent){
    TestWorld testWorld;

    auto cutterEntity = testWorld.world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);

    EXPECT_TRUE(cutterEntity.hasComponent<NWB::Impl::CsgCutterComponent>());
    EXPECT_EQ(cutter.receiverGroup, NAME_NONE);
    EXPECT_EQ(cutter.shapeType, NAME_NONE);
    EXPECT_TRUE(cutter.active);
    EXPECT_TRUE(cutter.parameterBytes.empty());

    EXPECT_TRUE(MatrixIsIdentity(LoadFloat(cutter.worldToShape)));
    EXPECT_TRUE(MatrixIsIdentity(LoadFloat(cutter.shapeToWorld)));

    cutter.receiverGroup = Name("project/csg/receiver_group_a");
    cutter.shapeType = Name("engine/csg/box");
    cutter.parameterBytes.push_back(0xAu);
    cutter.parameterBytes.push_back(0xBu);

    usize cutterCount = 0u;
    testWorld.world.view<NWB::Impl::CsgCutterComponent>().each(
        [&cutterCount](NWB::Core::ECS::EntityID entityId, NWB::Impl::CsgCutterComponent& viewCutter){
            ++cutterCount;
            EXPECT_TRUE(entityId.valid());
            EXPECT_EQ(viewCutter.receiverGroup, Name("project/csg/receiver_group_a"));
            EXPECT_EQ(viewCutter.shapeType, Name("engine/csg/box"));
            EXPECT_EQ(viewCutter.parameterBytes.size(), 2u);
            EXPECT_EQ(viewCutter.parameterBytes[0u], 0xAu);
            EXPECT_EQ(viewCutter.parameterBytes[1u], 0xBu);
        }
    );
    EXPECT_EQ(cutterCount, 1u);
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

TEST(Csg, CsgFrameStateKillSwitch){
    {
        TestWorld testWorld;
        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }

    {
        TestWorld testWorld;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name("project/csg/group_a");
        cutter.shapeType = Name("engine/csg/box");

        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");

        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
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

        EXPECT_TRUE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_FALSE(state.empty());
        EXPECT_TRUE(state.hasAnyWork);
        EXPECT_TRUE(state.hasOpaqueStaticWork);
        EXPECT_FALSE(state.hasOpaqueSkinnedWork);
        EXPECT_TRUE(state.hasTransparentStaticWork);
        EXPECT_FALSE(state.hasTransparentSkinnedWork);
        EXPECT_EQ(state.receiverCount, 1u);
        EXPECT_EQ(state.cutterCount, 1u);
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

        EXPECT_TRUE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(testWorld);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
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

        EXPECT_FALSE(state.empty());
        EXPECT_FALSE(state.hasOpaqueStaticWork);
        EXPECT_FALSE(state.hasOpaqueSkinnedWork);
        EXPECT_FALSE(state.hasTransparentStaticWork);
        EXPECT_TRUE(state.hasTransparentSkinnedWork);
        EXPECT_EQ(state.receiverCount, 1u);
        EXPECT_EQ(state.cutterCount, 1u);
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

        EXPECT_FALSE(state.empty());
        EXPECT_TRUE(state.hasOpaqueStaticWork);
        EXPECT_TRUE(state.hasTransparentStaticWork);
        EXPECT_EQ(state.receiverCount, 1u);
        EXPECT_EQ(state.cutterCount, 1u);
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

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }
}

TEST(Csg, CsgFrameReceiverLookup){
    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name("project/csg/group_a");

        NWB::Impl::CsgReceiverDrawState drawState;
        EXPECT_FALSE(ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            drawState
        ));
        EXPECT_FALSE(drawState.active);
        EXPECT_EQ(drawState.cutterCount, 0u);
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
        EXPECT_TRUE(ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            opaqueDrawState
        ));
        EXPECT_TRUE(opaqueDrawState.active);
        EXPECT_EQ(opaqueDrawState.receiverKind, NWB::Impl::CsgReceiverKind::Static);
        EXPECT_EQ(opaqueDrawState.cutterCount, 2u);

        usize resolvedCutterCount = 0u;
        {
            NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
            const NWB::Impl::CsgFrameReceiverLookup receiverLookup(testWorld.world, scratchArena);
            NWB::Impl::CsgReceiverDrawState resolvedDrawState;
            ASSERT_TRUE(receiverLookup.resolveReceiverDrawState(
                receiverEntity.id(),
                NWB::Impl::CsgReceiverPass::Opaque,
                resolvedDrawState
            ));
            receiverLookup.forEachReceiverCutter(
                resolvedDrawState,
                [&](const NWB::Core::ECS::EntityID, const NWB::Impl::CsgCutterComponent& resolvedCutter){
                    EXPECT_EQ(resolvedCutter.receiverGroup, receiver.receiverGroup);
                    ++resolvedCutterCount;
                }
            );
        }
        EXPECT_EQ(resolvedCutterCount, 2u);

        NWB::Impl::CsgReceiverDrawState transparentDrawState;
        EXPECT_TRUE(ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Transparent,
            transparentDrawState
        ));
        EXPECT_TRUE(transparentDrawState.active);
        EXPECT_EQ(transparentDrawState.receiverKind, NWB::Impl::CsgReceiverKind::Static);
        EXPECT_EQ(transparentDrawState.cutterCount, 2u);
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
        EXPECT_TRUE(ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            opaqueDrawState
        ));
        EXPECT_TRUE(opaqueDrawState.active);

        NWB::Impl::CsgReceiverDrawState transparentDrawState;
        EXPECT_FALSE(ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Transparent,
            transparentDrawState
        ));
        EXPECT_FALSE(transparentDrawState.active);
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
        EXPECT_FALSE(ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Opaque,
            opaqueDrawState
        ));
        EXPECT_FALSE(opaqueDrawState.active);

        NWB::Impl::CsgReceiverDrawState transparentDrawState;
        EXPECT_TRUE(ResolveTestCsgReceiverDrawState(
            testWorld,
            receiverEntity.id(),
            NWB::Impl::CsgReceiverPass::Transparent,
            transparentDrawState
        ));
        EXPECT_TRUE(transparentDrawState.active);
        EXPECT_EQ(transparentDrawState.receiverKind, NWB::Impl::CsgReceiverKind::Skinned);
        EXPECT_EQ(transparentDrawState.cutterCount, 1u);
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

TEST(Csg, CsgShapeRegistryBuiltIns){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);

    EXPECT_EQ(registry.shapeTypeCount(), 0u);
    EXPECT_EQ(registry.revision(), 0u);
    EXPECT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    EXPECT_EQ(registry.shapeTypeCount(), 4u);
    EXPECT_EQ(registry.revision(), 4u);

    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(Name("engine/csg/box"));
    EXPECT_NE(boxId, NWB::Impl::s_InvalidCsgShapeTypeId);
    EXPECT_EQ(boxId, NWB::Impl::CsgShapeTypeIdFromName(Name("engine/csg/box")));

    NWB::Impl::CsgShapeTypeInfo boxShape;
    EXPECT_TRUE(registry.findShapeType(boxId, boxShape));
    EXPECT_EQ(boxShape.desc.name, Name("engine/csg/box"));
    EXPECT_FALSE(boxShape.desc.shaderModule);
    EXPECT_EQ(boxShape.desc.parameterByteSize, sizeof(NWB::Impl::CsgBoxShapeParameters));

    EXPECT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    EXPECT_EQ(registry.shapeTypeCount(), 4u);
    EXPECT_EQ(registry.revision(), 8u);
    EXPECT_EQ(registry.findShapeTypeId(Name("engine/csg/box")), boxId);
}

TEST(Csg, CsgShapeRegistryBounds){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    EXPECT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));

    const SIMDMatrix shapeToWorldMatrix = MatrixTranslation(10.0f, -5.0f, 1.0f);

    NWB::Impl::CsgBoxShapeParameters boxParameters;
    boxParameters.halfExtents = Float4(2.0f, 3.0f, 4.0f, 0.0f);

    SIMDVector minBounds;
    SIMDVector maxBounds;
    bool finiteBounds = false;
    EXPECT_TRUE(registry.buildShapeBounds(
        Name("engine/csg/box"),
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    EXPECT_TRUE(finiteBounds);
    EXPECT_EQ(VectorGetX(minBounds), 8.0f);
    EXPECT_EQ(VectorGetY(minBounds), -8.0f);
    EXPECT_EQ(VectorGetZ(minBounds), -3.0f);
    EXPECT_EQ(VectorGetX(maxBounds), 12.0f);
    EXPECT_EQ(VectorGetY(maxBounds), -2.0f);
    EXPECT_EQ(VectorGetZ(maxBounds), 5.0f);

    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(Name("engine/csg/box"));
    EXPECT_TRUE(registry.buildShapeBounds(
        boxId,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    EXPECT_TRUE(finiteBounds);
    EXPECT_EQ(VectorGetX(minBounds), 8.0f);
    EXPECT_EQ(VectorGetX(maxBounds), 12.0f);

    NWB::Impl::CsgPlaneShapeParameters planeParameters;
    EXPECT_TRUE(registry.buildShapeBounds(
        Name("engine/csg/plane"),
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&planeParameters),
        sizeof(planeParameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    EXPECT_FALSE(finiteBounds);

    EXPECT_FALSE(registry.buildShapeBounds(
        Name("engine/csg/box"),
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&boxParameters),
        sizeof(boxParameters) - 1u,
        minBounds,
        maxBounds,
        finiteBounds
    ));
}

TEST(Csg, CsgShapeRegistryProjectShape){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);

    NWB::Impl::CsgShapeTypeDesc desc;
    desc.name = Name("project/csg/noise_blob");
    desc.shaderModule = Name("project/shaders/csg/noise_blob");
    desc.shaderModuleInclude = ACompactString("project/shaders/csg/noise_blob.slangi");
    desc.parameterByteSize = sizeof(TestProjectShapeParameters);
    desc.boundsCallback = &TestProjectShapeBounds;

    NWB::Impl::CsgShapeTypeId shapeTypeId = NWB::Impl::s_InvalidCsgShapeTypeId;
    EXPECT_TRUE(registry.registerShapeType(desc, shapeTypeId));
    EXPECT_NE(shapeTypeId, NWB::Impl::s_InvalidCsgShapeTypeId);
    EXPECT_EQ(shapeTypeId, NWB::Impl::CsgShapeTypeIdFromName(desc.name));
    EXPECT_EQ(registry.findShapeTypeId(desc.name), shapeTypeId);

    NWB::Impl::CsgShapeTypeInfo shapeType;
    EXPECT_TRUE(registry.findShapeType(desc.name, shapeType));
    EXPECT_EQ(shapeType.desc.shaderModule, desc.shaderModule);
    EXPECT_EQ(shapeType.desc.shaderModuleInclude, desc.shaderModuleInclude);
    EXPECT_EQ(shapeType.desc.boundsCallback, &TestProjectShapeBounds);

    ACompactString shaderModuleInclude;
    EXPECT_TRUE(registry.findShaderModuleInclude(desc.shaderModule, shaderModuleInclude));
    EXPECT_EQ(shaderModuleInclude, desc.shaderModuleInclude);

    TestProjectShapeParameters parameters;
    SIMDVector minBounds;
    SIMDVector maxBounds;
    bool finiteBounds = false;
    const SIMDMatrix shapeToWorldMatrix = LoadFloat(::Float34Identity());
    EXPECT_TRUE(registry.buildShapeBounds(
        desc.name,
        shapeToWorldMatrix,
        reinterpret_cast<const u8*>(&parameters),
        sizeof(parameters),
        minBounds,
        maxBounds,
        finiteBounds
    ));
    EXPECT_TRUE(finiteBounds);
    EXPECT_EQ(VectorGetX(minBounds), -2.0f);
    EXPECT_EQ(VectorGetY(minBounds), -3.0f);
    EXPECT_EQ(VectorGetZ(minBounds), -4.0f);
    EXPECT_EQ(VectorGetX(maxBounds), 2.0f);
    EXPECT_EQ(VectorGetY(maxBounds), 3.0f);
    EXPECT_EQ(VectorGetZ(maxBounds), 4.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST(Csg, CsgDeformSequentialCutsPreviewMatchesCommit){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Core::Alloc::GlobalArena commitArena(s_ScratchArena);

    auto makeVertex = [](const f32 x, const f32 y, const f32 z){
        NWB::Impl::CsgDeformVertex vertex;
        vertex.position = Float3U(x, y, z);
        vertex.normal = Float4(0.0f, 0.0f, 1.0f, 0.0f);
        vertex.tangent = Float4(1.0f, 0.0f, 0.0f, 1.0f);
        vertex.uv0 = Float2U(0.5f, 0.5f);
        vertex.color = Float4(1.0f, 1.0f, 1.0f, 1.0f);
        return vertex;
    };

    // Closed unit cube so each planar cut leaves a closable boundary loop for caps.
    const NWB::Impl::CsgDeformVertex inputVertices[] = {
        makeVertex(-1.0f, -1.0f, -1.0f),
        makeVertex(1.0f, -1.0f, -1.0f),
        makeVertex(1.0f, 1.0f, -1.0f),
        makeVertex(-1.0f, 1.0f, -1.0f),
        makeVertex(-1.0f, -1.0f, 1.0f),
        makeVertex(1.0f, -1.0f, 1.0f),
        makeVertex(1.0f, 1.0f, 1.0f),
        makeVertex(-1.0f, 1.0f, 1.0f),
    };
    const NWB::Impl::CsgDeformTriangle inputTriangles[] = {
        { { 0u, 1u, 2u } }, { { 0u, 2u, 3u } },
        { { 4u, 6u, 5u } }, { { 4u, 7u, 6u } },
        { { 0u, 4u, 5u } }, { { 0u, 5u, 1u } },
        { { 2u, 6u, 7u } }, { { 2u, 7u, 3u } },
        { { 0u, 3u, 7u } }, { { 0u, 7u, 4u } },
        { { 1u, 5u, 6u } }, { { 1u, 6u, 2u } },
    };

    // Two sequential plane cuts: keep x >= -0.5, then keep y >= -0.5.
    // Plane SDF keeps distance >= 0 with parameter0 = (normal, distance).
    NWB::Impl::CsgDeformCutDesc cuts[2u];
    cuts[0u].active = true;
    cuts[0u].shape.shapeType = Name("engine/csg/plane");
    cuts[0u].shape.worldToShape = ::Float34Identity();
    cuts[0u].shape.parameter0 = Float4(1.0f, 0.0f, 0.0f, 0.5f);
    cuts[1u].active = true;
    cuts[1u].shape.shapeType = Name("engine/csg/plane");
    cuts[1u].shape.worldToShape = ::Float34Identity();
    cuts[1u].shape.parameter0 = Float4(0.0f, 1.0f, 0.0f, 0.5f);

    const NWB::Impl::CsgDeformBuildOptions options;

    const NWB::Impl::CsgDeformViability viability = NWB::Impl::CheckCsgDeformCutsViability(
        scratchArena,
        inputVertices,
        8u,
        inputTriangles,
        12u,
        cuts,
        2u,
        options
    );
    EXPECT_TRUE(viability.viable);
    EXPECT_EQ(viability.reason, NWB::Impl::CsgDeformViabilityReason::Ok);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::ScratchArena> previewVertices(scratchArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    NWB::Impl::CsgDeformStats previewStats;
    EXPECT_TRUE(NWB::Impl::PreviewCsgDeformCuts(
        scratchArena,
        inputVertices,
        8u,
        inputTriangles,
        12u,
        cuts,
        2u,
        options,
        previewVertices,
        previewTriangles,
        previewStats
    ));
    EXPECT_GT(previewVertices.size(), 8u);
    EXPECT_GT(previewTriangles.size(), 0u);
    EXPECT_EQ(previewStats.appliedCutCount, 2u);
    EXPECT_GT(previewStats.capTriangleCount, 0u);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> commitVertices(commitArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> commitTriangles(commitArena);
    NWB::Impl::CsgDeformStats commitStats;
    EXPECT_TRUE(NWB::Impl::CommitCsgDeformCuts(
        scratchArena,
        commitArena,
        inputVertices,
        8u,
        inputTriangles,
        12u,
        cuts,
        2u,
        options,
        commitVertices,
        commitTriangles,
        commitStats
    ));
    EXPECT_EQ(commitVertices.size(), previewVertices.size());
    EXPECT_EQ(commitTriangles.size(), previewTriangles.size());
    EXPECT_EQ(commitStats.outputVertexCount, previewStats.outputVertexCount);
    EXPECT_EQ(commitStats.outputTriangleCount, previewStats.outputTriangleCount);
    EXPECT_EQ(commitStats.capTriangleCount, previewStats.capTriangleCount);

    // Sequential order matters: the second cut refines the first cut's output.
    // Every triangle-referenced kept vertex must satisfy both half-spaces.
    // (Unreferenced source verts are retained verbatim and never welded.)
    for(const NWB::Impl::CsgDeformTriangle& triangle : commitTriangles){
        for(const u32 index : triangle.indices){
            ASSERT_LT(index, commitVertices.size());
            EXPECT_GE(commitVertices[index].position.x, -0.5001f);
            EXPECT_GE(commitVertices[index].position.y, -0.5001f);
        }
    }
    // Seam-safe rebuild never welds or drops source verts: originals survive verbatim.
    EXPECT_EQ(commitVertices[0u].position.x, -1.0f);
    EXPECT_EQ(commitVertices[0u].position.y, -1.0f);
    EXPECT_EQ(commitVertices[2u].position.x, 1.0f);
    EXPECT_EQ(commitVertices[2u].position.y, 1.0f);
    // The second cut refines the first cut's output: at least one split vertex sits on x == -0.5.
    bool foundCutWall = false;
    for(const NWB::Impl::CsgDeformVertex& vertex : commitVertices){
        if(vertex.position.x > -0.5001f && vertex.position.x < -0.4999f)
            foundCutWall = true;
    }
    EXPECT_TRUE(foundCutWall);
}

TEST(Csg, CsgDeformCutViabilityRejectsDegenerateCommit){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Core::Alloc::GlobalArena commitArena(s_ScratchArena);

    NWB::Impl::CsgDeformVertex vertex;
    vertex.position = Float3U(-5.0f, 0.0f, 0.0f);
    vertex.normal = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    vertex.tangent = Float4(1.0f, 0.0f, 0.0f, 1.0f);
    vertex.uv0 = Float2U(0.0f, 0.0f);
    vertex.color = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    const NWB::Impl::CsgDeformVertex inputVertices[] = { vertex, vertex, vertex };
    const NWB::Impl::CsgDeformTriangle inputTriangles[] = { { { 0u, 1u, 2u } } };

    // Cut keeps x >= 0; the whole triangle sits at x == -5, fully outside the kept
    // half-space, so both preview and commit must agree on NoKeptGeometry failure.
    NWB::Impl::CsgDeformCutDesc cut;
    cut.active = true;
    cut.shape.shapeType = Name("engine/csg/plane");
    cut.shape.worldToShape = ::Float34Identity();
    cut.shape.parameter0 = Float4(1.0f, 0.0f, 0.0f, 0.0f);

    const NWB::Impl::CsgDeformBuildOptions options;
    const NWB::Impl::CsgDeformViability viability = NWB::Impl::CheckCsgDeformCutsViability(
        scratchArena,
        inputVertices,
        3u,
        inputTriangles,
        1u,
        &cut,
        1u,
        options
    );
    EXPECT_FALSE(viability.viable);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::ScratchArena> previewVertices(scratchArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    NWB::Impl::CsgDeformStats previewStats;
    EXPECT_FALSE(NWB::Impl::PreviewCsgDeformCuts(
        scratchArena,
        inputVertices,
        3u,
        inputTriangles,
        1u,
        &cut,
        1u,
        options,
        previewVertices,
        previewTriangles,
        previewStats
    ));

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> commitVertices(commitArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> commitTriangles(commitArena);
    NWB::Impl::CsgDeformStats commitStats;
    EXPECT_FALSE(NWB::Impl::CommitCsgDeformCuts(
        scratchArena,
        commitArena,
        inputVertices,
        3u,
        inputTriangles,
        1u,
        &cut,
        1u,
        options,
        commitVertices,
        commitTriangles,
        commitStats
    ));
    EXPECT_TRUE(commitVertices.empty());
    EXPECT_TRUE(commitTriangles.empty());
}




};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

