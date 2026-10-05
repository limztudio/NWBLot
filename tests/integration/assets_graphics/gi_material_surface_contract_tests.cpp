// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_gi_material_surface_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ECS_RENDER = "ecs_render";
static constexpr AStringView s_GRAPH = "graph";
static constexpr AStringView s_RETURN_FALSE = "return false";
static constexpr AStringView s_RETURN = "return;";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct GiMaterialSurfaceContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<GiMaterialSurfaceContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}

// Check that incomplete resource declarations return before publishing a prepared graph callback.
static bool ContainsBeforeClosingBrace(
    const AStringView text,
    const AStringView anchor,
    const AStringView expected
){
    const usize anchorOffset = text.find(anchor);
    if(anchorOffset == AStringView::npos)
        return false;
    const usize closingBraceOffset = text.find("}", anchorOffset);
    if(closingBraceOffset == AStringView::npos)
        return false;
    const usize expectedOffset = text.find(expected, anchorOffset);
    return expectedOffset != AStringView::npos && expectedOffset < closingBraceOffset;
}

// Match the whole negated call so braces in its input aggregate cannot hide an ignored failure result.
static bool ReturnsAfterFailedBuilderCall(const AStringView text, const AStringView call, const AStringView phase){
    const usize callOffset = text.find(call);
    if(callOffset == AStringView::npos)
        return false;
    usize offset = text.find("(", callOffset);
    if(offset == AStringView::npos)
        return false;
    usize depth = 0u;
    for(; offset < text.size(); ++offset){
        if(text[offset] == '(')
            ++depth;
        else if(text[offset] == ')' && --depth == 0u){
            ++offset;
            break;
        }
    }
    if(depth != 0u || !ContainsText(text.substr(callOffset, offset - callOffset), phase))
        return false;
    offset = text.find_first_not_of(" \t\r\n", offset);
    if(offset == AStringView::npos)
        return false;
    const AStringView tail = text.substr(offset, 13u);
    return tail == s_RETURN || tail.substr(0u, 12u) == s_RETURN_FALSE;
}


// Boolean GI occlusion: same geometric-blocking acceptance as closest, without attribute/material work.


TEST(EcsGraphics, PreparedMaterialGraphDeclarationsFailClosedWhenResourceSetsAreIncomplete){
    TestArena testArena;
    const TestPath repoRoot = NWB::Tests::RepoRootOf(testArena.arena, __FILE__);

    AString graphicsPrefixTaskGraphSource;
    AString deferredLightingTaskGraphSource;
    AString transparentCsgIntervalBuilderSource;
    AString avboitGeometryPreparationBuilderSource;
    AString avboitOccupancyGraphSource;
    AString avboitExtinctionGraphSource;
    AString avboitAccumulationGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "renderer_frame_pipeline_graphics_prefix.cpp", graphicsPrefixTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "renderer_frame_pipeline_graph.cpp", deferredLightingTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_GRAPH / "frame_graph_avboit_occupancy.cpp", avboitOccupancyGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_GRAPH / "frame_graph_avboit_extinction.cpp", avboitExtinctionGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_GRAPH / "frame_graph_avboit_accumulation.cpp", avboitAccumulationGraphSource));
    deferredLightingTaskGraphSource.insert(deferredLightingTaskGraphSource.end(), avboitOccupancyGraphSource.begin(), avboitOccupancyGraphSource.end());
    deferredLightingTaskGraphSource.insert(deferredLightingTaskGraphSource.end(), avboitExtinctionGraphSource.begin(), avboitExtinctionGraphSource.end());
    deferredLightingTaskGraphSource.insert(deferredLightingTaskGraphSource.end(), avboitAccumulationGraphSource.begin(), avboitAccumulationGraphSource.end());
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "csg" / "transparent_csg_interval_builder.cpp", transparentCsgIntervalBuilderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "avboit" / "geometry_preparation_builder.cpp", avboitGeometryPreparationBuilderSource));
    const AStringView graphicsPrefixTaskGraph(graphicsPrefixTaskGraphSource.data(), graphicsPrefixTaskGraphSource.size());
    const AStringView deferredLightingTaskGraph(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());
    const AStringView transparentCsgIntervalBuilder(transparentCsgIntervalBuilderSource.data(), transparentCsgIntervalBuilderSource.size());
    const AStringView avboitGeometryPreparationBuilder(avboitGeometryPreparationBuilderSource.data(), avboitGeometryPreparationBuilderSource.size());

    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque material geometry states",
        s_RETURN_FALSE
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque material sampled textures",
        s_RETURN_FALSE
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque CSG material geometry states",
        s_RETURN_FALSE
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque CSG material sampled textures",
        s_RETURN_FALSE
    ));

    EXPECT_TRUE(ContainsBeforeClosingBrace(
        transparentCsgIntervalBuilder,
        "if(!avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned)",
        s_RETURN_FALSE
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        transparentCsgIntervalBuilder,
        "if(!transparentCsgMaterialSampledTexturesCollected)",
        s_RETURN_FALSE
    ));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "if(!transparentCsgIntervalBuilder.declare("));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare transparent CSG interval producer",
        s_RETURN
    ));

    EXPECT_TRUE(ContainsBeforeClosingBrace(
        avboitGeometryPreparationBuilder,
        "if(!outResult.geometryOwned)",
        s_RETURN_FALSE
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        avboitGeometryPreparationBuilder,
        "if(!outResult.sampledTexturesCollected)",
        s_RETURN_FALSE
    ));
    EXPECT_TRUE(ReturnsAfterFailedBuilderCall(
        deferredLightingTaskGraph,
        "if(!occupancyGeometryPreparationBuilder.declare(",
        ".phase = AvboitGeometryPhase::Occupancy"
    ));
    EXPECT_TRUE(ReturnsAfterFailedBuilderCall(
        deferredLightingTaskGraph,
        "if(!extinctionGeometryPreparationBuilder.declare(",
        ".phase = AvboitGeometryPhase::Extinction"
    ));
    EXPECT_TRUE(ReturnsAfterFailedBuilderCall(
        deferredLightingTaskGraph,
        "if(!accumulationGeometryPreparationBuilder.declare(",
        ".phase = AvboitGeometryPhase::Accumulation"
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

