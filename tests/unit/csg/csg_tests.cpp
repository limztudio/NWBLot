// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/ecs_test_world.h>
#include <tests/unit/csg/name_cases.adhd.h>

#include <impl/ecs_csg/module.h>
#include <impl/ecs_csg/deform_edit.h>

#include <core/common/module.h>
#include <core/ecs/module.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ProjectCsgGroupA = "project/csg/group_a";
static constexpr AStringView s_EngineCsgBox = "engine/csg/box";
static constexpr AStringView s_EngineCsgPlane = "engine/csg/plane";
static constexpr AStringView s_ProjectCsgGroupB = "project/csg/group_b";
static constexpr AStringView s_EngineCsgCapsule = "engine/csg/capsule";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;

inline constexpr Name s_ScratchArena = NWB::Tests::CsgCases::s_OneOffNameTestsCsgScratch;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<NWB::Impl::CsgReceiverDrawState> ResolveTestCsgReceiverDrawState(const NWB::Core::ECS::EntityID entity, const NWB::Impl::CsgReceiverPass::Enum receiverPass, NWB::Core::Alloc::ScratchArena& scratchArena, TestWorld& testWorld){
    const NWB::Impl::CsgFrameReceiverLookup receiverLookup(testWorld.world, scratchArena);
    return receiverLookup.resolveReceiverDrawState(entity, receiverPass);
}

struct TestCsgVisibilityFilter{
    NWB::Core::ECS::EntityID hiddenEntity = NWB::Core::ECS::s_InvalidEntityId;
};

static b8 TestCsgReceiverVisible(
    [[maybe_unused]] NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID entity,
    [[maybe_unused]] const NWB::Impl::CsgReceiverKind::Enum receiverKind,
    [[maybe_unused]] const NWB::Impl::CsgReceiverComponent& receiver,
    void* userData
)noexcept{
    const auto* filter = static_cast<const TestCsgVisibilityFilter*>(userData);
    return !filter || entity != filter->hiddenEntity;
}

TEST(Csg, CsgFrameSkipsIncompleteDisabledHiddenAndUnmatchedInputs){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    {
        TestWorld testWorld;
        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }

    {
        TestWorld testWorld;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);

        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
    }


    {
        TestWorld testWorld;

        auto disabledReceiverEntity = testWorld.world.createEntity();
        auto& disabledReceiver = disabledReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        disabledReceiver.receiverGroup = Name(s_ProjectCsgGroupA);
        disabledReceiver.enabled = false;

        auto nonMatchingReceiverEntity = testWorld.world.createEntity();
        auto& nonMatchingReceiver = nonMatchingReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        nonMatchingReceiver.receiverGroup = Name(s_ProjectCsgGroupB);

        auto inactiveCutterEntity = testWorld.world.createEntity();
        auto& inactiveCutter = inactiveCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        inactiveCutter.receiverGroup = Name(s_ProjectCsgGroupB);
        inactiveCutter.shapeType = Name(s_EngineCsgBox);
        inactiveCutter.active = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        EXPECT_TRUE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }


    {
        TestWorld testWorld;

        auto hiddenReceiverEntity = testWorld.world.createEntity();
        auto& hiddenReceiver = hiddenReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        hiddenReceiver.receiverGroup = Name(s_ProjectCsgGroupA);

        auto visibleReceiverEntity = testWorld.world.createEntity();
        auto& visibleReceiver = visibleReceiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        visibleReceiver.receiverGroup = Name(s_ProjectCsgGroupA);

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgCapsule);

        TestCsgVisibilityFilter filter;
        filter.hiddenEntity = hiddenReceiverEntity.id();

        NWB::Impl::CsgFrameBuildDesc desc;
        desc.receiverVisible = &TestCsgReceiverVisible;
        desc.receiverVisibleUserData = &filter;

        const NWB::Impl::CsgFrameState state = NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena, desc);

        EXPECT_FALSE(state.empty());
        EXPECT_EQ(state.receiverCount, 1u);
        EXPECT_EQ(state.cutterCount, 1u);
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);
        receiver.affectTransparentPass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        NWB::Impl::CsgFrameBuildDesc desc;
        desc.includeOpaquePass = false;

        const NWB::Impl::CsgFrameState state = NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena, desc);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }
}

TEST(Csg, ReceiverLookupExcludesIncompleteInactiveUntypedAndWrongPassInputs){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);

        const auto drawStateResult1 = ResolveTestCsgReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque, scratchArena, testWorld);
        EXPECT_FALSE(drawStateResult1);
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);

        auto boxCutterEntity = testWorld.world.createEntity();
        auto& boxCutter = boxCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        boxCutter.receiverGroup = Name(s_ProjectCsgGroupA);
        boxCutter.shapeType = Name(s_EngineCsgBox);

        auto sphereCutterEntity = testWorld.world.createEntity();
        auto& sphereCutter = sphereCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        sphereCutter.receiverGroup = Name(s_ProjectCsgGroupA);
        sphereCutter.shapeType = Name(NWB::Tests::CsgCases::s_CsgInputNameEngineCsgSphere);

        auto inactiveCutterEntity = testWorld.world.createEntity();
        auto& inactiveCutter = inactiveCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        inactiveCutter.receiverGroup = Name(s_ProjectCsgGroupA);
        inactiveCutter.shapeType = Name(s_EngineCsgCapsule);
        inactiveCutter.active = false;

        auto untypedCutterEntity = testWorld.world.createEntity();
        auto& untypedCutter = untypedCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        untypedCutter.receiverGroup = Name(s_ProjectCsgGroupA);

        auto otherGroupCutterEntity = testWorld.world.createEntity();
        auto& otherGroupCutter = otherGroupCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        otherGroupCutter.receiverGroup = Name(s_ProjectCsgGroupB);
        otherGroupCutter.shapeType = Name(s_EngineCsgBox);

        const auto opaqueDrawStateResult2 = ResolveTestCsgReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque, scratchArena, testWorld);
        ASSERT_TRUE(opaqueDrawStateResult2);
        const auto& opaqueDrawState = *opaqueDrawStateResult2;
        EXPECT_EQ(opaqueDrawState.cutterCount, 2u);

        usize resolvedCutterCount = 0u;
        {
            const NWB::Impl::CsgFrameReceiverLookup receiverLookup(testWorld.world, scratchArena);
            const auto resolvedDrawStateResult1 = receiverLookup.resolveReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque);
            ASSERT_TRUE(resolvedDrawStateResult1);
            const auto& resolvedDrawState = *resolvedDrawStateResult1;
            receiverLookup.forEachReceiverCutter(resolvedDrawState, [&](const NWB::Core::ECS::EntityID, const NWB::Impl::CsgCutterComponent& resolvedCutter){
                EXPECT_EQ(resolvedCutter.receiverGroup, receiver.receiverGroup);
                ++resolvedCutterCount;
            });
        }
        EXPECT_EQ(resolvedCutterCount, 2u);

    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);
        receiver.affectTransparentPass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        const auto opaqueDrawStateResult3 = ResolveTestCsgReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque, scratchArena, testWorld);
        ASSERT_TRUE(opaqueDrawStateResult3);

        const auto transparentDrawStateResult4 = ResolveTestCsgReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Transparent, scratchArena, testWorld);
        EXPECT_FALSE(transparentDrawStateResult4);
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);
        receiver.affectOpaquePass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgCapsule);

        const auto opaqueDrawStateResult5 = ResolveTestCsgReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque, scratchArena, testWorld);
        EXPECT_FALSE(opaqueDrawStateResult5);

        const auto transparentDrawStateResult6 = ResolveTestCsgReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Transparent, scratchArena, testWorld);
        ASSERT_TRUE(transparentDrawStateResult6);
        const auto& transparentDrawState = *transparentDrawStateResult6;
        EXPECT_EQ(transparentDrawState.cutterCount, 1u);
    }
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

