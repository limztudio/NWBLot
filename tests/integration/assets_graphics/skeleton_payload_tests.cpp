// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_skeleton/cook.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_payload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_LEFT = "left";
static constexpr AStringView s_ROOT = "root";
static constexpr AStringView s_RIGHT = "right";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


struct SkeletonInputs{
    Core::Assets::AssetArena arena{ Name("tests/skeleton_payload/inputs") };
    Core::Alloc::ScratchArena scratchArena{ Name("tests/skeleton_payload/validation_scratch") };
    SkeletonCookEntry entry{ arena };
    Skeleton skeleton{ arena, Name("tests/skeleton_payload/skeleton") };


    SkeletonInputs(){
        entry.virtualPath = skeleton.virtualPath();
        entry.joints.reserve(4u);
        entry.joints.push_back(SkeletonCookJoint{ .name = Name(s_ROOT) });
        entry.joints.push_back(SkeletonCookJoint{ .name = Name(s_LEFT), .parent = Name(s_ROOT) });
        entry.joints.push_back(SkeletonCookJoint{ .name = Name(s_RIGHT), .parent = Name(s_ROOT) });
        entry.joints.push_back(SkeletonCookJoint{ .name = Name("hand"), .parent = Name(s_LEFT) });
        entry.joints[1u].localBindPose._14 = 2.0f;
        entry.joints[3u].localBindPose._24 = 3.0f;
    }
};


TEST(SkeletonPayload, MissingJointLookupReturnsInvalidIndex){
    SkeletonInputs inputs;
    auto skeletonBuildResult = BuildSkeletonAsset(inputs.entry, inputs.arena, inputs.scratchArena);
    ASSERT_TRUE(skeletonBuildResult);
    inputs.skeleton = Move(*skeletonBuildResult);
    EXPECT_EQ(inputs.skeleton.findJointIndex(Name("missing")), s_SkeletonInvalidJointIndex);
}

TEST(SkeletonPayload, RejectsLaterSelfAndMissingParentsAndRecoversWithValidParents){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(u32 invalidCase = 0u; invalidCase < 4u; ++invalidCase){
        SkeletonInputs inputs;
        switch(invalidCase){
        case 0u: inputs.entry.joints[0u].parent = Name(s_LEFT); break;
        case 1u: inputs.entry.joints[1u].parent = Name(s_LEFT); break;
        case s_ExpectedDualCount: inputs.entry.joints[3u].parent = Name("missing"); break;
        case 3u: inputs.entry.joints[1u].parent = Name(s_RIGHT); break;
        }
        EXPECT_FALSE(BuildSkeletonAsset(inputs.entry, inputs.arena, inputs.scratchArena)) << invalidCase;
        inputs.entry.joints[0u].parent = s_NameNone;
        inputs.entry.joints[1u].parent = Name(s_ROOT);
        inputs.entry.joints[3u].parent = Name(s_LEFT);
        EXPECT_TRUE(BuildSkeletonAsset(inputs.entry, inputs.arena, inputs.scratchArena));
    }
    EXPECT_EQ(logger.errorCount(), 4u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("references missing or later parent")));
}

TEST(SkeletonPayload, RejectsDuplicateCanonicalIdsAfterResolvingEarlierParent){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(const Name& duplicate : { Name(s_LEFT), Name("ROOT") }){
        SkeletonInputs inputs;
        inputs.entry.joints[3u].name = duplicate;
        inputs.entry.joints[3u].parent = duplicate;
        EXPECT_FALSE(BuildSkeletonAsset(inputs.entry, inputs.arena, inputs.scratchArena));
    }
    EXPECT_EQ(logger.errorCount(), s_ExpectedDualCount);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate joint name")));
}


TEST(SkeletonPayload, CodecRejectsEveryTruncatedTablePrefixWithoutReplacingPriorJoints){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkeletonInputs inputs;
    auto buildResult = BuildSkeletonAsset(inputs.entry, inputs.arena, inputs.scratchArena);
    ASSERT_TRUE(buildResult);
    inputs.skeleton = Move(*buildResult);
    SkeletonAssetCodec codec;
    Core::Assets::AssetBytes binary(inputs.arena);
    ASSERT_TRUE(codec.serialize(inputs.skeleton, binary));
    ASSERT_TRUE(inputs.skeleton.loadBinary(binary));
    for(usize prefix = 0u; prefix < binary.size(); ++prefix){
        Core::Assets::AssetBytes malformed(binary);
        malformed.resize(prefix);
        EXPECT_FALSE(inputs.skeleton.loadBinary(malformed)) << prefix;
        Core::Assets::AssetBytes retained(inputs.arena);
        ASSERT_TRUE(codec.serialize(inputs.skeleton, retained));
        EXPECT_EQ(retained, binary) << prefix;
    }
    EXPECT_EQ(logger.errorCount(), binary.size());
}

TEST(SkeletonPayload, RepeatedValidationReclaimsCallerScratchAfterSuccessAndInvalidLookup){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkeletonInputs inputs;
    auto buildResult = BuildSkeletonAsset(inputs.entry, inputs.arena, inputs.scratchArena);
    ASSERT_TRUE(buildResult);
    inputs.skeleton = Move(*buildResult);
    ASSERT_TRUE(inputs.skeleton.validatePayload(inputs.scratchArena));
    const ArenaMemoryStats warm = inputs.scratchArena.memoryStats();
    for(usize iteration = 0u; iteration < 16u; ++iteration){
        Skeleton::JointIndexMap jointIndices(inputs.skeleton.jointIndices());
        const bool valid = iteration % s_ExpectedDualCount == 0u;
        jointIndices.at(Name(s_LEFT)) = valid ? 1u : 0u;
        inputs.skeleton.setJoints(Skeleton::JointVector(inputs.skeleton.joints()), Move(jointIndices));
        EXPECT_EQ(inputs.skeleton.validatePayload(inputs.scratchArena), valid);
        const ArenaMemoryStats current = inputs.scratchArena.memoryStats();
        EXPECT_EQ(current.usedBytes, warm.usedBytes);
        EXPECT_EQ(current.reservedBytes, warm.reservedBytes);
    }
    EXPECT_EQ(logger.errorCount(), 8u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

