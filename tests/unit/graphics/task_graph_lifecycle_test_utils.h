// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PayloadDestroyTask{
    struct Payload{
        u32* destructionCount = nullptr;

        explicit Payload(u32* const value)
            : destructionCount(value)
        {}
        Payload(Payload&& other)noexcept
            : destructionCount(other.destructionCount)
        {
            other.destructionCount = nullptr;
        }
        Payload(const Payload&) = delete;
        ~Payload(){
            if(destructionCount)
                ++*destructionCount;
        }
    };
};

struct PacketLifecycleTask{
    struct Payload{
        u32* acceptedCount = nullptr;
        u32* discardedCount = nullptr;
        Graphics::QueueSubmissionToken* acceptedToken = nullptr;
    };

    static void accepted(Payload& payload, const Graphics::QueueSubmissionToken& token){
        if(payload.acceptedCount)
            ++*payload.acceptedCount;
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};

struct ReentrantCompileDuringDeclarationTask{
    struct Payload{
        const Graphics::GpuTaskGraph* graph = nullptr;
        Graphics::GpuTaskGraphAnalysis* analysis = nullptr;
        Core::Alloc::ScratchArena* scratchArena = nullptr;
        bool* compileAttempted = nullptr;
        bool* compileSucceeded = nullptr;

        Payload() = default;
        Payload(const Payload&) = delete;
        Payload(Payload&& other)
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
            const Graphics::GpuTaskGraphCompiler compiler;
            const Graphics::GpuTaskGraph::DeclarationReadView declarations =
                Graphics::GpuTaskGraph::DeclarationReadView::tryAcquire(*graph);
            bool succeeded = false;
            if(declarations.valid())
                succeeded = compiler.analyze(declarations, *analysis, *scratchArena);
            if(compileSucceeded)
                *compileSucceeded = succeeded;
        }
    };
};

struct ReentrantTelemetryDuringDeclarationState{
    const Graphics::GpuTaskGraph* graph = nullptr;
    const Graphics::GpuTaskGraphAnalysis* analysis = nullptr;
    const Graphics::GpuTaskGraphQueueAssignments* assignments = nullptr;
    const Graphics::GpuCompiledGraph* compiledGraph = nullptr;
    const Graphics::GpuGraphSubmissionTransaction* transaction = nullptr;
    Graphics::GpuTaskGraphQueueAssignmentTelemetryTracker* tracker = nullptr;
    Telemetry::FrameGraphBuilder* builder = nullptr;
    Core::Alloc::ScratchArena* scratchArena = nullptr;
    bool trackerValidationAttempted = false;
    bool trackerValidationSucceeded = true;
    bool trackerUpdateAttempted = false;
    bool trackerUpdateSucceeded = true;
    bool appendAttempted = false;
    bool appendSucceeded = true;
};

struct ReentrantTelemetryDuringDeclarationTask{
    struct Payload{
        ReentrantTelemetryDuringDeclarationState* state = nullptr;

        explicit Payload(ReentrantTelemetryDuringDeclarationState& value)noexcept
            : state(&value)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other)
            : state(other.state)
        {
            other.state = nullptr;
            if(
                !state
                || !state->graph
                || !state->analysis
                || !state->assignments
                || !state->compiledGraph
                || !state->transaction
                || !state->tracker
                || !state->builder
                || !state->scratchArena
            )
                return;

            const Graphics::GpuTaskGraph::DeclarationReadView declarations =
                Graphics::GpuTaskGraph::DeclarationReadView::tryAcquire(*state->graph);
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(*state->compiledGraph);
            state->trackerValidationAttempted = true;
            state->trackerValidationSucceeded = state->tracker->validFor(
                declarations,
                *state->assignments,
                compiledPlan
            );
            state->trackerUpdateAttempted = true;
            state->trackerUpdateSucceeded = state->tracker->update(
                declarations,
                *state->assignments,
                compiledPlan,
                *state->transaction,
                *state->scratchArena
            );
            const Graphics::GpuTaskGraphTelemetryOptions options{
                .queueAssignments = state->assignments,
                .compiledPlan = &compiledPlan,
                .queueAssignmentTelemetry = state->tracker,
            };
            state->appendAttempted = true;
            state->appendSucceeded = declarations.appendFrameGraphTelemetry(
                *state->builder,
                *state->analysis,
                *state->scratchArena,
                options
            );
        }
    };
};

struct NoexceptAcceptedLifecycleTask{
    struct Payload{};

    static void accepted(Payload&, const Graphics::QueueSubmissionToken&)noexcept{
    }
};

struct NoexceptRecordDiscardLifecycleTask{
    struct Payload{
        u32* recordCount = nullptr;
        u32* discardedCount = nullptr;
    };

    static bool record(const Payload& payload, Graphics::CommandList&, const Graphics::GpuTaskRecordContext&)noexcept{
        if(payload.recordCount)
            ++*payload.recordCount;
        return true;
    }
    static void discarded(Payload& payload)noexcept{
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};

struct NativeRecordProbeTask{
    struct Payload{
        u32* recordCount = nullptr;
    };

    static bool record(const Payload& payload, Graphics::CommandList&, const Graphics::GpuTaskRecordContext&){
        if(payload.recordCount)
            ++*payload.recordCount;
        return true;
    }
};

struct MalformedLifecycleTask{
    struct Payload{
        u32* recordCount = nullptr;
        u32* acceptedCount = nullptr;
        u32* discardedCount = nullptr;
    };

    static bool record(
        const Payload& payload,
        Graphics::CommandList&,
        const Graphics::GpuTaskRecordContext&,
        const u32 lookalike = 0u
    ){
        static_cast<void>(lookalike);
        if(payload.recordCount)
            ++*payload.recordCount;
        return true;
    }
    static bool accepted(Payload& payload, const Graphics::QueueSubmissionToken&){
        if(payload.acceptedCount)
            ++*payload.acceptedCount;
        return true;
    }
    static void discarded(const Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

