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
    using StandaloneTaskGraphDeclaration = Expected<GpuTaskId>(*)(void* userData, GpuTaskGraph& graph);


public:
    struct BufferSetupDesc{
        BufferDesc bufferDesc;
        const void* data = nullptr;
        usize dataSize = 0;
        u64 destOffsetBytes = 0;
        QueueSubmissionToken* acceptedToken = nullptr;
        CommandQueue::Enum queue = CommandQueue::kCount;


    public:
        template<typename T>
        void setData(const Span<const T> payload)noexcept{
            data = payload.data();
            dataSize = payload.size_bytes();
        }
    };

    struct TextureUploadRegion{
        const void* data = nullptr;
        usize dataSize = 0u;
        usize rowPitch = 0u;
        usize depthPitch = 0u;
        u32 arraySlice = 0u;
        u32 mipLevel = 0u;
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;


    public:
        void setBytes(const Span<const u8> bytes)noexcept{
            data = bytes.data();
            dataSize = bytes.size();
        }
    };

    struct TextureUploadBatchDesc{
        TextureHandle destination;
        const TextureUploadRegion* regions = nullptr;
        usize regionCount = 0u;
        ResourceStates::Mask finalState = ResourceStates::Unknown;
        ResourceStates::Mask physicalInitialState = ResourceStates::Unknown;
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
    [[nodiscard]] Expected<QueueSubmissionToken> uploadTextureBatch(const TextureUploadBatchDesc& desc)const;
    [[nodiscard]] Expected<QueueSubmissionToken> uploadTextureBatch(
        const TextureHandle& destination, const Span<const TextureUploadRegion> regions, const ResourceStates::Mask finalState, const ResourceStates::Mask initialState
    )const{
        return uploadTextureBatch(TextureUploadBatchDesc{
            .destination = destination, .regions = regions.data(), .regionCount = regions.size(), .finalState = finalState, .physicalInitialState = initialState,
            .hasPhysicalInitialState = true,
        });
    }
    [[nodiscard]] Expected<QueueSubmissionToken> submitStandaloneTaskGraph(
        void* userData,
        StandaloneTaskGraphDeclaration declareTask,
        GpuPhysicalQueueId requiredTerminalQueue = {},
        GpuTimingRecorder* timingRecorder = nullptr,
        GpuTimingFrameTransaction* frameTimingTransaction = nullptr
    )const;
    template<typename Declare>
    [[nodiscard]] Expected<QueueSubmissionToken> submitStandaloneTaskGraph(Declare&& declare, GpuPhysicalQueueId requiredTerminalQueue)const{
        return submitCallable(declare, requiredTerminalQueue, nullptr, nullptr);
    }

    template<typename Declare>
    [[nodiscard]] Expected<QueueSubmissionToken> submitStandaloneTaskGraphWithTiming(
        Declare&& declare, GpuPhysicalQueueId requiredTerminalQueue, GpuTimingRecorder& timingRecorder, GpuTimingFrameTransaction& frameTimingTransaction
    )const{
        return submitCallable(declare, requiredTerminalQueue, &timingRecorder, &frameTimingTransaction);
    }


private:
    template<typename Declare>
    [[nodiscard]] Expected<QueueSubmissionToken> submitCallable(
        Declare& declare, GpuPhysicalQueueId queue, GpuTimingRecorder* timingRecorder, GpuTimingFrameTransaction* frameTimingTransaction
    )const{
        auto invoke = [&declare](GpuTaskGraph& graph){ return declare(graph); };
        return submitStandaloneTaskGraph(&invoke, [](void* context, GpuTaskGraph& graph){
            return (*static_cast<decltype(invoke)*>(context))(graph);
        }, queue, timingRecorder, frameTimingTransaction);
    }



private:
    const GraphicsRuntime& m_graphics;
    GraphicsModuleDetail::StandaloneGraphWorkspacePool& m_workspaces;
    CpuTaskScheduler& m_cpuScheduler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

