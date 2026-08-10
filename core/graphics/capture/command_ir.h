// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/task_graph/task_desc.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


class GpuTaskGraph;
class GpuCompiledGraph;


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


// The reader validates only the self-contained v1 wire contract. Graph/resource topology, resource ranges and
// backend command legality require a later context-aware validation/replay phase.
namespace GpuCommandIrStreamReadStatus{
    enum Enum : u8{
        Record,
        End,
        Error,
    };
};

namespace GpuCommandIrStreamValidationError{
    enum Enum : u8{
        None,
        NullData,
        TruncatedStreamHeader,
        InvalidMagic,
        UnsupportedVersion,
        InvalidHeaderReserved,
        PayloadSizeMismatch,
        InvalidGraphGeneration,
        InvalidRecordCount,
        TruncatedRecord,
        InvalidRecordSize,
        UnsupportedOpcode,
        InvalidRecord,
        TrailingPayload,
    };
};

struct GpuCommandIrStreamValidationResult{
    GpuCommandIrStreamValidationError::Enum error = GpuCommandIrStreamValidationError::None;
    usize byteOffset = 0u;
    u64 recordIndex = Limit<u64>::s_Max;
    bool complete = false;

    [[nodiscard]] constexpr bool valid()const noexcept{
        return complete && error == GpuCommandIrStreamValidationError::None;
    }
    [[nodiscard]] constexpr bool failed()const noexcept{
        return error != GpuCommandIrStreamValidationError::None;
    }
};

// A non-owning sequential reader for the POD stream. Its backing bytes must stay alive and unchanged while it is
// used. `next` publishes its output and advances only after an entire record passes v1 syntax validation; `End` is
// the only successful terminal state.
class GpuCommandIrStreamReader final : NoCopy{
public:
    explicit GpuCommandIrStreamReader(BinaryByteView bytes)noexcept;


public:
    [[nodiscard]] GpuCommandIrStreamReadStatus::Enum next(GpuCommandIrBuiltinTaskRecord& outRecord)noexcept;
    [[nodiscard]] const GpuCommandIrStreamValidationResult& validation()const noexcept{ return m_validation; }
    [[nodiscard]] u64 graphGeneration()const noexcept{ return m_graphGeneration; }
    [[nodiscard]] u64 recordCount()const noexcept{ return m_recordCount; }


private:
    void fail(
        GpuCommandIrStreamValidationError::Enum error,
        usize byteOffset,
        u64 recordIndex
    )noexcept;


private:
    BinaryByteView m_bytes;
    GpuCommandIrStreamValidationResult m_validation;
    usize m_cursor = 0u;
    usize m_payloadEnd = 0u;
    u64 m_graphGeneration = 0u;
    u64 m_recordCount = 0u;
    u64 m_nextRecordIndex = 0u;
};

// Fully walks a stream with the syntax-only reader and returns either success or its first malformed location.
[[nodiscard]] GpuCommandIrStreamValidationResult ValidateGpuCommandIrStream(BinaryByteView bytes)noexcept;


// Replay remains opt-in tooling. A v1 stream contains only primitive task bodies, not packet state seeds,
// compiler barriers, ownership transfers, markers, or submission dependencies. It may therefore be lowered only
// into a caller-owned fresh packet body after the graph recorder has established the required state.
namespace GpuCommandIrReplayError{
    enum Enum : u8{
        None,
        InvalidStream,
        InvalidCompiledGraph,
        InvalidPacket,
        PacketQueueUnavailable,
        MissingTransferCapability,
        GraphGenerationMismatch,
        RecordPacketMismatch,
        RecordQueueMismatch,
        InvalidTask,
        CompiledTaskMismatch,
        TaskOrderMismatch,
        InvalidResource,
        ResourceUseMismatch,
        ResourceTypeMismatch,
        MissingBackendResource,
        InvalidBufferCopy,
        InvalidTextureCopy,
        InvalidBufferClear,
        InvalidTextureClear,
        CommandListNotRecording,
        CommandListRenderPassActive,
        CommandListQueueMismatch,
        StreamChangedDuringReplay,
    };
};

struct GpuCommandIrReplayResult{
    GpuCommandIrReplayError::Enum error = GpuCommandIrReplayError::None;
    // Syntax failures preserve the reader's exact byte diagnostic. Semantic failures identify the decoded record
    // that failed graph-aware preflight and use Limit<u64>::s_Max when no record was reached.
    GpuCommandIrStreamValidationResult streamValidation;
    u64 recordIndex = Limit<u64>::s_Max;

    [[nodiscard]] constexpr bool valid()const noexcept{
        return error == GpuCommandIrReplayError::None;
    }
};

// Validates the commands selected by `packet` from a compiler-owned capture without touching a native command
// list. The complete stream is syntax-validated first, then records for other packets are ignored so a normal
// multi-packet capture can be replayed one packet at a time.
[[nodiscard]] GpuCommandIrReplayResult PreflightGpuCommandIrPacket(
    BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    GpuSubmissionPacketId packet
)noexcept;

// Re-runs complete preflight before lowering only `packet`'s commands. The command list must be open, have the
// packet's resolved queue class, and have no active render pass. The bytes and graph must remain unchanged for the
// duration of the call, and resources must belong to that command list's device. This does not apply graph state
// seeds or barriers and does not submit work; callers retain the surrounding packet-recording contract.
[[nodiscard]] GpuCommandIrReplayResult ReplayGpuCommandIrPacket(
    BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    GpuSubmissionPacketId packet,
    CommandList& commandList
)noexcept;


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
