// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command_ir.h"
#include "command_ir_raster.h"

#include <cstddef>
#include <cstring>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GraphicsState;
struct GpuTaskGraphTaskView;


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DecodedRecordContext{
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
};

[[nodiscard]] inline Expected<DecodedRecordContext> DecodeRecordContext(const GpuCommandIrRecordContext& context, const u64 graphGeneration, const u64 planGeneration)noexcept{
    const DecodedRecordContext decoded{
        .task = { .generation = graphGeneration, .index = context.taskIndex },
        .packet = { .generation = planGeneration, .index = context.packetIndex },
        .queue = { .index = context.queueIndex, .deviceGeneration = context.queueDeviceGeneration },
    };
    if(!decoded.task.valid() || !decoded.packet.valid() || !decoded.queue.valid())
        return MakeUnexpected(Failure{});
    return decoded;
}

[[nodiscard]] inline GpuCommandIrRecordContext EncodeContext(const GpuTaskId task, const GpuSubmissionPacketId packet, const GpuPhysicalQueueId queue)noexcept{
    return GpuCommandIrRecordContext{ .taskIndex = task.index, .packetIndex = packet.index, .queueIndex = queue.index, .queueDeviceGeneration = queue.deviceGeneration, };
}

[[nodiscard]] inline GpuCommandIrRecordContext EncodeContext(const GpuCommandIrBuiltinTaskRecord& record)noexcept{
    return EncodeContext(record.task, record.packet, record.queue);
}

[[nodiscard]] inline GpuCommandIrRect EncodeRect(const Rect& rect)noexcept{
    return GpuCommandIrRect{ .minX = rect.minX, .maxX = rect.maxX, .minY = rect.minY, .maxY = rect.maxY, };
}

[[nodiscard]] inline GpuCommandIrFloatColor EncodeColor(const Color& color)noexcept{
    return GpuCommandIrFloatColor{ .r = color.r, .g = color.g, .b = color.b, .a = color.a, };
}

[[nodiscard]] inline GpuCommandIrUIntColor EncodeColor(const UIntColor& color)noexcept{
    return GpuCommandIrUIntColor{ .r = color.r, .g = color.g, .b = color.b, .a = color.a, };
}

[[nodiscard]] inline GpuCommandIrIntColor EncodeColor(const IntColor& color)noexcept{
    return GpuCommandIrIntColor{ .r = color.r, .g = color.g, .b = color.b, .a = color.a, };
}

template<typename RecordT>
inline void InitializeRecord(RecordT& record, const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    static_assert(IsStandardLayout_V<RecordT>, "Command IR records must be standard layout");
    static_assert(IsTriviallyCopyable_V<RecordT>, "Command IR records must be trivially copyable");
    FOUNDATION_MEMSET(&record, 0, sizeof(record));
    record.header.opcode = opcode;
    record.header.byteSize = static_cast<u16>(sizeof(record));
}

template<typename RecordT>
inline void InitializeRecord(RecordT& record, const GpuCommandIrWireOpcode::Enum opcode, const GpuTaskId task, const GpuSubmissionPacketId packet, const GpuPhysicalQueueId queue)noexcept{
    InitializeRecord(record, opcode);
    record.context = EncodeContext(task, packet, queue);
}


[[nodiscard]] b8 ValidateBuiltinRecord(const GpuCommandIrBuiltinTaskRecord& record)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateUploadOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateUploadWireOperation(
    GpuCommandIrWireOpcode::Enum opcode,
    const u8* recordPtr,
    u64 graphGeneration,
    BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateUploadBackendOperand(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuPhysicalQueueInfo& queue, CommandList& commandList)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateUploadWireBackendOperand(
    GpuCommandIrWireOpcode::Enum opcode,
    const u8* recordPtr,
    u64 graphGeneration,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList
)noexcept;
[[nodiscard]] b8 LowerUploadOperation(const GpuCommandIrBuiltinTaskRecord& record, BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList);
[[nodiscard]] b8 LowerUploadWireOperation(
    GpuCommandIrWireOpcode::Enum opcode,
    const u8* recordPtr,
    u64 graphGeneration,
    BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList
);
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateReplayCommandListQueue(const CommandListParameters& commandListDescription, const GpuPhysicalQueueId& packetQueue, CommandQueue::Enum packetQueueClass)noexcept;

struct CachedRasterVertexValidation{
    const BufferDesc* bufferDesc = nullptr;
    u64 offset = 0u;
    u32 stride = 0u;
    u32 requiredBytes = 0u;
    u8 vertexBindingIndex = 0u;
    b8 instanceRate = false;
};

struct RasterVertexCacheEntry{
    u64 resourceGeneration;
    u32 resourceIndex;
    u32 slot;
};

struct RasterOwnerTableView{
    const GpuCommandIrRasterStateOwner* stateOwners = nullptr;
    usize stateOwnerCount = 0u;
    const GpuCommandIrRasterHeapOwner* heapOwners = nullptr;
    usize heapOwnerCount = 0u;
    b8 hasOwnerSidecar = false;


public:
    [[nodiscard]] static RasterOwnerTableView None()noexcept{ return {}; }

    [[nodiscard]] static RasterOwnerTableView FromTable(const GpuCommandIrRasterOwnerTable* const owners)noexcept{
        RasterOwnerTableView view;
        view.hasOwnerSidecar = true;
        if(owners){
            view.stateOwners = owners->stateOwners.data();
            view.stateOwnerCount = owners->stateOwners.size();
            view.heapOwners = owners->heapOwners.data();
            view.heapOwnerCount = owners->heapOwners.size();
        }
        return view;
    }

    [[nodiscard]] static RasterOwnerTableView FromOwnedStream(const GpuCommandIrOwnedStream& stream)noexcept{
        return FromTable(stream.rasterOwners());
    }

    [[nodiscard]] static RasterOwnerTableView FromCapture(const GpuCommandIrCapture& capture)noexcept{
        return FromTable(capture.rasterOwners());
    }

    [[nodiscard]] const GpuCommandIrRasterStateOwner* rasterStateOwner(const u32 index)const noexcept{
        return stateOwners && index < stateOwnerCount ? &stateOwners[index] : nullptr;
    }

    [[nodiscard]] const GpuCommandIrRasterHeapOwner* rasterHeapOwner(const u32 index)const noexcept{
        return heapOwners && index < heapOwnerCount ? &heapOwners[index] : nullptr;
    }
};

struct RasterReplayState{
    GpuTaskId task;
    GpuTaskId cachedTask;
    GpuGraphPipelineId pipeline;
    GpuGraphResourceId colorAttachment;
    GpuGraphResourceId indexResource;
    u32 framebufferOwnerIndex = Limit<u32>::s_Max;
    u32 indexOffset = 0u;
    Format::Enum indexFormat = Format::UNKNOWN;
    u8 vertexBindingCount = 0u;
    b8 hasCachedGraphState = false;
    b8 active = false;
    b8 heapBound = false;
    b8 pushConstantsSet = false;
    RasterVertexCacheEntry vertexBuffers[s_MaxVertexAttributes];

    RasterReplayState()noexcept{}
};

struct BuiltinBackendResourceCache{
    static constexpr usize s_SlotCount = 16u;

    struct Slot{
        u64 resourceGeneration;
        u32 resourceIndex;
        ResourceStates::Mask validatedState;
        ResourceStates::Mask permanentState;
        u8 resourceKind;
        b8 permanentStateKnown;
        b8 hasValidatedState;
    };

    u16 populatedMask = 0u;
    Slot slots[s_SlotCount];
};

struct BackendRasterReplayState{
    FixedVector<CachedRasterVertexValidation, s_MaxVertexAttributes> cachedVertexValidations;
    const GpuCommandIrRasterStateOwner* cachedValidatedOwner = nullptr;
    const BufferDesc* cachedIndexBufferDesc = nullptr;
    GpuDescriptorHeap* cachedDescriptorHeap = nullptr;
    GpuCommandIrReplayError::Enum cachedInputLayoutError = GpuCommandIrReplayError::None;
    GpuTaskId task;
    GpuGraphPipelineId pipeline;
    GpuGraphResourceId colorAttachment;
    GpuGraphResourceId indexResource;
    u32 indexElementBytes = 0u;
    u32 framebufferOwnerIndex = Limit<u32>::s_Max;
    u32 indexOffset = 0u;
    Format::Enum indexFormat = Format::UNKNOWN;
    u8 vertexBindingCount = 0u;
    b8 cachedDescriptorHeapInitialized = false;
    b8 active = false;
    b8 heapBound = false;
    b8 pushConstantsSet = false;
    RasterVertexCacheEntry vertexBuffers[s_MaxVertexAttributes];
    const BufferDesc* vertexBufferDescs[s_MaxVertexAttributes];

    BackendRasterReplayState()noexcept{}
};

template<typename T>
[[nodiscard]] FOUNDATION_INLINE T ReadWirePod(const u8* const ptr)noexcept{
    static_assert(IsTriviallyCopyable_V<T>, "Wire POD decode requires a trivially copyable type");
    T value;
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

static_assert(offsetof(GpuCommandIrSetGraphicsStateRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrBindGraphicsHeapRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrSetPushConstantsRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrDrawRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrEndRenderPassRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrCopyBufferRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrCopyTextureRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrClearBufferRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrClearTextureRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrClearTextureRectUIntRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrUploadBufferRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(offsetof(GpuCommandIrUploadTextureRecord, context) == sizeof(GpuCommandIrHeader));
static_assert(static_cast<usize>(GpuCommandIrWireOpcode::Count) <= 32u, "Command IR opcode bitmask requires <= 32 opcodes");

inline constexpr u32 s_RasterOpcodeBitMask =
    (1u << static_cast<u32>(GpuCommandIrWireOpcode::SetGraphicsState))
    | (1u << static_cast<u32>(GpuCommandIrWireOpcode::Draw))
    | (1u << static_cast<u32>(GpuCommandIrWireOpcode::DrawIndexed))
    | (1u << static_cast<u32>(GpuCommandIrWireOpcode::BindGraphicsHeap))
    | (1u << static_cast<u32>(GpuCommandIrWireOpcode::SetPushConstants))
    | (1u << static_cast<u32>(GpuCommandIrWireOpcode::EndRenderPass))
;

[[nodiscard]] constexpr Array<usize, GpuCommandIrWireOpcode::Count> MakeRasterWireSizeTable()noexcept{
    Array<usize, GpuCommandIrWireOpcode::Count> table{};
    table[GpuCommandIrWireOpcode::SetGraphicsState] = sizeof(GpuCommandIrSetGraphicsStateRecord);
    table[GpuCommandIrWireOpcode::BindGraphicsHeap] = sizeof(GpuCommandIrBindGraphicsHeapRecord);
    table[GpuCommandIrWireOpcode::SetPushConstants] = sizeof(GpuCommandIrSetPushConstantsRecord);
    table[GpuCommandIrWireOpcode::Draw] = sizeof(GpuCommandIrDrawRecord);
    table[GpuCommandIrWireOpcode::DrawIndexed] = sizeof(GpuCommandIrDrawRecord);
    table[GpuCommandIrWireOpcode::EndRenderPass] = sizeof(GpuCommandIrEndRenderPassRecord);
    return table;
}

[[nodiscard]] constexpr Array<usize, GpuCommandIrWireOpcode::Count> MakeBuiltinWireSizeTable()noexcept{
    Array<usize, GpuCommandIrWireOpcode::Count> table{};
    table[GpuCommandIrWireOpcode::CopyBuffer] = sizeof(GpuCommandIrCopyBufferRecord);
    table[GpuCommandIrWireOpcode::CopyTexture] = sizeof(GpuCommandIrCopyTextureRecord);
    table[GpuCommandIrWireOpcode::ClearBuffer] = sizeof(GpuCommandIrClearBufferRecord);
    table[GpuCommandIrWireOpcode::ClearTexture] = sizeof(GpuCommandIrClearTextureRecord);
    table[GpuCommandIrWireOpcode::ClearTextureRectUInt] = sizeof(GpuCommandIrClearTextureRectUIntRecord);
    table[GpuCommandIrWireOpcode::UploadBuffer] = sizeof(GpuCommandIrUploadBufferRecord);
    table[GpuCommandIrWireOpcode::UploadTexture] = sizeof(GpuCommandIrUploadTextureRecord);
    return table;
}

inline constexpr Array<usize, GpuCommandIrWireOpcode::Count> s_RasterRecordWireByteSizes = MakeRasterWireSizeTable();
inline constexpr Array<usize, GpuCommandIrWireOpcode::Count> s_BuiltinRecordWireByteSizes = MakeBuiltinWireSizeTable();

[[nodiscard]] FOUNDATION_INLINE b8 IsRasterOpcode(const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    const u32 value = static_cast<u32>(opcode);
    return value < 32u && ((s_RasterOpcodeBitMask >> value) & 1u) != 0u;
}

[[nodiscard]] FOUNDATION_INLINE usize RasterRecordWireByteSize(const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    const usize index = static_cast<usize>(opcode);
    return index < s_RasterRecordWireByteSizes.size() ? s_RasterRecordWireByteSizes[index] : 0u;
}

[[nodiscard]] FOUNDATION_INLINE usize BuiltinRecordWireByteSize(const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    const usize index = static_cast<usize>(opcode);
    return index < s_BuiltinRecordWireByteSizes.size() ? s_BuiltinRecordWireByteSizes[index] : 0u;
}

union WireRecordStorage{
    GpuCommandIrHeader header;
    GpuCommandIrSetGraphicsStateRecord setGraphicsState;
    GpuCommandIrBindGraphicsHeapRecord bindGraphicsHeap;
    GpuCommandIrSetPushConstantsRecord setPushConstants;
    GpuCommandIrDrawRecord draw;
    GpuCommandIrEndRenderPassRecord endRenderPass;
    GpuCommandIrCopyBufferRecord copyBuffer;
    GpuCommandIrCopyTextureRecord copyTexture;
    GpuCommandIrClearBufferRecord clearBuffer;
    GpuCommandIrClearTextureRecord clearTexture;
    GpuCommandIrClearTextureRectUIntRecord clearTextureRectUInt;
    GpuCommandIrUploadBufferRecord uploadBuffer;
    GpuCommandIrUploadTextureRecord uploadTexture;

    WireRecordStorage()noexcept{}
};

namespace WireStepStatus{
    enum Enum : u8{
        Record,
        End,
        Error,
    };
};

[[nodiscard]] inline b8 IsRasterViewportValid(const Viewport& viewport)noexcept{
    return
        IsFinite(viewport.minX) && IsFinite(viewport.maxX)
        && IsFinite(viewport.minY) && IsFinite(viewport.maxY)
        && IsFinite(viewport.minZ) && IsFinite(viewport.maxZ)
        && viewport.minX < viewport.maxX && viewport.minY < viewport.maxY
        && viewport.minZ < viewport.maxZ && viewport.minZ >= 0.f && viewport.maxZ <= 1.f
    ;
}

[[nodiscard]] inline b8 IsWireGraphicsViewportValid(const GpuCommandIrSetGraphicsStateRecord& record)noexcept{
    return IsRasterViewportValid(Viewport{
        record.viewportMinX,
        record.viewportMaxX,
        record.viewportMinY,
        record.viewportMaxY,
        record.viewportMinZ,
        record.viewportMaxZ,
    });
}

[[nodiscard]] inline b8 ValidateSetGraphicsStateWireFields(
    const GpuCommandIrSetGraphicsStateRecord& record,
    const BinaryByteView blobBytes,
    const usize nextBlobOffset,
    GpuCommandIrStreamValidationError::Enum& outError
)noexcept{
    if(
        record.pipelineIndex == Limit<u32>::s_Max
        || record.colorAttachmentIndex == Limit<u32>::s_Max
        || record.framebufferOwnerIndex == Limit<u32>::s_Max
        || record.vertexBindingCount > s_MaxVertexAttributes
        || record.hasScissor > 1u
    ){
        outError = GpuCommandIrStreamValidationError::InvalidRecord;
        return false;
    }

    const u64 expectedBlobBytes = static_cast<u64>(record.vertexBindingCount) * sizeof(GpuCommandIrRasterVertexWire);
    if(
        record.blobSizeBytes != expectedBlobBytes
        || record.blobOffsetBytes != nextBlobOffset
        || record.blobSizeBytes > blobBytes.size() - nextBlobOffset
    ){
        outError = GpuCommandIrStreamValidationError::InvalidBlobRange;
        return false;
    }

    const b8 hasScissor = record.hasScissor != 0u;
    const Format::Enum indexFormat = static_cast<Format::Enum>(record.indexFormat);
    const b8 hasIndex = record.indexResourceIndex != Limit<u32>::s_Max;
    if(
        !IsWireGraphicsViewportValid(record)
        || !IsFinite(record.blendConstantColor.r) || !IsFinite(record.blendConstantColor.g)
        || !IsFinite(record.blendConstantColor.b) || !IsFinite(record.blendConstantColor.a)
        || (hasScissor ? (record.scissor.minX >= record.scissor.maxX || record.scissor.minY >= record.scissor.maxY) : (record.scissor.minX != 0 || record.scissor.maxX != 0 || record.scissor.minY != 0 || record.scissor.maxY != 0))
        || (hasIndex ? (indexFormat != Format::R16_UINT && indexFormat != Format::R32_UINT) : (indexFormat != Format::UNKNOWN || record.indexOffset != 0u))
    ){
        outError = GpuCommandIrStreamValidationError::InvalidRecord;
        return false;
    }

    if(record.vertexBindingCount != 0u){
        if(!blobBytes.data()){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        const u8* const vertexPtr = blobBytes.data() + static_cast<usize>(record.blobOffsetBytes);
        u32 usedSlotMask = 0u;
        Array<u32, s_MaxVertexAttributes> usedSlots{};
        b8 useFallbackSlotScan = false;
        for(usize index = 0u; index < record.vertexBindingCount; ++index){
            const auto wireBinding = ReadWirePod<GpuCommandIrRasterVertexWire>(vertexPtr + index * sizeof(GpuCommandIrRasterVertexWire));
            if(wireBinding.resourceIndex == Limit<u32>::s_Max){
                outError = GpuCommandIrStreamValidationError::InvalidRecord;
                return false;
            }
            if(wireBinding.slot < 32u){
                const u32 bit = 1u << wireBinding.slot;
                if((usedSlotMask & bit) != 0u){
                    outError = GpuCommandIrStreamValidationError::InvalidRecord;
                    return false;
                }
                usedSlotMask |= bit;
            }
            else{
                useFallbackSlotScan = true;
            }
            if(useFallbackSlotScan){
                for(usize prior = 0u; prior < index; ++prior){
                    if(usedSlots[prior] == wireBinding.slot){
                        outError = GpuCommandIrStreamValidationError::InvalidRecord;
                        return false;
                    }
                }
            }
            usedSlots[index] = wireBinding.slot;
        }
    }
    return true;
}

[[nodiscard]] inline b8 InitializeValidatedStreamHeader(
    const BinaryByteView headerSource,
    GpuCommandIrStreamHeader& outHeader,
    GpuCommandIrStreamValidationResult& outValidation
)noexcept{
    if(!headerSource.empty() && !headerSource.data()){
        outValidation.error = GpuCommandIrStreamValidationError::NullData;
        outValidation.byteOffset = 0u;
        outValidation.recordIndex = Limit<u64>::s_Max;
        outValidation.complete = true;
        return false;
    }
    if(headerSource.size() < sizeof(GpuCommandIrStreamHeaderPrefix)){
        outValidation.error = GpuCommandIrStreamValidationError::TruncatedStreamHeader;
        outValidation.byteOffset = 0u;
        outValidation.recordIndex = Limit<u64>::s_Max;
        outValidation.complete = true;
        return false;
    }
    const auto prefix = ReadWirePod<GpuCommandIrStreamHeaderPrefix>(headerSource.data());
    if(prefix.magic != s_GpuCommandIrStreamMagic){
        outValidation.error = GpuCommandIrStreamValidationError::InvalidMagic;
        outValidation.byteOffset = 0u;
        outValidation.recordIndex = Limit<u64>::s_Max;
        outValidation.complete = true;
        return false;
    }
    if(prefix.version != s_GpuCommandIrStreamVersion){
        outValidation.error = GpuCommandIrStreamValidationError::UnsupportedVersion;
        outValidation.byteOffset = 0u;
        outValidation.recordIndex = Limit<u64>::s_Max;
        outValidation.complete = true;
        return false;
    }
    if(prefix.reserved != 0u){
        outValidation.error = GpuCommandIrStreamValidationError::InvalidHeaderReserved;
        outValidation.byteOffset = 0u;
        outValidation.recordIndex = Limit<u64>::s_Max;
        outValidation.complete = true;
        return false;
    }
    if(headerSource.size() < sizeof(GpuCommandIrStreamHeader)){
        outValidation.error = GpuCommandIrStreamValidationError::TruncatedStreamHeader;
        outValidation.byteOffset = 0u;
        outValidation.recordIndex = Limit<u64>::s_Max;
        outValidation.complete = true;
        return false;
    }
    outHeader = ReadWirePod<GpuCommandIrStreamHeader>(headerSource.data());
    if(outHeader.commandBytes > static_cast<u64>(Limit<usize>::s_Max) || outHeader.blobBytes > static_cast<u64>(Limit<usize>::s_Max)){
        outValidation.error = GpuCommandIrStreamValidationError::PayloadSizeMismatch;
        outValidation.byteOffset = 0u;
        outValidation.recordIndex = Limit<u64>::s_Max;
        outValidation.complete = true;
        return false;
    }
    return true;
}

[[nodiscard]] inline b8 ValidateBuiltinWireFields(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordPtr,
    const BinaryByteView blobBytes,
    const u64 graphGeneration,
    const DecodedRecordContext& context,
    usize& inOutNextBlobOffset,
    GpuCommandIrStreamValidationError::Enum& outError
)noexcept{
    (void)context;
    if(graphGeneration == 0u){
        outError = GpuCommandIrStreamValidationError::InvalidRecord;
        return false;
    }
    constexpr u32 s_AllowedResourceStates = (static_cast<u32>(ResourceStates::ConvertCoopVecMatrixOutput) << 1u) - 1u;
    switch(opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        const auto wire = ReadWirePod<GpuCommandIrCopyBufferRecord>(recordPtr);
        if(wire.sourceResourceIndex == Limit<u32>::s_Max || wire.destinationResourceIndex == Limit<u32>::s_Max || wire.dataSizeBytes == 0u){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        return true;
    }
    case GpuCommandIrWireOpcode::CopyTexture:{
        const auto wire = ReadWirePod<GpuCommandIrCopyTextureRecord>(recordPtr);
        if(wire.sourceResourceIndex == Limit<u32>::s_Max || wire.destinationResourceIndex == Limit<u32>::s_Max){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        return true;
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        const auto wire = ReadWirePod<GpuCommandIrClearBufferRecord>(recordPtr);
        if(wire.destinationResourceIndex == Limit<u32>::s_Max){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        return true;
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        const auto wire = ReadWirePod<GpuCommandIrClearTextureRecord>(recordPtr);
        constexpr u8 s_AllowedFlags = static_cast<u8>(GpuCommandIrClearTextureFlag::ClearDepth | GpuCommandIrClearTextureFlag::ClearStencil);
        const u8 clearFlags = static_cast<u8>(wire.clearFlags);
        const b8 clearTypeOk = wire.clearTextureValueType == GpuClearTextureTaskValueType::DepthStencil
            ? clearFlags != GpuCommandIrClearTextureFlag::None
            : clearFlags == GpuCommandIrClearTextureFlag::None;
        if(
            wire.destinationResourceIndex == Limit<u32>::s_Max
            || wire.destinationSubresources.numMipLevels == 0u
            || wire.destinationSubresources.numArraySlices == 0u
            || wire.clearTextureValueType >= static_cast<u8>(GpuClearTextureTaskValueType::Count)
            || (clearFlags & ~s_AllowedFlags) != 0u
            || wire.reserved != 0u
            || !clearTypeOk
        ){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        return true;
    }
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:{
        const auto wire = ReadWirePod<GpuCommandIrClearTextureRectUIntRecord>(recordPtr);
        if(
            wire.destinationResourceIndex == Limit<u32>::s_Max
            || wire.destinationSubresources.numMipLevels == 0u
            || wire.destinationSubresources.numArraySlices == 0u
            || wire.clearRect.maxX <= wire.clearRect.minX
            || wire.clearRect.maxY <= wire.clearRect.minY
        ){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        return true;
    }
    case GpuCommandIrWireOpcode::UploadBuffer:{
        const auto wire = ReadWirePod<GpuCommandIrUploadBufferRecord>(recordPtr);
        if(wire.destinationResourceIndex == Limit<u32>::s_Max || wire.sourceUploadBlobIndex == Limit<u32>::s_Max || wire.reserved != 0u){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        if(wire.blobSizeBytes == 0u || wire.blobOffsetBytes != inOutNextBlobOffset || wire.blobSizeBytes > blobBytes.size() - inOutNextBlobOffset){
            outError = GpuCommandIrStreamValidationError::InvalidBlobRange;
            return false;
        }
        if(wire.finalState == ResourceStates::Unknown || (wire.finalState & ~s_AllowedResourceStates) != 0u){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        inOutNextBlobOffset += static_cast<usize>(wire.blobSizeBytes);
        return true;
    }
    case GpuCommandIrWireOpcode::UploadTexture:{
        const auto wire = ReadWirePod<GpuCommandIrUploadTextureRecord>(recordPtr);
        if(
            wire.destinationResourceIndex == Limit<u32>::s_Max
            || wire.sourceUploadBlobIndex == Limit<u32>::s_Max
            || wire.aspect >= TextureUploadAspect::Count
            || wire.reserved0 != 0u
            || wire.reserved1 != 0u
        ){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        if(wire.blobSizeBytes == 0u || wire.blobOffsetBytes != inOutNextBlobOffset || wire.blobSizeBytes > blobBytes.size() - inOutNextBlobOffset){
            outError = GpuCommandIrStreamValidationError::InvalidBlobRange;
            return false;
        }
        if(
            wire.finalState == ResourceStates::Unknown
            || (wire.finalState & ~s_AllowedResourceStates) != 0u
            || wire.destinationSlice.x != 0u
            || wire.destinationSlice.y != 0u
            || wire.destinationSlice.z != 0u
            || wire.destinationSlice.width == 0u
            || wire.destinationSlice.height == 0u
            || wire.destinationSlice.depth == 0u
            || wire.destinationSlice.width == TextureSlice::s_AllDimensions
            || wire.destinationSlice.height == TextureSlice::s_AllDimensions
            || wire.destinationSlice.depth == TextureSlice::s_AllDimensions
        ){
            outError = GpuCommandIrStreamValidationError::InvalidRecord;
            return false;
        }
        inOutNextBlobOffset += static_cast<usize>(wire.blobSizeBytes);
        return true;
    }
    default:
        outError = GpuCommandIrStreamValidationError::UnsupportedOpcode;
        return false;
    }
}

[[nodiscard]] b8 DecodeAndValidateBuiltinWireRecord(
    GpuCommandIrWireOpcode::Enum opcode,
    const u8* recordBytes,
    BinaryByteView blobBytes,
    u64 graphGeneration,
    u64 planGeneration,
    usize recordOffset,
    u64 recordIndex,
    usize& inOutNextBlobOffset,
    GpuCommandIrStreamValidationResult& outValidation,
    GpuCommandIrBuiltinTaskRecord& outDecoded
)noexcept;

class WireStreamCursor final : NoCopy{
public:
    WireStreamCursor(const BinaryByteView primaryBytes, const BinaryByteView secondaryBlobBytes, const b8 splitBuffers)noexcept{
        if(splitBuffers){
            if(!secondaryBlobBytes.empty() && !secondaryBlobBytes.data()){
                fail(GpuCommandIrStreamValidationError::NullData, 0u, Limit<u64>::s_Max);
                return;
            }
            GpuCommandIrStreamHeader header;
            if(!InitializeValidatedStreamHeader(primaryBytes, header, m_validation))
                return;
            if(header.commandBytes != static_cast<u64>(primaryBytes.size() - sizeof(GpuCommandIrStreamHeader)) || header.blobBytes != static_cast<u64>(secondaryBlobBytes.size())){
                fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
                return;
            }
            finalizeHeader(header, primaryBytes, secondaryBlobBytes);
        }
        else{
            GpuCommandIrStreamHeader header;
            if(!InitializeValidatedStreamHeader(primaryBytes, header, m_validation))
                return;
            const usize payloadBytes = primaryBytes.size() - sizeof(GpuCommandIrStreamHeader);
            const usize commandBytes = static_cast<usize>(header.commandBytes);
            const usize blobBytes = static_cast<usize>(header.blobBytes);
            if(commandBytes > payloadBytes || blobBytes != payloadBytes - commandBytes){
                fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
                return;
            }
            const BinaryByteView commandView{ primaryBytes.data(), sizeof(GpuCommandIrStreamHeader) + commandBytes };
            const BinaryByteView blobView = blobBytes != 0u ? BinaryByteView{ primaryBytes.data() + sizeof(GpuCommandIrStreamHeader) + commandBytes, blobBytes } : BinaryByteView{};
            finalizeHeader(header, commandView, blobView);
        }
    }


public:
    [[nodiscard]] WireStepStatus::Enum nextView(
        const u8*& outRecordPtr,
        GpuCommandIrWireOpcode::Enum& outOpcode,
        DecodedRecordContext& outContext
    )noexcept{
        b8 unusedMatchesPacket = false;
        return stepRecord<true, false>(GpuSubmissionPacketId{}, outRecordPtr, outOpcode, outContext, unusedMatchesPacket);
    }

    [[nodiscard]] WireStepStatus::Enum nextReplayFast(
        const u8*& outRecordPtr,
        GpuCommandIrWireOpcode::Enum& outOpcode,
        DecodedRecordContext& outContext
    )noexcept{
        b8 unusedMatchesPacket = false;
        return stepRecord<false, false>(GpuSubmissionPacketId{}, outRecordPtr, outOpcode, outContext, unusedMatchesPacket);
    }

    [[nodiscard]] WireStepStatus::Enum nextReplayFastForPacket(
        const GpuSubmissionPacketId targetPacket,
        const u8*& outRecordPtr,
        GpuCommandIrWireOpcode::Enum& outOpcode,
        DecodedRecordContext& outContext,
        b8& outMatchesPacket
    )noexcept{
        return stepRecord<false, true>(targetPacket, outRecordPtr, outOpcode, outContext, outMatchesPacket);
    }

    [[nodiscard]] WireStepStatus::Enum next(
        WireRecordStorage& outStorage,
        GpuCommandIrWireOpcode::Enum& outOpcode,
        DecodedRecordContext& outContext,
        GpuCommandIrBuiltinTaskRecord* const outBuiltin
    )noexcept{
        const u8* recordPtr = nullptr;
        b8 unusedMatchesPacket = false;
        const WireStepStatus::Enum status = stepRecord<true, false>(GpuSubmissionPacketId{}, recordPtr, outOpcode, outContext, unusedMatchesPacket);
        if(status == WireStepStatus::Record && recordPtr){
            const usize byteSize = IsRasterOpcode(outOpcode) ? RasterRecordWireByteSize(outOpcode) : BuiltinRecordWireByteSize(outOpcode);
            std::memcpy(&outStorage, recordPtr, byteSize);
            if(!IsRasterOpcode(outOpcode) && outBuiltin){
                GpuCommandIrStreamValidationResult unusedValidation;
                usize unusedBlobOffset = m_nextBlobOffset;
                (void)DecodeAndValidateBuiltinWireRecord(outOpcode, recordPtr, m_blobBytes, m_graphGeneration, m_planGeneration, m_cursor, m_nextRecordIndex, unusedBlobOffset, unusedValidation, *outBuiltin);
            }
        }
        return status;
    }

    void drain()noexcept{
        const u8* recordPtr = nullptr;
        GpuCommandIrWireOpcode::Enum opcode = GpuCommandIrWireOpcode::Count;
        DecodedRecordContext context;
        b8 unusedMatchesPacket = false;
        while(stepRecord<true, false>(GpuSubmissionPacketId{}, recordPtr, opcode, context, unusedMatchesPacket) == WireStepStatus::Record){}
    }

    [[nodiscard]] const GpuCommandIrStreamValidationResult& validation()const noexcept{ return m_validation; }
    [[nodiscard]] BinaryByteView blobBytes()const noexcept{ return m_validation.failed() || m_blobBytes.empty() ? BinaryByteView{} : m_blobBytes; }
    [[nodiscard]] u64 graphGeneration()const noexcept{ return m_graphGeneration; }
    [[nodiscard]] u64 planGeneration()const noexcept{ return m_planGeneration; }
    [[nodiscard]] u64 recordCount()const noexcept{ return m_recordCount; }


private:
    template<b8 FullWireValidation, b8 FilterByPacket>
    [[nodiscard]] WireStepStatus::Enum stepRecord(
        const GpuSubmissionPacketId targetPacket,
        const u8*& outRecordPtr,
        GpuCommandIrWireOpcode::Enum& outOpcode,
        DecodedRecordContext& outContext,
        b8& outMatchesPacket
    )noexcept{
        if constexpr(!FilterByPacket){
            (void)targetPacket;
            (void)outMatchesPacket;
        }
        if(m_validation.failed())
            return WireStepStatus::Error;
        if(m_nextRecordIndex == m_recordCount){
            if(m_cursor != m_payloadEnd){
                fail(GpuCommandIrStreamValidationError::TrailingPayload, m_cursor, m_nextRecordIndex);
                return WireStepStatus::Error;
            }
            if(m_nextBlobOffset != m_blobBytes.size()){
                fail(GpuCommandIrStreamValidationError::InvalidBlobRange, m_payloadEnd, m_nextRecordIndex);
                return WireStepStatus::Error;
            }
            m_validation.byteOffset = m_payloadEnd;
            m_validation.recordIndex = m_nextRecordIndex;
            m_validation.complete = true;
            return WireStepStatus::End;
        }

        const usize recordOffset = m_cursor;
        if(m_payloadEnd - recordOffset < sizeof(GpuCommandIrHeader)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return WireStepStatus::Error;
        }
        const u8* const recordPtr = m_commandBytes.data() + recordOffset;
        const auto header = ReadWirePod<GpuCommandIrHeader>(recordPtr);
        outOpcode = header.opcode;

        const b8 raster = IsRasterOpcode(header.opcode);
        const usize expectedByteSize = raster ? RasterRecordWireByteSize(header.opcode) : BuiltinRecordWireByteSize(header.opcode);
        if(!raster && header.byteSize < sizeof(GpuCommandIrHeader)){
            fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
            return WireStepStatus::Error;
        }
        if(expectedByteSize == 0u){
            fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
            return WireStepStatus::Error;
        }
        if(header.byteSize != expectedByteSize){
            fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
            return WireStepStatus::Error;
        }
        if(expectedByteSize > m_payloadEnd - recordOffset){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return WireStepStatus::Error;
        }

        const auto rawContext = ReadWirePod<GpuCommandIrRecordContext>(recordPtr + sizeof(GpuCommandIrHeader));
        const auto decodedContext = DecodeRecordContext(rawContext, m_graphGeneration, m_planGeneration);
        if(!decodedContext){
            fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
            return WireStepStatus::Error;
        }
        outContext = *decodedContext;
        if constexpr(FilterByPacket){
            outMatchesPacket = (decodedContext->packet == targetPacket);
            if(!raster && !outMatchesPacket){
                outRecordPtr = nullptr;
                usize nextBlobOffset = m_nextBlobOffset;
                if(header.opcode == GpuCommandIrWireOpcode::UploadBuffer){
                    const auto upload = ReadWirePod<GpuCommandIrUploadBufferRecord>(recordPtr);
                    if(upload.blobSizeBytes == 0u || !m_blobBytes.data() || upload.blobOffsetBytes != m_nextBlobOffset || upload.blobSizeBytes > m_blobBytes.size() - m_nextBlobOffset){
                        fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
                        return WireStepStatus::Error;
                    }
                    nextBlobOffset += static_cast<usize>(upload.blobSizeBytes);
                }
                else if(header.opcode == GpuCommandIrWireOpcode::UploadTexture){
                    const auto upload = ReadWirePod<GpuCommandIrUploadTextureRecord>(recordPtr);
                    if(upload.blobSizeBytes == 0u || !m_blobBytes.data() || upload.blobOffsetBytes != m_nextBlobOffset || upload.blobSizeBytes > m_blobBytes.size() - m_nextBlobOffset){
                        fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
                        return WireStepStatus::Error;
                    }
                    nextBlobOffset += static_cast<usize>(upload.blobSizeBytes);
                }
                m_cursor = recordOffset + expectedByteSize;
                m_nextBlobOffset = nextBlobOffset;
                ++m_nextRecordIndex;
                return WireStepStatus::Record;
            }
        }

        if(!raster){
            usize nextBlobOffset = m_nextBlobOffset;
            if constexpr(FullWireValidation){
                GpuCommandIrStreamValidationError::Enum builtinError = GpuCommandIrStreamValidationError::None;
                if(!ValidateBuiltinWireFields(header.opcode, recordPtr, m_blobBytes, m_graphGeneration, *decodedContext, nextBlobOffset, builtinError)){
                    fail(builtinError, recordOffset, m_nextRecordIndex);
                    return WireStepStatus::Error;
                }
            }
            else{
                if(header.opcode == GpuCommandIrWireOpcode::UploadBuffer){
                    const auto upload = ReadWirePod<GpuCommandIrUploadBufferRecord>(recordPtr);
                    if(upload.blobSizeBytes == 0u || !m_blobBytes.data() || upload.blobOffsetBytes != m_nextBlobOffset || upload.blobSizeBytes > m_blobBytes.size() - m_nextBlobOffset){
                        fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
                        return WireStepStatus::Error;
                    }
                    nextBlobOffset += static_cast<usize>(upload.blobSizeBytes);
                }
                else if(header.opcode == GpuCommandIrWireOpcode::UploadTexture){
                    const auto upload = ReadWirePod<GpuCommandIrUploadTextureRecord>(recordPtr);
                    if(upload.blobSizeBytes == 0u || !m_blobBytes.data() || upload.blobOffsetBytes != m_nextBlobOffset || upload.blobSizeBytes > m_blobBytes.size() - m_nextBlobOffset){
                        fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
                        return WireStepStatus::Error;
                    }
                    nextBlobOffset += static_cast<usize>(upload.blobSizeBytes);
                }
            }
            outRecordPtr = recordPtr;
            m_cursor = recordOffset + expectedByteSize;
            m_nextBlobOffset = nextBlobOffset;
            ++m_nextRecordIndex;
            return WireStepStatus::Record;
        }

        usize nextBlobOffset = m_nextBlobOffset;
        switch(header.opcode){
        case GpuCommandIrWireOpcode::SetGraphicsState:{
            const auto wire = ReadWirePod<GpuCommandIrSetGraphicsStateRecord>(recordPtr);
            if constexpr(FullWireValidation){
                GpuCommandIrStreamValidationError::Enum fieldError = GpuCommandIrStreamValidationError::None;
                if(!ValidateSetGraphicsStateWireFields(wire, m_blobBytes, m_nextBlobOffset, fieldError)){
                    fail(fieldError, recordOffset, m_nextRecordIndex);
                    return WireStepStatus::Error;
                }
            }
            else{
                if(
                    wire.pipelineIndex == Limit<u32>::s_Max
                    || wire.colorAttachmentIndex == Limit<u32>::s_Max
                    || wire.framebufferOwnerIndex == Limit<u32>::s_Max
                    || wire.vertexBindingCount > s_MaxVertexAttributes
                ){
                    fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
                    return WireStepStatus::Error;
                }
                const u64 expectedBlobBytes = static_cast<u64>(wire.vertexBindingCount) * sizeof(GpuCommandIrRasterVertexWire);
                if(
                    wire.blobSizeBytes != expectedBlobBytes
                    || wire.blobOffsetBytes != m_nextBlobOffset
                    || wire.blobSizeBytes > m_blobBytes.size() - m_nextBlobOffset
                    || (wire.vertexBindingCount != 0u && !m_blobBytes.data())
                ){
                    fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
                    return WireStepStatus::Error;
                }
            }
            nextBlobOffset += static_cast<usize>(wire.blobSizeBytes);
            break;
        }
        case GpuCommandIrWireOpcode::BindGraphicsHeap:
            if constexpr(FullWireValidation){
                const auto wire = ReadWirePod<GpuCommandIrBindGraphicsHeapRecord>(recordPtr);
                if(wire.pipelineIndex == Limit<u32>::s_Max || wire.heapOwnerIndex == Limit<u32>::s_Max){
                    fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
                    return WireStepStatus::Error;
                }
            }
            break;
        case GpuCommandIrWireOpcode::SetPushConstants:{
            const auto wire = ReadWirePod<GpuCommandIrSetPushConstantsRecord>(recordPtr);
            const u64 sizeBytes = wire.blobSizeBytes;
            const b8 sizeValid = sizeBytes != 0u && sizeBytes <= Limit<u32>::s_Max && (sizeBytes & (s_PushConstantSizeAlignment - 1u)) == 0u;
            if(sizeValid && (wire.blobOffsetBytes != m_nextBlobOffset || sizeBytes > m_blobBytes.size() - m_nextBlobOffset || !m_blobBytes.data())){
                fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
                return WireStepStatus::Error;
            }
            if(!sizeValid){
                fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
                return WireStepStatus::Error;
            }
            nextBlobOffset += static_cast<usize>(sizeBytes);
            break;
        }
        case GpuCommandIrWireOpcode::Draw:
        case GpuCommandIrWireOpcode::DrawIndexed:
        case GpuCommandIrWireOpcode::EndRenderPass:
            break;
        default:
            fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
            return WireStepStatus::Error;
        }

        outRecordPtr = recordPtr;
        m_cursor = recordOffset + expectedByteSize;
        m_nextBlobOffset = nextBlobOffset;
        ++m_nextRecordIndex;
        return WireStepStatus::Record;
    }

    void finalizeHeader(const GpuCommandIrStreamHeader& header, const BinaryByteView commandBytes, const BinaryByteView blobBytes)noexcept{
        if((header.recordCount == 0u && header.graphGeneration != 0u) || (header.recordCount != 0u && header.graphGeneration == 0u)){
            fail(GpuCommandIrStreamValidationError::InvalidGraphGeneration, 0u, Limit<u64>::s_Max);
            return;
        }
        if((header.recordCount == 0u && header.planGeneration != 0u) || (header.recordCount != 0u && header.planGeneration == 0u)){
            fail(GpuCommandIrStreamValidationError::InvalidPlanGeneration, 0u, Limit<u64>::s_Max);
            return;
        }
        if(header.recordCount > header.commandBytes / sizeof(GpuCommandIrHeader)){
            fail(GpuCommandIrStreamValidationError::InvalidRecordCount, 0u, Limit<u64>::s_Max);
            return;
        }
        m_commandBytes = commandBytes;
        m_blobBytes = blobBytes;
        m_cursor = sizeof(GpuCommandIrStreamHeader);
        m_payloadEnd = commandBytes.size();
        m_graphGeneration = header.graphGeneration;
        m_planGeneration = header.planGeneration;
        m_recordCount = header.recordCount;
    }

    void fail(const GpuCommandIrStreamValidationError::Enum error, const usize byteOffset, const u64 recordIndex)noexcept{
        if(m_validation.failed())
            return;
        m_validation.error = error;
        m_validation.byteOffset = byteOffset;
        m_validation.recordIndex = recordIndex;
        m_validation.complete = true;
    }


private:
    BinaryByteView m_commandBytes;
    BinaryByteView m_blobBytes;
    GpuCommandIrStreamValidationResult m_validation;
    usize m_cursor = 0u;
    usize m_payloadEnd = 0u;
    usize m_nextBlobOffset = 0u;
    u64 m_graphGeneration = 0u;
    u64 m_planGeneration = 0u;
    u64 m_recordCount = 0u;
    u64 m_nextRecordIndex = 0u;
};

[[nodiscard]] GpuCommandIrReplayError::Enum ValidateRasterGraphOperation(const GpuCommandIrRasterTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task, RasterReplayState& state)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateSetGraphicsStateWireGraphOperation(
    const u8* recordPtr,
    GpuTaskId taskId,
    BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    RasterReplayState& state
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateRasterBuiltinBoundary(const GpuTaskId& task, const RasterReplayState& state)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateBuiltinBackendOperand(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuPhysicalQueueInfo& queue, CommandList& commandList, BuiltinBackendResourceCache& resourceCache)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateBuiltinWireBackendOperand(
    GpuCommandIrWireOpcode::Enum opcode,
    const u8* recordPtr,
    u64 graphGeneration,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList,
    BuiltinBackendResourceCache& resourceCache
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateRasterWireBackendOperand(
    GpuCommandIrWireOpcode::Enum opcode,
    const u8* recordPtr,
    BinaryByteView blobBytes,
    GpuTaskId taskId,
    const RasterOwnerTableView& owners,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    u64 recordIndex,
    CommandList& commandList,
    BackendRasterReplayState& state
)noexcept;
[[nodiscard]] GpuCommandIrReplayResult PreflightGpuCommandIrPacketStream(
    BinaryByteView primaryBytes,
    BinaryByteView secondaryBlobBytes,
    b8 splitBuffers,
    const RasterOwnerTableView& owners,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledGraph,
    GpuSubmissionPacketId packet,
    CommandList* commandList
)noexcept;
[[nodiscard]] inline GpuCommandIrReplayResult ReplayFailure(const GpuCommandIrReplayError::Enum error, const GpuCommandIrStreamValidationResult& streamValidation = {}, const u64 recordIndex = Limit<u64>::s_Max)noexcept{
    return GpuCommandIrReplayResult{ .error = error, .streamValidation = streamValidation, .recordIndex = recordIndex, };
}

[[nodiscard]] const GpuTaskResourceUse* FindTaskResourceUse(const GpuTaskGraphTaskView& task, GpuGraphResourceId resource, ResourceStates::Mask state, GpuTaskResourceAccess::Enum access)noexcept;

void LowerSetGraphicsStateWireRecord(
    const u8* recordPtr,
    const GpuCommandIrRasterStateOwner& owner,
    BinaryByteView blobBytes,
    CommandList& commandList,
    GraphicsState& reusableState
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

