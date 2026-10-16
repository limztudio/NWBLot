// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_mesh/skin_binary_payload.h>
#include <impl/assets_model/binary_payload.h>
#include <impl/assets_skeleton/binary_payload.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_mesh_acceptance{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshT>
static Expected<NWB::Impl::MeshletPositionStreamRef> TestDecodeMeshletPositionRef(const MeshT& mesh, const NWB::Impl::MeshletDesc& meshlet, const u32 localPositionIndex)noexcept(noexcept(
    NWB::Impl::DecodeMeshletPositionRef(mesh.meshletPositionRefDeltas().data(), mesh.meshletPositionRefDeltas().size(), meshlet, localPositionIndex, NWB::Core::Mesh::MeshClassUsesSkinning(mesh.meshClass()))
)){
    return NWB::Impl::DecodeMeshletPositionRef(mesh.meshletPositionRefDeltas().data(), mesh.meshletPositionRefDeltas().size(), meshlet, localPositionIndex, NWB::Core::Mesh::MeshClassUsesSkinning(mesh.meshClass()));
}

template<typename MeshT>
static Expected<NWB::Impl::MeshletAttributeStreamRef> TestDecodeMeshletAttributeRef(const MeshT& mesh, const NWB::Impl::MeshletDesc& meshlet, const u32 localAttributeIndex)noexcept(noexcept(
    NWB::Impl::DecodeMeshletAttributeRef(mesh.meshletAttributeRefDeltas().data(), mesh.meshletAttributeRefDeltas().size(), meshlet, localAttributeIndex)
)){
    return NWB::Impl::DecodeMeshletAttributeRef(mesh.meshletAttributeRefDeltas().data(), mesh.meshletAttributeRefDeltas().size(), meshlet, localAttributeIndex);
}

template<
    typename MeshT,
    typename PositionStreamT,
    typename NormalStreamT,
    typename LocalRefVectorT
>
static bool TestMeshletHasPositionNormalValue(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const PositionStreamT& positions,
    const NormalStreamT& normals,
    const LocalRefVectorT& localRefs,
    const Float3U& expectedPosition,
    const Float4U& expectedNormal
){
    for(u32 localVertexIndex = 0u; localVertexIndex < NWB::Impl::MeshletVertexCount(meshlet); ++localVertexIndex){
        const NWB::Impl::MeshletLocalVertexRef& localRef = localRefs[meshlet.localVertexOffset + localVertexIndex];
        const auto positionRef = TestDecodeMeshletPositionRef(mesh, meshlet, localRef.localDeformedPosition);
        const auto attributeRef = TestDecodeMeshletAttributeRef(mesh, meshlet, localRef.localAttribute);
        if(!positionRef || !attributeRef)
            return false;

        const Float3U& position = positions[positionRef->position];
        const Float4U normal = LoadHalf4U(normals[attributeRef->normal]);
        if(position.x == expectedPosition.x && position.y == expectedPosition.y && position.z == expectedPosition.z && normal.x == expectedNormal.x && normal.y == expectedNormal.y && normal.z == expectedNormal.z && normal.w == expectedNormal.w)
            return true;
    }

    return false;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletLogicalPositionRefCount(const MeshT& mesh){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletPositionCount(meshlet);
    return count;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletLogicalAttributeRefCount(const MeshT& mesh){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletAttributeCount(meshlet);
    return count;
}

static Expected<UniquePtr<NWB::Core::Assets::IAsset>> CookAndLoadSmokeMesh(TestArena& testArena, AStringView assetFilename, const Name assetName, const AssetsGraphicsFixture::CookCase& cookCase){
    const bool cooked = AssetsGraphicsFixture::CookSmokeMeshMeta(assetFilename, testArena, cookCase);
    EXPECT_TRUE(cooked);
    if(!cooked)
        return MakeUnexpected(Failure{});
    return AssetsGraphicsFixture::LoadCookedMesh(testArena, cookCase.outputDirectory, assetName);
}

template<typename CallbackT>
static void RunSmokeMeshAcceptance(AStringView assetFilename, const AStringView caseName, const Name assetName, CallbackT&& callback){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, caseName);
    ASSERT_TRUE(cookCase);
    auto loadedAssetResult = CookAndLoadSmokeMesh(testArena, assetFilename, assetName, *cookCase);
    if(loadedAssetResult){
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(**loadedAssetResult);
        Forward<CallbackT>(callback)(loadedMesh);
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    EXPECT_TRUE(RemoveAllIfExists(cookCase->root));
}

TEST(AssetsGraphics, MeshAcceptanceHardEdgeCubeZippedRefs){
    RunSmokeMeshAcceptance("cube_hard_edges.nwb", "hard_edge_cube", Name("project/meshes/cube_hard_edges"), [](const NWB::Impl::Mesh& loadedMesh){
            ASSERT_EQ(loadedMesh.meshlets().size(), 1u);
            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0];
            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            const auto& positions = loadedMesh.positionStream();
            const auto& normals = loadedMesh.normalStream();
            EXPECT_TRUE(TestMeshletHasPositionNormalValue(loadedMesh, meshlet, positions, normals, localRefs, Float3U(-0.5f, -0.5f, -0.5f), Float4U(0.0f, 0.0f, -1.0f, 0.0f)));
            EXPECT_TRUE(TestMeshletHasPositionNormalValue(loadedMesh, meshlet, positions, normals, localRefs, Float3U(-0.5f, -0.5f, -0.5f), Float4U(0.0f, -1.0f, 0.0f, 0.0f)));
            EXPECT_TRUE(TestMeshletHasPositionNormalValue(loadedMesh, meshlet, positions, normals, localRefs, Float3U(-0.5f, -0.5f, -0.5f), Float4U(-1.0f, 0.0f, 0.0f, 0.0f)));
        }
    );
}


TEST(AssetsGraphics, OpposedSphereNormalsDisableMeshletCone){
    RunSmokeMeshAcceptance("sphere_smooth.nwb", "sphere_smooth", Name("project/meshes/sphere_smooth"), [](const NWB::Impl::Mesh& loadedMesh){
            ASSERT_EQ(loadedMesh.meshletBounds().size(), 1u);
            EXPECT_FALSE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceUvSeamQuad){
    RunSmokeMeshAcceptance("uv_seam_quad.nwb", "uv_seam_quad", Name("project/meshes/uv_seam_quad"), [](const NWB::Impl::Mesh& loadedMesh){
            ASSERT_EQ(loadedMesh.meshlets().size(), 1u);
            ASSERT_GE(loadedMesh.meshletLocalVertexRefs().size(), 5u);
            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            EXPECT_EQ(localRefs[0u].localDeformedPosition, localRefs[3u].localDeformedPosition);
            EXPECT_EQ(localRefs[s_ThirdElementIndex].localDeformedPosition, localRefs[4u].localDeformedPosition);
            EXPECT_NE(localRefs[0u].localAttribute, localRefs[3u].localAttribute);
            EXPECT_NE(localRefs[s_ThirdElementIndex].localAttribute, localRefs[4u].localAttribute);

            const auto attributeRef0 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[0u].localAttribute);
            const auto attributeRef2 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[s_ThirdElementIndex].localAttribute);
            const auto attributeRef3 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[3u].localAttribute);
            const auto attributeRef4 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[4u].localAttribute);
            ASSERT_TRUE(attributeRef0);
            ASSERT_TRUE(attributeRef2);
            ASSERT_TRUE(attributeRef3);
            ASSERT_TRUE(attributeRef4);
            EXPECT_EQ(attributeRef0->uv0, 0u);
            EXPECT_EQ(attributeRef2->uv0, s_ExpectedDualCount);
            EXPECT_EQ(attributeRef3->uv0, 1u);
            EXPECT_EQ(attributeRef4->uv0, 3u);
            EXPECT_EQ(attributeRef0->tangent, 0u);
            EXPECT_EQ(attributeRef2->tangent, 0u);
            EXPECT_EQ(attributeRef3->tangent, 1u);
            EXPECT_EQ(attributeRef4->tangent, 1u);
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceMirroredUvQuad){
    RunSmokeMeshAcceptance("mirrored_uv_quad.nwb", "mirrored_uv_quad", Name("project/meshes/mirrored_uv_quad"), [](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.tangentStream().size(), s_ExpectedDualCount);
            EXPECT_EQ(LoadHalf4U(loadedMesh.tangentStream()[0u]).w, 1.0f);
            EXPECT_EQ(LoadHalf4U(loadedMesh.tangentStream()[1u]).w, -1.0f);

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            const auto attributeRef0 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[0u].localAttribute);
            const auto attributeRef3 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[3u].localAttribute);
            ASSERT_TRUE(attributeRef0);
            ASSERT_TRUE(attributeRef3);
            EXPECT_EQ(attributeRef0->tangent, 0u);
            EXPECT_EQ(attributeRef3->tangent, 1u);
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceTwoSidedPlane){
    RunSmokeMeshAcceptance("two_sided_plane.nwb", "two_sided_plane", Name("project/meshes/two_sided_plane"), [](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(TestMeshletLogicalPositionRefCount(loadedMesh), 3u);
            EXPECT_EQ(TestMeshletLogicalAttributeRefCount(loadedMesh), 6u);
            EXPECT_FALSE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceLargeManyMeshlets){
    RunSmokeMeshAcceptance("large_mesh_many_meshlets.nwb", "large_mesh_many_meshlets", Name("project/meshes/large_mesh_many_meshlets"), [](const NWB::Impl::Mesh& loadedMesh){
            const usize expectedPrimitiveCount = static_cast<usize>(NWB::Impl::s_MeshMaxMeshletTriangles) + 1u;
            const usize expectedMeshletCount = s_ExpectedDualCount;
            EXPECT_EQ(loadedMesh.meshlets().size(), s_ExpectedDualCount);
            EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), expectedPrimitiveCount * 3u);
            if(loadedMesh.meshlets().size() < expectedMeshletCount)
                return;

            EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[0u]), NWB::Impl::s_MeshMaxMeshletTriangles);
            EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[1u]), 1u);
        }
    );
}


TEST(AssetsGraphics, MeshFailedReloadPreservesEveryPreviouslyPublishedStream){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "mesh_failed_reload");
    ASSERT_TRUE(cookCase);
    auto result = CookAndLoadSmokeMesh(testArena, "uv_seam_quad.nwb", Name("project/meshes/uv_seam_quad"), *cookCase);
    ASSERT_TRUE(result);
    auto& mesh = static_cast<Impl::Mesh&>(**result);
    Impl::MeshAssetCodec codec;
    Core::Assets::AssetBytes original(testArena.arena);
    ASSERT_TRUE(codec.serialize(mesh, original));
    for(u32 invalidCase = 0u; invalidCase < 3u; ++invalidCase){
        Core::Assets::AssetBytes malformed(original);
        if(invalidCase == 0u)
            malformed[0u] ^= 1u;
        else if(invalidCase == 1u)
            malformed.pop_back();
        else
            malformed.push_back(0u);
        EXPECT_FALSE(mesh.loadBinary(malformed)) << invalidCase;
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(mesh, retained));
        EXPECT_EQ(retained, original) << invalidCase;
    }
    EXPECT_TRUE(RemoveAllIfExists(cookCase->root));
}

TEST(AssetsGraphics, SkinBinaryRejectsInvalidWeightsJointsMatricesAndReservedFieldsWithoutReplacingState){
    using namespace Impl;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    SkinBinaryPayload::HeaderBinary header;
    header.meshNameHash = Name("project/meshes/original").hash();
    header.skeletonNameHash = Name("project/skeletons/original").hash();
    header.influenceCount = 1u;
    header.inverseBindMatrixCount = 1u;
    SkinInfluence4 influence;
    influence.weight = Float4U(1.0f, 0.0f, 0.0f, 0.0f);
    const SkeletonJointMatrix originalMatrix = MatrixIdentity<Float34>();
    Core::Assets::AssetBytes original(testArena.arena);
    AppendPOD(original, header);
    AppendPOD(original, influence);
    AppendPOD(original, originalMatrix);
    Skin skin(testArena.arena, Name("project/skins/original"));
    ASSERT_TRUE(skin.loadBinary(original));
    SkinAssetCodec codec;
    for(u32 invalidCase = 0u; invalidCase < 8u; ++invalidCase){
        SkinBinaryPayload::HeaderBinary malformedHeader = header;
        malformedHeader.meshNameHash = Name("project/meshes/replacement").hash();
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
    ModelBinaryPayload::ModelHeaderBinary header;
    header.skeletonObjectCount = 1u;
    header.staticMeshObjectCount = 1u;
    header.skinnedMeshObjectCount = 1u;
    ModelBinaryPayload::ModelSkeletonObjectBinary skeleton;
    skeleton.nameHash = Name("rig").hash();
    skeleton.skeletonNameHash = Name("project/skeletons/original").hash();
    skeleton.transform = MatrixIdentity<Float34>();
    ModelBinaryPayload::ModelStaticMeshObjectBinary staticMesh;
    staticMesh.nameHash = Name("prop").hash();
    staticMesh.meshNameHash = Name("project/meshes/prop").hash();
    staticMesh.transform = MatrixIdentity<Float34>();
    ModelBinaryPayload::ModelSkinnedMeshObjectBinary skinnedMesh;
    skinnedMesh.nameHash = Name("body").hash();
    skinnedMesh.skinNameHash = Name("project/skins/body").hash();
    skinnedMesh.skeletonObjectNameHash = skeleton.nameHash;
    skinnedMesh.transform = MatrixIdentity<Float34>();
    Core::Assets::AssetBytes original(testArena.arena);
    AppendPOD(original, header);
    AppendPOD(original, skeleton);
    AppendPOD(original, staticMesh);
    AppendPOD(original, skinnedMesh);
    Model model(testArena.arena, Name("project/models/original"));
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
    SkeletonBinaryPayload::HeaderBinary header;
    header.jointCount = 1u;
    SkeletonBinaryPayload::JointBinary joint;
    joint.nameHash = Name("root").hash();
    joint.parentIndex = s_SkeletonInvalidJointIndex;
    joint.localBindPose = MatrixIdentity<Float34>();
    Core::Assets::AssetBytes original(testArena.arena);
    AppendPOD(original, header);
    AppendPOD(original, joint);
    Skeleton skeleton(testArena.arena, Name("project/skeletons/original"));
    ASSERT_TRUE(skeleton.loadBinary(original));
    SkeletonAssetCodec codec;
    for(u32 invalidCase = 0u; invalidCase < 7u; ++invalidCase){
        auto malformedHeader = header;
        auto malformedJoint = joint;
        malformedJoint.nameHash = Name("replacement").hash();
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
        EXPECT_EQ(skeleton.findJointIndex(Name("root")), 0u);
        EXPECT_EQ(skeleton.findJointIndex(Name("replacement")), s_SkeletonInvalidJointIndex);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

