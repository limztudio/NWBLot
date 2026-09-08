// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "persistent.h"

#include <global/algorithm.h>
#include <global/overflow.h>

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_persistent{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_BlockAlignment = alignof(MaxAlign);


struct alignas(MaxAlign) Block{
    usize spanBytes = 0u;
    usize requestedBytes = 0u;
    Block* previous = nullptr;
    Block* next = nullptr;
    bool isFree = false;
};

struct FreeLinks{
    Block* previous = nullptr;
    Block* next = nullptr;
};

struct AllocationLayout{
    usize userOffset = 0u;
    usize spanBytes = 0u;
};


static_assert(alignof(Block) <= alignof(MaxAlign), "PersistentArena block alignment must fit CoreAlloc storage");
static_assert(sizeof(Block) % s_BlockAlignment == 0u, "PersistentArena block headers must preserve physical alignment");
static_assert(alignof(FreeLinks) <= alignof(MaxAlign), "PersistentArena free links must fit native block storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsSupportedAlignment(const usize align){
    return align <= 1u || (align & (align - 1u)) == 0u;
}

[[nodiscard]] constexpr usize EffectiveAlignment(const usize align){
    return align > s_BlockAlignment ? align : s_BlockAlignment;
}

[[nodiscard]] constexpr usize MinimumFreeSpan(){
    return AlignUp(sizeof(FreeLinks), s_BlockAlignment);
}

[[nodiscard]] inline u8* BlockData(Block& block){
    return reinterpret_cast<u8*>(&block) + sizeof(Block);
}

[[nodiscard]] inline const u8* BlockData(const Block& block){
    return reinterpret_cast<const u8*>(&block) + sizeof(Block);
}

[[nodiscard]] inline FreeLinks& Links(Block& block){
    return *reinterpret_cast<FreeLinks*>(BlockData(block));
}

[[nodiscard]] inline Block* BlockFromAllocation(void* const p)noexcept{
    Block* block = nullptr;
    NWB_MEMCPY(&block, sizeof(block), static_cast<u8*>(p) - sizeof(block), sizeof(block));
    return block;
}

inline void StoreBlockForAllocation(void* const p, Block* const block)noexcept{
    NWB_MEMCPY(static_cast<u8*>(p) - sizeof(block), sizeof(block), &block, sizeof(block));
}

[[nodiscard]] inline bool IsLiveBlock(const void* const bucket, const usize bucketSize, const Block* const block)noexcept{
    if(!bucket || !block || bucketSize < sizeof(Block))
        return false;

    const usize bucketBegin = reinterpret_cast<usize>(bucket);
    const usize blockBegin = reinterpret_cast<usize>(block);
    if(blockBegin < bucketBegin || blockBegin - bucketBegin > bucketSize - sizeof(Block))
        return false;

    return !block->isFree;
}

[[nodiscard]] inline bool BuildAllocationLayout(
    const Block& block,
    const usize alignment,
    const usize requestedBytes,
    AllocationLayout& outLayout
)noexcept{
    const usize dataAddress = reinterpret_cast<usize>(BlockData(block));
    if(AddOverflows<usize>(dataAddress, sizeof(Block*)))
        return false;

    usize userAddress = 0u;
    if(!AlignUpChecked(dataAddress + sizeof(Block*), alignment, userAddress))
        return false;
    if(userAddress < dataAddress)
        return false;

    const usize userOffset = userAddress - dataAddress;
    if(AddOverflows<usize>(userOffset, requestedBytes))
        return false;

    usize spanBytes = 0u;
    if(!AlignUpChecked(userOffset + requestedBytes, s_BlockAlignment, spanBytes))
        return false;

    outLayout = AllocationLayout{
        .userOffset = userOffset,
        .spanBytes = spanBytes,
    };
    return true;
}

[[nodiscard]] inline bool BuildExistingPointerLayout(
    const Block& block,
    void* const p,
    const usize requestedBytes,
    AllocationLayout& outLayout
)noexcept{
    const usize dataAddress = reinterpret_cast<usize>(BlockData(block));
    const usize pointerAddress = reinterpret_cast<usize>(p);
    if(pointerAddress < dataAddress || pointerAddress - dataAddress < sizeof(Block*))
        return false;

    const usize userOffset = pointerAddress - dataAddress;
    if(AddOverflows<usize>(userOffset, requestedBytes))
        return false;

    usize spanBytes = 0u;
    if(!AlignUpChecked(userOffset + requestedBytes, s_BlockAlignment, spanBytes))
        return false;

    outLayout = AllocationLayout{
        .userOffset = userOffset,
        .spanBytes = spanBytes,
    };
    return true;
}

[[nodiscard]] inline Block* CreateBlock(
    void* const address,
    const usize spanBytes,
    Block* const previous,
    Block* const next,
    const bool isFree
)noexcept{
    return new(address) Block{
        .spanBytes = spanBytes,
        .requestedBytes = 0u,
        .previous = previous,
        .next = next,
        .isFree = isFree,
    };
}

inline void InitializeLinks(Block& block, Block* const previous, Block* const next)noexcept{
    new(BlockData(block)) FreeLinks{
        .previous = previous,
        .next = next,
    };
}

inline void InsertFreeBlock(void*& freeHead, Block& block)noexcept{
    NWB_ASSERT(block.isFree);

    Block* const next = static_cast<Block*>(freeHead);
    InitializeLinks(block, nullptr, next);
    if(next)
        Links(*next).previous = &block;
    freeHead = &block;
}

inline void RemoveFreeBlock(void*& freeHead, Block& block)noexcept{
    NWB_ASSERT(block.isFree);

    const FreeLinks links = Links(block);
    if(links.previous)
        Links(*links.previous).next = links.next;
    else
        freeHead = links.next;
    if(links.next)
        Links(*links.next).previous = links.previous;
    block.isFree = false;
}

inline void MergeNextBlock(Block& block, Block& next)noexcept{
    NWB_ASSERT(block.next == &next);

    block.spanBytes += sizeof(Block) + next.spanBytes;
    block.next = next.next;
    if(block.next)
        block.next->previous = &block;
}

inline void SplitUsedBlock(void*& freeHead, Block& block, const usize requestedSpan)noexcept{
    NWB_ASSERT(!block.isFree);
    NWB_ASSERT(requestedSpan <= block.spanBytes);

    const usize remainder = block.spanBytes - requestedSpan;
    if(remainder < sizeof(Block) + MinimumFreeSpan())
        return;

    Block* const oldNext = block.next;
    Block* const tail = CreateBlock(
        BlockData(block) + requestedSpan,
        remainder - sizeof(Block),
        &block,
        oldNext,
        true
    );
    block.spanBytes = requestedSpan;
    block.next = tail;
    if(oldNext)
        oldNext->previous = tail;

    if(oldNext && oldNext->isFree){
        RemoveFreeBlock(freeHead, *oldNext);
        MergeNextBlock(*tail, *oldNext);
    }
    InsertFreeBlock(freeHead, *tail);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize PersistentArena::StructureAlignedSize(const usize byte){
    return StructureAlignedSize(byte, alignof(MaxAlign));
}

usize PersistentArena::StructureAlignedSize(const usize byte, const usize align){
    if(!__hidden_persistent::IsSupportedAlignment(align)){
        NWB_ASSERT_MSG(false, NWB_TEXT("PersistentArena alignment must be zero, one, or a power of two"));
        return 0u;
    }

    const usize alignment = __hidden_persistent::EffectiveAlignment(align);
    const usize requestedBytes = Alignment(align, byte);
    const usize prefixBytes = AddSize(sizeof(__hidden_persistent::Block*), alignment - 1u);
    const usize nativeSpan = Alignment(__hidden_persistent::s_BlockAlignment, AddSize(prefixBytes, requestedBytes));
    return AddSize(sizeof(__hidden_persistent::Block), nativeSpan);
}


PersistentArena::PersistentArena(const Name& allocationLog, const usize maxSize)
    : Base(allocationLog)
    , m_maxSize(maxSize)
{
    if(m_maxSize != 0u)
        m_bucket = CoreAlloc(m_maxSize);
    NWB_FATAL_ASSERT(m_bucket || m_maxSize == 0u);

    m_memoryStats.reset(static_cast<u64>(m_maxSize));
    if(!m_bucket)
        return;

    NWB_FATAL_ASSERT(reinterpret_cast<usize>(m_bucket) % __hidden_persistent::s_BlockAlignment == 0u);
    const usize managedBytes = m_maxSize - (m_maxSize % __hidden_persistent::s_BlockAlignment);
    if(managedBytes < sizeof(__hidden_persistent::Block) + __hidden_persistent::MinimumFreeSpan())
        return;

    auto* const first = __hidden_persistent::CreateBlock(
        m_bucket,
        managedBytes - sizeof(__hidden_persistent::Block),
        nullptr,
        nullptr,
        true
    );
    __hidden_persistent::InsertFreeBlock(m_freeHead, *first);
}

PersistentArena::~PersistentArena(){
    CoreFree(m_bucket);
    m_bucket = nullptr;
    m_freeHead = nullptr;
    m_memoryStats.releaseRetainedMemory();
}


void* PersistentArena::allocate(const usize align, usize size){
    size = Alignment(align, size);
    if(size == 0u)
        return nullptr;
    if(!__hidden_persistent::IsSupportedAlignment(align)){
        NWB_ASSERT_MSG(false, NWB_TEXT("PersistentArena alignment must be zero, one, or a power of two"));
        return nullptr;
    }

    NothrowScopedLock lock(m_mutex);

    void* const p = allocateLocked(align, size);
    if(p){
        const auto* const block = __hidden_persistent::BlockFromAllocation(p);
        m_memoryStats.recordAllocation(static_cast<u64>(block->spanBytes));
    }
    return p;
}

void* PersistentArena::reallocate(void* const p, const usize align, usize size){
    size = Alignment(align, size);
    if(!p)
        return size != 0u ? allocate(align, size) : nullptr;

    NothrowScopedLock lock(m_mutex);

    auto* const block = __hidden_persistent::BlockFromAllocation(p);
    NWB_ASSERT_MSG(
        __hidden_persistent::IsLiveBlock(m_bucket, m_maxSize, block),
        NWB_TEXT("PersistentArena reallocation must reference a live block from this arena")
    );
    if(!__hidden_persistent::IsLiveBlock(m_bucket, m_maxSize, block))
        return nullptr;

    const u64 oldSpanBytes = static_cast<u64>(block->spanBytes);
    if(size == 0u){
        deallocateLocked(p);
        m_memoryStats.recordReallocation(oldSpanBytes, 0u);
        return nullptr;
    }
    if(!__hidden_persistent::IsSupportedAlignment(align)){
        NWB_ASSERT_MSG(false, NWB_TEXT("PersistentArena alignment must be zero, one, or a power of two"));
        return nullptr;
    }

    const usize alignment = __hidden_persistent::EffectiveAlignment(align);
    __hidden_persistent::AllocationLayout layout;
    const usize pointerAddress = reinterpret_cast<usize>(p);
    const bool keepsPointer = pointerAddress % alignment == 0u
        && __hidden_persistent::BuildExistingPointerLayout(*block, p, size, layout)
    ;
    if(keepsPointer && layout.spanBytes <= block->spanBytes){
        __hidden_persistent::SplitUsedBlock(m_freeHead, *block, layout.spanBytes);
        block->requestedBytes = size;
        __hidden_persistent::StoreBlockForAllocation(p, block);
        m_memoryStats.recordReallocation(oldSpanBytes, static_cast<u64>(block->spanBytes));
        return p;
    }

    __hidden_persistent::Block* const next = block->next;
    if(keepsPointer && next && next->isFree){
        const usize combinedSpanBytes = block->spanBytes + sizeof(__hidden_persistent::Block) + next->spanBytes;
        if(layout.spanBytes <= combinedSpanBytes){
            __hidden_persistent::RemoveFreeBlock(m_freeHead, *next);
            __hidden_persistent::MergeNextBlock(*block, *next);
            __hidden_persistent::SplitUsedBlock(m_freeHead, *block, layout.spanBytes);
            block->requestedBytes = size;
            __hidden_persistent::StoreBlockForAllocation(p, block);
            m_memoryStats.recordReallocation(oldSpanBytes, static_cast<u64>(block->spanBytes));
            return p;
        }
    }

    const usize copyBytes = Min(block->requestedBytes, size);
    void* const replacement = allocateLocked(align, size);
    if(!replacement)
        return nullptr;

    auto* const replacementBlock = __hidden_persistent::BlockFromAllocation(replacement);
    NWB_MEMCPY(replacement, replacementBlock->requestedBytes, p, copyBytes);
    deallocateLocked(p);
    m_memoryStats.recordReallocation(oldSpanBytes, static_cast<u64>(replacementBlock->spanBytes));
    return replacement;
}

void PersistentArena::deallocate(void* const p, const usize align, const usize size){
    static_cast<void>(align);
    static_cast<void>(size);
    if(!p)
        return;

    NothrowScopedLock lock(m_mutex);

    auto* const block = __hidden_persistent::BlockFromAllocation(p);
    NWB_ASSERT_MSG(
        __hidden_persistent::IsLiveBlock(m_bucket, m_maxSize, block),
        NWB_TEXT("PersistentArena deallocation must reference a live block from this arena")
    );
    if(!__hidden_persistent::IsLiveBlock(m_bucket, m_maxSize, block))
        return;

    m_memoryStats.recordDeallocation(static_cast<u64>(block->spanBytes));
    deallocateLocked(p);
}


void* PersistentArena::allocateLocked(const usize align, const usize size)noexcept{
    const usize alignment = __hidden_persistent::EffectiveAlignment(align);
    __hidden_persistent::Block* selected = nullptr;
    __hidden_persistent::AllocationLayout selectedLayout;
    usize selectedSlackBytes = 0u;

    for(
        auto* block = static_cast<__hidden_persistent::Block*>(m_freeHead);
        block;
        block = __hidden_persistent::Links(*block).next
    ){
        __hidden_persistent::AllocationLayout layout;
        if(!__hidden_persistent::BuildAllocationLayout(*block, alignment, size, layout) || layout.spanBytes > block->spanBytes)
            continue;

        const usize slackBytes = block->spanBytes - layout.spanBytes;
        if(!selected || slackBytes < selectedSlackBytes){
            selected = block;
            selectedLayout = layout;
            selectedSlackBytes = slackBytes;
            if(slackBytes == 0u)
                break;
        }
    }
    if(!selected)
        return nullptr;

    __hidden_persistent::RemoveFreeBlock(m_freeHead, *selected);
    __hidden_persistent::SplitUsedBlock(m_freeHead, *selected, selectedLayout.spanBytes);
    selected->requestedBytes = size;
    void* const p = __hidden_persistent::BlockData(*selected) + selectedLayout.userOffset;
    __hidden_persistent::StoreBlockForAllocation(p, selected);
    return p;
}

void PersistentArena::deallocateLocked(void* const p)noexcept{
    auto* block = __hidden_persistent::BlockFromAllocation(p);
    block->requestedBytes = 0u;
    block->isFree = true;

    if(block->previous && block->previous->isFree){
        __hidden_persistent::Block* const previous = block->previous;
        __hidden_persistent::RemoveFreeBlock(m_freeHead, *previous);
        __hidden_persistent::MergeNextBlock(*previous, *block);
        block = previous;
    }
    if(block->next && block->next->isFree){
        __hidden_persistent::Block* const next = block->next;
        __hidden_persistent::RemoveFreeBlock(m_freeHead, *next);
        __hidden_persistent::MergeNextBlock(*block, *next);
    }

    block->isFree = true;
    __hidden_persistent::InsertFreeBlock(m_freeHead, *block);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

