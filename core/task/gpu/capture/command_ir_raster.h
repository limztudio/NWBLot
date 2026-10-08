// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command_ir.h"

#include <global/arena_object.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Raster records carry graph indices and scalars only. Variable vertex bindings and push bytes live in the stream blob.
// The first retained raster scope supports one color attachment, one viewport, optional scissor, and direct draws.
#pragma pack(push, 1)
struct GpuCommandIrRasterVertexWire{
    u32 resourceIndex = Limit<u32>::s_Max;
    u32 slot = 0u;
    u64 offset = 0u;
};

struct GpuCommandIrSetGraphicsStateRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u32 pipelineIndex = Limit<u32>::s_Max;
    u32 colorAttachmentIndex = Limit<u32>::s_Max;
    u32 framebufferOwnerIndex = Limit<u32>::s_Max;
    u32 indexResourceIndex = Limit<u32>::s_Max;
    u32 indexOffset = 0u;
    u64 blobOffsetBytes = 0u;
    u64 blobSizeBytes = 0u;
    GpuCommandIrFloatColor blendConstantColor;
    f32 viewportMinX = 0.f;
    f32 viewportMaxX = 0.f;
    f32 viewportMinY = 0.f;
    f32 viewportMaxY = 0.f;
    f32 viewportMinZ = 0.f;
    f32 viewportMaxZ = 0.f;
    GpuCommandIrRect scissor;
    u8 vertexBindingCount = 0u;
    u8 hasScissor = 0u;
    u8 indexFormat = Format::UNKNOWN;
    u8 stencilRef = 0u;
};

struct GpuCommandIrBindGraphicsHeapRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u32 pipelineIndex = Limit<u32>::s_Max;
    u32 heapOwnerIndex = Limit<u32>::s_Max;
};

struct GpuCommandIrSetPushConstantsRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u64 blobOffsetBytes = 0u;
    u64 blobSizeBytes = 0u;
};

struct GpuCommandIrDrawRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
    u32 vertexCount = 0u;
    u32 instanceCount = 0u;
    u32 startIndexLocation = 0u;
    u32 startVertexLocation = 0u;
    u32 startInstanceLocation = 0u;
};

struct GpuCommandIrEndRenderPassRecord{
    GpuCommandIrHeader header;
    GpuCommandIrRecordContext context;
};
#pragma pack(pop)

static_assert(sizeof(GpuCommandIrRasterVertexWire) == 16u);
static_assert(sizeof(GpuCommandIrSetGraphicsStateRecord) == 112u);
static_assert(sizeof(GpuCommandIrBindGraphicsHeapRecord) == 24u);
static_assert(sizeof(GpuCommandIrSetPushConstantsRecord) == 32u);
static_assert(sizeof(GpuCommandIrDrawRecord) == 36u);
static_assert(sizeof(GpuCommandIrEndRenderPassRecord) == 16u);
static_assert(IsStandardLayout_V<GpuCommandIrSetGraphicsStateRecord> && IsTriviallyCopyable_V<GpuCommandIrSetGraphicsStateRecord>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuCommandIrRasterVertexBinding{
    GpuGraphResourceId resource;
    u32 slot = 0u;
    u64 offset = 0u;
};

struct GpuCommandIrRasterVertexOwner{
    GpuGraphResourceId resource;
    BufferHandle buffer;
    u32 slot = 0u;
    u64 offset = 0u;
};

struct GpuCommandIrRasterStateDesc{
    GpuGraphPipelineId pipeline;
    GraphicsPipelineHandle pipelineOwner;
    GpuGraphResourceId colorAttachment;
    FramebufferHandle framebufferOwner;
    Viewport viewport;
    Rect scissor;
    Color blendConstantColor;
    FixedVector<GpuCommandIrRasterVertexOwner, s_MaxVertexAttributes> vertexBuffers;
    GpuGraphResourceId indexResource;
    BufferHandle indexBuffer;
    u32 indexOffset = 0u;
    Format::Enum indexFormat = Format::UNKNOWN;
    u8 dynamicStencilRefValue = 0u;
    bool hasScissor = false;
};

struct GpuCommandIrRasterTaskRecord{
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
    GpuGraphPipelineId pipeline;
    GpuGraphResourceId colorAttachment;
    GpuGraphResourceId indexResource;
    Viewport viewport;
    Rect scissor;
    Color blendConstantColor;
    FixedVector<GpuCommandIrRasterVertexBinding, s_MaxVertexAttributes> vertexBuffers;
    DrawArguments drawArguments;
    u64 blobOffsetBytes = 0u;
    u64 blobSizeBytes = 0u;
    u32 framebufferOwnerIndex = Limit<u32>::s_Max;
    u32 heapOwnerIndex = Limit<u32>::s_Max;
    u32 indexOffset = 0u;
    Format::Enum indexFormat = Format::UNKNOWN;
    GpuCommandIrWireOpcode::Enum opcode = GpuCommandIrWireOpcode::SetGraphicsState;
    u8 dynamicStencilRefValue = 0u;
    bool hasScissor = false;
};

struct GpuCommandIrDecodedRecord{
    GpuCommandIrWireOpcode::Enum opcode = GpuCommandIrWireOpcode::CopyBuffer;
    GpuCommandIrBuiltinTaskRecord builtin;
    GpuCommandIrRasterTaskRecord raster;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IGpuCommandIrRetainedOwner{
    virtual ~IGpuCommandIrRetainedOwner()noexcept = default;
    virtual void retain()noexcept = 0;
    virtual void release()noexcept = 0;
};

class GpuCommandIrOwnerAnchor final{
public:
    GpuCommandIrOwnerAnchor() = default;
    GpuCommandIrOwnerAnchor(IGpuCommandIrRetainedOwner* owner, AdoptRefT)noexcept;
    GpuCommandIrOwnerAnchor(const GpuCommandIrOwnerAnchor& other)noexcept;
    GpuCommandIrOwnerAnchor(GpuCommandIrOwnerAnchor&& other)noexcept;
    ~GpuCommandIrOwnerAnchor()noexcept;


public:
    GpuCommandIrOwnerAnchor& operator=(const GpuCommandIrOwnerAnchor& other)noexcept;
    GpuCommandIrOwnerAnchor& operator=(GpuCommandIrOwnerAnchor&& other)noexcept;
    [[nodiscard]] bool valid()const noexcept{ return m_owner != nullptr; }


private:
    IGpuCommandIrRetainedOwner* m_owner = nullptr;
};

template<typename T>
class GpuCommandIrRetainedOwnerBox final : public IGpuCommandIrRetainedOwner{
public:
    GpuCommandIrRetainedOwnerBox(GraphicsArena& arena, T owner)noexcept(IsNothrowMoveConstructible_V<T> && IsNothrowDestructible_V<T>)
        : m_arena(arena)
        , m_owner(Move(owner))
    {}
    virtual ~GpuCommandIrRetainedOwnerBox()noexcept override = default;


public:
    virtual void retain()noexcept override{ m_references.fetch_add(1u, MemoryOrder::relaxed); }
    virtual void release()noexcept override{
        if(m_references.fetch_sub(1u, MemoryOrder::release) == 1u){
            AtomicThreadFence(MemoryOrder::acquire);
            DestroyArenaObjectNoexcept(m_arena, this);
        }
    }


private:
    GraphicsArena& m_arena;
    T m_owner;
    Atomic<u32> m_references{ 1u };
};

// The arena backing an owner box must outlive every capture/exported stream that retains the returned anchor.
template<typename T>
[[nodiscard]] GpuCommandIrOwnerAnchor MakeGpuCommandIrOwnerAnchor(GraphicsArena& arena, T owner){
    return GpuCommandIrOwnerAnchor(NewArenaObject<GpuCommandIrRetainedOwnerBox<T>>(arena, arena, Move(owner)), s_AdoptRef);
}

struct GpuCommandIrRasterStateOwner{
    GraphicsPipelineHandle pipeline;
    FramebufferHandle framebuffer;
    FixedVector<BufferHandle, s_MaxVertexAttributes> vertexBuffers;
    BufferHandle indexBuffer;
    GpuGraphResourceId colorAttachment;
    usize recordIndex = 0u;
};

// Sidecar owners never enter the wire. A heap lease and descriptor owner must remain live until native replay records
// the exact heap binding; exported streams share these strong references with the capture rather than reacquiring them.
struct GpuCommandIrRasterHeapOwner{
    GraphicsPipelineHandle pipeline;
    GpuCommandIrOwnerAnchor lease;
    GpuCommandIrOwnerAnchor descriptors;
    GpuDescriptorHeap* heap = nullptr;
    usize recordIndex = 0u;
};

struct GpuCommandIrRasterOwnerTable{
    GraphicsVector<GpuCommandIrRasterStateOwner> stateOwners;
    GraphicsVector<GpuCommandIrRasterHeapOwner> heapOwners;

    explicit GpuCommandIrRasterOwnerTable(GraphicsArena& arena)
        : stateOwners(arena)
        , heapOwners(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

