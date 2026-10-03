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
static constexpr AStringView s_ASSETS = "assets";
static constexpr AStringView s_GRAPHICS = "graphics";
static constexpr AStringView s_GI = "gi";
static constexpr AStringView s_RAYTRACE = "raytrace";
static constexpr AStringView s_GI_SW_TRACE_SLANGI = "gi_sw_trace.slangi";
static constexpr AStringView s_GI_HW_TRACE_SLANGI = "gi_hw_trace.slangi";
static constexpr AStringView s_GRAPH = "graph";
static constexpr AStringView s_SHADOW = "shadow";
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


TEST(EcsGraphics, GiMaterialSurfaceDispatchSupportsHeterogeneousFrostInterface){
    TestArena testArena;
    const TestPath repoRoot = NWB::Tests::RepoRootOf(testArena.arena, __FILE__);

    AString swTraceSource;
    AString hwTraceSource;
    AString dispatchCodegenSource;
    AString rtDetailSource;
    AString instanceMaterialSource;
    AString frostBindSource;
    AString frostSurfaceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ASSETS / s_GRAPHICS / s_GI / s_GI_SW_TRACE_SLANGI, swTraceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ASSETS / s_GRAPHICS / s_GI / s_GI_HW_TRACE_SLANGI, hwTraceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / "assets_material" / "material_dispatch_codegen.cpp", dispatchCodegenSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "rt_detail.cpp", rtDetailSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ASSETS / s_GRAPHICS / s_SHADOW / "instance_material.slangi", instanceMaterialSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / s_ASSETS / "shaders" / "frost_surface.bind", frostBindSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / s_ASSETS / "shaders" / "frost.surface", frostSurfaceSource));

    const AStringView swTrace(swTraceSource.data(), swTraceSource.size());
    const AStringView hwTrace(hwTraceSource.data(), hwTraceSource.size());
    const AStringView dispatchCodegen(dispatchCodegenSource.data(), dispatchCodegenSource.size());
    const AStringView rtDetail(rtDetailSource.data(), rtDetailSource.size());
    const AStringView instanceMaterial(instanceMaterialSource.data(), instanceMaterialSource.size());
    const AStringView frostBind(frostBindSource.data(), frostBindSource.size());
    const AStringView frostSurface(frostSurfaceSource.data(), frostSurfaceSource.size());

    EXPECT_TRUE(ContainsText(swTrace, "#include \"shadow/generated/surface_dispatch.slangi\""));
    EXPECT_TRUE(ContainsText(swTrace, "const NwbMeshSurface surface = nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(swTrace, "closest.albedo = surface.baseColor;"));
    EXPECT_TRUE(ContainsText(hwTrace, "#include \"shadow/generated/surface_dispatch.slangi\""));
    EXPECT_TRUE(ContainsText(hwTrace, "const NwbMeshSurface surface = nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(hwTrace, "closest.albedo = surface.baseColor;"));

    EXPECT_TRUE(ContainsText(dispatchCodegen, "s_ShadowSurfaceBindNamespacePrefix = \"nwbShadowBindModel\""));
    EXPECT_TRUE(ContainsText(dispatchCodegen, "NwbMeshSurface nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(dispatchCodegen, "half3(0.5h, 0.5h, 0.5h)"));

    EXPECT_TRUE(ContainsText(frostBind, "struct NwbFrostSurfaceMaterial"));
    EXPECT_TRUE(ContainsText(frostBind, "frost_albedo"));
    EXPECT_FALSE(ContainsText(frostBind, "color_tint"));
    EXPECT_TRUE(ContainsText(frostSurface, "nwbMaterialBindLoadFrost"));
    EXPECT_TRUE(ContainsText(frostSurface, "nwbMakeMeshSurface(baseColor"));

    EXPECT_FALSE(ContainsText(rtDetail, "runtime.color_tint"));
    EXPECT_FALSE(ContainsText(instanceMaterial, "baseColorR"));
    EXPECT_FALSE(ContainsText(instanceMaterial, "baseColorG"));
    EXPECT_FALSE(ContainsText(instanceMaterial, "baseColorB"));
}


// Boolean GI occlusion: same geometric-blocking acceptance as closest, without attribute/material work.
TEST(EcsGraphics, GiBooleanOcclusionSharesClosestAcceptanceWithoutReconstruction){
    TestArena testArena;
    const TestPath repoRoot = NWB::Tests::RepoRootOf(testArena.arena, __FILE__);

    AString commonSource;
    AString swSource;
    AString hwSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ASSETS / s_GRAPHICS / s_GI / "gi_trace_common.slangi", commonSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ASSETS / s_GRAPHICS / s_GI / s_GI_SW_TRACE_SLANGI, swSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ASSETS / s_GRAPHICS / s_GI / s_GI_HW_TRACE_SLANGI, hwSource));

    const AStringView common(commonSource.data(), commonSource.size());
    const AStringView sw(swSource.data(), swSource.size());
    const AStringView hw(hwSource.data(), hwSource.size());

    EXPECT_TRUE(ContainsText(common, "bool nwbGiTraceOccluded(float3 origin, float3 direction, float tMin, float tMax);"));
    EXPECT_TRUE(ContainsText(common, "nwbGiShadeHit"));
    EXPECT_TRUE(ContainsText(sw, "bool nwbGiTraceOccluded(float3 origin, float3 direction, float tMin, float tMax){"));
    EXPECT_TRUE(ContainsText(sw, "nwbGiSwInstanceOccluded"));
    EXPECT_TRUE(ContainsText(sw, "nwbRayTriangleMollerTrumbore(origin, direction, tMin, tMax, v0, v1, v2)"));
    EXPECT_TRUE(ContainsText(hw, "bool nwbGiTraceOccluded(float3 origin, float3 direction, float tMin, float tMax){"));
    EXPECT_TRUE(ContainsText(hw, "RAY_FLAG_FORCE_OPAQUE"));
}


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

    EXPECT_TRUE(ContainsText(transparentCsgIntervalBuilder, "GatherPreparedMaterialGeometryResourceSet("));
    EXPECT_TRUE(ContainsText(transparentCsgIntervalBuilder, "GatherPreparedMaterialSampledTextureResourceSet("));
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

    EXPECT_TRUE(ContainsText(avboitGeometryPreparationBuilder, "GatherPreparedMaterialGeometryResourceSet("));
    EXPECT_TRUE(ContainsText(avboitGeometryPreparationBuilder, "GatherPreparedMaterialSampledTextureResourceSet("));
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

