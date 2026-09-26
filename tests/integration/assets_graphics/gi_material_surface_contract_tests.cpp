// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_gi_material_surface_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct GiMaterialSurfaceContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<GiMaterialSurfaceContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}

// A graph task may not retain a prepared material callback if its dynamic geometry or sampled-image collection failed. Keep this source-level contract narrow: the graph builder must leave the current frame for the native compatibility path before it can compile a callback with an undeclared bindless access.
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
    return tail == "return;" || tail.substr(0u, 12u) == "return false";
}

static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


TEST(EcsGraphics, GiMaterialSurfaceDispatchSupportsHeterogeneousFrostInterface){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString swTraceSource;
    AString hwTraceSource;
    AString dispatchCodegenSource;
    AString rtDetailSource;
    AString instanceMaterialSource;
    AString frostBindSource;
    AString frostSurfaceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_sw_trace.slangi", swTraceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_hw_trace.slangi", hwTraceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets_material" / "material_dispatch_codegen.cpp", dispatchCodegenSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_detail.cpp", rtDetailSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "shadow" / "instance_material.slangi", instanceMaterialSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "assets" / "shaders" / "frost_surface.bind", frostBindSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "assets" / "shaders" / "frost.surface", frostSurfaceSource));

    const AStringView swTrace(swTraceSource.data(), swTraceSource.size());
    const AStringView hwTrace(hwTraceSource.data(), hwTraceSource.size());
    const AStringView dispatchCodegen(dispatchCodegenSource.data(), dispatchCodegenSource.size());
    const AStringView rtDetail(rtDetailSource.data(), rtDetailSource.size());
    const AStringView instanceMaterial(instanceMaterialSource.data(), instanceMaterialSource.size());
    const AStringView frostBind(frostBindSource.data(), frostBindSource.size());
    const AStringView frostSurface(frostSurfaceSource.data(), frostSurfaceSource.size());

    EXPECT_TRUE(ContainsText(swTrace, "#include \"shadow/generated/transmittance_dispatch.slangi\""));
    EXPECT_TRUE(ContainsText(swTrace, "const NwbMeshSurface surface = nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(swTrace, "closest.albedo = surface.baseColor;"));
    EXPECT_TRUE(ContainsText(hwTrace, "#include \"shadow/generated/transmittance_dispatch.slangi\""));
    EXPECT_TRUE(ContainsText(hwTrace, "const NwbMeshSurface surface = nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(hwTrace, "closest.albedo = surface.baseColor;"));

    EXPECT_TRUE(ContainsText(dispatchCodegen, "s_ShadowTransmittanceBindNamespacePrefix = \"nwbShadowBindModel\""));
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
    const TestPath repoRoot = RepoRoot(testArena);

    AString commonSource;
    AString swSource;
    AString hwSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_trace_common.slangi", commonSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_sw_trace.slangi", swSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_hw_trace.slangi", hwSource));

    const AStringView common(commonSource.data(), commonSource.size());
    const AStringView sw(swSource.data(), swSource.size());
    const AStringView hw(hwSource.data(), hwSource.size());

    EXPECT_TRUE(ContainsText(common, "bool nwbGiTraceOccluded(float3 origin, float3 direction, float tMin, float tMax);"));
    EXPECT_TRUE(ContainsText(common, "nwbGiTraceOccluded("));
    EXPECT_TRUE(ContainsText(common, "nwbGiShadeHit"));
    EXPECT_TRUE(ContainsText(sw, "bool nwbGiTraceOccluded(float3 origin, float3 direction, float tMin, float tMax){"));
    EXPECT_TRUE(ContainsText(sw, "nwbGiSwInstanceOccluded"));
    EXPECT_TRUE(ContainsText(sw, "nwbRayTriangleMollerTrumbore(origin, direction, tMin, tMax, v0, v1, v2)"));
    EXPECT_TRUE(ContainsText(hw, "bool nwbGiTraceOccluded(float3 origin, float3 direction, float tMin, float tMax){"));
    EXPECT_TRUE(ContainsText(hw, "RAY_FLAG_FORCE_OPAQUE"));
}


// Every trace backend evaluates the generated material-surface dispatcher. Keep its dynamic Texture2D accesses coupled to the preflight snapshot and the graph's immutable ShaderResource set, rather than relying on the material heap selector alone.
TEST(EcsGraphics, TraceMaterialSampledTexturesAreFrozenAndGraphDeclared){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredLightingTaskGraphSource;
    AString shadowVisibilityTaskGraphSource;
    AString causticsTaskGraphSource;
    AString hardwareCausticsStageSource;
    AString surfelGiTaskGraphSource;
    AString materialSurfaceSource;
    AString rayTracingSystemSource;
    AString rayTracingSystemHeader;
    AString swBvhSource;
    AString swShadowTraceSource;
    AString hwShadowTraceSource;
    AString swCausticSource;
    AString hwCausticSource;
    AString swGiTraceSource;
    AString hwGiTraceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", causticsTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "hardware_caustics_stage_builder.cpp", hardwareCausticsStageSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_surface.cpp", materialSurfaceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingSystemHeader));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh_scene_swbvh.cpp", swBvhSource));
    AString swBvhTlasSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh_scene_tlas.cpp", swBvhTlasSource));
    swBvhSource.insert(swBvhSource.end(), swBvhTlasSource.begin(), swBvhTlasSource.end());
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "shadow" / "sw_shadow_traverse.slangi", swShadowTraceSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "assets" / "graphics" / "shadow" / "hardware_transparent_evaluate.slangi", hwShadowTraceSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "caustic" / "caustic_photon_sw_cs.slang", swCausticSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "caustic" / "caustic_photon_hw_chit.slang", hwCausticSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_sw_trace.slangi", swGiTraceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_hw_trace.slangi", hwGiTraceSource));

    const AStringView deferredLightingTaskGraph(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());
    const AStringView shadowVisibilityTaskGraph(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());
    const AStringView causticsTaskGraph(causticsTaskGraphSource.data(), causticsTaskGraphSource.size());
    const AStringView hardwareCausticsStage(hardwareCausticsStageSource.data(), hardwareCausticsStageSource.size());
    const usize hardwareSampledUseStart = hardwareCausticsStage.find("const Core::GpuTaskResourceSetUse traceMaterialSampledTextureSetUse{");
    ASSERT_NE(hardwareSampledUseStart, AStringView::npos);
    const usize hardwareSampledUseEnd = hardwareCausticsStage.find("};", hardwareSampledUseStart);
    ASSERT_NE(hardwareSampledUseEnd, AStringView::npos);
    const AStringView hardwareSampledUse = hardwareCausticsStage.substr(hardwareSampledUseStart, hardwareSampledUseEnd - hardwareSampledUseStart);
    const AStringView surfelGiTaskGraph(surfelGiTaskGraphSource.data(), surfelGiTaskGraphSource.size());
    const AStringView materialSurface(materialSurfaceSource.data(), materialSurfaceSource.size());
    const AStringView rayTracingSystem(rayTracingSystemSource.data(), rayTracingSystemSource.size());
    const AStringView rayTracingSystemHeaderView(rayTracingSystemHeader.data(), rayTracingSystemHeader.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView swShadowTrace(swShadowTraceSource.data(), swShadowTraceSource.size());
    const AStringView hwShadowTrace(hwShadowTraceSource.data(), hwShadowTraceSource.size());
    const AStringView swCaustic(swCausticSource.data(), swCausticSource.size());
    const AStringView hwCaustic(hwCausticSource.data(), hwCausticSource.size());
    const AStringView swGiTrace(swGiTraceSource.data(), swGiTraceSource.size());
    const AStringView hwGiTrace(hwGiTraceSource.data(), hwGiTraceSource.size());

    EXPECT_TRUE(ContainsText(materialSurface, "appendPreparedMaterialSurfaceSampledTextures"));
    EXPECT_TRUE(ContainsText(rayTracingSystemHeaderView, "PreparedShadowTraceMaterialSampledTextureVector"));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "appendPreparedShadowTraceMaterialSampledTextures"));
    EXPECT_TRUE(ContainsText(swBvh, "materialInfo->shadowTransmittanceModelId != Limit<u32>::s_Max"));

    EXPECT_TRUE(ContainsText(swShadowTrace, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(hwShadowTrace, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(swCaustic, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(hwCaustic, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(swGiTrace, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(hwGiTrace, "nwbShadowDispatchSurface"));

    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "render.trace_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "Trace Material Sampled Textures"));
    EXPECT_TRUE(ContainsText(shadowVisibilityTaskGraph, "render.shadow_visibility.soft_transparent_trace"));
    EXPECT_TRUE(ContainsText(shadowVisibilityTaskGraph, "render.shadow_visibility"));
    EXPECT_TRUE(ContainsText(causticsTaskGraph, "render.software_caustics.photons"));
    EXPECT_TRUE(ContainsText(hardwareCausticsStage, "render.hardware_caustics.photons"));
    EXPECT_TRUE(ContainsText(surfelGiTaskGraph, "render.surfel_gi.trace"));
    EXPECT_TRUE(ContainsText(hardwareCausticsStage, "traceMaterialSampledTextureSetUse"));
    EXPECT_TRUE(ContainsText(shadowVisibilityTaskGraph, "MakeTraceResourceSetUses(traceGeometrySet, traceMaterialSampledTextureSet)"));
    EXPECT_TRUE(ContainsText(causticsTaskGraph, "traceMaterialSampledTextureSetUse"));
    EXPECT_TRUE(ContainsText(surfelGiTaskGraph, "MakeTraceResourceSetUses(traceGeometrySet, traceMaterialSampledTextureSet)"));
    EXPECT_TRUE(ContainsText(hardwareSampledUse, ".resourceSet = inputs.traceMaterialSampledTextureSet"));
    EXPECT_TRUE(ContainsText(hardwareSampledUse, ".requiredState = Core::ResourceStates::ShaderResource"));
    EXPECT_TRUE(ContainsText(hardwareSampledUse, ".access = Core::GpuTaskResourceAccess::Read"));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        hardwareCausticsStage,
        "if(inputs.traceMaterialSampledTextureSet.valid())",
        "hardwarePhotonResourceSetUses[hardwarePhotonResourceSetUseCount++] = traceMaterialSampledTextureSetUse;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        hardwareCausticsStage,
        "Core::GpuTaskDesc hardwarePhotonDesc;",
        "hardwarePhotonResourceSetUseCount != 0u ? hardwarePhotonResourceSetUses : nullptr"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "HardwareCausticsStageInputs{",
        ".traceMaterialSampledTextureSet = traceMaterialSampledTextureSet"
    ));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "if(!hardwareCausticsStageBuilder.declare("));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare hardware-caustics stage",
        "return;"
    ));
    EXPECT_TRUE(ContainsText(
        swBvh,
        "HW shadow material context changed after graph preflight; rejecting frozen upload batch"
    ));
    // The software scene-BVH path prepares its material context preflight-only
    EXPECT_TRUE(ContainsText(
        swBvh,
        "could not freeze software scene traversal"
    ));
}


TEST(EcsGraphics, PreparedMaterialGraphDeclarationsFailClosedWhenResourceSetsAreIncomplete){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsPrefixTaskGraphSource;
    AString deferredLightingTaskGraphSource;
    AString transparentCsgIntervalBuilderSource;
    AString avboitGeometryPreparationBuilderSource;
    AString avboitOccupancyGraphSource;
    AString avboitExtinctionGraphSource;
    AString avboitAccumulationGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", graphicsPrefixTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "graph" / "frame_graph_avboit_occupancy.cpp", avboitOccupancyGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "graph" / "frame_graph_avboit_extinction.cpp", avboitExtinctionGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "graph" / "frame_graph_avboit_accumulation.cpp", avboitAccumulationGraphSource));
    deferredLightingTaskGraphSource.insert(deferredLightingTaskGraphSource.end(), avboitOccupancyGraphSource.begin(), avboitOccupancyGraphSource.end());
    deferredLightingTaskGraphSource.insert(deferredLightingTaskGraphSource.end(), avboitExtinctionGraphSource.begin(), avboitExtinctionGraphSource.end());
    deferredLightingTaskGraphSource.insert(deferredLightingTaskGraphSource.end(), avboitAccumulationGraphSource.begin(), avboitAccumulationGraphSource.end());
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "transparent_csg_interval_builder.cpp", transparentCsgIntervalBuilderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "geometry_preparation_builder.cpp", avboitGeometryPreparationBuilderSource));
    const AStringView graphicsPrefixTaskGraph(graphicsPrefixTaskGraphSource.data(), graphicsPrefixTaskGraphSource.size());
    const AStringView deferredLightingTaskGraph(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());
    const AStringView transparentCsgIntervalBuilder(transparentCsgIntervalBuilderSource.data(), transparentCsgIntervalBuilderSource.size());
    const AStringView avboitGeometryPreparationBuilder(avboitGeometryPreparationBuilderSource.data(), avboitGeometryPreparationBuilderSource.size());

    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque material geometry states",
        "return false;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque material sampled textures",
        "return false;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque CSG material geometry states",
        "return false;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        graphicsPrefixTaskGraph,
        "could not declare prepared opaque CSG material sampled textures",
        "return false;"
    ));

    EXPECT_TRUE(ContainsText(transparentCsgIntervalBuilder, "GatherPreparedMaterialGeometryResourceSet("));
    EXPECT_TRUE(ContainsText(transparentCsgIntervalBuilder, "GatherPreparedMaterialSampledTextureResourceSet("));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        transparentCsgIntervalBuilder,
        "if(!avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned)",
        "return false;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        transparentCsgIntervalBuilder,
        "if(!transparentCsgMaterialSampledTexturesCollected)",
        "return false;"
    ));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "if(!transparentCsgIntervalBuilder.declare("));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare transparent CSG interval producer",
        "return;"
    ));

    EXPECT_TRUE(ContainsText(avboitGeometryPreparationBuilder, "GatherPreparedMaterialGeometryResourceSet("));
    EXPECT_TRUE(ContainsText(avboitGeometryPreparationBuilder, "GatherPreparedMaterialSampledTextureResourceSet("));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        avboitGeometryPreparationBuilder,
        "if(!outResult.geometryOwned)",
        "return false;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        avboitGeometryPreparationBuilder,
        "if(!outResult.sampledTexturesCollected)",
        "return false;"
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

