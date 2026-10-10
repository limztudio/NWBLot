// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_skeleton/cook.h>

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_payload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_LEFT = "left";
static constexpr AStringView s_ROOT = "root";
static constexpr AStringView s_RIGHT = "right";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


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
    auto skeletonBuildResult = BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena());
    ASSERT_TRUE(skeletonBuildResult);
    inputs.skeleton = Move(*skeletonBuildResult);
    EXPECT_EQ(inputs.skeleton.findJointIndex(Name("missing")), s_SkeletonInvalidJointIndex);
}

TEST(SkeletonPayload, RejectsLaterSelfAndMissingParentsWithoutReplacingPreviousValue){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(u32 invalidCase = 0u; invalidCase < 4u; ++invalidCase){
        SkeletonInputs inputs;
        auto skeletonBuildResult2 = BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena());
        ASSERT_TRUE(skeletonBuildResult2);
        inputs.skeleton = Move(*skeletonBuildResult2);
        switch(invalidCase){
        case 0u: inputs.entry.joints[0u].parent = Name(s_LEFT); break;
        case 1u: inputs.entry.joints[1u].parent = Name(s_LEFT); break;
        case s_ExpectedDualCount: inputs.entry.joints[3u].parent = Name("missing"); break;
        case 3u: inputs.entry.joints[1u].parent = Name(s_RIGHT); break;
        }
        EXPECT_FALSE(BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena())) << invalidCase;
        EXPECT_EQ(inputs.skeleton.jointCount(), 4u);
        EXPECT_EQ(inputs.skeleton.findJointIndex(Name("hand")), 3u);
        EXPECT_EQ(inputs.skeleton.joints()[3u].parentIndex, 1u);
        EXPECT_EQ(inputs.skeleton.virtualPath(), inputs.entry.virtualPath);
        inputs.entry.joints[0u].parent = s_NameNone;
        inputs.entry.joints[1u].parent = Name(s_ROOT);
        inputs.entry.joints[3u].parent = Name(s_LEFT);
        auto skeletonBuildResult3 = BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena());
        EXPECT_TRUE(skeletonBuildResult3);
        inputs.skeleton = Move(*skeletonBuildResult3);
        EXPECT_EQ(inputs.skeleton.jointCount(), 4u);
    }
    EXPECT_EQ(logger.errorCount(), 4u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("references missing or later parent")));
}

TEST(SkeletonPayload, RejectsDuplicateCanonicalIdsAfterResolvingEarlierParent){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(const Name& duplicate : { Name(s_LEFT), Name("ROOT") }){
        SkeletonInputs inputs;
        auto skeletonBuildResult4 = BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena());
        ASSERT_TRUE(skeletonBuildResult4);
        inputs.skeleton = Move(*skeletonBuildResult4);
        inputs.entry.joints[3u].name = duplicate;
        inputs.entry.joints[3u].parent = duplicate;
        EXPECT_FALSE(BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena()));
        EXPECT_EQ(inputs.skeleton.jointCount(), 4u);
        EXPECT_EQ(inputs.skeleton.findJointIndex(Name("hand")), 3u);
        EXPECT_EQ(inputs.skeleton.joints()[3u].parentIndex, 1u);
    }
    EXPECT_EQ(logger.errorCount(), s_ExpectedDualCount);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate joint name")));
}

TEST(SkeletonPayload, ReplacesParentLinksWithoutRetainingPreviousRelationships){
    SkeletonInputs inputs;
    auto skeletonBuildResult5 = BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena());
    ASSERT_TRUE(skeletonBuildResult5);
    inputs.skeleton = Move(*skeletonBuildResult5);
    inputs.entry.joints[s_ThirdElementIndex].parent = s_NameNone;
    inputs.entry.joints[3u].parent = Name(s_RIGHT);
    auto skeletonBuildResult6 = BuildSkeletonAsset(inputs.entry, inputs.entry.joints.get_allocator().arena());
    ASSERT_TRUE(skeletonBuildResult6);
    inputs.skeleton = Move(*skeletonBuildResult6);
    EXPECT_EQ(inputs.skeleton.joints()[0u].parentIndex, s_SkeletonInvalidJointIndex);
    EXPECT_EQ(inputs.skeleton.joints()[s_ThirdElementIndex].parentIndex, s_SkeletonInvalidJointIndex);
    EXPECT_EQ(inputs.skeleton.joints()[3u].parentIndex, s_ExpectedDualCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

