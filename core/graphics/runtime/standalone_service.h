// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/rhi/command.h>
#include <core/graphics/rhi/resource.h>
#include <core/task/gpu/task_desc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;
class CpuTaskScheduler;
class GpuTaskGraph;
class GpuTimingRecorder;
class GpuTimingFrameTransaction;
namespace GraphicsModuleDetail{ class StandaloneGraphWorkspacePool; };


class GraphicsStandaloneService final : NoCopy{
public:
    // A synchronous caller may declare one isolated graph through this callback.
    // The graph owns all native recording and submission; the callback keeps only declaration-time inputs and returns its terminal task.
    // `userData` stays caller-owned during the call.
    using StandaloneTaskGraphDeclaration = Expected<GpuTaskId>(*)(void* userData, GpuTaskGraph& graph);


public:
    struct BufferSetupDesc{
        BufferDesc bufferDesc;
        const void* data = nullptr;
        // Upload sizes and destination offsets must be four-byte aligned.
        usize dataSize = 0;
        u64 destOffsetBytes = 0;
        // Optional output written after producer submission and consumer readiness bridges.
        QueueSubmissionToken* acceptedToken = nullptr;
        // Direct native consumer timeline; kCount resolves from payload size, final state, and available queues.
        // The scheduler independently places the producer and submits readiness bridges for consumers.
        CommandQueue::Enum queue = CommandQueue::kCount;
    };

    // One immutable CPU payload per subresource. Batch copies regions into graph blobs first, so callers may free decoded memory on return.
    // A 3D mip is one region at arraySlice 0 whose depthPitch covers a single Z slice; 2D/cube uploads provide one region per array slice.
    struct TextureUploadRegion{
        const void* data = nullptr;
        usize dataSize = 0u;
        usize rowPitch = 0u;
        usize depthPitch = 0u;
        u32 arraySlice = 0u;
        u32 mipLevel = 0u;
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;
    };

    // Multi-subresource texture upload via a compiler-owned graph.
    // Explicit `finalState` (keepInitialState: == initialState).
    // hasPhysicalInitialState declares the native pre-upload state; explicit Unknown starts fresh images UNDEFINED.
    struct TextureUploadBatchDesc{
        TextureHandle destination;
        const TextureUploadRegion* regions = nullptr;
        usize regionCount = 0u;
        QueueSubmissionToken* acceptedToken = nullptr;
        ResourceStates::Mask finalState = ResourceStates::Unknown;
        ResourceStates::Mask physicalInitialState = ResourceStates::Unknown;
        // See BufferSetupDesc::queue; the resolved native consumer must be admitted by the destination sharing contract.
        CommandQueue::Enum queue = CommandQueue::kCount;
        b8 hasPhysicalInitialState = false;
    };



public:
    GraphicsStandaloneService(const GraphicsRuntime& graphics, GraphicsModuleDetail::StandaloneGraphWorkspacePool& workspaces, CpuTaskScheduler& scheduler)noexcept
        : m_graphics(graphics)
        , m_workspaces(workspaces)
        , m_cpuScheduler(scheduler)
    {}


public:
    [[nodiscard]] Expected<BufferHandle> setupBuffer(const BufferSetupDesc& desc)const;
    [[nodiscard]] b8 uploadTextureBatch(const TextureUploadBatchDesc& desc)const;
    // Compiles, records, and submits an isolated graph synchronously.
    // This is the graph-owned escape hatch for a standalone caller that has no renderer-owned frame graph but can still provide immutable task inputs.
    [[nodiscard]] Expected<QueueSubmissionToken> submitStandaloneTaskGraph(
        void* userData,
        StandaloneTaskGraphDeclaration declareTask,
        GpuPhysicalQueueId requiredTerminalQueue = {},
        GpuTimingRecorder* timingRecorder = nullptr,
        GpuTimingFrameTransaction* frameTimingTransaction = nullptr
    )const;



private:
    const GraphicsRuntime& m_graphics;
    GraphicsModuleDetail::StandaloneGraphWorkspacePool& m_workspaces;
    CpuTaskScheduler& m_cpuScheduler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

