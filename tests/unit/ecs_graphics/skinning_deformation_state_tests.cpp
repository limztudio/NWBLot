// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/deformation_state.h>
#include <impl/ecs_mesh/skinning/runtime_instance.h>
#include <impl/ecs_mesh/skinning/runtime_mesh_liveness.h>

#include <tests/common/graphics_metadata_test_objects.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_deformation_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

inline constexpr Core::BufferHandle MeshSkinningRuntimeInstance::* s_InstanceBuffers[] = {
    &MeshSkinningRuntimeInstance::restPositionBuffer,
    &MeshSkinningRuntimeInstance::restNormalBuffer,
    &MeshSkinningRuntimeInstance::restTangentBuffer,
    &MeshSkinningRuntimeInstance::skinnedPositionBuffer,
    &MeshSkinningRuntimeInstance::skinnedNormalBuffer,
    &MeshSkinningRuntimeInstance::skinnedTangentBuffer,
    &MeshSkinningRuntimeInstance::uv0Buffer,
    &MeshSkinningRuntimeInstance::colorBuffer,
    &MeshSkinningRuntimeInstance::meshletDescBuffer,
    &MeshSkinningRuntimeInstance::meshletBoundsBuffer,
    &MeshSkinningRuntimeInstance::meshletPositionRefDeltaBuffer,
    &MeshSkinningRuntimeInstance::meshletAttributeRefDeltaBuffer,
    &MeshSkinningRuntimeInstance::meshletLocalVertexRefBuffer,
    &MeshSkinningRuntimeInstance::meshletPrimitiveIndexBuffer,
    &MeshSkinningRuntimeInstance::attributeSkinBuffer,
    &MeshSkinningRuntimeInstance::triangleIndexBuffer,
    &MeshSkinningRuntimeInstance::attributeBuffer,
    &MeshSkinningRuntimeInstance::meshletLocalBoundsBuffer,
    &MeshSkinningRuntimeInstance::localBoundsBuffer,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformationContext{
    Core::Alloc::GlobalArena arena{ Name("tests/skinning_deformation/owner") };
    Core::GraphicsAllocator graphicsAllocator{ arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Array<Core::BufferHandle, 2u> buffers;
    MeshSkinningRuntimeInstance instance{ arena };
    MeshSkinningDeformationInputs inputs;

    DeformationContext(){
        for(Core::BufferHandle& buffer : buffers){
            Core::Buffer* const raw = Tests::NewMetadataOnlyBuffer(
                arena, context, allocator, Core::BufferDesc{}.setByteSize(256u)
            );
            buffer = Core::BufferHandle(raw, Core::BufferHandle::deleter_type(&arena), AdoptRef);
        }
        instance.handle.value = 17u;
        instance.entity = Core::ECS::EntityID(3u);
        instance.sourceName = Name("tests/skinning_deformation/source");
        instance.localBounds.minBounds.w = s_RuntimeMeshBoundsValidFlag | s_RuntimeMeshBoundsFiniteFlag;
        instance.restPositions.resize(1u);
        instance.restNormals.resize(1u);
        instance.restTangents.resize(1u);
        instance.uv0.resize(1u);
        instance.colors.resize(1u);
        instance.meshlets.resize(1u);
        instance.meshletBounds.resize(1u);
        instance.meshletPositionRefDeltas.resize(1u);
        instance.meshletAttributeRefDeltas.resize(1u);
        instance.meshletLocalVertexRefs.resize(1u);
        instance.meshletPrimitiveIndices.resize(3u);
        instance.attributeSkins.resize(1u);
        instance.skin.resize(1u);
        instance.meshletPositionRefCount = 1u;
        instance.meshletAttributeRefCount = 1u;
        instance.editRevision = 7u;
        instance.dirtyFlags = RuntimeMeshDirtyFlag::None;
        for(const auto member : s_InstanceBuffers)
            instance.*member = buffers[0u];
        inputs.resourceSlotsBuffer = buffers[0u];
        inputs.editRevision = instance.editRevision;
    }
};

TEST(SkinningDeformationState, FirstUseAndRejectedSubmissionStayUnknownUntilLatestCandidateIsAccepted){
    DeformationContext context;
    MeshSkinningDeformationState& state = context.instance.deformationState;
    const SkeletonJointMatrix joint = Float34Identity();
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
    const u64 rejected = state.stage(context.inputs, &joint, 1u);
    ASSERT_NE(rejected, 0u);
    EXPECT_EQ(state.contentRevision(), 0u);
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
    const u64 retry = state.stage(context.inputs, &joint, 1u);
    ASSERT_NE(retry, 0u);
    EXPECT_NE(retry, rejected);
    EXPECT_FALSE(state.accept(rejected));
    EXPECT_FALSE(state.accept(0u));
    EXPECT_EQ(state.contentRevision(), 0u);
    ASSERT_TRUE(state.accept(retry));
    EXPECT_EQ(state.contentRevision(), 1u);
    EXPECT_FALSE(state.accept(retry));
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    EXPECT_EQ(state.contentRevision(), 1u);
}

TEST(SkinningDeformationState, OwnsResolvedPaletteAndComparesContentsInsteadOfPaletteAddresses){
    DeformationContext context;
    MeshSkinningDeformationState& state = context.instance.deformationState;
    Array<SkeletonJointMatrix, 2u> joints{ Float34Identity(), Float34Identity() };
    joints[1u].rows[2u].w = 3.5f;
    const Array<SkeletonJointMatrix, 2u> original = joints;
    const u64 candidate = state.stage(context.inputs, joints.data(), joints.size());
    ASSERT_NE(candidate, 0u);
    joints[1u].rows[2u].w = 3.75f;
    ASSERT_TRUE(state.accept(candidate));
    state.refreshCurrent(context.inputs, joints.data(), joints.size(), false);
    EXPECT_EQ(state.contentRevision(), 0u);
    state.refreshCurrent(context.inputs, original.data(), original.size(), false);
    EXPECT_EQ(state.contentRevision(), 1u);
    state.refreshCurrent(context.inputs, original.data(), 1u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
    state.refreshCurrent(context.inputs, nullptr, 2u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
}

TEST(SkinningDeformationState, MeshEditsModesDirtyInputsAndResourceGenerationsRequireNewProduction){
    DeformationContext context;
    MeshSkinningDeformationState& state = context.instance.deformationState;
    const SkeletonJointMatrix joint = Float34Identity();
    const u64 initial = state.stage(context.inputs, &joint, 1u);
    ASSERT_TRUE(state.accept(initial));
    for(u32 change = 0u; change < 5u; ++change){
        MeshSkinningDeformationInputs changed = context.inputs;
        switch(change){
        case 0u: ++changed.editRevision; break;
        case 1u: changed.skinningMode = SkeletonSkinningMode::DualQuaternion; break;
        case 2u: changed.resourceSlotsBuffer = context.buffers[1u]; break;
        case 3u: changed.resourceSlotsBuffer = nullptr; break;
        default: break;
        }
        state.refreshCurrent(changed, &joint, 1u, change == 4u);
        EXPECT_EQ(state.contentRevision(), 0u) << change;
        state.refreshCurrent(context.inputs, &joint, 1u, false);
        EXPECT_EQ(state.contentRevision(), 1u) << change;
    }
    MeshSkinningDeformationInputs replacement = context.inputs;
    replacement.resourceSlotsBuffer = context.buffers[1u];
    const u64 candidate = state.stage(replacement, &joint, 1u);
    ASSERT_TRUE(state.accept(candidate));
    EXPECT_EQ(state.contentRevision(), 2u);
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
    state.refreshCurrent(replacement, &joint, 1u, false);
    EXPECT_EQ(state.contentRevision(), 2u);
}

TEST(SkinningDeformationState, ReturningToAcceptedPoseAfterRejectionCancelsStalePublication){
    DeformationContext context;
    MeshSkinningDeformationState& state = context.instance.deformationState;
    const SkeletonJointMatrix original = Float34Identity();
    const u64 initial = state.stage(context.inputs, &original, 1u);
    ASSERT_TRUE(state.accept(initial));
    SkeletonJointMatrix changed = original;
    changed.rows[0u].w = 4.0f;
    state.refreshCurrent(context.inputs, &changed, 1u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
    const u64 rejected = state.stage(context.inputs, &changed, 1u);
    ASSERT_NE(rejected, 0u);
    state.refreshCurrent(context.inputs, &original, 1u, false);
    EXPECT_EQ(state.contentRevision(), 1u);
    EXPECT_FALSE(state.accept(rejected));
    EXPECT_EQ(state.contentRevision(), 1u);
    state.refreshCurrent(context.inputs, &changed, 1u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
    const u64 retry = state.stage(context.inputs, &changed, 1u);
    ASSERT_TRUE(state.accept(retry));
    EXPECT_EQ(state.contentRevision(), 2u);
}

TEST(SkinningDeformationState, PoseRemovalProducesRestGeometryOnceAndPoseReintroductionInvalidatesIt){
    DeformationContext context;
    MeshSkinningDeformationState& state = context.instance.deformationState;
    SkeletonJointMatrix joint = Float34Identity();
    joint.rows[1u].w = 2.0f;
    const u64 initial = state.stage(context.inputs, &joint, 1u);
    ASSERT_TRUE(state.accept(initial));
    state.refreshCurrent(context.inputs, nullptr, 0u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
    const u64 rest = state.stage(context.inputs, nullptr, 0u);
    ASSERT_TRUE(state.accept(rest));
    EXPECT_EQ(state.contentRevision(), 2u);
    state.refreshCurrent(context.inputs, nullptr, 0u, false);
    EXPECT_EQ(state.contentRevision(), 2u);
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    EXPECT_EQ(state.contentRevision(), 0u);
}

TEST(SkinningDeformationState, EverySourceAndOutputBufferReplacementChangesResourceIdentity){
    DeformationContext context;
    const MeshSkinningResourceBuffers original = CaptureMeshSkinningResourceBuffers(context.instance);
    for(const auto member : s_InstanceBuffers){
        context.instance.*member = context.buffers[1u];
        EXPECT_NE(CaptureMeshSkinningResourceBuffers(context.instance), original);
        context.instance.*member = context.buffers[0u];
        EXPECT_EQ(CaptureMeshSkinningResourceBuffers(context.instance), original);
    }
    context.instance.attributeBuffer = nullptr;
    EXPECT_NE(CaptureMeshSkinningResourceBuffers(context.instance), original);
}

TEST(SkinningDeformationState, RuntimeDescriptionPublishesPendingZeroWithoutChangingMeshIdentity){
    DeformationContext context;
    MeshSkinningRuntimeInstance& instance = context.instance;
    MeshSkinningDeformationState& state = instance.deformationState;
    ASSERT_TRUE(instance.valid());
    RuntimeMeshDesc initial;
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(instance.entity, instance.handle, &instance, false, false, initial));
    EXPECT_EQ(initial.geometryContentRevision, 0u);
    EXPECT_EQ(initial.localBoundsBuffer, nullptr);
    SkeletonJointMatrix joint = Float34Identity();
    const u64 candidate = state.stage(context.inputs, &joint, 1u);
    ASSERT_TRUE(state.accept(candidate));
    RuntimeMeshDesc accepted;
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(instance.entity, instance.handle, &instance, false, false, accepted));
    EXPECT_EQ(accepted.geometryContentRevision, 1u);
    EXPECT_EQ(accepted.localBoundsBuffer, instance.localBoundsBuffer);
    EXPECT_EQ(accepted.meshKey, initial.meshKey);
    EXPECT_EQ(accepted.version, initial.version);
    joint.rows[2u].w = 1.0f;
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    RuntimeMeshDesc pending;
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(instance.entity, instance.handle, &instance, false, false, pending));
    EXPECT_EQ(pending.geometryContentRevision, 0u);
    EXPECT_EQ(pending.localBoundsBuffer, nullptr);
    EXPECT_EQ(pending.meshKey, accepted.meshKey);
    EXPECT_EQ(pending.version, accepted.version);
    EXPECT_EQ(pending.positionBuffer, accepted.positionBuffer);

    // An unaccepted new pose cannot borrow the previously accepted bounds, even with the same output allocation.
    const u64 rejected = state.stage(context.inputs, &joint, 1u);
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(instance.entity, instance.handle, &instance, false, false, pending));
    EXPECT_EQ(pending.localBoundsBuffer, nullptr);
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    const u64 retry = state.stage(context.inputs, &joint, 1u);
    EXPECT_FALSE(state.accept(rejected));
    ASSERT_TRUE(state.accept(retry));
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(instance.entity, instance.handle, &instance, false, false, accepted));
    EXPECT_EQ(accepted.localBoundsBuffer, instance.localBoundsBuffer);
    EXPECT_EQ(accepted.geometryContentRevision, 2u);
    state.refreshCurrent(context.inputs, &joint, 1u, false);
    RuntimeMeshDesc unchanged;
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(instance.entity, instance.handle, &instance, false, false, unchanged));
    EXPECT_EQ(unchanged.localBoundsBuffer, accepted.localBoundsBuffer);
    EXPECT_EQ(unchanged.geometryContentRevision, accepted.geometryContentRevision);
    state.refreshCurrent(context.inputs, &joint, 1u, true);
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(instance.entity, instance.handle, &instance, false, false, pending));
    EXPECT_EQ(pending.localBoundsBuffer, nullptr);
}

TEST(SkinningDeformationState, AcceptanceAndRepeatedUnchangedQueriesDoNotAllocate){
    DeformationContext context;
    MeshSkinningDeformationState& state = context.instance.deformationState;
    const Array<SkeletonJointMatrix, 4u> joints{
        Float34Identity(), Float34Identity(), Float34Identity(), Float34Identity(),
    };
    for(u32 generation = 1u; generation <= 2u; ++generation){
        const u64 candidate = state.stage(context.inputs, joints.data(), joints.size());
        const ArenaMemoryStats before = context.arena.memoryStats();
        ASSERT_TRUE(state.accept(candidate));
        for(u32 repeat = 0u; repeat < 32u; ++repeat){
            state.refreshCurrent(context.inputs, joints.data(), joints.size(), false);
            EXPECT_EQ(state.contentRevision(), generation);
        }
        const ArenaMemoryStats after = context.arena.memoryStats();
        EXPECT_EQ(after.allocationCount, before.allocationCount);
        EXPECT_EQ(after.usedBytes, before.usedBytes);
        EXPECT_EQ(after.reservedBytes, before.reservedBytes);
        EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

