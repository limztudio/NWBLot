// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/runtime/runtime_setup_upload_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_setup_upload_graph_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SetupUploadGraph, LargeThresholdKeepsQueueRoutingAndSubmissionBoundary){
    using Core::GraphicsModuleDetail::SetupUploadGraph;
    const auto below = SetupUploadGraph::scheduling(SetupUploadGraph::s_LargeMinimumBytes - 1u, true, true);
    const auto at = SetupUploadGraph::scheduling(SetupUploadGraph::s_LargeMinimumBytes, true, true);
    EXPECT_EQ(below.cost, Core::GpuTaskCostHint::Tiny);
    EXPECT_EQ(at.cost, Core::GpuTaskCostHint::Large);
    for(const auto& hint : { below, at }){
        EXPECT_TRUE(hint.forceSubmissionBoundary);
        EXPECT_FALSE(hint.allowPacketMerge);
        EXPECT_TRUE(hint.allowSameClassQueueRouting);
        EXPECT_TRUE(hint.preferNonPrimarySameClassQueue);
        EXPECT_TRUE(hint.allowCrossFamilySameClassQueueRouting);
    }
}

TEST(SetupUploadGraph, UnknownStateAndCrossFamilySharingKeepUploadContract){
    using Core::GraphicsModuleDetail::SetupUploadGraph;
    EXPECT_EQ(SetupUploadGraph::finalState(Core::ResourceStates::Unknown), Core::ResourceStates::CopyDest);
    EXPECT_EQ(SetupUploadGraph::finalState(Core::ResourceStates::ShaderResource), Core::ResourceStates::ShaderResource);
    EXPECT_EQ(SetupUploadGraph::resolveConsumerSharing(Core::ResourceQueueSharing::Exclusive, Core::CommandQueue::Compute, false), Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    EXPECT_EQ(SetupUploadGraph::resolveConsumerSharing(Core::ResourceQueueSharing::Exclusive, Core::CommandQueue::Compute, true), Core::ResourceQueueSharing::AsyncCompute);
    EXPECT_EQ(SetupUploadGraph::resolveConsumerSharing(Core::ResourceQueueSharing::Graphics, Core::CommandQueue::Transfer, false), Core::ResourceQueueSharing::GraphicsAndTransfer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


