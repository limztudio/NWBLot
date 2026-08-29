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

// A graph task may not retain a prepared material callback if its dynamic geometry or sampled-image collection
// failed. Keep this source-level contract narrow: the graph builder must leave the current frame for the native
// compatibility path before it can compile a callback with an undeclared bindless access.
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


// Every trace backend evaluates the generated material-surface dispatcher. Keep its dynamic Texture2D accesses
// coupled to the preflight snapshot and the graph's immutable ShaderResource set, rather than relying on the
// material heap selector alone.
TEST(EcsGraphics, TraceMaterialSampledTexturesAreFrozenAndGraphDeclared){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredLightingTaskGraphSource;
    AString shadowVisibilityTaskGraphSource;
    AString causticsTaskGraphSource;
    AString surfelGiTaskGraphSource;
    AString materialSurfaceSource;
    AString rayTracingSystemSource;
    AString rayTracingSystemHeader;
    AString swBvhSource;
    AString swShadowTraceSource;
    AString swCausticSource;
    AString hwCausticSource;
    AString swGiTraceSource;
    AString hwGiTraceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", causticsTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_surface.cpp", materialSurfaceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingSystemHeader));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "shadow" / "sw_shadow_traverse.slangi", swShadowTraceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "caustic" / "caustic_photon_sw_cs.slang", swCausticSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "caustic" / "caustic_photon_hw_chit.slang", hwCausticSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_sw_trace.slangi", swGiTraceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets" / "graphics" / "gi" / "gi_hw_trace.slangi", hwGiTraceSource));

    const AStringView deferredLightingTaskGraph(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());
    const AStringView shadowVisibilityTaskGraph(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());
    const AStringView causticsTaskGraph(causticsTaskGraphSource.data(), causticsTaskGraphSource.size());
    const AStringView surfelGiTaskGraph(surfelGiTaskGraphSource.data(), surfelGiTaskGraphSource.size());
    const AStringView materialSurface(materialSurfaceSource.data(), materialSurfaceSource.size());
    const AStringView rayTracingSystem(rayTracingSystemSource.data(), rayTracingSystemSource.size());
    const AStringView rayTracingSystemHeaderView(rayTracingSystemHeader.data(), rayTracingSystemHeader.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView swShadowTrace(swShadowTraceSource.data(), swShadowTraceSource.size());
    const AStringView swCaustic(swCausticSource.data(), swCausticSource.size());
    const AStringView hwCaustic(hwCausticSource.data(), hwCausticSource.size());
    const AStringView swGiTrace(swGiTraceSource.data(), swGiTraceSource.size());
    const AStringView hwGiTrace(hwGiTraceSource.data(), hwGiTraceSource.size());

    EXPECT_TRUE(ContainsText(materialSurface, "appendPreparedMaterialSurfaceSampledTextures"));
    EXPECT_TRUE(ContainsText(rayTracingSystemHeaderView, "PreparedShadowTraceMaterialSampledTextureVector"));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "appendPreparedShadowTraceMaterialSampledTextures"));
    EXPECT_TRUE(ContainsText(swBvh, "materialInfo->shadowTransmittanceModelId != Limit<u32>::s_Max"));

    EXPECT_TRUE(ContainsText(swShadowTrace, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(swCaustic, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(hwCaustic, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(swGiTrace, "nwbShadowDispatchSurface"));
    EXPECT_TRUE(ContainsText(hwGiTrace, "nwbShadowDispatchSurface"));

    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "render.trace_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "Trace Material Sampled Textures"));
    EXPECT_TRUE(ContainsText(shadowVisibilityTaskGraph, "render.shadow_visibility.soft_transparent_trace"));
    EXPECT_TRUE(ContainsText(shadowVisibilityTaskGraph, "render.shadow_visibility"));
    EXPECT_TRUE(ContainsText(causticsTaskGraph, "render.software_caustics.photons"));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "render.hardware_caustics.photons"));
    EXPECT_TRUE(ContainsText(surfelGiTaskGraph, "render.surfel_gi.trace"));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "traceMaterialSampledTextureSetUse"));
    EXPECT_TRUE(ContainsText(shadowVisibilityTaskGraph, "traceMaterialSampledTextureSetUse"));
    EXPECT_TRUE(ContainsText(causticsTaskGraph, "traceMaterialSampledTextureSetUse"));
    EXPECT_TRUE(ContainsText(surfelGiTaskGraph, "traceMaterialSampledTextureSetUse"));
    EXPECT_TRUE(ContainsText(deferredLightingTaskGraph, "hardwarePhotonResourceSetUses"));
    EXPECT_TRUE(ContainsText(
        rayTracingSystem,
        "frozen hybrid hardware material-context restore failed; rejecting shadow preparation packet"
    ));
    EXPECT_FALSE(ContainsText(rayTracingSystem, "hybrid hardware material-context fallback retried directly"));
}


TEST(EcsGraphics, PreparedMaterialGraphDeclarationsFailClosedWhenResourceSetsAreIncomplete){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsPrefixTaskGraphSource;
    AString deferredLightingTaskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", graphicsPrefixTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingTaskGraphSource));
    const AStringView graphicsPrefixTaskGraph(graphicsPrefixTaskGraphSource.data(), graphicsPrefixTaskGraphSource.size());
    const AStringView deferredLightingTaskGraph(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());

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

    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared transparent CSG material geometry states",
        "return;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared transparent CSG material sampled textures",
        "return;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared AVBOIT occupancy material geometry states",
        "return;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared AVBOIT occupancy material sampled textures",
        "return;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared AVBOIT extinction material geometry states",
        "return;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared AVBOIT extinction material sampled textures",
        "return;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared AVBOIT accumulation material geometry states",
        "return;"
    ));
    EXPECT_TRUE(ContainsBeforeClosingBrace(
        deferredLightingTaskGraph,
        "could not declare prepared AVBOIT accumulation material sampled textures",
        "return;"
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

