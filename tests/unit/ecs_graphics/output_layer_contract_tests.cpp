// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <impl/ecs_render/deferred/task_graph_present_task.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_output_layer_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::Accepted(payload, {});
    EXPECT_EQ(contributor.m_acceptanceCount, 0u);
    const Core::QueueSubmissionToken consumerToken{
        .value = 101u,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 3u,
        .queue = Core::CommandQueue::Graphics,
    };
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::Accepted(payload, consumerToken);
    EXPECT_EQ(contributor.m_acceptanceCount, 1u);
    EXPECT_EQ(contributor.m_acceptedGeneration, 17u);
    EXPECT_EQ(contributor.m_acceptedToken.value, consumerToken.value);
    EXPECT_TRUE(contributor.m_acceptedToken.matchesPhysicalQueue(0u, 3u));
    payload.outputLayer.frameGeneration = 0u;
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::Accepted(payload, consumerToken);
    EXPECT_EQ(contributor.m_acceptanceCount, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

