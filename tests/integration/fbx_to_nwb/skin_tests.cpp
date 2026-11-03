// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <utilities/fbx_to_nwb/skin.h>
#include <utilities/fbx_to_nwb/asset_writer_skeleton.h>

#include <global/simdmath.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_skin_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FbxSkin, RejectsFiniteDoubleMatricesThatCannotBeStoredAsFloats){
    static_assert(sizeof(ufbx_real) == sizeof(f64));
    NWB::Tests::TestArena<> arena;
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/fbx/skin"));
    NWB::FbxToNwb::ImportOptions options;
    options.bakeTransforms = false;
    options.scale = 1.0;
    NWB::FbxToNwb::MeshInstance instance;
    for(u32 testCase = 0u; testCase < 3u; ++testCase){
        ufbx_node bone{};
        ufbx_skin_cluster cluster{};
        cluster.bone_node = &bone;
        cluster.geometry_to_bone.m00 = 1.0;
        cluster.geometry_to_bone.m11 = 1.0;
        cluster.geometry_to_bone.m22 = 1.0;
        if(testCase == 0u)
            cluster.geometry_to_bone.m03 = 1e40;
        else if(testCase == 1u){
            cluster.geometry_to_bone.m00 = 1e-40;
            cluster.geometry_to_bone.m11 = 1e20;
            cluster.geometry_to_bone.m22 = 1e20;
        }
        else{
            cluster.geometry_to_bone.m00 = 1e200;
            cluster.geometry_to_bone.m11 = 1e200;
            cluster.geometry_to_bone.m22 = 1e200;
        }
        ufbx_skin_cluster* clusters[] = { &cluster };
        ufbx_skin_deformer skin{};
        skin.clusters.data = clusters;
        skin.clusters.count = 1u;
        NWB::FbxToNwb::FbxSkinDetail::ExportContext context(scratchArena);
        EXPECT_FALSE(NWB::FbxToNwb::FbxSkinDetail::BuildClusterJointMap(instance, options, skin, scratchArena, context));
        EXPECT_TRUE(context.joints.empty());
        EXPECT_TRUE(context.bindPoseMatrices.empty());
        EXPECT_TRUE(context.inverseBindMatrices.empty());
        EXPECT_TRUE(context.jointLookup.empty());
    }
    EXPECT_GE(logger.errorCount(), 3u);
}

TEST(FbxSkin, RejectsMismatchedOrNonFiniteSkeletonMatrices){
    NWB::Tests::TestArena<> arena;
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/fbx/skeleton"));
    ufbx_node root{};
    ufbx_node* joints[] = { &root };
    NWB::FbxToNwb::JointMatrix bind = MatrixIdentity<NWB::FbxToNwb::JointMatrix>();
    NWB::FbxToNwb::JointMatrix inverse = bind;
    using NWB::FbxToNwb::AssetWriterSkeletonDetail::BuildSkeletonOutputData;

    EXPECT_FALSE(BuildSkeletonOutputData(joints, {}, { &inverse, 1u }, scratchArena));
    EXPECT_FALSE(BuildSkeletonOutputData(joints, { &bind, 1u }, {}, scratchArena));
    bind._11 = Limit<f32>::s_Infinity;
    EXPECT_FALSE(BuildSkeletonOutputData(joints, { &bind, 1u }, { &inverse, 1u }, scratchArena));
    bind = MatrixIdentity<NWB::FbxToNwb::JointMatrix>();
    inverse._11 = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(BuildSkeletonOutputData(joints, { &bind, 1u }, { &inverse, 1u }, scratchArena));
    EXPECT_GE(logger.errorCount(), 4u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

