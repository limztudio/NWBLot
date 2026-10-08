// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/cook_topology.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MeshSolidTopology, MixedClosedOpenAndInwardComponentsKeepIndependentMembership){
    Core::Alloc::ScratchArena scratch(Name("tests/assets_mesh/mixed_solid_topology"));
    const Float3U positions[] = {
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f},
        {2.f, 0.f, 0.f}, {3.f, 0.f, 0.f}, {2.f, 1.f, 0.f}, {2.f, 0.f, 1.f},
        {4.f, 0.f, 0.f}, {5.f, 0.f, 0.f}, {4.f, 1.f, 0.f},
    };
    const u32 indices[] = {
        0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u,
        4u, 5u, 6u, 4u, 7u, 5u, 4u, 6u, 7u, 5u, 7u, 6u,
        8u, 9u, 10u,
    };
    const auto words = Impl::BuildSolidTriangleWords(positions, indices, scratch);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 1u);
    EXPECT_EQ((*words)[0], 0xffu);
}

TEST(MeshSolidTopology, BrokenWindingNonManifoldAndDegenerateFacesInvalidateTheirWholeComponent){
    Core::Alloc::ScratchArena scratch(Name("tests/assets_mesh/invalid_solid_topology"));
    const Float3U positions[] = {
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f},
        {0.5f, 0.f, 0.f},
    };
    const u32 brokenWinding[] = {0u, 1u, 2u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    const u32 nonManifold[] = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u, 0u, 1u, 4u};
    const u32 degenerate[] = {0u, 2u, 1u, 0u, 1u, 4u, 0u, 4u, 2u, 1u, 2u, 4u};
    for(const Span<const u32> indices : {Span<const u32>(brokenWinding), Span<const u32>(nonManifold), Span<const u32>(degenerate)}){
        const auto words = Impl::BuildSolidTriangleWords(positions, indices, scratch);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0u);
    }
}

TEST(MeshSolidTopology, CoincidentIndependentPositionsDoNotRepairAnOpenSeam){
    Core::Alloc::ScratchArena scratch(Name("tests/assets_mesh/independent_position_seam"));
    const Float3U positions[] = {
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f},
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
    };
    const u32 indices[] = {4u, 6u, 5u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    const auto words = Impl::BuildSolidTriangleWords(positions, indices, scratch);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 1u);
    EXPECT_EQ((*words)[0], 0u);
}

TEST(MeshSolidTopology, MembershipCrossesWordBoundariesWithoutSettingTailPadding){
    Core::Alloc::ScratchArena scratch(Name("tests/assets_mesh/solid_membership_word_boundary"));
    Vector<Float3U, Core::Alloc::ScratchArena> positions{scratch};
    Vector<u32, Core::Alloc::ScratchArena> indices{scratch};
    constexpr u32 s_Tetrahedron[] = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(u32 component = 0u; component < 9u; ++component){
        const f32 x = static_cast<f32>(component) * 2.f;
        positions.push_back({x, 0.f, 0.f});
        positions.push_back({x + 1.f, 0.f, 0.f});
        positions.push_back({x, 1.f, 0.f});
        positions.push_back({x, 0.f, 1.f});
        for(const u32 corner : s_Tetrahedron)
            indices.push_back(component * 4u + corner);
    }
    const auto words = Impl::BuildSolidTriangleWords(positions, indices, scratch);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 2u);
    EXPECT_EQ((*words)[0], 0xffffffffu);
    EXPECT_EQ((*words)[1], 0xfu);
}

TEST(MeshSolidTopology, NonFinitePositionsAndInvalidCornerRangesCannotProduceMembership){
    Core::Alloc::ScratchArena scratch(Name("tests/assets_mesh/invalid_topology_inputs"));
    Float3U positions[] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    const u32 validCorners[] = {0u, 1u, 2u};
    const u32 missingCorner[] = {0u, 1u};
    const u32 outOfRange[] = {0u, 1u, 3u};
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, missingCorner, scratch));
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, outOfRange, scratch));
    positions[2].x = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, validCorners, scratch));
}

TEST(MeshSolidTopology, OpposedSheetsAndCoplanarClosedComponentsRemainSurfaces){
    Core::Alloc::ScratchArena scratch(Name("tests/assets_mesh/zero_solid_volume"));
    const Float3U positions[] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}};
    const u32 sheet[] = {0u, 1u, 2u, 0u, 2u, 1u};
    const u32 flatTetrahedron[] = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(const Span<const u32> indices : {Span<const u32>(sheet), Span<const u32>(flatTetrahedron)}){
        const auto words = Impl::BuildSolidTriangleWords(positions, indices, scratch);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0u);
    }
}

TEST(MeshSolidTopology, RelativeComponentVolumePreservesTinyAndTranslatedSolids){
    Core::Alloc::ScratchArena scratch(Name("tests/assets_mesh/relative_solid_volume"));
    struct VolumeCase{
        Float3U origin;
        f32 size;
    };
    const VolumeCase cases[] = {
        {{0.f, 0.f, 0.f}, 0.001f},
        {{0.f, 0.f, 0.f}, 1.e-30f},
        {{1.e20f, -1.e20f, 1.e20f}, 1.e14f},
    };
    const u32 indices[] = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(const VolumeCase& volumeCase : cases){
        const Float3U& origin = volumeCase.origin;
        const Float3U positions[] = {
            origin,
            {origin.x + volumeCase.size, origin.y, origin.z},
            {origin.x, origin.y + volumeCase.size, origin.z},
            {origin.x, origin.y, origin.z + volumeCase.size},
        };
        const auto words = Impl::BuildSolidTriangleWords(positions, indices, scratch);
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

