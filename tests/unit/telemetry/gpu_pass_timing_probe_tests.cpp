// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/smoke/gpu_pass_timing_probe.h>

#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_pass_timing_probe_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_PUBLISHED_WINDOWS_2 = GLB_TEXT("published_windows=2");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;


TEST(GpuPassTimingProbe, ReportsEveryRegisteredScopeBeyondSixtyFour){
    Tests::TestArena<> testArena;
    Core::Perf::TimingRecorder recorder(testArena.arena);
    recorder.setEnabled(true);
    constexpr u32 s_ScopeCount = 96u;
    Name lastScope;
    for(u32 index = 0u; index < s_ScopeCount; ++index){
        char scopeText[32u] = {};
        lastScope = Name(FormatDecimal(index, scopeText));
        const Core::Perf::TimingScopeId scope = recorder.registerScope(lastScope);
        ASSERT_TRUE(scope.valid());
        recorder.recordSample(scope, 0.001, 1u);
    }
    recorder.publishFrame(1u);
    const Core::Perf::TimingView view(recorder);
    Tests::Smoke::GpuPassTimingProbe probe(GLB_TEXT("Probe regression"));
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger);

    probe.recordFrame(0.25f, view);
    probe.recordFrame(0.25f, view);

    EXPECT_EQ(logger.messageCount(), s_ScopeCount + 1u);
    const TString<Core::Alloc::GlobalArena> lastScopeText = StringFormat(
        testArena.arena, GLB_TEXT("  {}: gpu_window_ms"), StringConvert(lastScope.resolvedText())
    );
    EXPECT_TRUE(logger.sawMessageContaining(lastScopeText));
    EXPECT_FALSE(logger.sawMessageContaining(s_PUBLISHED_WINDOWS_2));
}

TEST(GpuPassTimingProbe, GrowthPreservesAccumulationAndWatermarksAcrossIntervals){
    Tests::TestArena<> testArena;
    Core::Perf::TimingRecorder recorder(testArena.arena);
    recorder.setEnabled(true);
    const Core::Perf::TimingScopeId firstScope = recorder.registerScope(Name("tests/probe/first"));
    ASSERT_TRUE(firstScope.valid());
    recorder.recordSample(firstScope, 0.003, 1u);
    recorder.publishFrame(1u);
    const Core::Perf::TimingView view(recorder);
    Tests::Smoke::GpuPassTimingProbe probe(GLB_TEXT("Probe growth"));
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger);

    probe.recordFrame(0.25f, view);
    constexpr u32 s_AddedScopes = 96u;
    Core::Perf::TimingScopeId lastScope;
    for(u32 index = 0u; index < s_AddedScopes; ++index){
        char scopeText[32u] = {};
        lastScope = recorder.registerScope(Name(FormatDecimal(index, scopeText)));
        ASSERT_TRUE(lastScope.valid());
    }
    probe.recordFrame(0.25f, view);
    EXPECT_EQ(logger.messageCount(), 2u);
    EXPECT_FALSE(logger.sawMessageContaining(s_PUBLISHED_WINDOWS_2));

    // The old publication remains visible, but the next interval must not count it again.
    probe.recordFrame(0.25f, view);
    probe.recordFrame(0.25f, view);
    EXPECT_EQ(logger.messageCount(), 3u);

    recorder.recordSample(firstScope, 0.002, 2u);
    recorder.recordSample(lastScope, 0.004, 2u);
    recorder.publishFrame(2u);
    probe.recordFrame(0.25f, view);
    probe.recordFrame(0.25f, view);
    EXPECT_EQ(logger.messageCount(), 6u);
    const TString<Core::Alloc::GlobalArena> lastScopeText = StringFormat(
        testArena.arena, GLB_TEXT("  {}: gpu_window_ms"), StringConvert(recorder.scopeNameAt(s_AddedScopes).resolvedText())
    );
    EXPECT_TRUE(logger.sawMessageContaining(lastScopeText));
    EXPECT_FALSE(logger.sawMessageContaining(s_PUBLISHED_WINDOWS_2));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

