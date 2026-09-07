// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_skeleton/cook.h>

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_payload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


struct SkeletonInputs{
    Core::Assets::AssetArena arena{ Name("tests/skeleton_payload/inputs") };
    SkeletonCookEntry entry{ arena };
    Skeleton skeleton{ arena, Name("tests/skeleton_payload/skeleton") };


    SkeletonInputs(){
        entry.virtualPath = skeleton.virtualPath();
        entry.joints.reserve(4u);
        entry.joints.push_back(SkeletonCookJoint{ .name = Name("root") });
        entry.joints.push_back(SkeletonCookJoint{ .name = Name("left"), .parent = Name("root") });
        entry.joints.push_back(SkeletonCookJoint{ .name = Name("right"), .parent = Name("root") });
        entry.joints.push_back(SkeletonCookJoint{ .name = Name("hand"), .parent = Name("left") });
        entry.joints[1u].localBindPose._14 = 2.0f;
        entry.joints[3u].localBindPose._24 = 3.0f;
    }
};

[[nodiscard]] static Name IndexedJointName(const usize index){
    char indexText[32u] = {};
    return DeriveName(Name("tests/skeleton_payload/joint/"), FormatDecimal(index, indexText));
}

TEST(SkeletonPayload, ResolvesEarlierParentsAndBuildsExactChildRanges){
    SkeletonInputs inputs;
    ASSERT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
    ASSERT_EQ(inputs.skeleton.jointCount(), 4u);
    EXPECT_EQ(inputs.skeleton.rootJointCount(), 1u);
    EXPECT_EQ(inputs.skeleton.findJointIndex(Name("root")), 0u);
    EXPECT_EQ(inputs.skeleton.findJointIndex(Name("hand")), 3u);
    EXPECT_EQ(inputs.skeleton.findJointIndex(Name("missing")), s_SkeletonInvalidJointIndex);
    EXPECT_EQ(inputs.skeleton.joints()[0u].parentIndex, s_SkeletonInvalidJointIndex);
    EXPECT_EQ(inputs.skeleton.joints()[1u].parentIndex, 0u);
    EXPECT_EQ(inputs.skeleton.joints()[2u].parentIndex, 0u);
    EXPECT_EQ(inputs.skeleton.joints()[3u].parentIndex, 1u);
    ASSERT_EQ(inputs.skeleton.jointChildRanges().size(), 4u);
    ASSERT_EQ(inputs.skeleton.jointChildIndices().size(), 3u);
    EXPECT_EQ(inputs.skeleton.jointChildRanges()[0u].firstChild, 0u);
    EXPECT_EQ(inputs.skeleton.jointChildRanges()[0u].childCount, 2u);
    EXPECT_EQ(inputs.skeleton.jointChildRanges()[1u].firstChild, 2u);
    EXPECT_EQ(inputs.skeleton.jointChildRanges()[1u].childCount, 1u);
    EXPECT_EQ(inputs.skeleton.jointChildRanges()[2u].childCount, 0u);
    EXPECT_EQ(inputs.skeleton.jointChildIndices()[0u], 1u);
    EXPECT_EQ(inputs.skeleton.jointChildIndices()[1u], 2u);
    EXPECT_EQ(inputs.skeleton.jointChildIndices()[2u], 3u);
}

TEST(SkeletonPayload, RejectsLaterSelfAndMissingParentsAndClearsPreviousOutput){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(u32 invalidCase = 0u; invalidCase < 4u; ++invalidCase){
        SkeletonInputs inputs;
        ASSERT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
        switch(invalidCase){
        case 0u: inputs.entry.joints[0u].parent = Name("left"); break;
        case 1u: inputs.entry.joints[1u].parent = Name("left"); break;
        case 2u: inputs.entry.joints[3u].parent = Name("missing"); break;
        case 3u: inputs.entry.joints[1u].parent = Name("right"); break;
        }
        EXPECT_FALSE(BuildSkeletonAsset(inputs.entry, inputs.skeleton)) << invalidCase;
        EXPECT_TRUE(inputs.skeleton.joints().empty());
        EXPECT_TRUE(inputs.skeleton.jointIndices().empty());
        EXPECT_TRUE(inputs.skeleton.jointChildRanges().empty());
        EXPECT_TRUE(inputs.skeleton.jointChildIndices().empty());
        EXPECT_EQ(inputs.skeleton.virtualPath(), inputs.entry.virtualPath);
        inputs.entry.joints[0u].parent = NAME_NONE;
        inputs.entry.joints[1u].parent = Name("root");
        inputs.entry.joints[3u].parent = Name("left");
        EXPECT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
        EXPECT_EQ(inputs.skeleton.jointCount(), 4u);
    }
    EXPECT_EQ(logger.errorCount(), 4u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("references missing or later parent")));
}

TEST(SkeletonPayload, RejectsDuplicateCanonicalIdsAfterResolvingEarlierParent){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(const Name duplicate : { Name("left"), Name("ROOT") }){
        SkeletonInputs inputs;
        ASSERT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
        inputs.entry.joints[3u].name = duplicate;
        inputs.entry.joints[3u].parent = duplicate;
        EXPECT_FALSE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
        EXPECT_TRUE(inputs.skeleton.joints().empty());
        EXPECT_TRUE(inputs.skeleton.jointIndices().empty());
        EXPECT_TRUE(inputs.skeleton.jointChildRanges().empty());
        EXPECT_TRUE(inputs.skeleton.jointChildIndices().empty());
    }
    EXPECT_EQ(logger.errorCount(), 2u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate joint name")));
}

TEST(SkeletonPayload, RebuildsChangedHierarchyAndSerializesJointIdentityAndMatrices){
    SkeletonInputs inputs;
    ASSERT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
    inputs.entry.joints[2u].parent = NAME_NONE;
    inputs.entry.joints[3u].parent = Name("right");
    ASSERT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
    EXPECT_EQ(inputs.skeleton.rootJointCount(), 2u);
    EXPECT_EQ(inputs.skeleton.joints()[3u].parentIndex, 2u);
    EXPECT_EQ(inputs.skeleton.jointChildRanges()[1u].childCount, 0u);
    EXPECT_EQ(inputs.skeleton.jointChildRanges()[2u].childCount, 1u);

    Core::Assets::AssetBytes bytes(inputs.arena);
    SkeletonAssetCodec codec;
    ASSERT_TRUE(codec.serialize(inputs.skeleton, bytes));
    Skeleton loaded(inputs.arena, inputs.entry.virtualPath);
    ASSERT_TRUE(loaded.loadBinary(bytes));
    ASSERT_EQ(loaded.jointCount(), inputs.skeleton.jointCount());
    EXPECT_EQ(loaded.rootJointCount(), 2u);
    for(usize jointIndex = 0u; jointIndex < inputs.entry.joints.size(); ++jointIndex){
        EXPECT_EQ(loaded.findJointIndex(inputs.entry.joints[jointIndex].name), jointIndex);
        EXPECT_EQ(loaded.joints()[jointIndex].parentIndex, inputs.skeleton.joints()[jointIndex].parentIndex);
        EXPECT_FLOAT_EQ(loaded.joints()[jointIndex].localBindPose._14, inputs.entry.joints[jointIndex].localBindPose._14);
        EXPECT_FLOAT_EQ(loaded.joints()[jointIndex].localBindPose._24, inputs.entry.joints[jointIndex].localBindPose._24);
    }
    EXPECT_EQ(loaded.jointChildRanges()[0u].childCount, 1u);
    EXPECT_EQ(loaded.jointChildRanges()[2u].childCount, 1u);
    ASSERT_EQ(loaded.jointChildIndices().size(), 2u);
    EXPECT_EQ(loaded.jointChildIndices()[0u], 1u);
    EXPECT_EQ(loaded.jointChildIndices()[1u], 3u);
}

static void BenchmarkSkeletonBuild(const usize jointCount, const usize iterations){
    SkeletonInputs inputs;
    inputs.entry.joints.clear();
    inputs.entry.joints.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        inputs.entry.joints.push_back(SkeletonCookJoint{
            .name = IndexedJointName(jointIndex),
            .parent = jointIndex == 0u ? NAME_NONE : inputs.entry.joints.back().name,
        });
    }
    ASSERT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.skeleton));
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    usize succeeded = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if(BuildSkeletonAsset(inputs.entry, inputs.skeleton))
            ++succeeded;
    }
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    EXPECT_EQ(succeeded, iterations);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    ASSERT_EQ(inputs.skeleton.jointCount(), jointCount);
    EXPECT_EQ(inputs.skeleton.rootJointCount(), 1u);
    EXPECT_EQ(inputs.skeleton.findJointIndex(inputs.entry.joints.back().name), jointCount - 1u);
    for(usize jointIndex = 1u; jointIndex < jointCount; ++jointIndex)
        EXPECT_EQ(inputs.skeleton.joints()[jointIndex].parentIndex, jointIndex - 1u);

    char elapsedText[32u] = {};
    char iterationsText[32u] = {};
    char jointsText[32u] = {};
    char allocationsText[32u] = {};
    testing::Test::RecordProperty("skeleton_build_ns", FormatDecimal(elapsed, elapsedText).data());
    testing::Test::RecordProperty("skeleton_build_iterations", FormatDecimal(iterations, iterationsText).data());
    testing::Test::RecordProperty("skeleton_joint_count", FormatDecimal(jointCount, jointsText).data());
    testing::Test::RecordProperty(
        "skeleton_build_backing_allocations", FormatDecimal(after.allocationCount - before.allocationCount, allocationsText).data()
    );
}

// Opt in with --gtest_also_run_disabled_tests and a SkeletonPayloadBenchmark.* filter.
TEST(SkeletonPayloadBenchmark, DISABLED_BuildsLongJointChain){
    BenchmarkSkeletonBuild(4096u, 4u);
}

TEST(SkeletonPayloadBenchmark, DISABLED_BuildsSingleJoint){
    BenchmarkSkeletonBuild(1u, 4096u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

