// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <tests/integration/assets_graphics/case_types.adhd.h>

#include <impl/assets_mesh/skin_asset.h>
#include <impl/assets_skeleton/asset.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_reload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, SkinBinaryRejectsInvalidWeightsJointsMatricesAndReservedFieldsWithoutReplacingState){
    using namespace Impl;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    SkinHeaderBinary header;
    header.meshNameHash = Name(AssetsGraphicsFixture::s_AssetCaseProjectMeshesOriginal).hash();
    header.skeletonNameHash = Name(AssetsGraphicsFixture::s_AssetCaseProjectSkeletonsOriginal).hash();
    header.influenceCount = 1u;
    header.inverseBindMatrixCount = 1u;
    SkinInfluence4 influence;
    influence.weight = Float4U(1.0f, 0.0f, 0.0f, 0.0f);
    const SkeletonJointMatrix originalMatrix = MatrixIdentity<Float34>();
    Core::Assets::AssetBytes original(testArena.arena);
    AppendPOD(original, header);
    AppendPOD(original, influence);
    AppendPOD(original, originalMatrix);
    Skin skin(testArena.arena, Name(AssetsGraphicsFixture::s_AssetCaseProjectSkinsOriginal));
    ASSERT_TRUE(skin.loadBinary(original));
    SkinAssetCodec codec;
    for(u32 invalidCase = 0u; invalidCase < 8u; ++invalidCase){
        SkinHeaderBinary malformedHeader = header;
        malformedHeader.meshNameHash = Name(AssetsGraphicsFixture::s_AssetCaseProjectMeshesReplacement).hash();
        SkinInfluence4 malformedInfluence = influence;
        SkeletonJointMatrix matrix = originalMatrix;
        switch(invalidCase){
        case 0u: malformedHeader.padding0 = 1u; break;
        case 1u: malformedInfluence.weight.x = BitCast<f32>(0x7fc00001u); break;
        case 2u: malformedInfluence.weight.x = -1.0f; break;
        case 3u: malformedInfluence.weight.x = 0.5f; break;
        case 4u: malformedInfluence.joint[3u] = 1u; break;
        case 5u: matrix._11 = 0.0f; break;
        case 6u: matrix._14 = BitCast<f32>(0x7f800000u); break;
        }
        Core::Assets::AssetBytes malformed(testArena.arena);
        AppendPOD(malformed, malformedHeader);
        AppendPOD(malformed, malformedInfluence);
        AppendPOD(malformed, matrix);
        if(invalidCase == 7u)
            malformed.push_back(0u);
        EXPECT_FALSE(skin.loadBinary(malformed)) << invalidCase;
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(skin, retained));
        EXPECT_EQ(retained, original) << invalidCase;
    }
}

TEST(AssetsGraphics, ModelBinaryRejectsNonFiniteTransformsAndReservedFieldsWithoutReplacingState){
    using namespace Impl;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    ModelHeaderBinary header;
    header.skeletonObjectCount = 1u;
    header.staticMeshObjectCount = 1u;
    header.skinnedMeshObjectCount = 1u;
    ModelSkeletonObjectBinary skeleton;
    skeleton.nameHash = Name(AssetsGraphicsFixture::s_AssetCaseRig).hash();
    skeleton.skeletonNameHash = Name(AssetsGraphicsFixture::s_AssetCaseProjectSkeletonsOriginal).hash();
    skeleton.transform = MatrixIdentity<Float34>();
    ModelStaticMeshObjectBinary staticMesh;
    staticMesh.nameHash = Name(AssetsGraphicsFixture::s_AssetCaseProp).hash();
    staticMesh.meshNameHash = Name(AssetsGraphicsFixture::s_AssetCaseProjectMeshesProp).hash();
    staticMesh.transform = MatrixIdentity<Float34>();
    ModelSkinnedMeshObjectBinary skinnedMesh;
    skinnedMesh.nameHash = Name(AssetsGraphicsFixture::s_AssetCaseBody).hash();
    skinnedMesh.skinNameHash = Name(AssetsGraphicsFixture::s_AssetCaseProjectSkinsBody).hash();
    skinnedMesh.skeletonObjectNameHash = skeleton.nameHash;
    skinnedMesh.transform = MatrixIdentity<Float34>();
    Core::Assets::AssetBytes original(testArena.arena);
    AppendPOD(original, header);
    AppendPOD(original, skeleton);
    AppendPOD(original, staticMesh);
    AppendPOD(original, skinnedMesh);
    Model model(testArena.arena, Name(AssetsGraphicsFixture::s_AssetCaseProjectModelsOriginal));
    ASSERT_TRUE(model.loadBinary(original));
    ModelAssetCodec codec;
    for(u32 invalidCase = 0u; invalidCase < 7u; ++invalidCase){
        auto malformedHeader = header;
        auto malformedSkeleton = skeleton;
        auto malformedStatic = staticMesh;
        auto malformedSkinned = skinnedMesh;
        switch(invalidCase){
        case 0u: malformedHeader.padding0 = 1u; break;
        case 1u: malformedSkeleton.transform._11 = BitCast<f32>(0x7fc00001u); break;
        case 2u: malformedStatic.transform._24 = BitCast<f32>(0x7f800000u); break;
        case 3u: malformedSkinned.transform._33 = BitCast<f32>(0xff800000u); break;
        case 4u: malformedStatic.nameHash = skeleton.nameHash; break;
        case 5u: malformedSkinned.skeletonObjectNameHash = staticMesh.nameHash; break;
        }
        Core::Assets::AssetBytes malformed(testArena.arena);
        AppendPOD(malformed, malformedHeader);
        AppendPOD(malformed, malformedSkeleton);
        AppendPOD(malformed, malformedStatic);
        AppendPOD(malformed, malformedSkinned);
        if(invalidCase == 6u)
            malformed.pop_back();
        EXPECT_FALSE(model.loadBinary(malformed)) << invalidCase;
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(model, retained));
        EXPECT_EQ(retained, original) << invalidCase;
    }
}

TEST(AssetsGraphics, SkeletonBinaryRejectsNonFinitePosesInvalidParentsAndReservedFieldsWithoutReplacingState){
    using namespace Impl;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    SkeletonHeaderBinary header;
    header.jointCount = 1u;
    SkeletonJointBinary joint;
    joint.nameHash = Name(AssetsGraphicsFixture::s_AssetCaseRoot).hash();
    joint.parentIndex = s_SkeletonInvalidJointIndex;
    joint.localBindPose = MatrixIdentity<Float34>();
    Core::Assets::AssetBytes original(testArena.arena);
    AppendPOD(original, header);
    AppendPOD(original, joint);
    Skeleton skeleton(testArena.arena, Name(AssetsGraphicsFixture::s_AssetCaseProjectSkeletonsOriginal));
    ASSERT_TRUE(skeleton.loadBinary(original));
    SkeletonAssetCodec codec;
    for(u32 invalidCase = 0u; invalidCase < 7u; ++invalidCase){
        auto malformedHeader = header;
        auto malformedJoint = joint;
        malformedJoint.nameHash = Name(AssetsGraphicsFixture::s_AssetCaseReplacement).hash();
        switch(invalidCase){
        case 0u: malformedHeader.padding0 = 1u; break;
        case 1u: malformedJoint.padding0 = 1u; break;
        case 2u: malformedJoint.padding1[1u] = 1u; break;
        case 3u: malformedJoint.localBindPose._14 = BitCast<f32>(0x7fc00001u); break;
        case 4u: malformedJoint.parentIndex = 0u; break;
        case 5u: malformedHeader.jointCount = static_cast<u64>(Limit<u32>::s_Max) + 1u; break;
        }
        Core::Assets::AssetBytes malformed(testArena.arena);
        AppendPOD(malformed, malformedHeader);
        AppendPOD(malformed, malformedJoint);
        if(invalidCase == 6u)
            malformed.push_back(0u);
        EXPECT_FALSE(skeleton.loadBinary(malformed)) << invalidCase;
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(skeleton, retained));
        EXPECT_EQ(retained, original) << invalidCase;
        EXPECT_EQ(skeleton.findJointIndex(Name(NWB::Tests::AssetsGraphicsFixture::s_AssetInputNameRoot)), 0u);
        EXPECT_EQ(skeleton.findJointIndex(Name(NWB::Tests::AssetsGraphicsFixture::s_AssetInputNameReplacement)), s_SkeletonInvalidJointIndex);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

