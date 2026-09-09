// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"

#include <impl/ecs_render/raytrace/scene_resources.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ray_surface_lighting_contract_tests{


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;

TEST(EcsGraphics, RaySceneSnapshotDoesNotTreatMissingResourcesAsHardwareAvailability){
    NWB::Impl::RayTracingSceneGraphResources resources;
    EXPECT_FALSE(resources.valid());
    resources.hardwareAvailable = true;
    EXPECT_FALSE(resources.valid());
}

TEST(EcsGraphics, RayHitLightingUsesMaterialBxdfWithExplicitObserverAndNoPrimaryScreenTransport){
    TestArena testArena;
    const TestPath root = RepoRoot(testArena);
    AString lightingSource;
    AString materialSource;
    AString cpuSource;
    ASSERT_TRUE(ReadTextFile(root / "impl/assets/graphics/raytrace/surface_lighting.slangi", lightingSource));
    ASSERT_TRUE(ReadTextFile(root / "impl/assets/graphics/shadow/instance_material.slangi", materialSource));
    ASSERT_TRUE(ReadTextFile(root / "impl/ecs_render/raytrace/rt_detail.cpp", cpuSource));
    const AStringView lighting(lightingSource.data(), lightingSource.size());
    EXPECT_TRUE(ContainsText(lighting, "surface.viewVector = nwbSafeNormalize(observerDirection"));
    EXPECT_TRUE(ContainsText(lighting, "surface.shadingModel = hit.shadingModelId"));
    EXPECT_TRUE(ContainsText(lighting, "nwbDeferredDispatchBxdf(surface.shadingModel, surface, int2(-1, -1))"));
    const usize disableScreenTransport = lighting.find("#define NWB_SCENE_SHADOW_CAUSTIC_SAMPLING_DISABLED");
    const usize sceneLighting = lighting.find("#include \"../scene/lighting.slangi\"");
    ASSERT_NE(disableScreenTransport, AStringView::npos);
    ASSERT_NE(sceneLighting, AStringView::npos);
    EXPECT_LT(disableScreenTransport, sceneLighting);
    EXPECT_TRUE(ContainsText(lighting, "bool nwbBxdfRequiresLinearOutput(){\n    return true;"));
    EXPECT_TRUE(ContainsText(AStringView(materialSource.data(), materialSource.size()), "uint shadingModelId;"));
    EXPECT_TRUE(ContainsText(AStringView(cpuSource.data(), cpuSource.size()), "material.shadingModelId = materialInfo.shadingModelId;"));
}

TEST(EcsGraphics, LinearSceneTransportDefersDisplayMappingUntilPresentation){
    TestArena testArena;
    const TestPath graphics = RepoRoot(testArena) / "impl/assets/graphics";
    AString lightingSource;
    AString presentationSource;
    ASSERT_TRUE(ReadTextFile(graphics / "deferred/lighting_cs.slang", lightingSource));
    ASSERT_TRUE(ReadTextFile(graphics / "deferred/composite_ps.slang", presentationSource));
    const AStringView lighting(lightingSource.data(), lightingSource.size());
    const AStringView presentation(presentationSource.data(), presentationSource.size());
    EXPECT_FALSE(ContainsText(lighting, "clamp(resolvedColor"));
    EXPECT_TRUE(ContainsText(presentation, "exposedColor / (exposedColor + g_NwbDeferredCompositePushConstants.shoulder)"));
    EXPECT_TRUE(ContainsText(presentation, "nwbHdr10EncodeScene(exposedColor)"));
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

