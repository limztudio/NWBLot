// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_mesh/meshlet_ref_decoding.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_mesh_acceptance_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshT>
static Expected<NWB::Impl::MeshletPositionStreamRef> TestDecodeMeshletPositionRef(const MeshT& mesh, const NWB::Impl::MeshletDesc& meshlet, const u32 localPositionIndex)noexcept(noexcept(
    NWB::Impl::TryDecodeMeshletPositionRef(mesh.meshletPositionRefDeltas(), meshlet, localPositionIndex, false)
)){
    return NWB::Impl::TryDecodeMeshletPositionRef(mesh.meshletPositionRefDeltas(), meshlet, localPositionIndex, false);
}

template<typename MeshT>
static Expected<NWB::Impl::MeshletAttributeStreamRef> TestDecodeMeshletAttributeRef(const MeshT& mesh, const NWB::Impl::MeshletDesc& meshlet, const u32 localAttributeIndex)noexcept(noexcept(
    NWB::Impl::TryDecodeMeshletAttributeRef(mesh.meshletAttributeRefDeltas(), meshlet, localAttributeIndex)
)){
    return NWB::Impl::TryDecodeMeshletAttributeRef(mesh.meshletAttributeRefDeltas(), meshlet, localAttributeIndex);
}

template<
    typename MeshT,
    typename PositionStreamT,
    typename NormalStreamT,
    typename LocalRefVectorT
>
static b8 TestMeshletHasPositionNormalValue(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const PositionStreamT& positions,
    const NormalStreamT& normals,
    const LocalRefVectorT& localRefs,
    const Float3U& expectedPosition,
    const Float4U& expectedNormal
)noexcept(noexcept(DeclVal<void(*)(const NWB::Impl::MeshletLocalVertexRef&)noexcept>()(localRefs[DeclVal<u32>()]))
    && noexcept(TestDecodeMeshletPositionRef(mesh, meshlet, DeclVal<u32>()))
    && noexcept(TestDecodeMeshletAttributeRef(mesh, meshlet, DeclVal<u32>()))
    && noexcept(DeclVal<void(*)(const Float3U&)noexcept>()(positions[DeclVal<u32>()]))
    && noexcept(LoadHalf4U(normals[DeclVal<u32>()]))
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
[[nodiscard]] static usize TestMeshletLogicalPositionRefCount(const MeshT& mesh)noexcept([]()constexpr noexcept{
    using Range = RemoveReference_T<decltype(DeclVal<const MeshT&>().meshlets())>;
    if constexpr(IsArray_V<Range>)
        return noexcept(DeclVal<void(*)(const NWB::Impl::MeshletDesc&)noexcept>()(DeclVal<RemoveExtent_T<Range>&>()));
    else if constexpr(requires(Range& values){ values.begin(); values.end(); }){
        using Iterator = RemoveCVRef_T<decltype(DeclVal<Range&>().begin())>;
        using Sentinel = RemoveCVRef_T<decltype(DeclVal<Range&>().end())>;
        return
            noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<Range&>().begin()))
            && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<Range&>().end()))
            && IsNothrowDestructible_V<Iterator>
            && IsNothrowDestructible_V<Sentinel>
            && noexcept(static_cast<b8>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
            && noexcept(++DeclVal<Iterator&>())
            && noexcept(DeclVal<void(*)(const NWB::Impl::MeshletDesc&)noexcept>()(*DeclVal<Iterator&>()))
        ;
    }
    else
        return false;
}() && noexcept(mesh.meshlets())){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletPositionCount(meshlet);
    return count;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletLogicalAttributeRefCount(const MeshT& mesh)noexcept([]()constexpr noexcept{
    using Range = RemoveReference_T<decltype(DeclVal<const MeshT&>().meshlets())>;
    if constexpr(IsArray_V<Range>)
        return noexcept(DeclVal<void(*)(const NWB::Impl::MeshletDesc&)noexcept>()(DeclVal<RemoveExtent_T<Range>&>()));
    else if constexpr(requires(Range& values){ values.begin(); values.end(); }){
        using Iterator = RemoveCVRef_T<decltype(DeclVal<Range&>().begin())>;
        using Sentinel = RemoveCVRef_T<decltype(DeclVal<Range&>().end())>;
        return
            noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<Range&>().begin()))
            && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<Range&>().end()))
            && IsNothrowDestructible_V<Iterator>
            && IsNothrowDestructible_V<Sentinel>
            && noexcept(static_cast<b8>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
            && noexcept(++DeclVal<Iterator&>())
            && noexcept(DeclVal<void(*)(const NWB::Impl::MeshletDesc&)noexcept>()(*DeclVal<Iterator&>()))
        ;
    }
    else
        return false;
}() && noexcept(mesh.meshlets())){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletAttributeCount(meshlet);
    return count;
}

static Expected<UniquePtr<NWB::Core::Assets::IAsset>> CookAndLoadSmokeMesh(AStringView assetFilename, const Name assetName, const AssetsGraphicsFixture::CookCase& cookCase, TestArena& testArena){
    const b8 cooked = AssetsGraphicsFixture::CookSmokeAssetMeta("meshes", assetFilename, cookCase, testArena);
    EXPECT_TRUE(cooked);
    if(!cooked)
        return MakeUnexpected(Failure{});
    return AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::MeshAssetCodec>(cookCase.outputDirectory, assetName, 1u, testArena);
}

template<typename CallbackT>
static void RunSmokeMeshAcceptance(AStringView assetFilename, const AStringView caseName, const Name assetName, CallbackT&& callback){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, caseName);
    ASSERT_TRUE(cookCase);
    auto loadedAssetResult = CookAndLoadSmokeMesh(assetFilename, assetName, *cookCase, testArena);
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
    });
}

TEST(AssetsGraphics, OpposedSphereNormalsDisableMeshletCone){
    RunSmokeMeshAcceptance("sphere_smooth.nwb", "sphere_smooth", Name("project/meshes/sphere_smooth"), [](const NWB::Impl::Mesh& loadedMesh){
        ASSERT_EQ(loadedMesh.meshletBounds().size(), 1u);
        EXPECT_FALSE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
    });
}

TEST(AssetsGraphics, MeshAcceptanceUvSeamQuad){
    RunSmokeMeshAcceptance("uv_seam_quad.nwb", "uv_seam_quad", Name("project/meshes/uv_seam_quad"), [](const NWB::Impl::Mesh& loadedMesh){
        ASSERT_EQ(loadedMesh.meshlets().size(), 1u);
        ASSERT_GE(loadedMesh.meshletLocalVertexRefs().size(), 5u);
        const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
        const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
        EXPECT_EQ(localRefs[0u].localDeformedPosition, localRefs[3u].localDeformedPosition);
        EXPECT_EQ(localRefs[2u].localDeformedPosition, localRefs[4u].localDeformedPosition);
        EXPECT_NE(localRefs[0u].localAttribute, localRefs[3u].localAttribute);
        EXPECT_NE(localRefs[2u].localAttribute, localRefs[4u].localAttribute);

        const auto attributeRef0 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[0u].localAttribute);
        const auto attributeRef2 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[2u].localAttribute);
        const auto attributeRef3 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[3u].localAttribute);
        const auto attributeRef4 = TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[4u].localAttribute);
        ASSERT_TRUE(attributeRef0);
        ASSERT_TRUE(attributeRef2);
        ASSERT_TRUE(attributeRef3);
        ASSERT_TRUE(attributeRef4);
        EXPECT_EQ(attributeRef0->uv0, 0u);
        EXPECT_EQ(attributeRef2->uv0, 2u);
        EXPECT_EQ(attributeRef3->uv0, 1u);
        EXPECT_EQ(attributeRef4->uv0, 3u);
        EXPECT_EQ(attributeRef0->tangent, 0u);
        EXPECT_EQ(attributeRef2->tangent, 0u);
        EXPECT_EQ(attributeRef3->tangent, 1u);
        EXPECT_EQ(attributeRef4->tangent, 1u);
    });
}

TEST(AssetsGraphics, MeshAcceptanceMirroredUvQuad){
    RunSmokeMeshAcceptance("mirrored_uv_quad.nwb", "mirrored_uv_quad", Name("project/meshes/mirrored_uv_quad"), [](const NWB::Impl::Mesh& loadedMesh){
        EXPECT_EQ(loadedMesh.tangentStream().size(), 2u);
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
    });
}

TEST(AssetsGraphics, MeshAcceptanceTwoSidedPlane){
    RunSmokeMeshAcceptance("two_sided_plane.nwb", "two_sided_plane", Name("project/meshes/two_sided_plane"), [](const NWB::Impl::Mesh& loadedMesh){
        EXPECT_EQ(TestMeshletLogicalPositionRefCount(loadedMesh), 3u);
        EXPECT_EQ(TestMeshletLogicalAttributeRefCount(loadedMesh), 6u);
        EXPECT_FALSE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
    });
}

TEST(AssetsGraphics, MeshAcceptanceLargeManyMeshlets){
    RunSmokeMeshAcceptance("large_mesh_many_meshlets.nwb", "large_mesh_many_meshlets", Name("project/meshes/large_mesh_many_meshlets"), [](const NWB::Impl::Mesh& loadedMesh){
        const usize expectedPrimitiveCount = static_cast<usize>(NWB::Impl::s_MeshMaxMeshletTriangles) + 1u;
        const usize expectedMeshletCount = 2u;
        EXPECT_EQ(loadedMesh.meshlets().size(), 2u);
        EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), expectedPrimitiveCount * 3u);
        if(loadedMesh.meshlets().size() < expectedMeshletCount)
            return;

        EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[0u]), NWB::Impl::s_MeshMaxMeshletTriangles);
        EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[1u]), 1u);
    });
}

TEST(AssetsGraphics, MeshFailedReloadPreservesEveryPreviouslyPublishedStream){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "mesh_failed_reload");
    ASSERT_TRUE(cookCase);
    auto result = CookAndLoadSmokeMesh("uv_seam_quad.nwb", Name("project/meshes/uv_seam_quad"), *cookCase, testArena);
    ASSERT_TRUE(result);
    auto& mesh = static_cast<Impl::Mesh&>(**result);
    Impl::MeshAssetCodec codec;
    Core::Assets::AssetBytes original(testArena.arena);
    ASSERT_TRUE(codec.serialize(mesh, original));
    for(u32 invalidCase = 0u; invalidCase < 4u; ++invalidCase){
        Core::Assets::AssetBytes malformed(original);
        if(invalidCase == 0u)
            malformed[0u] ^= 1u;
        else if(invalidCase == 1u)
            malformed.pop_back();
        else if(invalidCase == 2u)
            malformed.push_back(0u);
        else{
            constexpr u32 s_OldMeshMagic = 0x4D534837u;
            ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(0u, s_OldMeshMagic, malformed));
        }
        EXPECT_FALSE(mesh.loadBinary(malformed)) << invalidCase;
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(mesh, retained));
        EXPECT_EQ(retained, original) << invalidCase;
    }
    EXPECT_TRUE(RemoveAllIfExists(cookCase->root));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

