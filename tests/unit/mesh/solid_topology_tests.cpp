// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/mesh/solid_test_types.adhd.h>

#include <impl/assets_mesh/cook_solid_topology.adhd.h>

#include <global/simd/vector.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MeshSolidTopology, MixedClosedOpenAndInwardComponentsKeepIndependentMembership){
    const Array<Float3U, 11u> positions = {
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{0.f, 0.f, 1.f},
        Float3U{2.f, 0.f, 0.f}, Float3U{3.f, 0.f, 0.f}, Float3U{2.f, 1.f, 0.f}, Float3U{2.f, 0.f, 1.f},
        Float3U{4.f, 0.f, 0.f}, Float3U{5.f, 0.f, 0.f}, Float3U{4.f, 1.f, 0.f},
    };
    const Array<u32, 27u> indices = {
        0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u,
        4u, 5u, 6u, 4u, 7u, 5u, 4u, 6u, 7u, 5u, 7u, 6u,
        8u, 9u, 10u,
    };
    const auto words = Impl::BuildSolidTriangleWords(positions, indices);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 1u);
    EXPECT_EQ((*words)[0], 0xffu);
}

TEST(MeshSolidTopology, BrokenWindingNonManifoldAndDegenerateFacesInvalidateTheirWholeComponent){
    const Array<Float3U, 5u> positions = {
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{0.f, 0.f, 1.f},
        Float3U{0.5f, 0.f, 0.f},
    };
    const Array<u32, 12u> brokenWinding = {0u, 1u, 2u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    const Array<u32, 15u> nonManifold = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u, 0u, 1u, 4u};
    const Array<u32, 12u> degenerate = {0u, 2u, 1u, 0u, 1u, 4u, 0u, 4u, 2u, 1u, 2u, 4u};
    for(const Span<const u32> indices : {Span<const u32>(brokenWinding), Span<const u32>(nonManifold), Span<const u32>(degenerate)}){
        const auto words = Impl::BuildSolidTriangleWords(positions, indices);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0u);
    }
}

TEST(MeshSolidTopology, CoincidentIndependentPositionsDoNotRepairAnOpenSeam){
    const Array<Float3U, 7u> positions = {
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{0.f, 0.f, 1.f},
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f},
    };
    const Array<u32, 12u> indices = {4u, 6u, 5u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    const auto words = Impl::BuildSolidTriangleWords(positions, indices);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 1u);
    EXPECT_EQ((*words)[0], 0u);
}

TEST(MeshSolidTopology, MembershipCrossesWordBoundariesWithoutSettingTailPadding){
    Core::Alloc::ScratchArena scratch{Name(NWB::Tests::s_SolidCaseAssetsMeshSolidMembershipWordBoundary)};
    Vector<Float3U, Core::Alloc::ScratchArena> positions{scratch};
    Vector<u32, Core::Alloc::ScratchArena> indices{scratch};
    constexpr Array<u32, 12u> s_Tetrahedron = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(u32 component = 0u; component < 9u; ++component){
        const f32 x = static_cast<f32>(component) * 2.f;
        positions.push_back({x, 0.f, 0.f});
        positions.push_back({x + 1.f, 0.f, 0.f});
        positions.push_back({x, 1.f, 0.f});
        positions.push_back({x, 0.f, 1.f});
        for(const u32 corner : s_Tetrahedron)
            indices.push_back(component * 4u + corner);
    }
    const auto words = Impl::BuildSolidTriangleWords(positions, indices);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 2u);
    EXPECT_EQ((*words)[0], 0xffffffffu);
    EXPECT_EQ((*words)[1], 0xfu);
}

TEST(MeshSolidTopology, NonFinitePositionsAndInvalidCornerRangesCannotProduceMembership){
    Array<Float3U, 3u> positions = {Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}};
    const Array<u32, 3u> validCorners = {0u, 1u, 2u};
    const Array<u32, 2u> missingCorner = {0u, 1u};
    const Array<u32, 3u> outOfRange = {0u, 1u, 3u};
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, missingCorner));
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, outOfRange));
    positions[2].x = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, validCorners));
}

TEST(MeshSolidTopology, OpposedSheetsAndCoplanarClosedComponentsRemainSurfaces){
    const Array<Float3U, 4u> positions = {Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{1.f, 1.f, 0.f}};
    const Array<u32, 6u> sheet = {0u, 1u, 2u, 0u, 2u, 1u};
    const Array<u32, 12u> flatTetrahedron = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(const Span<const u32> indices : {Span<const u32>(sheet), Span<const u32>(flatTetrahedron)}){
        const auto words = Impl::BuildSolidTriangleWords(positions, indices);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0u);
    }
}

TEST(MeshSolidTopology, RelativeComponentVolumePreservesTinyAndTranslatedSolids){
    const Array<MeshSolidVolumeCase, 3u> cases = {{
        { .origin = Float3U{0.f, 0.f, 0.f}, .size = 0.001f },
        { .origin = Float3U{0.f, 0.f, 0.f}, .size = 1.e-30f },
        { .origin = Float3U{1.e20f, -1.e20f, 1.e20f}, .size = 1.e14f },
    }};
    const Array<u32, 12u> indices = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(const MeshSolidVolumeCase& volumeCase : cases){
        const Float3U& origin = volumeCase.origin;
        const SIMDVectorSingle shifted = VectorAdd(VectorSet(origin.x, origin.y, origin.z, 0.f), VectorReplicate(volumeCase.size));
        const Array<Float3U, 4u> positions = {
            origin,
            Float3U{VectorGetX(shifted), origin.y, origin.z},
            Float3U{origin.x, VectorGetY(shifted), origin.z},
            Float3U{origin.x, origin.y, VectorGetZ(shifted)},
        };
        const auto words = Impl::BuildSolidTriangleWords(positions, indices);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0xfu);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

