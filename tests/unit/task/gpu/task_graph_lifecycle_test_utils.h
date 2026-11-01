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


    public:
        explicit Payload(u32* const value)noexcept
            : destructionCount(value)
        {}
        Payload(Payload&& other)noexcept;
        Payload(const Payload&) = delete;
        ~Payload()noexcept;
    };
};

struct PacketLifecycleTask{
    struct Payload{
        u32* acceptedCount = nullptr;
        u32* discardedCount = nullptr;
        Core::GpuQueueCapability::Mask requiredCapabilities = Core::GpuQueueCapability::None;
    };


public:
    static Core::GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept{ return Core::GpuTaskCommandRequirements{ payload.requiredCapabilities }; }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken&)noexcept;

    static void Discarded(Payload& payload)noexcept;
};

struct ReentrantCompileDuringDeclarationTask{
    struct Payload{
        const Core::GpuTaskGraph* graph = nullptr;
        Core::GpuTaskGraphAnalysis* analysis = nullptr;
        Core::Alloc::ScratchArena* scratchArena = nullptr;
        b8* compileAttempted = nullptr;
        b8* compileSucceeded = nullptr;


    public:
        Payload()noexcept = default;
        Payload(const Payload&) = delete;
        Payload(Payload&& other);
    };
};

struct ReentrantTelemetryDuringDeclarationState{
    const Core::GpuTaskGraph* graph = nullptr;
    const Core::GpuTaskGraphAnalysis* analysis = nullptr;
    const Core::GpuTaskGraphQueueAssignments* assignments = nullptr;
    const Core::GpuCompiledGraph* compiledGraph = nullptr;
    const Core::GpuGraphSubmissionTransaction* transaction = nullptr;
    Core::GpuTaskGraphQueueAssignmentTelemetryTracker* tracker = nullptr;
    Telemetry::FrameGraphBuilder* builder = nullptr;
    Core::Alloc::ScratchArena* scratchArena = nullptr;
    b8 trackerValidationAttempted = false;
    b8 trackerValidationSucceeded = true;
    b8 trackerUpdateAttempted = false;
    b8 trackerUpdateSucceeded = true;
    b8 appendAttempted = false;
    b8 appendSucceeded = true;
};

struct ReentrantTelemetryDuringDeclarationTask{
    struct Payload{
        ReentrantTelemetryDuringDeclarationState* state = nullptr;


    public:
        explicit Payload(ReentrantTelemetryDuringDeclarationState& value)noexcept
            : state(&value)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other);
    };
};

struct NativeRecordProbeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::Graphics };


public:
    struct Payload{
        u32* recordCount = nullptr;
    };


public:
    static b8 Record(const Payload& payload, Core::CommandList&, const Core::GpuTaskRecordContext&)noexcept;
};

struct MalformedLifecycleTask{
    struct Payload{
        u32* recordCount = nullptr;
        u32* acceptedCount = nullptr;
        u32* discardedCount = nullptr;
    };


public:
    static b8 Record(const Payload& payload, Core::CommandList&, const Core::GpuTaskRecordContext&, const u32 lookalike = 0u)noexcept;

    static b8 Accepted(Payload& payload, const Core::QueueSubmissionToken&)noexcept;

    static void Discarded(const Payload& payload)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

