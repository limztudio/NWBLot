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
    virtual Expected<Core::GpuTaskGraphOutputLayer> declareTaskGraphOutputLayer(Core::GpuTaskGraph& graph)override{
        static_cast<void>(graph);
        return Core::GpuTaskGraphOutputLayer{};
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
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::AcceptOutputLayer(nullptr, 17u, {});
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::AcceptOutputLayer(&contributor, 17u, {});
    EXPECT_EQ(contributor.m_acceptanceCount, 0u);
    const Core::QueueSubmissionToken consumerToken{
        .value = 101u,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 3u,
        .queue = Core::CommandQueue::Graphics,
    };
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::AcceptOutputLayer(&contributor, 17u, consumerToken);
    EXPECT_EQ(contributor.m_acceptanceCount, 1u);
    EXPECT_EQ(contributor.m_acceptedGeneration, 17u);
    EXPECT_EQ(contributor.m_acceptedToken.value, consumerToken.value);
    EXPECT_TRUE(contributor.m_acceptedToken.matchesPhysicalQueue(0u, 3u));
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::AcceptOutputLayer(&contributor, 0u, consumerToken);
    EXPECT_EQ(contributor.m_acceptanceCount, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

