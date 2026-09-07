// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/skin_payload.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_payload_tests{


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

TEST(SkinningPayload, RuntimePayloadFollowsPalettePoseAndModeChanges){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skin.assign(4u, MakeSkinInfluence(1u));
    instance.skeletonJointCount = 2u;
    instance.inverseBindMatrices.assign(2u, MakeTranslationJoint(-0.25f, 0.0f, 0.0f));

    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.push_back(MakeTranslationJoint(1.0f, 0.0f, 0.0f));
    palette.joints.push_back(MakeTranslationJoint(0.0f, 2.0f, 0.0f));

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), 2u);
    EXPECT_FLOAT_EQ(payload.jointMatrices[0].rows[0].w, 0.75f);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].w, 2.0f);

    palette.joints[1].rows[1].w = 4.0f;
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].w, 4.0f);

    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.parentJoints.push_back(NWB::Impl::s_SkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(MakeTranslationJoint(3.0f, 0.0f, 0.0f));
    pose.localJoints.push_back(MakeTranslationJoint(0.0f, 5.0f, 0.0f));
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    EXPECT_FLOAT_EQ(payload.jointMatrices[0].rows[0].w, 2.75f);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[0].w, 2.75f);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].w, 5.0f);

    pose.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    EXPECT_EQ(payload.resolvedSkinningMode, NWB::Impl::SkeletonSkinningMode::DualQuaternion);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[0].w, 1.0f);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].x, 1.375f);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].y, 2.5f);

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
    instance.skin.assign(9u, MakeSkinInfluence(2u));
    instance.skeletonJointCount = 3u;
    instance.inverseBindMatrices.assign(3u, MakeTranslationJoint(-2.0f, 0.0f, 0.0f));
    palette.joints.assign(3u, MakeTranslationJoint(7.0f, 0.0f, 0.0f));
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), 3u);
    EXPECT_FLOAT_EQ(payload.jointMatrices[2].rows[0].w, 5.0f);

    ++instance.editRevision;
    instance.skin.assign(2u, MakeSkinInfluence(0u));
    instance.skeletonJointCount = 1u;
    instance.inverseBindMatrices.clear();
    palette.joints.assign(1u, MakeTranslationJoint(11.0f, 0.0f, 0.0f));
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_EQ(payload.jointMatrices.size(), 1u);
    EXPECT_FLOAT_EQ(payload.jointMatrices[0].rows[0].w, 11.0f);
}

TEST(SkinningPayload, StaticInfluencePayloadReflectsEveryMeshEdit){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = 5u;
    instance.skin.assign(2u, MakeSkinInfluence(0u));
    for(u32 index = 0u; index < NWB::Impl::s_SkinInfluenceJointCount; ++index)
        instance.skin[0].joint[index] = static_cast<u16>(index);
    instance.skin[0].weight = Float4U(0.125f, 0.125f, 0.25f, 0.5f);
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    Vector<NWB::Impl::MeshSkinningInfluenceGpu, NWB::Core::Alloc::ScratchArena> influences(scratchArena);
    ASSERT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    ASSERT_EQ(influences.size(), 2u);
    for(u32 index = 0u; index < NWB::Impl::s_SkinInfluenceJointCount; ++index)
        EXPECT_EQ(influences[0].joint[index], index);
    EXPECT_FLOAT_EQ(influences[0].weight.x, 0.125f);
    EXPECT_FLOAT_EQ(influences[0].weight.y, 0.125f);
    EXPECT_FLOAT_EQ(influences[0].weight.z, 0.25f);
    EXPECT_FLOAT_EQ(influences[0].weight.w, 0.5f);

    ++instance.editRevision;
    instance.skin[0] = MakeSkinInfluence(4u);
    instance.skin.push_back(MakeSkinInfluence(3u));
    ASSERT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    ASSERT_EQ(influences.size(), 3u);
    EXPECT_EQ(influences[0].joint[0], 4u);
    EXPECT_FLOAT_EQ(influences[0].weight.x, 1.0f);
    EXPECT_FLOAT_EQ(influences[0].weight.w, 0.0f);
    EXPECT_EQ(influences[2].joint[0], 3u);

    instance.skin.clear();
    ASSERT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    EXPECT_TRUE(influences.empty());
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
    EXPECT_EQ(payload.skinInfluenceCount, instance.skin.size());
    EXPECT_FLOAT_EQ(payload.jointMatrices[2].rows[0].w, 5.0f);
}

TEST(SkinningPayload, RuntimeScratchAllocationDoesNotScaleWithInfluenceCount){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = 2u;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(2u, Float34Identity());

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

TEST(SkinningPayload, RuntimeScratchStorageIsReusedAcrossMeshes){
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = 2u;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(2u, Float34Identity());
    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.localJoints.assign(2u, Float34Identity());
    pose.parentJoints.assign(2u, NWB::Impl::s_SkeletonRootParent);

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    {
        NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
        ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
        ASSERT_TRUE(payload.hasActiveSkin());
    }
    const ArenaMemoryStats initialMemory = scratchArena.memoryStats();
    for(u32 meshIndex = 0u; meshIndex < 64u; ++meshIndex){
        NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
        ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, meshIndex % 2u == 0u ? &pose : nullptr, payload));
        ASSERT_TRUE(payload.hasActiveSkin());
    }
    const ArenaMemoryStats finalMemory = scratchArena.memoryStats();
    EXPECT_EQ(finalMemory.usedBytes, initialMemory.usedBytes);
    EXPECT_EQ(finalMemory.reservedBytes, initialMemory.reservedBytes);
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
    instance.skeletonJointCount = 2u;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(1u, Float34Identity());

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.joints.assign(2u, Float34Identity());
    palette.skinningMode = Limit<u32>::s_Max;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.skinningMode = NWB::Impl::SkeletonSkinningMode::LinearBlend;
    palette.joints[1].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.joints.assign(2u, Float34Identity());
    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.localJoints.assign(2u, Float34Identity());
    pose.parentJoints.assign(2u, NWB::Impl::s_SkeletonRootParent);
    pose.parentJoints[1] = 1u;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, &pose, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    palette.joints[0].rows[0].x = 2.0f;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    palette.joints.assign(2u, Float34Identity());
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), 2u);
    EXPECT_EQ(logger.errorCount(), 5u);
}

TEST(SkinningPayload, InverseBindBoundsAndPartialFailuresLeaveNoActivePayload){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistration(logger);
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = 2u;
    instance.skin.assign(3u, MakeSkinInfluence(1u));
    instance.inverseBindMatrices.assign(2u, Float34Identity());
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(2u, Float34Identity());

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

    instance.inverseBindMatrices.assign(2u, Float34Identity());
    instance.inverseBindMatrices[1].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    instance.inverseBindMatrices[1] = Float34Identity();
    instance.inverseBindMatrices[1].rows[0].x = 0.001f;
    instance.inverseBindMatrices[1].rows[1].y = 0.001f;
    instance.inverseBindMatrices[1].rows[2].z = 0.001f;
    EXPECT_FALSE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ExpectEmptyRuntimePayload(payload);

    instance.inverseBindMatrices.assign(2u, Float34Identity());
    NWB::Impl::SkeletonPoseComponent pose(arena);
    pose.localJoints.assign(2u, Float34Identity());
    pose.parentJoints.assign(2u, NWB::Impl::s_SkeletonRootParent);
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
    ASSERT_EQ(payload.jointMatrices.size(), 2u);
    EXPECT_FLOAT_EQ(payload.jointMatrices[1].rows[1].y, 3.0f);
    EXPECT_EQ(logger.errorCount(), 7u);
}

TEST(SkinningPayload, StaticInfluenceEncodingRejectsInvalidMeshEditsAndRecovers){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistration(logger);
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = 2u;
    instance.skin.assign(2u, MakeSkinInfluence(1u));
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    Vector<NWB::Impl::MeshSkinningInfluenceGpu, NWB::Core::Alloc::ScratchArena> influences(scratchArena);
    ASSERT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    ASSERT_EQ(influences.size(), 2u);

    ++instance.editRevision;
    instance.skin[1].weight.x = 0.5f;
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    EXPECT_TRUE(influences.empty());

    instance.skin[1].weight = Float4U(-0.5f, 1.5f, 0.0f, 0.0f);
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    EXPECT_TRUE(influences.empty());

    instance.skin[1].weight = Float4U(Limit<f32>::s_QuietNaN, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    EXPECT_TRUE(influences.empty());

    instance.skin[1] = MakeSkinInfluence(1u);
    instance.skin[1].joint[3] = 2u;
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    EXPECT_TRUE(influences.empty());

    instance.skin[1] = MakeSkinInfluence(1u);
    instance.skeletonJointCount = 0u;
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    EXPECT_TRUE(influences.empty());

    instance.skeletonJointCount = static_cast<u32>(Limit<u16>::s_Max) + 2u;
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    EXPECT_TRUE(influences.empty());

    instance.skeletonJointCount = 2u;
    ASSERT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinInfluences(instance, influences));
    ASSERT_EQ(influences.size(), 2u);
    EXPECT_EQ(influences[1].joint[0], 1u);
    EXPECT_FLOAT_EQ(influences[1].weight.x, 1.0f);
    EXPECT_EQ(logger.errorCount(), 6u);
}
#endif

TEST(SkinningPayload, RepeatedRuntimePayloadWorkload){
    constexpr usize s_InfluenceCount = 65536u;
    constexpr usize s_JointCount = 128u;
    constexpr usize s_BuildCount = 128u;
    auto& arena = NWB::Tests::TestDetail::Arena();
    NWB::Impl::MeshSkinningRuntimeInstance instance(arena);
    instance.skeletonJointCount = static_cast<u32>(s_JointCount);
    instance.skin.reserve(s_InfluenceCount);
    for(usize index = 0u; index < s_InfluenceCount; ++index)
        instance.skin.push_back(MakeSkinInfluence(static_cast<u16>(index % s_JointCount)));
    NWB::Impl::SkeletonJointPaletteComponent palette(arena);
    palette.joints.assign(s_JointCount, Float34Identity());

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::RuntimeSkinPayloadScratch payload(scratchArena);
    ASSERT_TRUE(BuildRuntimeSkinPayload(instance, &palette, nullptr, payload));
    ASSERT_TRUE(payload.hasActiveSkin());
    ASSERT_EQ(payload.jointMatrices.size(), s_JointCount);

    usize activeBuildCount = 0u;
    f32 translationSum = 0.0f;
    bool builtAll = true;
    const Timer begin = TimerNow();
    for(usize buildIndex = 0u; buildIndex < s_BuildCount; ++buildIndex){
        palette.joints[0].rows[0].w = static_cast<f32>(buildIndex);
        if(!BuildRuntimeSkinPayload(instance, &palette, nullptr, payload)){
            builtAll = false;
            break;
        }
        activeBuildCount += payload.hasActiveSkin() ? 1u : 0u;
        translationSum += payload.jointMatrices[0].rows[0].w;
    }
    const u64 elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);

    ASSERT_TRUE(builtAll);
    EXPECT_EQ(activeBuildCount, s_BuildCount);
    EXPECT_FLOAT_EQ(translationSum, static_cast<f32>(s_BuildCount * (s_BuildCount - 1u) / 2u));
    EXPECT_EQ(payload.jointMatrices.size(), s_JointCount);
    char durationText[32] = {};
    RecordProperty("skin_payload_ns", FormatDecimal(elapsedNanoseconds, durationText).data());
    RecordProperty("influence_count", static_cast<int>(s_InfluenceCount));
    RecordProperty("joint_count", static_cast<int>(s_JointCount));
    RecordProperty("build_count", static_cast<int>(s_BuildCount));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

