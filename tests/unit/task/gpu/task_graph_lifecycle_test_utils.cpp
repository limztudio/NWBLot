// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_lifecycle_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PayloadDestroyTask::Payload::Payload(Payload&& other)noexcept
    : destructionCount(other.destructionCount)
{
    other.destructionCount = nullptr;
}

PayloadDestroyTask::Payload::~Payload()noexcept{
    if(destructionCount)
        ++*destructionCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PacketLifecycleTask::Accepted(Payload& payload, const Core::QueueSubmissionToken&)noexcept{
    if(payload.acceptedCount)
        ++*payload.acceptedCount;
}

void PacketLifecycleTask::Discarded(Payload& payload)noexcept{
    if(payload.discardedCount)
        ++*payload.discardedCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReentrantCompileDuringDeclarationTask::Payload::Payload(Payload&& other)
    : graph(other.graph)
    , analysis(other.analysis)
    , scratchArena(other.scratchArena)
    , compileAttempted(other.compileAttempted)
    , compileSucceeded(other.compileSucceeded)
{
    other.graph = nullptr;
    other.analysis = nullptr;
    other.scratchArena = nullptr;
    other.compileAttempted = nullptr;
    other.compileSucceeded = nullptr;
    if(!graph || !analysis || !scratchArena)
        return;

    if(compileAttempted)
        *compileAttempted = true;
    const Core::GpuTaskGraphCompiler compiler;
    const Core::GpuTaskGraph::DeclarationReadView declarations = Core::GpuTaskGraph::DeclarationReadView::TryAcquire(*graph);
    b8 succeeded = false;
    if(declarations.valid())
        succeeded = compiler.analyze(declarations, *scratchArena, *analysis);
    if(compileSucceeded)
        *compileSucceeded = succeeded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReentrantTelemetryDuringDeclarationTask::Payload::Payload(Payload&& other)
    : state(other.state)
{
    other.state = nullptr;
    if(!state || !state->graph || !state->analysis || !state->assignments || !state->compiledGraph || !state->transaction || !state->tracker || !state->builder || !state->scratchArena)
        return;

    const Core::GpuTaskGraph::DeclarationReadView declarations = Core::GpuTaskGraph::DeclarationReadView::TryAcquire(*state->graph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(*state->compiledGraph);
    state->trackerValidationAttempted = true;
    state->trackerValidationSucceeded = state->tracker->validFor(declarations, *state->assignments, compiledPlan);
    state->trackerUpdateAttempted = true;
    state->trackerUpdateSucceeded = state->tracker->update(declarations, *state->assignments, compiledPlan, *state->transaction, *state->scratchArena);
    const Core::GpuTaskGraphTelemetryOptions options{ .queueAssignments = state->assignments, .compiledPlan = &compiledPlan, .queueAssignmentTelemetry = state->tracker, };
    state->appendAttempted = true;
    state->appendSucceeded = declarations.appendFrameGraphTelemetry(*state->analysis, *state->scratchArena, *state->builder, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 NativeRecordProbeTask::Record(const Payload& payload, Core::CommandList&, const Core::GpuTaskRecordContext&)noexcept{
    if(payload.recordCount)
        ++*payload.recordCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MalformedLifecycleTask::Record(const Payload& payload, Core::CommandList&, const Core::GpuTaskRecordContext&, [[maybe_unused]] const u32 lookalike)noexcept{
    if(payload.recordCount)
        ++*payload.recordCount;
    return true;
}

b8 MalformedLifecycleTask::Accepted(Payload& payload, const Core::QueueSubmissionToken&)noexcept{
    if(payload.acceptedCount)
        ++*payload.acceptedCount;
    return true;
}

void MalformedLifecycleTask::Discarded(const Payload& payload)noexcept{
    if(payload.discardedCount)
        ++*payload.discardedCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

