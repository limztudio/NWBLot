// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/ecs_test_world.h>

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


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;

inline constexpr Name s_ScratchArena("tests/csg/scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Impl::CsgFrameState BuildTestCsgFrameState(NWB::Core::Alloc::ScratchArena& scratchArena, TestWorld& testWorld, const NWB::Impl::CsgFrameBuildDesc& desc = NWB::Impl::CsgFrameBuildDesc{}){
    return NWB::Impl::BuildCsgFrameState(testWorld.world, scratchArena, desc);
}

static Expected<NWB::Impl::CsgReceiverDrawState> ResolveTestCsgReceiverDrawState(NWB::Core::Alloc::ScratchArena& scratchArena, TestWorld& testWorld, const NWB::Core::ECS::EntityID entity, const NWB::Impl::CsgReceiverPass::Enum receiverPass){
    const NWB::Impl::CsgFrameReceiverLookup receiverLookup(testWorld.world, scratchArena);
    return receiverLookup.resolveReceiverDrawState(entity, receiverPass);
}

struct TestCsgVisibilityFilter{
    NWB::Core::ECS::EntityID hiddenEntity = NWB::Core::ECS::s_InvalidEntityId;
};

static bool TestCsgReceiverVisible(NWB::Core::ECS::World& world, const NWB::Core::ECS::EntityID entity, const NWB::Impl::CsgReceiverKind::Enum receiverKind, const NWB::Impl::CsgReceiverComponent& receiver, void* userData)noexcept{
    static_cast<void>(world);
    static_cast<void>(receiverKind);
    static_cast<void>(receiver);

    const auto* filter = static_cast<const TestCsgVisibilityFilter*>(userData);
    return !filter || entity != filter->hiddenEntity;
}

TEST(Csg, ShapeIdentityRetains64BitFoldForCookedAssetsAndRejectsAnEmptyName){
    using namespace NWB::Impl;
    EXPECT_FALSE(CsgShapeTypeIdFromName(Name{}));
    const auto plane = CsgShapeTypeIdFromName(s_CsgPlaneShapeName);
    const auto box = CsgShapeTypeIdFromName(s_CsgBoxShapeName);
    const auto sphere = CsgShapeTypeIdFromName(s_CsgSphereShapeName);
    const auto capsule = CsgShapeTypeIdFromName(s_CsgCapsuleShapeName);
    ASSERT_TRUE(plane);
    ASSERT_TRUE(box);
    ASSERT_TRUE(sphere);
    ASSERT_TRUE(capsule);
    EXPECT_EQ(*plane, 0x92f0021au);
    EXPECT_EQ(*box, 0xb957a584u);
    EXPECT_EQ(*sphere, 0x56176cbfu);
    EXPECT_EQ(*capsule, 0xe0e38c7bu);
}

TEST(Csg, CsgFrameSkipsIncompleteDisabledHiddenAndUnmatchedInputs){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    {
        TestWorld testWorld;
        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(scratchArena, testWorld);

        EXPECT_TRUE(state.empty());
        EXPECT_FALSE(state.hasAnyWork);
        EXPECT_EQ(state.receiverCount, 0u);
        EXPECT_EQ(state.cutterCount, 0u);
    }

    {
        TestWorld testWorld;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        EXPECT_FALSE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(scratchArena, testWorld);

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
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(scratchArena, testWorld);

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
        auto& inactiveCutter = inactiveCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        inactiveCutter.receiverGroup = Name(s_ProjectCsgGroupB);
        inactiveCutter.shapeType = Name(s_EngineCsgBox);
        inactiveCutter.active = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        EXPECT_TRUE(NWB::Impl::HasCsgFrameCandidates(testWorld.world));
        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(scratchArena, testWorld);

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
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgCapsule);

        TestCsgVisibilityFilter filter;
        filter.hiddenEntity = hiddenReceiverEntity.id();

        NWB::Impl::CsgFrameBuildDesc desc;
        desc.receiverVisible = &TestCsgReceiverVisible;
        desc.receiverVisibleUserData = &filter;

        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(scratchArena, testWorld, desc);

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
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        NWB::Impl::CsgFrameBuildDesc desc;
        desc.includeOpaquePass = false;

        const NWB::Impl::CsgFrameState state = BuildTestCsgFrameState(scratchArena, testWorld, desc);

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

        const auto drawStateResult1 = ResolveTestCsgReceiverDrawState(scratchArena, testWorld, receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque);
        EXPECT_FALSE(drawStateResult1);
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);

        auto boxCutterEntity = testWorld.world.createEntity();
        auto& boxCutter = boxCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        boxCutter.receiverGroup = Name(s_ProjectCsgGroupA);
        boxCutter.shapeType = Name(s_EngineCsgBox);

        auto sphereCutterEntity = testWorld.world.createEntity();
        auto& sphereCutter = sphereCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        sphereCutter.receiverGroup = Name(s_ProjectCsgGroupA);
        sphereCutter.shapeType = Name("engine/csg/sphere");

        auto inactiveCutterEntity = testWorld.world.createEntity();
        auto& inactiveCutter = inactiveCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        inactiveCutter.receiverGroup = Name(s_ProjectCsgGroupA);
        inactiveCutter.shapeType = Name(s_EngineCsgCapsule);
        inactiveCutter.active = false;

        auto untypedCutterEntity = testWorld.world.createEntity();
        auto& untypedCutter = untypedCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        untypedCutter.receiverGroup = Name(s_ProjectCsgGroupA);

        auto otherGroupCutterEntity = testWorld.world.createEntity();
        auto& otherGroupCutter = otherGroupCutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        otherGroupCutter.receiverGroup = Name(s_ProjectCsgGroupB);
        otherGroupCutter.shapeType = Name(s_EngineCsgBox);

        const auto opaqueDrawStateResult2 = ResolveTestCsgReceiverDrawState(scratchArena, testWorld, receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque);
        ASSERT_TRUE(opaqueDrawStateResult2);
        const auto& opaqueDrawState = *opaqueDrawStateResult2;
        EXPECT_EQ(opaqueDrawState.cutterCount, s_ExpectedDualCount);

        usize resolvedCutterCount = 0u;
        {
            const NWB::Impl::CsgFrameReceiverLookup receiverLookup(testWorld.world, scratchArena);
            const auto resolvedDrawStateResult1 = receiverLookup.resolveReceiverDrawState(receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque);
            ASSERT_TRUE(resolvedDrawStateResult1);
            const auto& resolvedDrawState = *resolvedDrawStateResult1;
            receiverLookup.forEachReceiverCutter(resolvedDrawState, [&](const NWB::Core::ECS::EntityID, const NWB::Impl::CsgCutterComponent& resolvedCutter){
                EXPECT_EQ(resolvedCutter.receiverGroup, receiver.receiverGroup);
                ++resolvedCutterCount;
            }
            );
        }
        EXPECT_EQ(resolvedCutterCount, s_ExpectedDualCount);

    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);
        receiver.affectTransparentPass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgBox);

        const auto opaqueDrawStateResult3 = ResolveTestCsgReceiverDrawState(scratchArena, testWorld, receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque);
        ASSERT_TRUE(opaqueDrawStateResult3);

        const auto transparentDrawStateResult4 = ResolveTestCsgReceiverDrawState(scratchArena, testWorld, receiverEntity.id(), NWB::Impl::CsgReceiverPass::Transparent);
        EXPECT_FALSE(transparentDrawStateResult4);
    }

    {
        TestWorld testWorld;

        auto receiverEntity = testWorld.world.createEntity();
        auto& receiver = receiverEntity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
        receiver.receiverGroup = Name(s_ProjectCsgGroupA);
        receiver.affectOpaquePass = false;

        auto cutterEntity = testWorld.world.createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = Name(s_ProjectCsgGroupA);
        cutter.shapeType = Name(s_EngineCsgCapsule);

        const auto opaqueDrawStateResult5 = ResolveTestCsgReceiverDrawState(scratchArena, testWorld, receiverEntity.id(), NWB::Impl::CsgReceiverPass::Opaque);
        EXPECT_FALSE(opaqueDrawStateResult5);

        const auto transparentDrawStateResult6 = ResolveTestCsgReceiverDrawState(scratchArena, testWorld, receiverEntity.id(), NWB::Impl::CsgReceiverPass::Transparent);
        ASSERT_TRUE(transparentDrawStateResult6);
        const auto& transparentDrawState = *transparentDrawStateResult6;
        EXPECT_EQ(transparentDrawState.cutterCount, 1u);
    }
}

TEST(Csg, RepeatedBuiltInRegistrationPreservesTypeIdentityAndCount){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(Name(s_EngineCsgBox));
    ASSERT_NE(boxId, NWB::Impl::s_InvalidCsgShapeTypeId);

    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    EXPECT_EQ(registry.shapeTypeCount(), 4u);
    EXPECT_EQ(registry.findShapeTypeId(Name(s_EngineCsgBox)), boxId);
}

TEST(Csg, ShapeBoundsRejectShortPayloadAndKeepPlaneUnbounded){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    const SIMDMatrix shapeToWorldMatrix = MatrixTranslation(10.0f, -5.0f, 1.0f);
    NWB::Impl::CsgPlaneShapeParameters planeParameters;
    const auto planeBounds = registry.buildShapeBounds(Name(s_EngineCsgPlane), shapeToWorldMatrix, reinterpret_cast<const u8*>(&planeParameters), sizeof(planeParameters));
    ASSERT_TRUE(planeBounds);
    EXPECT_FALSE(planeBounds->finiteBounds);

    NWB::Impl::CsgBoxShapeParameters boxParameters;
    EXPECT_FALSE(registry.buildShapeBounds(Name(s_EngineCsgBox), shapeToWorldMatrix, reinterpret_cast<const u8*>(&boxParameters), sizeof(boxParameters) - 1u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Csg, SequentialCutsPreserveEarlierHalfSpaceAndUnweldedSourceVertices){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Core::Alloc::GlobalArena commitArena(s_ScratchArena);

    auto makeVertex = [](const f32 x, const f32 y, const f32 z)noexcept{
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
        { { 0u, 1u, s_ExpectedDualCount } }, { { 0u, s_ExpectedDualCount, 3u } },
        { { 4u, 6u, 5u } }, { { 4u, 7u, 6u } },
        { { 0u, 4u, 5u } }, { { 0u, 5u, 1u } },
        { { s_ExpectedDualCount, 6u, 7u } }, { { s_ExpectedDualCount, 7u, 3u } },
        { { 0u, 3u, 7u } }, { { 0u, 7u, 4u } },
        { { 1u, 5u, 6u } }, { { 1u, 6u, s_ExpectedDualCount } },
    };

    // Two sequential plane cuts: keep x >= -0.5, then keep y >= -0.5.
    // Plane SDF keeps distance >= 0 with parameter0 = (normal, distance).
    NWB::Impl::CsgDeformCutDesc cuts[s_ThirdElementIndex];
    cuts[0u].active = true;
    cuts[0u].shape.shapeType = Name(s_EngineCsgPlane);
    cuts[0u].shape.worldToShape = ::MatrixIdentity<Float34>();
    cuts[0u].shape.parameter0 = Float4(1.0f, 0.0f, 0.0f, 0.5f);
    cuts[1u].active = true;
    cuts[1u].shape.shapeType = Name(s_EngineCsgPlane);
    cuts[1u].shape.worldToShape = ::MatrixIdentity<Float34>();
    cuts[1u].shape.parameter0 = Float4(0.0f, 1.0f, 0.0f, 0.5f);

    const NWB::Impl::CsgDeformBuildOptions options;

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> commitVertices(commitArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> commitTriangles(commitArena);
    const auto commit = NWB::Impl::CommitCsgDeformCuts(scratchArena, commitArena, MakeNotNull(inputVertices), 8u, MakeNotNull(inputTriangles), 12u, cuts, s_ExpectedDualCount, options, commitVertices, commitTriangles);
    ASSERT_TRUE(commit);
    EXPECT_EQ(commit->appliedCutCount, s_ExpectedDualCount);
    ASSERT_FALSE(commitTriangles.empty());
    ASSERT_GE(commitVertices.size(), 8u);

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
    EXPECT_EQ(commitVertices[s_ThirdElementIndex].position.x, 1.0f);
    EXPECT_EQ(commitVertices[s_ThirdElementIndex].position.y, 1.0f);
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
    const NWB::Impl::CsgDeformTriangle inputTriangles[] = { { { 0u, 1u, s_ExpectedDualCount } } };

    // Cut keeps x >= 0; the whole triangle sits at x == -5, fully outside the kept half-space, so both preview and commit must agree on NoKeptGeometry failure.
    NWB::Impl::CsgDeformCutDesc cut;
    cut.active = true;
    cut.shape.shapeType = Name(s_EngineCsgPlane);
    cut.shape.worldToShape = ::MatrixIdentity<Float34>();
    cut.shape.parameter0 = Float4(1.0f, 0.0f, 0.0f, 0.0f);

    const NWB::Impl::CsgDeformBuildOptions options;
    const NWB::Impl::CsgDeformViability viability = NWB::Impl::CheckCsgDeformCutsViability(scratchArena, MakeNotNull(inputVertices), 3u, MakeNotNull(inputTriangles), 1u, &cut, 1u, options);
    EXPECT_FALSE(viability.viable);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::ScratchArena> previewVertices(scratchArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    const auto preview = NWB::Impl::PreviewCsgDeformCuts(scratchArena, MakeNotNull(inputVertices), 3u, MakeNotNull(inputTriangles), 1u, &cut, 1u, options, previewVertices, previewTriangles);
    ASSERT_FALSE(preview);
    EXPECT_EQ(preview.error().reason, NWB::Impl::CsgDeformViabilityReason::NoKeptGeometry);
    EXPECT_EQ(preview.error().stats.inputVertexCount, 3u);
    EXPECT_EQ(preview.error().stats.inputTriangleCount, 1u);
    EXPECT_EQ(preview.error().stats.outputVertexCount, 0u);
    EXPECT_EQ(preview.error().stats.outputTriangleCount, 0u);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> commitVertices(commitArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> commitTriangles(commitArena);
    const auto commit = NWB::Impl::CommitCsgDeformCuts(scratchArena, commitArena, MakeNotNull(inputVertices), 3u, MakeNotNull(inputTriangles), 1u, &cut, 1u, options, commitVertices, commitTriangles);
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().reason, NWB::Impl::CsgDeformViabilityReason::NoKeptGeometry);
    EXPECT_EQ(commit.error().stats.inputVertexCount, 3u);
    EXPECT_EQ(commit.error().stats.inputTriangleCount, 1u);
    EXPECT_EQ(commit.error().stats.outputVertexCount, 0u);
    EXPECT_EQ(commit.error().stats.outputTriangleCount, 0u);
    EXPECT_TRUE(commitVertices.empty());
    EXPECT_TRUE(commitTriangles.empty());

    NWB::Core::Alloc::GlobalArena otherArena(s_ScratchArena);
    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> wrongVertices(otherArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> wrongTriangles(otherArena);
    wrongVertices.push_back(vertex);
    wrongTriangles.push_back(inputTriangles[0u]);
    const auto wrongArena = NWB::Impl::CommitCsgDeformCuts(scratchArena, commitArena, MakeNotNull(inputVertices), 3u, MakeNotNull(inputTriangles), 1u, &cut, 1u, options, wrongVertices, wrongTriangles);
    ASSERT_FALSE(wrongArena);
    EXPECT_EQ(wrongArena.error().reason, NWB::Impl::CsgDeformViabilityReason::InvalidOutputArena);
    EXPECT_EQ(wrongArena.error().stats.inputVertexCount, 0u);
    EXPECT_EQ(wrongArena.error().stats.inputTriangleCount, 0u);
    ASSERT_EQ(wrongVertices.size(), 1u);
    ASSERT_EQ(wrongTriangles.size(), 1u);
    EXPECT_EQ(wrongVertices.front().position.x, vertex.position.x);
    EXPECT_EQ(wrongTriangles.front().indices[0u], inputTriangles[0u].indices[0u]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

