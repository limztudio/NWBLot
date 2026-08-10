// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/task_graph/task_desc.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Phase 11 capture is opt-in tooling. Native packet recording remains the ordinary runtime path unless a caller
// explicitly supplies a capture object; its byte stream contains graph IDs, never backend pointers.
namespace GpuCommandIrOpcode{
    // This is the original in-memory built-in capture enum. Keep its width and ordinal values stable for callers
    // that inspect GpuCommandIrBuiltinTaskRecord; the byte stream has its own extensible u16 opcode below.
    enum Enum : u8{
        CopyBuffer = 0u,
        CopyTexture = 1u,
        ClearBuffer = 2u,
        ClearTexture = 3u,

        kCount,
    };
};

namespace GpuCommandIrWireOpcode{
    enum Enum : u16{
        SetGraphicsState = 0u,
        Draw,
        DrawIndexed,
        DrawIndirect,
        SetComputeState,
        Dispatch,
        DispatchIndirect,
        SetRayTracingState,
        DispatchRays,
        CopyBuffer,
        CopyTexture,
        ClearBuffer,
        ClearTexture,
        LocalMemoryBarrier,
        BeginMarker,
        EndMarker,

        kCount,
    };
};

// The stream is a same-host tooling format for now. The magic/version make a future reader reject incompatible
// layouts before interpreting its POD records; remote or persistent cross-platform transport will add endian policy.
inline constexpr u32 s_GpuCommandIrStreamMagic = 0x4E574349u; // NWCI
inline constexpr u16 s_GpuCommandIrStreamVersion = 1u;

#pragma pack(push, 1)
struct GpuCommandIrStreamHeader{
    u32 magic = s_GpuCommandIrStreamMagic;
    u16 version = s_GpuCommandIrStreamVersion;
    u16 reserved = 0u;
    u64 graphGeneration = 0u;
    u64 recordCount = 0u;
    u64 payloadBytes = 0u;
};

struct GpuCommandIrHeader{
    GpuCommandIrWireOpcode::Enum opcode = GpuCommandIrWireOpcode::CopyBuffer;
    // Total bytes occupied by this record, including this header. Every v1 opcode has one fixed-size payload.
    u16 byteSize = 0u;
};

// Every v1 built-in record targets one graph generation supplied by GpuCommandIrStreamHeader. Queue generation
// remains command-local because it names a physical-device queue lifetime rather than graph metadata.
struct GpuCommandIrRecordContext{
    u32 taskIndex = Limit<u32>::s_Max;
    u32 packetIndex = Limit<u32>::s_Max;
    u16 queueIndex = Limit<u16>::s_Max;
    u16 queueDeviceGeneration = 0u;
};

struct GpuCommandIrTextureSlice{
    u32 x = 0u;
    u32 y = 0u;
    u32 z = 0u;
    u32 width = TextureSlice::AllDimensions;
    u32 height = TextureSlice::AllDimensions;
    u32 depth = TextureSlice::AllDimensions;
    u32 mipLevel = 0u;
    u32 arraySlice = 0u;
};

struct GpuCommandIrTextureSubresourceSet{
    u32 baseMipLevel = 0u;
    u32 numMipLevels = 1u;
    u32 baseArraySlice = 0u;
    u32 numArraySlices = 1u;
};

struct GpuCommandIrFloatColor{
    f32 r = 0.f;
    f32 g = 0.f;
    f32 b = 0.f;
    f32 a = 0.f;
};

struct GpuCommandIrUIntColor{
    u32 r = 0u;
    u32 g = 0u;
    u32 b = 0u;
    u32 a = 0u;
};

struct GpuCommandIrIntColor{
    i32 r = 0;
    i32 g = 0;
    i32 b = 0;
    i32 a = 0;
};

struct GpuCommandIrCopyBufferRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u32 sourceResourceIndex = Limit<u32>::s_Max;
    u32 destinationResourceIndex = Limit<u32>::s_Max;
    u64 sourceOffsetBytes = 0u;
    u64 destinationOffsetBytes = 0u;
    u64 dataSizeBytes = 0u;
};

struct GpuCommandIrCopyTextureRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u32 sourceResourceIndex = Limit<u32>::s_Max;
    u32 destinationResourceIndex = Limit<u32>::s_Max;
    GpuCommandIrTextureSlice sourceSlice;
    GpuCommandIrTextureSlice destinationSlice;
};

struct GpuCommandIrClearBufferRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u32 destinationResourceIndex = Limit<u32>::s_Max;
    u32 clearValue = 0u;
};

namespace GpuCommandIrClearTextureFlag{
    enum Mask : u8{
        None = 0u,
        ClearDepth = 1u << 0u,
        ClearStencil = 1u << 1u,
    };
};

struct GpuCommandIrClearTextureRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u32 destinationResourceIndex = Limit<u32>::s_Max;
    GpuCommandIrTextureSubresourceSet destinationSubresources;
    GpuCommandIrFloatColor floatClearValue;
    GpuCommandIrUIntColor uintClearValue;
    GpuCommandIrIntColor intClearValue;
    f32 depthClearValue = 1.f;
    u8 stencilClearValue = 0u;
    u8 clearTextureValueType = GpuClearTextureTaskValueType::UInt;
    GpuCommandIrClearTextureFlag::Mask clearFlags = GpuCommandIrClearTextureFlag::None;
    u8 reserved = 0u;
};
#pragma pack(pop)

static_assert(sizeof(GpuCommandIrStreamHeader) == 32u, "Command IR stream header wire layout drifted");
static_assert(sizeof(GpuCommandIrHeader) == 4u, "Command IR command header wire layout drifted");
static_assert(sizeof(GpuCommandIrRecordContext) == 12u, "Command IR context wire layout drifted");
static_assert(sizeof(GpuCommandIrTextureSlice) == 32u, "Command IR texture slice wire layout drifted");
static_assert(sizeof(GpuCommandIrTextureSubresourceSet) == 16u, "Command IR subresource wire layout drifted");
static_assert(sizeof(GpuCommandIrCopyBufferRecord) == 48u, "Command IR copy-buffer wire layout drifted");
static_assert(sizeof(GpuCommandIrCopyTextureRecord) == 88u, "Command IR copy-texture wire layout drifted");
static_assert(sizeof(GpuCommandIrClearBufferRecord) == 24u, "Command IR clear-buffer wire layout drifted");
static_assert(sizeof(GpuCommandIrClearTextureRecord) == 92u, "Command IR clear-texture wire layout drifted");
static_assert(alignof(GpuCommandIrStreamHeader) == 1u, "Command IR stream header must stay packed");
static_assert(alignof(GpuCommandIrCopyBufferRecord) == 1u, "Command IR records must stay packed");
static_assert(IsStandardLayout_V<GpuCommandIrStreamHeader>, "Command IR stream header must be binary-serializable");
static_assert(IsTriviallyCopyable_V<GpuCommandIrStreamHeader>, "Command IR stream header must be binary-serializable");
static_assert(IsStandardLayout_V<GpuCommandIrCopyBufferRecord>, "Command IR records must be binary-serializable");
static_assert(IsTriviallyCopyable_V<GpuCommandIrCopyBufferRecord>, "Command IR records must be binary-serializable");
static_assert(IsStandardLayout_V<GpuCommandIrCopyTextureRecord>, "Command IR records must be binary-serializable");
static_assert(IsTriviallyCopyable_V<GpuCommandIrCopyTextureRecord>, "Command IR records must be binary-serializable");
static_assert(IsStandardLayout_V<GpuCommandIrClearBufferRecord>, "Command IR records must be binary-serializable");
static_assert(IsTriviallyCopyable_V<GpuCommandIrClearBufferRecord>, "Command IR records must be binary-serializable");
static_assert(IsStandardLayout_V<GpuCommandIrClearTextureRecord>, "Command IR records must be binary-serializable");
static_assert(IsTriviallyCopyable_V<GpuCommandIrClearTextureRecord>, "Command IR records must be binary-serializable");


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
        , m_commandBytes(arena)
    {
        m_commandBytes.resize(sizeof(GpuCommandIrStreamHeader));
        writeStreamHeader();
    }


public:
    void reset()noexcept;

    [[nodiscard]] usize recordCount()const noexcept{ return m_records.size(); }
    [[nodiscard]] u64 graphGeneration()const noexcept{ return m_graphGeneration; }
    [[nodiscard]] const GpuCommandIrBuiltinTaskRecord* recordAt(usize index)const noexcept;
    [[nodiscard]] BinaryByteView commandBytes()const noexcept{
        return BinaryByteView{ m_commandBytes.data(), m_commandBytes.size() };
    }
    // Packet recording checkpoints before invoking task payloads and rolls back an incomplete task-recording
    // attempt. This is a recording trace; a later reader may separately correlate it with submission acceptance.
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
    [[nodiscard]] bool appendCommandBytes(const GpuCommandIrBuiltinTaskRecord& record);
    [[nodiscard]] usize byteOffsetAfterRecordCount(usize recordCount)const noexcept;
    void writeStreamHeader()noexcept;


private:
    GraphicsVector<GpuCommandIrBuiltinTaskRecord> m_records;
    GraphicsBytes m_commandBytes;
    u64 m_graphGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
