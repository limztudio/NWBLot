// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_raster.h"
#include "command_ir_internal.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_raster_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HeapLeaseOwner final : public IGpuCommandIrRetainedOwner{
public:
    HeapLeaseOwner(GraphicsArena& arena, GpuDescriptorHeap& heap)
        : m_arena(arena)
        , m_lease(heap.acquirePendingRecordingLease())
    {}
    virtual ~HeapLeaseOwner()noexcept override = default;


public:
    virtual void retain()noexcept override{ m_references.fetch_add(1u, MemoryOrder::relaxed); }
    virtual void release()noexcept override{
        if(m_references.fetch_sub(1u, MemoryOrder::release) == 1u){
            AtomicThreadFence(MemoryOrder::acquire);
            DestroyArenaObjectNoexcept(m_arena, this);
        }
    }
    [[nodiscard]] bool valid()const noexcept{ return m_lease.valid(); }


private:
    GraphicsArena& m_arena;
    GpuDescriptorHeap::PendingRecordingLease m_lease;
    Atomic<u32> m_references{ 1u };
};

[[nodiscard]] static GpuCommandIrRecordContext EncodeContext(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue
)noexcept{
    return { task.index, packet.index, queue.index, queue.deviceGeneration };
}

template<typename RecordT>
static void InitializeRecord(
    RecordT& record,
    const GpuCommandIrWireOpcode::Enum opcode,
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue
)noexcept{
    static_assert(IsStandardLayout_V<RecordT> && IsTriviallyCopyable_V<RecordT>);
    GLB_MEMSET(&record, 0, sizeof(record));
    record.header.opcode = opcode;
    record.header.byteSize = static_cast<u16>(sizeof(record));
    record.context = EncodeContext(task, packet, queue);
}

[[nodiscard]] static GpuCommandIrFloatColor EncodeColor(const Color& color)noexcept{
    return { color.r, color.g, color.b, color.a };
}

[[nodiscard]] static GpuCommandIrRect EncodeRect(const Rect& rect)noexcept{
    return { rect.minX, rect.maxX, rect.minY, rect.maxY };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrOwnerAnchor::GpuCommandIrOwnerAnchor(IGpuCommandIrRetainedOwner* const owner, AdoptRefT)noexcept
    : m_owner(owner)
{}

GpuCommandIrOwnerAnchor::GpuCommandIrOwnerAnchor(const GpuCommandIrOwnerAnchor& other)noexcept
    : m_owner(other.m_owner){
    if(m_owner)
        m_owner->retain();
}

GpuCommandIrOwnerAnchor::GpuCommandIrOwnerAnchor(GpuCommandIrOwnerAnchor&& other)noexcept
    : m_owner(Exchange(other.m_owner, nullptr))
{}

GpuCommandIrOwnerAnchor::~GpuCommandIrOwnerAnchor()noexcept{
    if(m_owner)
        m_owner->release();
}

GpuCommandIrOwnerAnchor& GpuCommandIrOwnerAnchor::operator=(const GpuCommandIrOwnerAnchor& other)noexcept{
    if(this == &other)
        return *this;
    IGpuCommandIrRetainedOwner* const next = other.m_owner;
    if(next)
        next->retain();
    IGpuCommandIrRetainedOwner* const previous = Exchange(m_owner, next);
    if(previous)
        previous->release();
    return *this;
}

GpuCommandIrOwnerAnchor& GpuCommandIrOwnerAnchor::operator=(GpuCommandIrOwnerAnchor&& other)noexcept{
    if(this == &other)
        return *this;
    IGpuCommandIrRetainedOwner* const previous = Exchange(m_owner, Exchange(other.m_owner, nullptr));
    if(previous)
        previous->release();
    return *this;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrOwnedStream::~GpuCommandIrOwnedStream()noexcept{
    if(m_rasterOwners)
        DestroyArenaObjectNoexcept(m_arena, m_rasterOwners);
}

const GpuCommandIrRasterStateOwner* GpuCommandIrOwnedStream::rasterStateOwner(const u32 index)const noexcept{
    return m_rasterOwners && index < m_rasterOwners->stateOwners.size() ? &m_rasterOwners->stateOwners[index] : nullptr;
}

const GpuCommandIrRasterHeapOwner* GpuCommandIrOwnedStream::rasterHeapOwner(const u32 index)const noexcept{
    return m_rasterOwners && index < m_rasterOwners->heapOwners.size() ? &m_rasterOwners->heapOwners[index] : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrCapture::~GpuCommandIrCapture()noexcept{
    if(m_rasterOwners)
        DestroyArenaObjectNoexcept(m_arena, m_rasterOwners);
}

bool GpuCommandIrCapture::appendRasterBytes(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const BinaryByteView encoded,
    const BinaryByteView blob,
    const GpuCommandIrRasterStateOwner* const stateOwner,
    const GpuCommandIrRasterHeapOwner* const heapOwner
){
    if(
        !task.valid() || !packet.valid() || !queue.valid()
        || encoded.size() < sizeof(GpuCommandIrHeader) || !encoded.data()
        || (!blob.empty() && !blob.data())
        || (stateOwner && heapOwner)
        || (m_graphGeneration != 0u && m_graphGeneration != task.generation)
        || (m_planGeneration != 0u && m_planGeneration != packet.generation)
        || m_nextSerial == Limit<u64>::s_Max
        || m_recordEndOffsets.size() == Limit<usize>::s_Max
        || !BinaryDetail::CanAppendBytes(m_commandBytes, encoded.size())
        || !BinaryDetail::CanAppendBytes(m_blobBytes, blob.size())
        || m_commandBytes.size() - sizeof(GpuCommandIrStreamHeader) > Limit<u64>::s_Max - encoded.size()
        || m_blobBytes.size() > Limit<u64>::s_Max - blob.size()
    )
        return false;

    const usize nextCount = m_recordEndOffsets.size() + 1u;
    if(
        !BinaryDetail::CanStoreValueCount(m_recordEndOffsets, nextCount)
        || !BinaryDetail::CanStoreValueCount(m_blobEndOffsets, nextCount)
        || !BinaryDetail::CanStoreValueCount(m_recordSerials, nextCount)
    )
        return false;
    if((stateOwner || heapOwner) && !m_rasterOwners)
        m_rasterOwners = NewArenaObject<GpuCommandIrRasterOwnerTable>(m_arena, m_arena);
    if(stateOwner && m_rasterOwners->stateOwners.size() >= Limit<u32>::s_Max)
        return false;
    if(heapOwner && m_rasterOwners->heapOwners.size() >= Limit<u32>::s_Max)
        return false;

    ContainerDetail::ReserveGrowingCapacity(m_recordEndOffsets, nextCount);
    ContainerDetail::ReserveGrowingCapacity(m_blobEndOffsets, nextCount);
    ContainerDetail::ReserveGrowingCapacity(m_recordSerials, nextCount);
    ContainerDetail::ReserveGrowingCapacity(m_commandBytes, m_commandBytes.size() + encoded.size());
    if(!blob.empty())
        ContainerDetail::ReserveGrowingCapacity(m_blobBytes, m_blobBytes.size() + blob.size());
    if(stateOwner)
        ContainerDetail::ReserveGrowingCapacity(m_rasterOwners->stateOwners, m_rasterOwners->stateOwners.size() + 1u);
    if(heapOwner)
        ContainerDetail::ReserveGrowingCapacity(m_rasterOwners->heapOwners, m_rasterOwners->heapOwners.size() + 1u);

    const usize oldCommandSize = m_commandBytes.size();
    m_commandBytes.resize(oldCommandSize + encoded.size());
    GLB_MEMCPY(m_commandBytes.data() + oldCommandSize, encoded.size(), encoded.data(), encoded.size());
    if(!blob.empty()){
        const usize oldBlobSize = m_blobBytes.size();
        m_blobBytes.resize(oldBlobSize + blob.size());
        GLB_MEMCPY(m_blobBytes.data() + oldBlobSize, blob.size(), blob.data(), blob.size());
    }
    if(stateOwner){
        GpuCommandIrRasterStateOwner owner = *stateOwner;
        owner.recordIndex = m_recordEndOffsets.size();
        m_rasterOwners->stateOwners.push_back(Move(owner));
    }
    if(heapOwner){
        GpuCommandIrRasterHeapOwner owner = *heapOwner;
        owner.recordIndex = m_recordEndOffsets.size();
        m_rasterOwners->heapOwners.push_back(Move(owner));
    }
    m_recordEndOffsets.push_back(m_commandBytes.size());
    m_blobEndOffsets.push_back(m_blobBytes.size());
    m_recordSerials.push_back(m_nextSerial++);
    if(m_graphGeneration == 0u)
        m_graphGeneration = task.generation;
    if(m_planGeneration == 0u)
        m_planGeneration = packet.generation;
    writeStreamHeader();
    return true;
}

bool GpuCommandIrCapture::captureSetGraphicsState(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuCommandIrRasterStateDesc& state
){
    if(
        !state.pipeline.valid() || state.pipeline.generation != task.generation
        || !state.colorAttachment.valid() || state.colorAttachment.generation != task.generation
        || !GpuCommandIrDetail::IsRasterViewportValid(state.viewport)
        || (state.hasScissor && (state.scissor.maxX <= state.scissor.minX || state.scissor.maxY <= state.scissor.minY))
        || state.vertexBuffers.size() > s_MaxVertexAttributes
        || (state.indexResource.valid() && state.indexResource.generation != task.generation)
        || (state.indexResource.valid() && state.indexFormat != Format::R16_UINT && state.indexFormat != Format::R32_UINT)
        || (!state.indexResource.valid() && (state.indexOffset != 0u || state.indexFormat != Format::UNKNOWN))
        || !IsFinite(state.blendConstantColor.r) || !IsFinite(state.blendConstantColor.g)
        || !IsFinite(state.blendConstantColor.b) || !IsFinite(state.blendConstantColor.a)
    )
        return false;
    if(state.framebufferOwner){
        const FramebufferDesc& framebuffer = state.framebufferOwner->getDescription();
        if(framebuffer.colorAttachments.size() != 1u || framebuffer.depthAttachment.valid() || framebuffer.shadingRateAttachment.valid())
            return false;
    }
    if(m_rasterOwners && m_rasterOwners->stateOwners.size() >= Limit<u32>::s_Max)
        return false;
    GpuCommandIrRasterVertexWire wires[s_MaxVertexAttributes]{};
    GpuCommandIrRasterStateOwner owner;
    owner.pipeline = state.pipelineOwner;
    owner.framebuffer = state.framebufferOwner;
    owner.indexBuffer = state.indexBuffer;
    owner.colorAttachment = state.colorAttachment;
    for(usize index = 0u; index < state.vertexBuffers.size(); ++index){
        const GpuCommandIrRasterVertexOwner& binding = state.vertexBuffers[index];
        if(!binding.resource.valid() || binding.resource.generation != task.generation)
            return false;
        for(usize prior = 0u; prior < index; ++prior){
            if(state.vertexBuffers[prior].slot == binding.slot)
                return false;
        }
        wires[index] = { binding.resource.index, binding.slot, binding.offset };
        owner.vertexBuffers.push_back(binding.buffer);
    }
    GpuCommandIrSetGraphicsStateRecord record;
    __hidden_gpu_command_ir_raster_capture::InitializeRecord(record, GpuCommandIrWireOpcode::SetGraphicsState, task, packet, queue);
    record.pipelineIndex = state.pipeline.index;
    record.colorAttachmentIndex = state.colorAttachment.index;
    record.framebufferOwnerIndex = m_rasterOwners ? static_cast<u32>(m_rasterOwners->stateOwners.size()) : 0u;
    record.indexResourceIndex = state.indexResource.index;
    record.indexOffset = state.indexOffset;
    record.blobOffsetBytes = static_cast<u64>(m_blobBytes.size());
    record.blobSizeBytes = static_cast<u64>(state.vertexBuffers.size() * sizeof(GpuCommandIrRasterVertexWire));
    record.blendConstantColor = __hidden_gpu_command_ir_raster_capture::EncodeColor(state.blendConstantColor);
    record.viewportMinX = state.viewport.minX;
    record.viewportMaxX = state.viewport.maxX;
    record.viewportMinY = state.viewport.minY;
    record.viewportMaxY = state.viewport.maxY;
    record.viewportMinZ = state.viewport.minZ;
    record.viewportMaxZ = state.viewport.maxZ;
    record.scissor = state.hasScissor ? __hidden_gpu_command_ir_raster_capture::EncodeRect(state.scissor) : GpuCommandIrRect{};
    record.vertexBindingCount = static_cast<u8>(state.vertexBuffers.size());
    record.hasScissor = state.hasScissor ? 1u : 0u;
    record.indexFormat = static_cast<u8>(state.indexFormat);
    record.stencilRef = state.dynamicStencilRefValue;
    return appendRasterBytes(
        task, packet, queue,
        BinaryByteView{ reinterpret_cast<const u8*>(&record), sizeof(record) },
        BinaryByteView{ reinterpret_cast<const u8*>(wires), static_cast<usize>(record.blobSizeBytes) },
        &owner, nullptr
    );
}

bool GpuCommandIrCapture::captureBindGraphicsHeap(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphPipelineId pipeline,
    const GraphicsPipelineHandle& pipelineOwner,
    GpuDescriptorHeap& heap,
    GraphicsArena& ownerArena,
    const GpuCommandIrOwnerAnchor& descriptorOwner
){
    if(!pipeline.valid() || pipeline.generation != task.generation || !pipelineOwner || !descriptorOwner.valid() || !heap.isInitialized())
        return false;
    if(m_rasterOwners && m_rasterOwners->heapOwners.size() >= Limit<u32>::s_Max)
        return false;
    auto* const lease = NewArenaObject<__hidden_gpu_command_ir_raster_capture::HeapLeaseOwner>(ownerArena, ownerArena, heap);
    GpuCommandIrOwnerAnchor leaseAnchor(lease, AdoptRef);
    if(!lease->valid())
        return false;
    GpuCommandIrRasterHeapOwner owner;
    owner.pipeline = pipelineOwner;
    owner.heap = &heap;
    owner.lease = Move(leaseAnchor);
    owner.descriptors = descriptorOwner;
    GpuCommandIrBindGraphicsHeapRecord record;
    __hidden_gpu_command_ir_raster_capture::InitializeRecord(record, GpuCommandIrWireOpcode::BindGraphicsHeap, task, packet, queue);
    record.pipelineIndex = pipeline.index;
    record.heapOwnerIndex = m_rasterOwners ? static_cast<u32>(m_rasterOwners->heapOwners.size()) : 0u;
    return appendRasterBytes(task, packet, queue, BinaryByteView{ reinterpret_cast<const u8*>(&record), sizeof(record) }, {}, nullptr, &owner);
}

bool GpuCommandIrCapture::capturePushConstants(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const BinaryByteView bytes
){
    if(bytes.empty() || !bytes.data() || bytes.size() > Limit<u32>::s_Max || (bytes.size() & 3u) != 0u)
        return false;
    GpuCommandIrSetPushConstantsRecord record;
    __hidden_gpu_command_ir_raster_capture::InitializeRecord(record, GpuCommandIrWireOpcode::SetPushConstants, task, packet, queue);
    record.blobOffsetBytes = static_cast<u64>(m_blobBytes.size());
    record.blobSizeBytes = static_cast<u64>(bytes.size());
    return appendRasterBytes(task, packet, queue, BinaryByteView{ reinterpret_cast<const u8*>(&record), sizeof(record) }, bytes, nullptr, nullptr);
}

bool GpuCommandIrCapture::captureDraw(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const DrawArguments& arguments,
    const bool indexed
){
    GpuCommandIrDrawRecord record;
    __hidden_gpu_command_ir_raster_capture::InitializeRecord(
        record, indexed ? GpuCommandIrWireOpcode::DrawIndexed : GpuCommandIrWireOpcode::Draw, task, packet, queue
    );
    record.vertexCount = arguments.vertexCount;
    record.instanceCount = arguments.instanceCount;
    record.startIndexLocation = arguments.startIndexLocation;
    record.startVertexLocation = arguments.startVertexLocation;
    record.startInstanceLocation = arguments.startInstanceLocation;
    return appendRasterBytes(task, packet, queue, BinaryByteView{ reinterpret_cast<const u8*>(&record), sizeof(record) }, {}, nullptr, nullptr);
}

bool GpuCommandIrCapture::captureEndRenderPass(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue
){
    GpuCommandIrEndRenderPassRecord record;
    __hidden_gpu_command_ir_raster_capture::InitializeRecord(record, GpuCommandIrWireOpcode::EndRenderPass, task, packet, queue);
    return appendRasterBytes(task, packet, queue, BinaryByteView{ reinterpret_cast<const u8*>(&record), sizeof(record) }, {}, nullptr, nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

