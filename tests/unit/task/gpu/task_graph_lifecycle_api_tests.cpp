// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_lifecycle_api_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

template<typename GraphT>
concept HasDeclarationTaskAcceptance = requires(
    GraphT& graph,
    const Graphics::GpuTaskId& task,
    const Graphics::QueueSubmissionToken& token
){
    graph.acceptTask(task, token);
};

template<typename GraphT>
concept HasAttemptTaskAcceptance = requires(
    GraphT& graph,
    const Graphics::GpuTaskId& task,
    const Graphics::QueueSubmissionToken& token,
    const u64 recordingAttemptGeneration
){
    graph.acceptTask(task, token, recordingAttemptGeneration);
};

template<typename GraphT>
concept HasDeclarationTaskDiscard = requires(
    const GraphT& graph,
    const Graphics::GpuTaskId& task
){
    graph.discardTask(task);
};

template<typename GraphT>
concept HasPublicPacketSubmissionLease = requires{
    typename GraphT::PacketSubmissionLease;
};

template<typename GraphT>
concept HasPublicTaskLifecycleState = requires{
    typename GraphT::TaskLifecycleState;
};

template<typename GraphT>
concept HasSinglePacketDiscard = requires(
    const GraphT& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuSubmissionPacketId& packet,
    const u64 recordingAttemptGeneration
){
    graph.discardUnacceptedPacket(compiledGraph, packet, recordingAttemptGeneration);
};

template<typename GraphT>
concept HasCompiledBarrierApplication = requires(
    const GraphT& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuCompiledBarrier& barrier,
    Graphics::CommandList& commandList
){
    graph.applyCompiledBarrier(compiledGraph, barrier, commandList);
};

template<typename GraphT>
concept HasTaskRetainedResourceStateSeeding = requires(
    const GraphT& graph,
    const Graphics::GpuTaskId& task,
    Graphics::CommandList& commandList
){
    graph.seedTaskRetainedResourceStates(task, commandList);
};

template<typename TransactionT>
concept HasSinglePacketRejection = requires(
    TransactionT& transaction,
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuSubmissionPacketId& packet
){
    transaction.rejectPacket(graph, compiledGraph, packet);
};

template<typename TransactionT>
concept HasAttemptSinglePacketRejection = requires(
    TransactionT& transaction,
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuSubmissionPacketId& packet,
    const u64 recordingAttemptGeneration
){
    transaction.rejectPacket(graph, compiledGraph, packet, recordingAttemptGeneration);
};

template<typename TransactionT>
concept HasGenerationInferredTaskRejection = requires(
    TransactionT& transaction,
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuTaskId& task
){
    transaction.rejectTask(graph, compiledGraph, task);
};

template<typename TransactionT>
concept HasAttemptTaskRejection = requires(
    TransactionT& transaction,
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuTaskId& task,
    const u64 recordingAttemptGeneration
){
    transaction.rejectTask(graph, compiledGraph, task, recordingAttemptGeneration);
};

template<typename TransactionT>
concept HasGenerationInferredDiscardUnaccepted = requires(
    TransactionT& transaction,
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuCompiledGraph& compiledGraph
){
    transaction.discardUnaccepted(graph, compiledGraph);
};

template<typename TransactionT>
concept HasAttemptDiscardUnaccepted = requires(
    TransactionT& transaction,
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
){
    transaction.discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration);
};

template<typename TransactionT>
concept HasAcceptedQueueFrontierWaitTokens = requires(
    const TransactionT& transaction,
    const Graphics::GpuPhysicalQueueId& destinationQueue,
    Vector<Graphics::QueueSubmissionToken, Graphics::Alloc::ScratchArena>& outTokens
){
    transaction.appendAcceptedQueueFrontierWaitTokens(destinationQueue, outTokens);
};

template<typename TransactionT>
concept HasNoexceptGraphAwareExternalResourceHandoff = requires(
    const TransactionT& transaction,
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuRecordedGraph& recordedGraph,
    const Graphics::GpuGraphResourceId& resource
){
    { transaction.externalResourceHandoff(graph, compiledGraph, recordedGraph, resource) } noexcept;
};

template<typename TransactionT>
concept HasNoexceptLegacyExternalResourceHandoff = requires(
    const TransactionT& transaction,
    const Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuRecordedGraph& recordedGraph,
    const Graphics::GpuGraphResourceId& resource
){
    { transaction.externalResourceHandoff(compiledGraph, recordedGraph, resource) } noexcept;
};

template<typename HandoffT>
concept HasImplicitScratchFanIn = requires(
    HandoffT& result,
    const HandoffT& base,
    const HandoffT* const* branches
){
    result.buildFanIn(base, branches, 0u);
};

template<typename TransactionT>
concept HasPublicPacketRuntimeState = requires{
    typename TransactionT::PacketRuntimeState;
};

template<typename TransactionT>
concept HasPublicPacketRuntime = requires{
    typename TransactionT::PacketRuntime;
};

static_assert(!HasDeclarationTaskAcceptance<Graphics::GpuTaskGraph>);
static_assert(!HasAttemptTaskAcceptance<Graphics::GpuTaskGraph>);
static_assert(!HasDeclarationTaskDiscard<Graphics::GpuTaskGraph>);
static_assert(!HasPublicPacketSubmissionLease<Graphics::GpuTaskGraph>);
static_assert(!HasPublicTaskLifecycleState<Graphics::GpuTaskGraph>);
static_assert(!HasSinglePacketDiscard<Graphics::GpuTaskGraph>);
static_assert(!HasCompiledBarrierApplication<Graphics::GpuTaskGraph>);
static_assert(!HasTaskRetainedResourceStateSeeding<Graphics::GpuTaskGraph>);
static_assert(!HasSinglePacketRejection<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasAttemptSinglePacketRejection<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasGenerationInferredTaskRejection<Graphics::GpuGraphSubmissionTransaction>);
static_assert(HasAttemptTaskRejection<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasGenerationInferredDiscardUnaccepted<Graphics::GpuGraphSubmissionTransaction>);
static_assert(HasAttemptDiscardUnaccepted<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasAcceptedQueueFrontierWaitTokens<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasNoexceptGraphAwareExternalResourceHandoff<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasNoexceptLegacyExternalResourceHandoff<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasImplicitScratchFanIn<Graphics::CommandListResourceStateHandoff>);
static_assert(!HasPublicPacketRuntimeState<Graphics::GpuGraphSubmissionTransaction>);
static_assert(!HasPublicPacketRuntime<Graphics::GpuGraphSubmissionTransaction>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

