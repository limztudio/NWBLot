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


static constexpr TStringView s_PublishedWindows2 = NWB_TEXT("published_windows=2");


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
        const auto scopeName = Name::FromText(FormatDecimal(index, scopeText));
        ASSERT_TRUE(scopeName);
        lastScope = *scopeName;
        const auto scopeResult = recorder.registerScope(lastScope);
        ASSERT_TRUE(scopeResult);
        const Core::Perf::TimingScopeId scope = *scopeResult;
        ASSERT_TRUE(scope.valid());
        recorder.recordSample(scope, 0.001, 1u);
    }
    recorder.publishFrame(1u);
    const Core::Perf::TimingView view(recorder);
    Tests::Smoke::GpuPassTimingProbe probe(NWB_TEXT("Probe regression"));
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger);

    probe.recordFrame(0.25f, view);
    probe.recordFrame(0.25f, view);

    EXPECT_EQ(logger.messageCount(), s_ScopeCount + 1u);
    const TString<Core::Alloc::GlobalArena> lastScopeText = StringFormat(testArena.arena, NWB_TEXT("  {}: gpu_window_ms"), StringConvert(lastScope.resolvedText()));
    EXPECT_TRUE(logger.sawMessageContaining(lastScopeText));
    EXPECT_FALSE(logger.sawMessageContaining(s_PublishedWindows2));
}

TEST(GpuPassTimingProbe, GrowthPreservesAccumulationAndWatermarksAcrossIntervals){
    Tests::TestArena<> testArena;
    Core::Perf::TimingRecorder recorder(testArena.arena);
    recorder.setEnabled(true);
    const auto firstScopeResult = recorder.registerScope(Name("tests/probe/first"));
    ASSERT_TRUE(firstScopeResult);
    const Core::Perf::TimingScopeId firstScope = *firstScopeResult;
    ASSERT_TRUE(firstScope.valid());
    recorder.recordSample(firstScope, 0.003, 1u);
    recorder.publishFrame(1u);
    const Core::Perf::TimingView view(recorder);
    Tests::Smoke::GpuPassTimingProbe probe(NWB_TEXT("Probe growth"));
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger);

    probe.recordFrame(0.25f, view);
    constexpr u32 s_AddedScopes = 96u;
    Core::Perf::TimingScopeId lastScope;
    for(u32 index = 0u; index < s_AddedScopes; ++index){
        char scopeText[32u] = {};
        const auto scopeName = Name::FromText(FormatDecimal(index, scopeText));
        ASSERT_TRUE(scopeName);
        const auto scopeResult = recorder.registerScope(*scopeName);
        ASSERT_TRUE(scopeResult);
        lastScope = *scopeResult;
        ASSERT_TRUE(lastScope.valid());
    }
    probe.recordFrame(0.25f, view);
    EXPECT_EQ(logger.messageCount(), 2u);
    EXPECT_FALSE(logger.sawMessageContaining(s_PublishedWindows2));

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
    const TString<Core::Alloc::GlobalArena> lastScopeText = StringFormat(testArena.arena, NWB_TEXT("  {}: gpu_window_ms"), StringConvert(recorder.scopeNameAt(s_AddedScopes).resolvedText()));
    EXPECT_TRUE(logger.sawMessageContaining(lastScopeText));
    EXPECT_FALSE(logger.sawMessageContaining(s_PublishedWindows2));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

