// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>
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

static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().lexically_normal();
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
    EXPECT_TRUE(ContainsText(dispatchCodegen, "half3(0.5, 0.5, 0.5)"));

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

