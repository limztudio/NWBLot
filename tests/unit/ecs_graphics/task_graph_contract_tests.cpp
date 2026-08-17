// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct TaskGraphContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<TaskGraphContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}

static usize CountText(const AStringView text, const AStringView expected){
    if(expected.empty())
        return 0u;
    usize count = 0u;
    usize offset = 0u;
    while(offset < text.size()){
        const usize found = text.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}

static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


// Caustics and Surfel GI choose a semantic producer task at graph declaration. Keep their normal-frame merge and
// presence validation task-based so a later packet split cannot leak compiler packet identities back into the
// renderer's effect policy.
TEST(EcsGraphics, EffectsTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "const Core::GpuTaskId causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticPhotonTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticResolveUpsampleTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiIrradianceClearTask,\n            m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiResolveTask,\n                m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSurfelGiTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredHardwareCausticsTask)"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId hardwareCausticsPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticPhotonPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId surfelGiPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticsPacket"));
}


// Prefix and shadow record spans are task-addressed. The renderer can still query the compiler for the exact
// terminal presentation packet elsewhere, but ordinary readiness and merge validation must not mirror packet IDs.
TEST(EcsGraphics, PrefixAndShadowTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowPrepareTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixDeferredClearFirstTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowVisibilityTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSoftwareCausticsTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_graphicsPrefixDeferredClearFirstTask"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowPreparePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId graphicsPrefixPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowVisibilityPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId softwareCausticsPacket"));
}


// The AVBOIT routing choice can still produce one or five submissions, but validation must ask whether semantic
// stages compiled and share their declared packet rather than duplicate packet IDs for each stage.
TEST(EcsGraphics, AvboitTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitPreTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitDepthWarpTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitExtinctionTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitIntegrationTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitAccumulationTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_deferredAvboitPreTask"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitPrePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitDepthWarpPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitExtinctionPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitIntegrationPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitAccumulationPacket"));
}


// The exact terminal packet is retained solely for the swap-chain binary signal. Every other normal renderer
// readiness check uses a declared task anchor or a semantic task range.
TEST(EcsGraphics, OnlyTerminalPresentationRetainsAPacketIdentity){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_EQ(CountText(system, "packetForTask("), 1u);
    EXPECT_TRUE(ContainsText(system, "packetForTask(terminalPresentationTask)"));
    EXPECT_TRUE(ContainsText(system, "terminalPresentationPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredLightingPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredCompositePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredPresentPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredLaggedLightingHistoryPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredFrameRecoveryPacket"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
