// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/skin_payload.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_payload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_ScratchArena("tests/skinning_payload/scratch");

using NWB::Impl::MeshSkinningPayload::BuildRuntimeSkinPayload;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Impl::SkeletonJointMatrix MakeTranslationJoint(const f32 x, const f32 y, const f32 z){
    NWB::Impl::SkeletonJointMatrix joint = Float34Identity();
    joint.rows[0].w = x;
    joint.rows[1].w = y;
    joint.rows[2].w = z;
    return joint;
}

NWB::Impl::SkinInfluence4 MakeSkinInfluence(const u16 joint){
    NWB::Impl::SkinInfluence4 influence{};
    influence.joint[0] = joint;
    influence.weight.x = 1.0f;
    return influence;
}

TEST(SkinningPayload, EmptyPoseClearsPreviousDualQuaternionPayloadAndUsesCurrentPalette){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skin.assign(4u, MakeSkinInfluence(1u));
    instance.skeletonJointCount = s_ExpectedDualCount;
    instance.inverseBindMatrices.assign(s_ExpectedDualCount, MakeTranslationJoint(-0.25f, 0.0f, 0.0f));

    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.push_back(MakeTranslationJoint(1.0f, 0.0f, 0.0f));
    palette.joints.push_back(MakeTranslationJoint(0.0f, 2.0f, 0.0f));

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    palette.joints[1].rows[1].w = 4.0f;
    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.parentJoints.push_back(NWB::Impl::s_SkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(MakeTranslationJoint(3.0f, 0.0f, 0.0f));
    pose.localJoints.push_back(MakeTranslationJoint(0.0f, 5.0f, 0.0f));
    pose.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    EXPECT_EQ(payload.resolvedSkinningMode, NWB::Impl::SkeletonSkinningMode::DualQuaternion);

    pose.localJoints.clear();
    pose.parentJoints.clear();
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    EXPECT_EQ(payload.resolvedSkinningMode, NWB::Impl::SkeletonSkinningMode::LinearBlend);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].w, 4.0f);
}

TEST(SkinningPayload, RuntimePayloadClearsWhenSkinOrPoseIsRemoved){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skin.assign(3u, MakeSkinInfluence(0u));
    instance.skeletonJointCount = 1u;
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.push_back(Float34Identity());

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, nullptr, nullptr, payload));
    EXPECT_FALSE(payload.hasActiveSkin());
    EXPECT_TRUE(payload.jointMatrices.empty());

    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    instance.skin.clear();
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    EXPECT_FALSE(payload.hasActiveSkin());
    EXPECT_TRUE(payload.jointMatrices.empty());

    instance.skin.assign(7u, MakeSkinInfluence(0u));
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    palette.joints.clear();
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    EXPECT_FALSE(payload.hasActiveSkin());
    EXPECT_TRUE(payload.jointMatrices.empty());
}

TEST(SkinningPayload, RuntimePayloadFollowsReplacementMeshAndJointCounts){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.handle.value = 13u;
    instance.sourceName = Name("tests/skinning_payload/first_source");
    instance.skin.assign(3u, MakeSkinInfluence(0u));
    instance.skeletonJointCount = 1u;
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.push_back(MakeTranslationJoint(1.0f, 0.0f, 0.0f));

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_EQ(payload.jointMatrices.size(), 1u);

    instance.sourceName = Name("tests/skinning_payload/replacement_source");
    ++instance.editRevision;
    instance.skin.assign(9u, MakeSkinInfluence(s_ExpectedDualCount));
    instance.skeletonJointCount = 3u;
    instance.inverseBindMatrices.assign(3u, MakeTranslationJoint(-2.0f, 0.0f, 0.0f));
    palette.joints.assign(3u, MakeTranslationJoint(7.0f, 0.0f, 0.0f));
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), 3u);
    EXPECT_FLOAT_EQ(payload.jointMatrices[2].rows[0].w, 5.0f);

    ++instance.editRevision;
    instance.skin.assign(s_ExpectedDualCount, MakeSkinInfluence(0u));
    instance.skeletonJointCount = 1u;
    instance.inverseBindMatrices.clear();
    palette.joints.assign(1u, MakeTranslationJoint(11.0f, 0.0f, 0.0f));
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_EQ(payload.jointMatrices.size(), 1u);
    EXPECT_FLOAT_EQ(payload.jointMatrices[0].rows[0].w, 11.0f);
}

TEST(SkinningPayload, EmptySkinProducesEmptyInfluencePayload){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = 5u;
    instance.skin.assign(s_ExpectedDualCount, MakeSkinInfluence(0u));
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    auto influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    ASSERT_TRUE(influences);
    ASSERT_EQ(influences->size(), s_ExpectedDualCount);

    instance.skin.clear();
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    ASSERT_TRUE(influences);
    EXPECT_TRUE(influences->empty());
}

TEST(SkinningPayload, GpuInfluencesPreserveUpperU16JointBitsAndFp32WeightBits){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = static_cast<u32>(Limit<u16>::s_Max) + 1u;
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[1] = Limit<u16>::s_Max;
    skin.joint[2] = 0x8000u;
    skin.joint[3] = 0xfffeu;
    skin.weight = Float4U(BitCast<f32>(0x3e000001u), BitCast<f32>(0x3ebfffffu), 0.5f, -0.0f);
    instance.skin.push_back(skin);

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    const auto influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    ASSERT_TRUE(influences);
    ASSERT_EQ(influences->size(), 1u);
    const auto& gpuSkin = influences->front();
    EXPECT_EQ(gpuSkin.packedJoint[0], 0xffff0000u);
    EXPECT_EQ(gpuSkin.packedJoint[1], 0xfffe8000u);
    for(u32 influenceIndex = 0u; influenceIndex < NWB_SKINNED_MESH_MAX_INFLUENCE_COUNT; ++influenceIndex)
        EXPECT_EQ(BitCast<u32>(gpuSkin.weight.raw[influenceIndex]), BitCast<u32>(skin.weight.raw[influenceIndex]));
}

TEST(SkinningPayload, PaletteCanExceedSkeletonWithoutInverseBindMatrices){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = 1u;
    instance.skin.assign(3u, MakeSkinInfluence(0u));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(3u, MakeTranslationJoint(5.0f, 0.0f, 0.0f));

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), 3u);
    EXPECT_FLOAT_EQ(payload.jointMatrices[2].rows[0].w, 5.0f);
}

TEST(SkinningPayload, RuntimeScratchAllocationDoesNotScaleWithInfluenceCount){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = s_ExpectedDualCount;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(s_ExpectedDualCount, Float34Identity());

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    const ArenaMemoryStats initialMemory = scratchArena.memoryStats();

    instance.skin.assign(65536u, MakeSkinInfluence(1u));
    ++instance.editRevision;
    palette.joints[1].rows[2].w = 7.0f;
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    const ArenaMemoryStats finalMemory = scratchArena.memoryStats();
    EXPECT_EQ(finalMemory.allocationCount, initialMemory.allocationCount);
    EXPECT_EQ(finalMemory.reallocationCount, initialMemory.reallocationCount);
    EXPECT_EQ(finalMemory.usedBytes, initialMemory.usedBytes);
    EXPECT_EQ(payload.skinInfluenceCount, instance.skin.size());
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[2].w, 7.0f);
}

TEST(SkinningPayload, PayloadScopeDestructionReclaimsScratchUsageAcrossMeshes){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = s_ExpectedDualCount;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(s_ExpectedDualCount, Float34Identity());
    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.localJoints.assign(s_ExpectedDualCount, Float34Identity());
    pose.parentJoints.assign(s_ExpectedDualCount, NWB::Impl::s_SkeletonRootParent);

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    {
        NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
        ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
        ASSERT_TRUE(payload.hasActiveSkin());
    }
    const ArenaMemoryStats initialMemory = scratchArena.memoryStats();
    for(u32 meshIndex = 0u; meshIndex < 64u; ++meshIndex){
        NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
        ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, meshIndex % s_ExpectedDualCount == 0u ? &pose : nullptr, payload));
        ASSERT_TRUE(payload.hasActiveSkin());
    }
    const ArenaMemoryStats finalMemory = scratchArena.memoryStats();
    EXPECT_EQ(finalMemory.usedBytes, initialMemory.usedBytes);
}

#if defined(NWB_FINAL)
void ExpectEmptyRuntimePayload(const NWB::Impl::RuntimeSkinPayloadScratch& payload){
    EXPECT_FALSE(payload.hasActiveSkin());
    EXPECT_EQ(payload.skinInfluenceCount, 0u);
    EXPECT_TRUE(payload.jointMatrices.empty());
    EXPECT_TRUE(payload.poseJoints.empty());
}

TEST(SkinningPayload, RuntimePayloadRejectsInvalidDynamicInputs){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistration(logger);
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = s_ExpectedDualCount;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(1u, Float34Identity());

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.joints.assign(s_ExpectedDualCount, Float34Identity());
    palette.skinningMode = Limit<u32>::s_Max;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.skinningMode = NWB::Impl::SkeletonSkinningMode::LinearBlend;
    palette.joints[1].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.joints.assign(s_ExpectedDualCount, Float34Identity());
    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.localJoints.assign(s_ExpectedDualCount, Float34Identity());
    pose.parentJoints.assign(s_ExpectedDualCount, NWB::Impl::s_SkeletonRootParent);
    pose.parentJoints[1] = 1u;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    palette.joints[0].rows[0].x = 2.0f;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.joints.assign(s_ExpectedDualCount, Float34Identity());
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), s_ExpectedDualCount);
    EXPECT_EQ(logger.errorCount(), 5u);
}

TEST(SkinningPayload, InverseBindBoundsAndPartialFailuresLeaveNoActivePayload){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistration(logger);
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = s_ExpectedDualCount;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    instance.inverseBindMatrices.assign(s_ExpectedDualCount, Float34Identity());
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(s_ExpectedDualCount, Float34Identity());

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    palette.joints.push_back(Float34Identity());
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    payload.jointMatrices.assign(1u, Float34Identity());
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinJointPalette(
        instance,
        palette.joints,
        palette.skinningMode,
        payload.jointMatrices
    ));
    EXPECT_TRUE(payload.jointMatrices.empty());

    palette.joints.pop_back();
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    instance.inverseBindMatrices.pop_back();
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    instance.inverseBindMatrices.assign(s_ExpectedDualCount, Float34Identity());
    instance.inverseBindMatrices[1].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    instance.inverseBindMatrices[1] = Float34Identity();
    instance.inverseBindMatrices[1].rows[0].x = 0.001f;
    instance.inverseBindMatrices[1].rows[1].y = 0.001f;
    instance.inverseBindMatrices[1].rows[2].z = 0.001f;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    instance.inverseBindMatrices.assign(s_ExpectedDualCount, Float34Identity());
    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.localJoints.assign(s_ExpectedDualCount, Float34Identity());
    pose.parentJoints.assign(s_ExpectedDualCount, NWB::Impl::s_SkeletonRootParent);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    pose.parentJoints[1] = 1u;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    ExpectEmptyRuntimePayload(payload);

    pose.parentJoints[1] = 0u;
    pose.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    pose.localJoints[1].rows[0].x = 2.0f;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    ExpectEmptyRuntimePayload(payload);

    pose.localJoints[1] = MakeTranslationJoint(0.0f, 6.0f, 0.0f);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), s_ExpectedDualCount);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].y, 3.0f);
    EXPECT_EQ(logger.errorCount(), 7u);
}

TEST(SkinningPayload, StaticInfluenceEncodingRejectsInvalidMeshEditsAndRecovers){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistration(logger);
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = s_ExpectedDualCount;
    instance.skin.assign(s_ExpectedDualCount, MakeSkinInfluence(1u));
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    auto influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    ASSERT_TRUE(influences);
    ASSERT_EQ(influences->size(), s_ExpectedDualCount);

    ++instance.editRevision;
    instance.skin[1].weight.x = 0.5f;
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    EXPECT_FALSE(influences);

    instance.skin[1].weight = Float4U(-0.5f, 1.5f, 0.0f, 0.0f);
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    EXPECT_FALSE(influences);

    instance.skin[1].weight = Float4U(Limit<f32>::s_QuietNaN, 0.0f, 0.0f, 0.0f);
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    EXPECT_FALSE(influences);

    instance.skin[1] = MakeSkinInfluence(1u);
    instance.skin[1].joint[3] = s_ExpectedDualCount;
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    EXPECT_FALSE(influences);

    instance.skin[1] = MakeSkinInfluence(1u);
    instance.skeletonJointCount = 0u;
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    EXPECT_FALSE(influences);

    instance.skeletonJointCount = static_cast<u32>(Limit<u16>::s_Max) + s_ExpectedDualCount;
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    EXPECT_FALSE(influences);

    instance.skeletonJointCount = s_ExpectedDualCount;
    influences = NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, scratchArena);
    ASSERT_TRUE(influences);
    ASSERT_EQ(influences->size(), s_ExpectedDualCount);
    EXPECT_EQ((*influences)[1].packedJoint[0] & NWB_SKINNED_MESH_SKIN_JOINT_MASK, 1u);
    EXPECT_FLOAT_EQ((*influences)[1].weight.x, 1.0f);
    EXPECT_EQ(logger.errorCount(), 6u);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

