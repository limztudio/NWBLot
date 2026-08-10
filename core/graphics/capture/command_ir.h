// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/task_graph/task_desc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Phase 11 starts as an opt-in capture seam for graph-native primitive work.  These records deliberately retain
// graph resource handles rather than backend pointers, but they are not yet the general byte-stream/replay layer:
// native packet recording remains the ordinary runtime path unless a caller explicitly supplies a capture object.
namespace GpuCommandIrOpcode{
    enum Enum : u8{
        CopyBuffer,
        CopyTexture,
        ClearBuffer,
        ClearTexture,

        kCount,
    };
};


struct GpuCommandIrBuiltinTaskRecord{
    GpuCommandIrOpcode::Enum opcode = GpuCommandIrOpcode::CopyBuffer;
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;

    GpuGraphResourceId source;
    GpuGraphResourceId destination;
    u64 sourceOffsetBytes = 0u;
    u64 destinationOffsetBytes = 0u;
    u64 dataSizeBytes = 0u;
    TextureSlice sourceSlice;
    TextureSlice destinationSlice;
    TextureSubresourceSet destinationSubresources = s_AllSubresources;

    GpuClearTextureTaskValueType::Enum clearTextureValueType = GpuClearTextureTaskValueType::UInt;
    Color floatClearValue;
    UIntColor uintClearValue;
    IntColor intClearValue;
    f32 depthClearValue = 1.f;
    u8 stencilClearValue = 0u;
    bool clearDepth = false;
    bool clearStencil = false;
};


class GpuCommandIrCapture final : NoCopy{
public:
    explicit GpuCommandIrCapture(GraphicsArena& arena)
        : m_records(arena)
    {}


public:
    void reset()noexcept;

    [[nodiscard]] usize recordCount()const noexcept{ return m_records.size(); }
    [[nodiscard]] u64 graphGeneration()const noexcept{ return m_graphGeneration; }
    [[nodiscard]] const GpuCommandIrBuiltinTaskRecord* recordAt(usize index)const noexcept;
    // Packet recording checkpoints before invoking task payloads and rolls back an incomplete packet, so a capture
    // never advertises commands whose native command list failed to record.
    void rollback(usize recordCount)noexcept;

    [[nodiscard]] bool captureCopyBuffer(
        GpuTaskId task,
        GpuSubmissionPacketId packet,
        GpuPhysicalQueueId queue,
        GpuGraphResourceId source,
        u64 sourceOffsetBytes,
        GpuGraphResourceId destination,
        u64 destinationOffsetBytes,
        u64 dataSizeBytes
    );
    [[nodiscard]] bool captureCopyTexture(
        GpuTaskId task,
        GpuSubmissionPacketId packet,
        GpuPhysicalQueueId queue,
        GpuGraphResourceId source,
        TextureSlice sourceSlice,
        GpuGraphResourceId destination,
        TextureSlice destinationSlice
    );
    [[nodiscard]] bool captureClearBuffer(
        GpuTaskId task,
        GpuSubmissionPacketId packet,
        GpuPhysicalQueueId queue,
        GpuGraphResourceId destination,
        u32 clearValue
    );
    [[nodiscard]] bool captureClearTexture(
        GpuTaskId task,
        GpuSubmissionPacketId packet,
        GpuPhysicalQueueId queue,
        GpuGraphResourceId destination,
        const GpuClearTextureTaskDesc& clearDesc
    );


private:
    [[nodiscard]] bool append(const GpuCommandIrBuiltinTaskRecord& record);


private:
    GraphicsVector<GpuCommandIrBuiltinTaskRecord> m_records;
    u64 m_graphGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
