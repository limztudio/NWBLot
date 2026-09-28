// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"

#include <impl/ecs_render/deferred/task_graph_present_task.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_output_layer_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;
using namespace NWB;

class RecordingLayerContributor final : public Core::IGpuTaskGraphOutputLayerContributor{
public:
    virtual bool prepareTaskGraphOutputLayer(const Core::AcquiredPresentationFrame& frame)override{
        static_cast<void>(frame);
        return true;
    }
    virtual bool declareTaskGraphOutputLayer(Core::GpuTaskGraph& graph, Core::GpuTaskGraphOutputLayer& layer)override{
        static_cast<void>(graph);
        layer = {};
        return true;
    }
    virtual void acceptTaskGraphOutputLayer(const u64 generation, const Core::QueueSubmissionToken& token)override{
        ++m_acceptanceCount;
        m_acceptedGeneration = generation;
        m_acceptedToken = token;
    }


public:
    u32 m_acceptanceCount = 0u;
    u64 m_acceptedGeneration = 0u;
    Core::QueueSubmissionToken m_acceptedToken;
};

TEST(OutputLayer, FinalPresentAcceptanceForwardsExactImmutableGenerationAndConsumerToken){
    RecordingLayerContributor contributor;
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::Payload payload;
    payload.outputLayer.frameGeneration = 17u;
    payload.outputLayerContributor = &contributor;
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::accepted(payload, {});
    EXPECT_EQ(contributor.m_acceptanceCount, 0u);
    const Core::QueueSubmissionToken consumerToken{
        .value = 101u,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 3u,
        .queue = Core::CommandQueue::Graphics,
    };
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::accepted(payload, consumerToken);
    EXPECT_EQ(contributor.m_acceptanceCount, 1u);
    EXPECT_EQ(contributor.m_acceptedGeneration, 17u);
    EXPECT_EQ(contributor.m_acceptedToken.value, consumerToken.value);
    EXPECT_TRUE(contributor.m_acceptedToken.matchesPhysicalQueue(0u, 3u));
    payload.outputLayer.frameGeneration = 0u;
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::accepted(payload, consumerToken);
    EXPECT_EQ(contributor.m_acceptanceCount, 1u);
}

TEST(OutputLayer, RendererJoinsProducedVersionBeforeTerminalOverlayAndKeepsSceneTimingScopeExplicit){
    TestArena testArena;
    const TestPath root = RepoRoot(testArena);
    AString suffixSource;
    AString presentSource;
    AString shaderSource;
    AString timingSource;
    ASSERT_TRUE(ReadTextFile(root / "impl/ecs_render/deferred/task_graph_suffix_builder.cpp", suffixSource));
    ASSERT_TRUE(ReadTextFile(root / "impl/ecs_render/deferred/task_graph_present_task.cpp", presentSource));
    ASSERT_TRUE(ReadTextFile(root / "impl/assets/graphics/deferred/composite_ps.slang", shaderSource));
    ASSERT_TRUE(ReadTextFile(root / "impl/ecs_render/kernel/timing_names.h", timingSource));
    const AStringView suffix(suffixSource.data(), suffixSource.size());
    const AStringView present(presentSource.data(), presentSource.size());
    const AStringView shader(shaderSource.data(), shaderSource.size());
    EXPECT_TRUE(ContainsText(suffix, "declareTaskGraphOutputLayer(m_graph, outputLayer)"));
    EXPECT_TRUE(ContainsText(suffix, ".version = outputLayer.colorVersion,"));
    EXPECT_TRUE(ContainsText(suffix, ".role = Core::GpuTaskResourceVersionRole::Consume,"));
    EXPECT_TRUE(ContainsText(suffix, "presentDependencies[presentDependencyCount++] = outputLayer.readyTask;"));
    EXPECT_TRUE(ContainsText(present, "acceptTaskGraphOutputLayer(payload.outputLayer.frameGeneration, token)"));
    EXPECT_LT(suffix.find("declareTaskGraphOutputLayer("), suffix.find("backbuffer_availability"));
    EXPECT_LT(suffix.find("addTask<DeferredPresentGraphTask>"), suffix.find("declareTaskGraphPresentation("));
    EXPECT_TRUE(ContainsText(shader, "nwbOutputLayerOver(float3(sdrColor), outputLayer.rgb, outputLayer.a)"));
    EXPECT_TRUE(ContainsText(shader, "nwbHdr10EncodeSceneWithUi(float3(exposedColor), outputLayer.rgb, outputLayer.a)"));
    const AStringView timing(timingSource.data(), timingSource.size());
    EXPECT_TRUE(ContainsText(timing, "Independent UI work before scene begin is excluded"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

