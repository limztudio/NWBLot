// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/shadow_cutter_snapshot.h>
#include <impl/ecs_render/shadow/light_space_csg.h>
#include <impl/ecs_render/shadow/light_space_capture_history.h>
#include <impl/ecs_render/raytrace/instance_material.h>
#include <impl/ecs_render/mesh/mesh_system.h>

#include <core/ecs/entity.h>

#include <tests/common/ecs_test_world.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_shadow_snapshot_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

inline constexpr Name s_ReceiverGroup("tests/csg_shadow/group");
inline constexpr Name s_OtherGroup("tests/csg_shadow/other");
inline constexpr Name s_CustomShape("tests/csg_shadow/custom");

struct SnapshotContext{
    Tests::EcsTestWorld testWorld;
    Core::Alloc::ScratchArena scratch{ Name("tests/csg_shadow/scratch") };
    CsgShapeRegistry registry{ testWorld.arena };
    CsgShadowSnapshot snapshot{ testWorld.arena };


public:
    [[nodiscard]] bool build(const CsgShadowReceiverInput* receivers, const usize receiverCount){
        return BuildCsgShadowSnapshot(testWorld.world, registry, receivers, receiverCount, scratch, snapshot);
    }

    [[nodiscard]] Core::ECS::EntityID addReceiver(const Name group = s_ReceiverGroup, const bool skinned = false){
        auto entity = testWorld.world.createEntity();
        if(skinned)
            entity.addComponent<SkinnedCsgMeshComponent>().receiverGroup = group;
        else
            entity.addComponent<StaticCsgMeshComponent>().receiverGroup = group;
        return entity.id();
    }

    [[nodiscard]] Core::ECS::EntityID addCutter(const Name shape, const Name group = s_ReceiverGroup){
        auto entity = testWorld.world.createEntity();
        auto& cutter = entity.addComponent<CsgCutterComponent>(testWorld.arena);
        cutter.receiverGroup = group;
        cutter.shapeType = shape;
        return entity.id();
    }
};

[[nodiscard]] CsgShadowReceiverInput ReceiverInput(const Core::ECS::EntityID entity){
    CsgShadowReceiverInput input;
    input.entity = entity;
    input.worldMin = Float3U(-2.f, -2.f, -2.f);
    input.worldMax = Float3U(2.f, 2.f, 2.f);
    input.boundsValid = true;
    return input;
}

void SetParameters(CsgCutterComponent& cutter, const Float4& parameter){
    cutter.parameterBytes.resize(sizeof(parameter));
    NWB_MEMCPY(cutter.parameterBytes.data(), cutter.parameterBytes.size(), &parameter, sizeof(parameter));
}

[[nodiscard]] bool CustomBounds(
    const SIMDMatrix& shapeToWorld, const u8* parameterBytes, const usize parameterByteSize,
    SIMDVector& outMin, SIMDVector& outMax, bool& outFinite
){
    static_cast<void>(parameterBytes);
    if(parameterByteSize != 0u)
        return false;
    outFinite = true;
    return AabbTests::Transform(shapeToWorld, VectorReplicate(-1.f), VectorReplicate(1.f), outMin, outMax);
}

[[nodiscard]] bool RegisterCustomShape(SnapshotContext& context, const Name name = s_CustomShape){
    CsgShapeTypeDesc description;
    description.name = name;
    description.shaderModule = Name("tests/csg_shadow/evaluator");
    description.shaderModuleInclude = ACompactString("tests/csg_shadow/evaluator.slangi");
    description.boundsCallback = &CustomBounds;
    CsgShapeTypeId typeId = s_InvalidCsgShapeTypeId;
    return context.registry.registerShapeType(description, typeId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CsgShadowSnapshot, EmptyAndDisabledReceiversKeepTheZeroCsgPath){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    CsgShadowReceiverInput input = ReceiverInput(context.addReceiver());
    ASSERT_TRUE(context.build(&input, 1u));
    EXPECT_FALSE(context.snapshot.hasCsg);
    EXPECT_TRUE(context.snapshot.receiverRanges.empty());
    EXPECT_TRUE(context.snapshot.cutters.empty());
    EXPECT_EQ(context.snapshot.identity, 0u);

    const Core::ECS::EntityID cutterEntity = context.addCutter(s_CsgSphereShapeName);
    ASSERT_TRUE(context.build(&input, 1u));
    ASSERT_TRUE(context.snapshot.hasCsg);
    auto* receiver = context.testWorld.world.tryGetComponent<StaticCsgMeshComponent>(input.entity);
    ASSERT_NE(receiver, nullptr);
    receiver->enabled = false;
    ASSERT_TRUE(context.build(&input, 1u));
    EXPECT_FALSE(context.snapshot.hasCsg);
    EXPECT_TRUE(context.snapshot.receiverRanges.empty());
    EXPECT_TRUE(context.snapshot.cutters.empty());

    receiver->enabled = true;
    auto* cutter = context.testWorld.world.tryGetComponent<CsgCutterComponent>(cutterEntity);
    ASSERT_NE(cutter, nullptr);
    cutter->active = false;
    ASSERT_TRUE(context.build(&input, 1u));
    EXPECT_FALSE(context.snapshot.hasCsg);
    EXPECT_TRUE(context.snapshot.receiverRanges.empty());
    EXPECT_FALSE(context.build(nullptr, 1u));
    EXPECT_TRUE(context.build(nullptr, 0u));
}

TEST(CsgShadowSnapshot, ResolvesGroupsAndMaterialPassesInShadowInstanceOrder){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    const Core::ECS::EntityID receiverEntity = context.addReceiver();
    const Core::ECS::EntityID otherReceiver = context.addReceiver(s_OtherGroup);
    const Core::ECS::EntityID cutterEntity = context.addCutter(s_CsgBoxShapeName);
    ASSERT_TRUE(cutterEntity.valid());
    auto* receiver = context.testWorld.world.tryGetComponent<StaticCsgMeshComponent>(receiverEntity);
    ASSERT_NE(receiver, nullptr);
    receiver->affectTransparentPass = false;
    CsgShadowReceiverInput inputs[] = {
        ReceiverInput(otherReceiver), ReceiverInput(receiverEntity), ReceiverInput(receiverEntity)
    };
    inputs[1].receiverPass = CsgReceiverPass::Transparent;
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    ASSERT_TRUE(context.snapshot.hasCsg);
    ASSERT_EQ(context.snapshot.receiverRanges.size(), LengthOf(inputs));
    EXPECT_EQ(context.snapshot.receiverRanges[0].flags, 0u);
    EXPECT_EQ(context.snapshot.receiverRanges[1].flags, 0u);
    EXPECT_EQ(context.snapshot.receiverRanges[2].flags, NWB_CSG_SHADOW_RECEIVER_ACTIVE);
    EXPECT_EQ(context.snapshot.receiverRanges[2].cutterCount, 1u);
    ASSERT_EQ(context.snapshot.cutters.size(), 1u);
}

TEST(CsgShadowSnapshot, CullsOnlyFiniteOutsideCuttersWithTrustedReceiverBounds){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    CsgShadowReceiverInput inputs[] = {
        ReceiverInput(context.addReceiver()), ReceiverInput(context.addReceiver(s_ReceiverGroup, true))
    };
    inputs[1].boundsValid = false;
    const Core::ECS::EntityID sphereEntity = context.addCutter(s_CsgSphereShapeName);
    const Core::ECS::EntityID planeEntity = context.addCutter(s_CsgPlaneShapeName);
    ASSERT_TRUE(planeEntity.valid());
    auto* sphere = context.testWorld.world.tryGetComponent<CsgCutterComponent>(sphereEntity);
    ASSERT_NE(sphere, nullptr);
    StoreFloat(MatrixTranslation(10.f, 0.f, 0.f), sphere->shapeToWorld);
    StoreFloat(MatrixTranslation(-10.f, 0.f, 0.f), sphere->worldToShape);
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    ASSERT_EQ(context.snapshot.receiverRanges.size(), LengthOf(inputs));
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 1u);
    EXPECT_EQ(context.snapshot.receiverRanges[1].cutterCount, 2u);
    EXPECT_EQ(context.snapshot.cutters[0].shapeType, NWB_CSG_SHADOW_SHAPE_PLANE);
    EXPECT_EQ(context.snapshot.cutters.size(), 3u);

    inputs[0].worldMin = Float3U(2.f, 2.f, 2.f);
    inputs[0].worldMax = Float3U(-2.f, -2.f, -2.f);
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 2u);
}

TEST(CsgShadowSnapshot, CustomEvaluatorUsesConservativeUncutFallbackOnlyWhenItCanOverlap){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    ASSERT_TRUE(RegisterCustomShape(context));
    const CsgShadowReceiverInput input = ReceiverInput(context.addReceiver());
    const Core::ECS::EntityID boxEntity = context.addCutter(s_CsgBoxShapeName);
    const Core::ECS::EntityID customEntity = context.addCutter(s_CustomShape);
    ASSERT_TRUE(boxEntity.valid());
    ASSERT_TRUE(context.build(&input, 1u));
    ASSERT_TRUE(context.snapshot.hasCsg);
    ASSERT_EQ(context.snapshot.receiverRanges.size(), 1u);
    EXPECT_EQ(
        context.snapshot.receiverRanges[0].flags,
        NWB_CSG_SHADOW_RECEIVER_ACTIVE | NWB_CSG_SHADOW_RECEIVER_UNSUPPORTED
    );
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 0u);
    EXPECT_TRUE(context.snapshot.cutters.empty());

    auto* custom = context.testWorld.world.tryGetComponent<CsgCutterComponent>(customEntity);
    ASSERT_NE(custom, nullptr);
    StoreFloat(MatrixTranslation(10.f, 0.f, 0.f), custom->shapeToWorld);
    StoreFloat(MatrixTranslation(-10.f, 0.f, 0.f), custom->worldToShape);
    ASSERT_TRUE(context.build(&input, 1u));
    EXPECT_EQ(context.snapshot.receiverRanges[0].flags, NWB_CSG_SHADOW_RECEIVER_ACTIVE);
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 1u);
}

TEST(CsgShadowSnapshot, CutterLimitFallsBackForTheWholeReceiverWithoutPartialSubtraction){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    const CsgShadowReceiverInput input = ReceiverInput(context.addReceiver());
    for(u32 index = 0u; index < NWB_CSG_SHADOW_MAX_CUTTERS; ++index){
        const Core::ECS::EntityID cutterEntity = context.addCutter(s_CsgPlaneShapeName);
        ASSERT_TRUE(cutterEntity.valid());
    }
    ASSERT_TRUE(context.build(&input, 1u));
    ASSERT_EQ(context.snapshot.cutters.size(), NWB_CSG_SHADOW_MAX_CUTTERS);
    EXPECT_EQ(context.snapshot.receiverRanges[0].flags, NWB_CSG_SHADOW_RECEIVER_ACTIVE);
    const Core::ECS::EntityID overflowCutter = context.addCutter(s_CsgPlaneShapeName);
    ASSERT_TRUE(overflowCutter.valid());
    ASSERT_TRUE(context.build(&input, 1u));
    EXPECT_EQ(
        context.snapshot.receiverRanges[0].flags,
        NWB_CSG_SHADOW_RECEIVER_ACTIVE | NWB_CSG_SHADOW_RECEIVER_UNSUPPORTED
    );
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 0u);
    EXPECT_TRUE(context.snapshot.cutters.empty());
}

TEST(CsgShadowSnapshot, MalformedAndUnregisteredCuttersFollowRasterSkipSemantics){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    const CsgShadowReceiverInput input = ReceiverInput(context.addReceiver());
    const Core::ECS::EntityID planeEntity = context.addCutter(s_CsgPlaneShapeName);
    const Core::ECS::EntityID malformedEntity = context.addCutter(s_CsgBoxShapeName);
    const Core::ECS::EntityID missingEntity = context.addCutter(s_CustomShape);
    ASSERT_TRUE(planeEntity.valid());
    ASSERT_TRUE(missingEntity.valid());
    auto* malformed = context.testWorld.world.tryGetComponent<CsgCutterComponent>(malformedEntity);
    ASSERT_NE(malformed, nullptr);
    malformed->parameterBytes.push_back(0u);
    ASSERT_TRUE(context.build(&input, 1u));
    ASSERT_EQ(context.snapshot.cutters.size(), 1u);
    EXPECT_EQ(context.snapshot.cutters[0].shapeType, NWB_CSG_SHADOW_SHAPE_PLANE);
    EXPECT_EQ(context.snapshot.receiverRanges[0].flags, NWB_CSG_SHADOW_RECEIVER_ACTIVE);
}

TEST(CsgShadowSnapshot, RepeatedSameSizeBuildsClearDisabledAndUnsupportedReceiverRanges){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    ASSERT_TRUE(RegisterCustomShape(context));
    CsgShadowReceiverInput inputs[] = {
        ReceiverInput(context.addReceiver()), ReceiverInput(context.addReceiver(s_OtherGroup))
    };
    const Core::ECS::EntityID firstCutter = context.addCutter(s_CsgBoxShapeName);
    const Core::ECS::EntityID secondCutter = context.addCutter(s_CsgSphereShapeName, s_OtherGroup);
    ASSERT_TRUE(secondCutter.valid());
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    ASSERT_EQ(context.snapshot.receiverRanges.size(), LengthOf(inputs));
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 1u);
    EXPECT_EQ(context.snapshot.receiverRanges[1].firstCutter, 1u);
    auto* receiver = context.testWorld.world.tryGetComponent<StaticCsgMeshComponent>(inputs[0].entity);
    ASSERT_NE(receiver, nullptr);
    receiver->enabled = false;
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_EQ(context.snapshot.receiverRanges[0].flags, 0u);
    EXPECT_EQ(context.snapshot.receiverRanges[0].firstCutter, 0u);
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 0u);
    EXPECT_EQ(context.snapshot.receiverRanges[1].firstCutter, 0u);
    EXPECT_EQ(context.snapshot.cutters.size(), 1u);

    receiver->enabled = true;
    auto* cutter = context.testWorld.world.tryGetComponent<CsgCutterComponent>(firstCutter);
    ASSERT_NE(cutter, nullptr);
    cutter->shapeType = s_CustomShape;
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_NE(context.snapshot.receiverRanges[0].flags & NWB_CSG_SHADOW_RECEIVER_UNSUPPORTED, 0u);
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 0u);
    cutter->shapeType = s_CsgBoxShapeName;
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_EQ(context.snapshot.receiverRanges[0].flags, NWB_CSG_SHADOW_RECEIVER_ACTIVE);
    EXPECT_EQ(context.snapshot.receiverRanges[0].cutterCount, 1u);
    EXPECT_EQ(context.snapshot.receiverRanges[1].firstCutter, 1u);
    EXPECT_EQ(context.snapshot.cutters.size(), 2u);
}

TEST(CsgShadowSnapshot, HardwareRootExclusionAndDisabledReceiversClearCapturedState){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    const Core::ECS::EntityID receiver = context.addReceiver();
    const Core::ECS::EntityID cutter = context.addCutter(s_CsgBoxShapeName);
    ASSERT_TRUE(cutter.valid());
    LightSpaceCsgState state(context.testWorld.arena);
    ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
    mesh.meshletPrimitiveIndexCount = 3u;
    mesh.csgLocalBounds.minBounds = Float3Int(-2.f, -2.f, -2.f, s_CsgBoundsValidFlag | s_CsgBoundsFiniteFlag);
    mesh.csgLocalBounds.maxBounds = Float3Int(2.f, 2.f, 2.f, 0);
    BeginLightSpaceCsgGather(state, context.testWorld.world, 1u, false);
    AppendLightSpaceCsgReceiver(state, receiver, false, MatrixIdentity(), mesh);
    ASSERT_TRUE(FinishLightSpaceCsgGather(state, context.testWorld.world, context.registry, context.scratch));
    ASSERT_TRUE(state.snapshot.hasCsg);
    constexpr u32 s_SoftwareRootSlot = 17u;
    mesh.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::Make(Core::GpuDescriptorClass::StorageBuffer, s_SoftwareRootSlot);
    BeginLightSpaceCsgGather(state, context.testWorld.world, 1u, true);
    AppendLightSpaceCsgReceiver(state, receiver, false, MatrixIdentity(), mesh);
    ASSERT_TRUE(FinishLightSpaceCsgGather(state, context.testWorld.world, context.registry, context.scratch));
    EXPECT_EQ(state.instances[0].meshRootSlot, Limit<u32>::s_Max);
    BeginLightSpaceCsgGather(state, context.testWorld.world, 1u, false);
    AppendLightSpaceCsgReceiver(state, receiver, false, MatrixIdentity(), mesh);
    ASSERT_TRUE(FinishLightSpaceCsgGather(state, context.testWorld.world, context.registry, context.scratch));
    EXPECT_EQ(state.instances[0].meshRootSlot, s_SoftwareRootSlot);

    auto* cutterComponent = context.testWorld.world.tryGetComponent<CsgCutterComponent>(cutter);
    ASSERT_NE(cutterComponent, nullptr);
    cutterComponent->active = false;
    BeginLightSpaceCsgGather(state, context.testWorld.world, 1u, false);
    AppendLightSpaceCsgReceiver(state, receiver, false, MatrixIdentity(), mesh);
    ASSERT_TRUE(FinishLightSpaceCsgGather(state, context.testWorld.world, context.registry, context.scratch));
    EXPECT_FALSE(state.snapshot.hasCsg);
    EXPECT_EQ(state.snapshot.identity, 0u);
    EXPECT_TRUE(state.bytes.empty());
}

TEST(CsgShadowSnapshot, BoundedCaptureReuseAllowsOnlyTransformLagAndRefreshesWithinItsCadence){
    struct CaptureCase{
        SoftwareShadowCaptureCadence::Enum cadence;
        u32 refreshInterval;
    };
    const CaptureCase cases[] = {
        { SoftwareShadowCaptureCadence::EveryFrame, 1u },
        { SoftwareShadowCaptureCadence::ReuseOneFrame, 2u },
        { SoftwareShadowCaptureCadence::ReuseTwoFrames, 3u },
    };
    for(const auto& captureCase : cases){
        SnapshotContext context;
        ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
        const Core::ECS::EntityID receiver = context.addReceiver(s_ReceiverGroup, true);
        const Core::ECS::EntityID cutterEntity = context.addCutter(s_CsgBoxShapeName);
        auto* cutter = context.testWorld.world.tryGetComponent<CsgCutterComponent>(cutterEntity);
        ASSERT_NE(cutter, nullptr);
        LightSpaceCsgState state(context.testWorld.arena);
        ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
        mesh.runtimeMesh = true;
        mesh.runtimeMeshVersion = 3u;
        mesh.runtimeGeometryContentRevision = 7u;
        mesh.meshletPrimitiveIndexCount = 3u;
        NwbRtInstanceMaterialGpu material;
        InstanceGpuData instance;
        LightSpaceCaptureHistory history;
        u64 previousExact = 0u;
        u64 previousContent = 0u;
        for(u32 frame = 0u; frame < 7u; ++frame){
            const f32 x = static_cast<f32>(frame) * 0.25f;
            StoreFloat(MatrixTranslation(x, 0.f, 0.f), cutter->shapeToWorld);
            StoreFloat(MatrixTranslation(-x, 0.f, 0.f), cutter->worldToShape);
            instance.translation.x = x;
            StoreFloat(QuaternionRotationRollPitchYaw(0.f, x, 0.f), instance.rotation);
            BeginLightSpaceCsgGather(state, context.testWorld.world, 1u, true);
            AppendLightSpaceCsgReceiver(state, receiver, false, MatrixTranslation(x, 0.f, 0.f), mesh);
            ASSERT_TRUE(FinishLightSpaceCsgGather(state, context.testWorld.world, context.registry, context.scratch));
            ASSERT_TRUE(state.snapshot.hasCsg);
            ASSERT_TRUE(state.captureGeometryTrusted);
            const u64 content = BuildLightSpaceCsgCaptureIdentity(state, &material, &instance, 1u, nullptr, 0u, nullptr, 0u);
            if(frame != 0u){
                EXPECT_NE(state.snapshot.identity, previousExact);
                EXPECT_EQ(content, previousContent);
            }
            previousExact = state.snapshot.identity;
            previousContent = content;
            history.beginFrame();
            const LightSpaceCaptureIdentity identity{ content, 11u, 13u, state.captureGeometryTrusted };
            const auto ticket = history.prepare(identity, captureCase.cadence);
            EXPECT_EQ(ticket.reuse, (frame % captureCase.refreshInterval) != 0u);
            if(!ticket.reuse)
                history.recordCapture(ticket);
            ASSERT_TRUE(history.accept(ticket));
        }
    }
}

TEST(CsgShadowSnapshot, CaptureReuseRefreshesForContentTopologyMembershipAndBindingChanges){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    Core::ECS::EntityID receivers[] = { context.addReceiver(), context.addReceiver() };
    const Core::ECS::EntityID cutterEntity = context.addCutter(s_CsgBoxShapeName);
    auto* cutter = context.testWorld.world.tryGetComponent<CsgCutterComponent>(cutterEntity);
    ASSERT_NE(cutter, nullptr);
    LightSpaceCsgState state(context.testWorld.arena);
    ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
    mesh.runtimeMesh = true;
    mesh.runtimeMeshVersion = 3u;
    mesh.runtimeGeometryContentRevision = 7u;
    mesh.meshletPrimitiveIndexCount = 3u;
    NwbRtInstanceMaterialGpu materials[LengthOf(receivers)];
    InstanceGpuData instances[LengthOf(receivers)];
    u8 materialBytes[] = { 1u, 2u, 3u, 4u };
    const auto build = [&](){
        BeginLightSpaceCsgGather(state, context.testWorld.world, LengthOf(receivers), false);
        for(const auto receiver : receivers)
            AppendLightSpaceCsgReceiver(state, receiver, false, MatrixIdentity(), mesh);
        return FinishLightSpaceCsgGather(state, context.testWorld.world, context.registry, context.scratch);
    };
    const auto identity = [&](){
        return LightSpaceCaptureIdentity{
            BuildLightSpaceCsgCaptureIdentity(state, materials, instances, LengthOf(receivers),
                materialBytes, sizeof(materialBytes), nullptr, 0u),
            11u, 13u, state.captureGeometryTrusted,
        };
    };
    const auto expectRefresh = [&](const auto& change){
        ASSERT_TRUE(build());
        ASSERT_TRUE(state.snapshot.hasCsg);
        const SoftwareShadowCaptureCadence::Enum cadences[] = {
            SoftwareShadowCaptureCadence::ReuseOneFrame, SoftwareShadowCaptureCadence::ReuseTwoFrames,
        };
        LightSpaceCaptureHistory histories[LengthOf(cadences)];
        for(usize index = 0u; index < LengthOf(cadences); ++index){
            auto& history = histories[index];
            history.beginFrame();
            const auto captured = history.prepare(identity(), cadences[index]);
            history.recordCapture(captured);
            ASSERT_TRUE(history.accept(captured));
        }
        change();
        ASSERT_TRUE(build());
        for(usize index = 0u; index < LengthOf(cadences); ++index){
            histories[index].beginFrame();
            EXPECT_FALSE(histories[index].prepare(identity(), cadences[index]).reuse);
        }
    };
    expectRefresh([&](){ ++mesh.runtimeGeometryContentRevision; });
    expectRefresh([&](){ ++mesh.runtimeMeshVersion; });
    expectRefresh([&](){ mesh.meshletPrimitiveIndexCount += 3u; });
    expectRefresh([&](){ mesh.meshName = Name("tests/csg_shadow/replacement_mesh"); });
    expectRefresh([&](){
        mesh.runtimeLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Make(Core::GpuDescriptorClass::StorageBuffer, 17u);
    });
    expectRefresh([&](){ materials[0].surfaceDispatchId = 5u; });
    expectRefresh([&](){ materials[0].positionSlot = 9u; });
    expectRefresh([&](){ materials[0].flags = RtInstanceMaterialFlag::Transparent; });
    expectRefresh([&](){ ++instances[0].translation.w; });
    expectRefresh([&](){ ++instances[0].geometryHeapSlots[0]; });
    expectRefresh([&](){ ++materialBytes[0]; });
    expectRefresh([&](){ SetParameters(*cutter, Float4(0.5f, 1.f, 1.f, 0.f)); });
    expectRefresh([&](){ Swap(receivers[0], receivers[1]); });
    expectRefresh([&](){ ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry)); });
    expectRefresh([&](){ cutter->active = false; });
}

TEST(CsgShadowSnapshot, UnknownOrPendingRuntimePoseCannotReuseAnAcceptedCapture){
    for(const auto cadence : { SoftwareShadowCaptureCadence::ReuseOneFrame, SoftwareShadowCaptureCadence::ReuseTwoFrames }){
        SnapshotContext context;
        ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
        const Core::ECS::EntityID receiver = context.addReceiver(s_ReceiverGroup, true);
        const Core::ECS::EntityID cutter = context.addCutter(s_CsgBoxShapeName);
        ASSERT_TRUE(cutter.valid());
        LightSpaceCsgState state(context.testWorld.arena);
        ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
        mesh.runtimeMesh = true;
        mesh.meshletPrimitiveIndexCount = 3u;
        NwbRtInstanceMaterialGpu material;
        InstanceGpuData instance;
        LightSpaceCaptureHistory history;
        for(const u64 revision : { 7ull, 0ull, 0ull, 8ull }){
            mesh.runtimeGeometryContentRevision = revision;
            BeginLightSpaceCsgGather(state, context.testWorld.world, 1u, false);
            AppendLightSpaceCsgReceiver(state, receiver, false, MatrixIdentity(), mesh);
            ASSERT_TRUE(FinishLightSpaceCsgGather(state, context.testWorld.world, context.registry, context.scratch));
            EXPECT_EQ(state.captureGeometryTrusted, revision != 0u);
            const LightSpaceCaptureIdentity identity{
                BuildLightSpaceCsgCaptureIdentity(state, &material, &instance, 1u, nullptr, 0u, nullptr, 0u),
                11u, 13u, state.captureGeometryTrusted,
            };
            history.beginFrame();
            const auto ticket = history.prepare(identity, cadence);
            EXPECT_FALSE(ticket.reuse);
            history.recordCapture(ticket);
            ASSERT_TRUE(history.accept(ticket));
        }
    }
}

TEST(CsgShadowSnapshot, IdentityTracksEffectiveCutterEditsRegistryAndInstanceOrder){
    SnapshotContext context;
    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    CsgShadowReceiverInput inputs[] = {
        ReceiverInput(context.addReceiver()), ReceiverInput(context.addReceiver(s_OtherGroup))
    };
    const Core::ECS::EntityID cutterEntity = context.addCutter(s_CsgBoxShapeName);
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    u64 identity = context.snapshot.identity;
    EXPECT_NE(identity, 0u);
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_EQ(context.snapshot.identity, identity);

    auto* cutter = context.testWorld.world.tryGetComponent<CsgCutterComponent>(cutterEntity);
    ASSERT_NE(cutter, nullptr);
    SetParameters(*cutter, Float4(0.5f, 1.f, 1.f, 0.f));
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_NE(context.snapshot.identity, identity);
    identity = context.snapshot.identity;
    StoreFloat(MatrixTranslation(0.5f, 0.f, 0.f), cutter->shapeToWorld);
    StoreFloat(MatrixTranslation(-0.5f, 0.f, 0.f), cutter->worldToShape);
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_NE(context.snapshot.identity, identity);
    identity = context.snapshot.identity;

    ASSERT_TRUE(RegisterBuiltInCsgShapeTypes(context.registry));
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_NE(context.snapshot.identity, identity);
    identity = context.snapshot.identity;
    Swap(inputs[0], inputs[1]);
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_NE(context.snapshot.identity, identity);
    identity = context.snapshot.identity;
    inputs[1].worldMin.x -= 1.f;
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_NE(context.snapshot.identity, identity);

    cutter->receiverGroup = s_NameNone;
    ASSERT_TRUE(context.build(inputs, LengthOf(inputs)));
    EXPECT_FALSE(context.snapshot.hasCsg);
    EXPECT_EQ(context.snapshot.identity, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

