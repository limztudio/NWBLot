// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_smoke_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Windows may deny foreground activation to the parent smoke harness after bootstrap. Keep the opt-in local to the
// frame-lagged executable so it bypasses only its focus throttle, uses Graphics' normal render-pass extension, and
// unregisters before the renderer/world can be destroyed.
TEST(EcsGraphics, FrameLaggedSmokeRendersWhenUnfocused){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", smokeSource));
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(smoke, "class FrameLaggedAsyncLightingUnfocusedPass final : public NWB::Core::IRenderPass"));
    EXPECT_TRUE(ContainsText(smoke, "virtual bool shouldRenderUnfocused()override{ return true; }"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.addRenderPassToBack(m_frameLaggedAsyncLightingUnfocusedPass);"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.removeRenderPass(m_frameLaggedAsyncLightingUnfocusedPass);"));

    const usize shutdownOffset = smoke.find("virtual void onShutdown()override");
    const usize updateOffset = smoke.find("virtual bool onUpdate", shutdownOffset);
    ASSERT_NE(shutdownOffset, AStringView::npos);
    ASSERT_NE(updateOffset, AStringView::npos);
    const AStringView shutdown = smoke.substr(shutdownOffset, updateOffset - shutdownOffset);
    const usize removeOffset = shutdown.find("removeFrameLaggedAsyncLightingUnfocusedPass();");
    const usize destroyOffset = shutdown.find("destroyWorld();");
    ASSERT_NE(removeOffset, AStringView::npos);
    ASSERT_NE(destroyOffset, AStringView::npos);
    EXPECT_LT(removeOffset, destroyOffset);
}


// The parity baseline must pin temporal AVBOIT in test code, not introduce a renderer/core feature toggle. The
// runner waits for the smoke marker only after that project has suspended its next submission.
TEST(EcsGraphics, TransparentAvboitBaselineCaptureIsFrameLockedAndTestOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString runnerSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "run.py", runnerSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView runner(runnerSource.data(), runnerSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(profiles, "capture_freeze_frame=96"));
    EXPECT_TRUE(ContainsText(profiles, "capture_ready_log=\"TransparentMultiSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(profiles, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(runner, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(runner, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(runner, "wait_for_log_message("));
    ExpectRendererBaselineEnvOwnedBySmokeHelper(repoRoot);
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(true)"));
    EXPECT_TRUE(ContainsText(smoke, "renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(false)"));
}


// Skinned CSG animates both the receiver and the cutter. It must use the same test-only freeze contract before its
// baseline can act as a runtime-skinning/CSG parity reference.
TEST(EcsGraphics, SkinnedCsgBaselineCaptureIsFrameLockedAndTestOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "csg_skinned_visible_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(profiles, "window_title=\"NWB Skinned CSG Smoke\""));
    EXPECT_TRUE(ContainsText(profiles, "capture_ready_log=\"CsgSkinnedVisibleSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(profiles, "fixed_delta_seconds=1.0 / 60.0"));
    ExpectRendererBaselineEnvOwnedBySmokeHelper(repoRoot);
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(true)"));
    EXPECT_TRUE(ContainsText(smoke, "CsgSkinnedVisibleSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(false)"));
}


// Caustic accumulation is temporal even with a static refractor. Keep its baseline warm-up frame-counted and on the
// same test-only fixed clock rather than allowing host throughput to choose an arbitrary convergence point.
TEST(EcsGraphics, CausticBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const usize causticsOffset = profiles.find("\"caustics\": BaselineProfile(");
    const usize surfelOffset = profiles.find("\"surfel-gi\": BaselineProfile(", causticsOffset);
    ASSERT_NE(causticsOffset, AStringView::npos);
    ASSERT_NE(surfelOffset, AStringView::npos);
    ASSERT_LT(causticsOffset, surfelOffset);
    const AStringView caustics = profiles.substr(causticsOffset, surfelOffset - causticsOffset);

    EXPECT_TRUE(ContainsText(caustics, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(caustics, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(caustics, "capture_ready_log=\"TransparentMultiSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(caustics, "fixed_delta_seconds=1.0 / 60.0"));
}


// Surfel GI has its own temporal producer/resolve sequence. Its baseline therefore needs a project-local, fixed
// frame boundary rather than inheriting wall-clock capture timing from an unrelated smoke scene.
TEST(EcsGraphics, SurfelGiBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "gi_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize surfelOffset = profiles.find("\"surfel-gi\": BaselineProfile(");
    const usize stressOffset = profiles.find("\"stress\": BaselineProfile(", surfelOffset);
    ASSERT_NE(surfelOffset, AStringView::npos);
    ASSERT_NE(stressOffset, AStringView::npos);
    ASSERT_LT(surfelOffset, stressOffset);
    const AStringView surfel = profiles.substr(surfelOffset, stressOffset - surfelOffset);

    EXPECT_TRUE(ContainsText(surfel, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(surfel, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(surfel, "capture_ready_log=\"GiTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(surfel, "fixed_delta_seconds=1.0 / 60.0"));
    ExpectRendererBaselineEnvOwnedBySmokeHelper(repoRoot);
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "GiTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
}


// Soft shadows retain temporal history even when the camera/yaw are frozen. Capture the same accepted history phase
// through the smoke-only fixed clock before using this scene as a parity reference.
TEST(EcsGraphics, SoftShadowBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "soft_shadow_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize softShadowOffset = profiles.find("\"soft-shadows\": BaselineProfile(");
    const usize causticsOffset = profiles.find("\"caustics\": BaselineProfile(", softShadowOffset);
    ASSERT_NE(softShadowOffset, AStringView::npos);
    ASSERT_NE(causticsOffset, AStringView::npos);
    ASSERT_LT(softShadowOffset, causticsOffset);
    const AStringView softShadows = profiles.substr(softShadowOffset, causticsOffset - softShadowOffset);

    EXPECT_TRUE(ContainsText(softShadows, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(softShadows, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(softShadows, "capture_ready_log=\"SoftShadowTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(softShadows, "fixed_delta_seconds=1.0 / 60.0"));
    ExpectRendererBaselineEnvOwnedBySmokeHelper(repoRoot);
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "SoftShadowTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
}


// The stress scene already has a dedicated M4 capture contract. Its baseline path must remain separate, while still
// pinning the dense skinned transparent/opaque workload to a reproducible frame and simulation clock.
TEST(EcsGraphics, StressBaselineUsesSeparateFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize stressOffset = profiles.find("\"stress\": BaselineProfile(");
    ASSERT_NE(stressOffset, AStringView::npos);
    const AStringView stress = profiles.substr(stressOffset);

    EXPECT_TRUE(ContainsText(stress, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(stress, "capture_freeze_frame=96"));
    EXPECT_TRUE(ContainsText(stress, "capture_ready_log=\"StressTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(stress, "fixed_delta_seconds=1.0 / 60.0"));
    ExpectRendererBaselineEnvOwnedBySmokeHelper(repoRoot);
    EXPECT_TRUE(ContainsText(smoke, "m4PixelCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "StressTestSmokeProject: M4 pixel capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "StressTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
}


// Device feature support is backend-owned immutable state after device creation. Runtime and smoke code may observe
// that state to qualify a route, but must not mutate it or compile duplicate executables that pretend a feature is
// absent. The paired captures therefore use one native executable and capability-skip the route this adapter lacks.
TEST(EcsGraphics, FeatureSupportAndSmokeRoutesRemainNativeCapabilityAuthoritative){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString moduleHeaderSource;
    AString moduleFeatureQueriesSource;
    AString testbedRuntimeSource;
    AString smokeHelperSource;
    AString smokeProjectSource;
    AString smokeCmakeSource;
    AString smokeLauncherSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime.h", moduleHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime_feature_queries.cpp", moduleFeatureQueriesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "CoolStuff" / "Testbed" / "runtime.cpp", testbedRuntimeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "smoke_scene_helpers.h", smokeHelperSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "CMakeLists.txt", smokeCmakeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "launch.py", smokeLauncherSource));

    static constexpr StringView s_SmokeProjectSourceNames[] = {
        "transparent_multi_project.cpp",
        "csg_visible_project.cpp",
        "skinned_caustic_project.cpp",
        "stress_test_project.cpp",
        "flicker_test_project.cpp",
        "soft_shadow_test_project.cpp",
        "gi_test_project.cpp",
    };
    for(const StringView sourceName : s_SmokeProjectSourceNames){
        AString source;
        ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / sourceName.data(), source));
        if(!smokeProjectSource.empty())
            smokeProjectSource += "\n\n";
        smokeProjectSource.append(source.data(), source.size());
    }

    const AStringView moduleHeader(moduleHeaderSource.data(), moduleHeaderSource.size());
    const AStringView moduleFeatureQueries(moduleFeatureQueriesSource.data(), moduleFeatureQueriesSource.size());
    const AStringView testbedRuntime(testbedRuntimeSource.data(), testbedRuntimeSource.size());
    const AStringView smokeHelper(smokeHelperSource.data(), smokeHelperSource.size());
    const AStringView smokeProjects(smokeProjectSource.data(), smokeProjectSource.size());
    const AStringView smokeCmake(smokeCmakeSource.data(), smokeCmakeSource.size());
    const AStringView smokeLauncher(smokeLauncherSource.data(), smokeLauncherSource.size());

    EXPECT_TRUE(ContainsText(
        moduleFeatureQueries,
        "bool GraphicsRuntime::queryFeatureSupport(const Feature::Enum feature, void* featureInfo, const usize featureInfoSize)const{\n"
        "    auto& device = getDevice();\n"
        "    return device.queryFeatureSupport(feature, featureInfo, featureInfoSize);\n"
        "}"
    ));

    static constexpr StringView s_RetiredProductionTokens[] = {
        "setFeatureSupportDisabledForTesting",
        "clearFeatureSupportDisabledForTesting",
        "m_disabledFeatureSupportMask",
        "NWB_TESTBED_FORCE_RAYTRACING_EMULATION",
    };
    for(const StringView token : s_RetiredProductionTokens){
        EXPECT_FALSE(ContainsText(moduleHeader, token));
        EXPECT_FALSE(ContainsText(moduleFeatureQueries, token));
        EXPECT_FALSE(ContainsText(testbedRuntime, token));
    }
    EXPECT_FALSE(ContainsText(smokeHelper, "DisableSmokeRayTracingForTesting"));

    static constexpr StringView s_RetiredSmokeForceMacros[] = {
        "NWB_TRANSPARENT_MULTI_FORCE_RT_EMULATION",
        "NWB_SKINNED_CAUSTIC_FORCE_RT_EMULATION",
        "NWB_STRESS_TEST_FORCE_RT_EMULATION",
        "NWB_FLICKER_TEST_FORCE_RT_EMULATION",
        "NWB_SOFT_SHADOW_TEST_FORCE_RT_EMULATION",
        "NWB_GI_TEST_FORCE_RT_EMULATION",
        "NWB_CSG_VISIBLE_FORCE_MESHLET_EMULATION",
    };
    for(const StringView token : s_RetiredSmokeForceMacros){
        EXPECT_FALSE(ContainsText(smokeProjects, token));
        EXPECT_FALSE(ContainsText(smokeCmake, token));
        EXPECT_FALSE(ContainsText(smokeLauncher, token));
    }

    static constexpr StringView s_RetiredSmokeTargets[] = {
        "nwb_transparent_multi_sw_smoke",
        "nwb_caustic_sphere_sw_smoke",
        "nwb_csg_visible_compute_emulation_smoke",
        "nwb_skinned_caustic_sw_smoke",
        "nwb_stress_test_sw_smoke",
        "nwb_flicker_test_sw_smoke",
        "nwb_soft_shadow_test_sw_smoke",
        "nwb_gi_test_sw_smoke",
    };
    for(const StringView target : s_RetiredSmokeTargets){
        EXPECT_FALSE(ContainsText(smokeCmake, target));
        EXPECT_FALSE(ContainsText(smokeLauncher, target));
    }

    static constexpr StringView s_NativeHybridMarker =
        "TransparentMultiSmokeProject: natural hybrid shadow route selected on RayQuery-capable hardware";
    static constexpr StringView s_NativeSoftwareMarker =
        "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable";
    static constexpr StringView s_NativeMeshMarker =
        "CsgVisibleSmokeProject: natural native mesh-shader route selected";
    static constexpr StringView s_NativeComputeMarker =
        "CsgVisibleSmokeProject: natural compute-emulation route selected because Meshlets are unavailable";
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeHybridMarker));
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeSoftwareMarker));
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeMeshMarker));
    EXPECT_TRUE(ContainsText(smokeProjects, s_NativeComputeMarker));
    EXPECT_EQ(CountText(smokeCmake, s_NativeHybridMarker), 2u);
    EXPECT_EQ(CountText(smokeCmake, s_NativeSoftwareMarker), 2u);
    EXPECT_EQ(CountText(smokeCmake, s_NativeMeshMarker), 2u);
    EXPECT_EQ(CountText(smokeCmake, s_NativeComputeMarker), 2u);
    EXPECT_TRUE(ContainsText(
        smokeCmake,
        "nwb_transparent_multi_sw_capture_smoke\n"
        "            transparent_multi_sw_capture.bmp\n"
        "            \"$<TARGET_FILE:nwb_transparent_multi_smoke>\""
    ));
    EXPECT_TRUE(ContainsText(
        smokeCmake,
        "nwb_csg_visible_compute_emulation_capture_smoke\n"
        "            csg_visible_compute_emulation_capture.bmp\n"
        "            \"$<TARGET_FILE:nwb_csg_visible_smoke>\""
    ));
    EXPECT_GE(CountText(smokeCmake, "\"--skip-blocking-log-message\" \"VUID-\""), 4u);

    EXPECT_EQ(CountText(smokeLauncher, "\"native\": SmokeExecutable("), 15u);
    EXPECT_TRUE(ContainsText(smokeLauncher, "\"native\": SmokeExecutable(\"nwb_refraction_smoke\", \"refraction_smoke\")"));
    EXPECT_FALSE(ContainsText(smokeLauncher, "\"hw\": SmokeExecutable("));
    EXPECT_FALSE(ContainsText(smokeLauncher, "\"sw\": SmokeExecutable("));
    EXPECT_FALSE(ContainsText(smokeLauncher, "\"compute\": SmokeExecutable("));
    EXPECT_TRUE(ContainsText(smokeLauncher, "default=\"native\""));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

