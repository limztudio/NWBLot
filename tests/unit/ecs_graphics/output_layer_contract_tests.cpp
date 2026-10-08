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
    virtual void acceptTaskGraphOutputLayer(const u64, const Core::QueueSubmissionToken&)override{
        ++m_acceptanceCount;
    }


public:
    u32 m_acceptanceCount = 0u;
};

TEST(OutputLayer, MissingContributorInvalidTokenAndZeroGenerationCannotPublishAcceptance){
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
    Impl::RendererTaskGraphDetail::DeferredPresentGraphTask::AcceptOutputLayer(&contributor, 0u, consumerToken);
    EXPECT_EQ(contributor.m_acceptanceCount, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

