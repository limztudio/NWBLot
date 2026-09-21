// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_hybrid_fallback_contract_tests{


constexpr AStringView s_TransparentMaterialClassText = "const bool transparentMaterialClass = (index % 2u) == 0u;";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Hardware and explicit software captures retain distinct route expectations and the same benchmark scene geometry.

TEST(EcsGraphics, ShadowBenchmarkCapturesKeepExplicitRoutesAndComparableGeometry){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString smokeCmakeSource;
    AString stressTestProjectSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "CMakeLists.txt", smokeCmakeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", stressTestProjectSource));
    const AStringView smokeCmake(smokeCmakeSource.data(), smokeCmakeSource.size());
    const AStringView stressTestProject(stressTestProjectSource.data(), stressTestProjectSource.size());

    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_hybrid_shadow_boundary_fallback_benchmark"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_hybrid_shadow_boundary_fallback_capture_smoke"));
    const usize healthyCaptureOffset = smokeCmake.find("            nwb_hybrid_shadow_boundary_healthy_capture_smoke");
    const usize opaqueCaptureOffset = smokeCmake.find(
        "            nwb_hybrid_shadow_boundary_opaque_capture_smoke",
        healthyCaptureOffset
    );
    const usize opaqueCaptureEndOffset = smokeCmake.find(
        "        nwb_declare_executable(nwb_async_shadow_m4_sync_benchmark)",
        opaqueCaptureOffset
    );
    ASSERT_NE(healthyCaptureOffset, AStringView::npos);
    ASSERT_NE(opaqueCaptureOffset, AStringView::npos);
    ASSERT_NE(opaqueCaptureEndOffset, AStringView::npos);
    ASSERT_LT(healthyCaptureOffset, opaqueCaptureOffset);
    ASSERT_LT(opaqueCaptureOffset, opaqueCaptureEndOffset);
    const AStringView healthyCapture = smokeCmake.substr(
        healthyCaptureOffset,
        opaqueCaptureOffset - healthyCaptureOffset
    );
    const AStringView opaqueCapture = smokeCmake.substr(
        opaqueCaptureOffset,
        opaqueCaptureEndOffset - opaqueCaptureOffset
    );
    EXPECT_TRUE(ContainsText(healthyCapture, "$<TARGET_FILE:nwb_hybrid_shadow_boundary_healthy_benchmark>"));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: enabled healthy hardware transparent-shadow benchmark\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: RayQuery-capable hardware shadow route available\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"RendererSystem: dispatched hardware transparent shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--skip-log-message\" \"StressTestSmokeProject: hardware shadow boundary skipped because RayQuery-capable hardware is unavailable\""
    ));
    EXPECT_TRUE(ContainsText(healthyCapture, "\"--skip-blocking-log-message\" \"[ERROR]\""));
    EXPECT_TRUE(ContainsText(healthyCapture, "\"--skip-blocking-log-message\" \"failed to resolve shader\""));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: split opaque soft-shadow producer failed\\; retaining all-lit visibility\""
    ));
    EXPECT_TRUE(ContainsText(healthyCapture, "NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE=0"));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "CONFIGURATIONS dbg opt"
    ));
    EXPECT_FALSE(ContainsText(healthyCapture, "enabled natural opaque hardware-shadow baseline"));
    EXPECT_FALSE(ContainsText(healthyCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    EXPECT_TRUE(ContainsText(opaqueCapture, "$<TARGET_FILE:nwb_hybrid_shadow_boundary_healthy_benchmark>"));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: enabled natural opaque hardware-shadow baseline\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: RayQuery-capable hardware shadow route available\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--skip-log-message\" \"StressTestSmokeProject: hardware shadow boundary skipped because RayQuery-capable hardware is unavailable\""
    ));
    EXPECT_TRUE(ContainsText(opaqueCapture, "\"--skip-blocking-log-message\" \"[ERROR]\""));
    EXPECT_TRUE(ContainsText(opaqueCapture, "\"--skip-blocking-log-message\" \"failed to resolve shader\""));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: dispatched hardware transparent shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: split opaque soft-shadow producer failed\\; retaining all-lit visibility\""
    ));
    EXPECT_TRUE(ContainsText(opaqueCapture, "NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE=1"));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "CONFIGURATIONS dbg opt"
    ));
    EXPECT_FALSE(ContainsText(opaqueCapture, "enabled healthy hardware transparent-shadow benchmark"));
    EXPECT_FALSE(ContainsText(opaqueCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    const usize transparentCaptureOffset = smokeCmake.find("            nwb_transparent_multi_capture_smoke");
    const usize transparentCaptureEndOffset = smokeCmake.find(
        "            nwb_transparent_multi_sw_capture_smoke",
        transparentCaptureOffset
    );
    ASSERT_NE(transparentCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCaptureEndOffset, AStringView::npos);
    ASSERT_LT(transparentCaptureOffset, transparentCaptureEndOffset);
    const AStringView transparentCapture = smokeCmake.substr(
        transparentCaptureOffset,
        transparentCaptureEndOffset - transparentCaptureOffset
    );
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "\"--expect-log-message\" \"RendererSystem: dispatched hardware transparent shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "ENVIRONMENT \"NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=6\""
    ));
    EXPECT_FALSE(ContainsText(transparentCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    const usize opaqueBaselineHelperOffset = stressTestProject.find(
        "[[nodiscard]] static bool hybridShadowOpaqueBaseline()"
    );
    const usize opaqueBaselineHelperEndOffset = stressTestProject.find(
        "static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(",
        opaqueBaselineHelperOffset
    );
    ASSERT_NE(opaqueBaselineHelperOffset, AStringView::npos);
    ASSERT_NE(opaqueBaselineHelperEndOffset, AStringView::npos);
    const AStringView opaqueBaselineHelper = stressTestProject.substr(
        opaqueBaselineHelperOffset,
        opaqueBaselineHelperEndOffset - opaqueBaselineHelperOffset
    );
    EXPECT_TRUE(ContainsText(opaqueBaselineHelper, "#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)"));
    EXPECT_TRUE(ContainsText(
        opaqueBaselineHelper,
        "ReadSmokeEnvironmentFlag(\"NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE\")"
    ));
    EXPECT_TRUE(ContainsText(opaqueBaselineHelper, "return s_enabled;"));

    const usize createCharacterOffset = stressTestProject.find(
        "[[nodiscard]] NWB::Core::ECS::EntityID createCharacter(const u32 index)"
    );
    const usize createCharacterEndOffset = stressTestProject.find(
        "[[nodiscard]] NWB::Core::ECS::EntityID createWall(",
        createCharacterOffset
    );
    ASSERT_NE(createCharacterOffset, AStringView::npos);
    ASSERT_NE(createCharacterEndOffset, AStringView::npos);
    const AStringView createCharacter = stressTestProject.substr(
        createCharacterOffset,
        createCharacterEndOffset - createCharacterOffset
    );
    EXPECT_TRUE(ContainsText(createCharacter, s_TransparentMaterialClassText));
    EXPECT_TRUE(ContainsText(
        createCharacter,
        "const bool transparent = !hybridShadowOpaqueBaseline() && transparentMaterialClass;"
    ));
    EXPECT_TRUE(ContainsText(createCharacter, "const f32 z = transparentMaterialClass ? s_TransparentRowZ : s_OpaqueRowZ;"));

    const usize hybridBenchmarkStartupOffset = stressTestProject.find(
        "#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)",
        createCharacterEndOffset
    );
    const usize hybridBenchmarkStartupEndOffset = stressTestProject.find(
        "const u32 transparentCharacterCount",
        hybridBenchmarkStartupOffset
    );
    ASSERT_NE(hybridBenchmarkStartupOffset, AStringView::npos);
    ASSERT_NE(hybridBenchmarkStartupEndOffset, AStringView::npos);
    const AStringView hybridBenchmarkStartup = stressTestProject.substr(
        hybridBenchmarkStartupOffset,
        hybridBenchmarkStartupEndOffset - hybridBenchmarkStartupOffset
    );
    EXPECT_TRUE(ContainsText(
        hybridBenchmarkStartup,
        "queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)"
    ));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "queryFeatureSupport(NWB::Core::Feature::RayQuery)"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "if(!rayQueryCapable)"));
    EXPECT_TRUE(ContainsText(
        hybridBenchmarkStartup,
        "hardware shadow boundary skipped because RayQuery-capable hardware is unavailable"
    ));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "if(hybridShadowOpaqueBaseline())"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "enabled natural opaque hardware-shadow baseline"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "enabled healthy hardware transparent-shadow benchmark"));
    EXPECT_FALSE(ContainsText(hybridBenchmarkStartup, "NWB_FATAL_ASSERT_MSG("));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

