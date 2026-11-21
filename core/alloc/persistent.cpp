// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "persistent.h"

#include <global/algorithm.h>
#include <global/bit.h>
#include <global/overflow.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_persistent{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_BlockAlignment = alignof(MaxAlign);
constexpr usize s_FreeBinCount = 64u;
constexpr usize s_SmallBinCount = 32u;
constexpr usize s_SmallBinMaxSpan = s_SmallBinCount * s_BlockAlignment;
constexpr usize s_LogBinBaseShift = static_cast<usize>(CountTrailingZeros(s_SmallBinMaxSpan));
constexpr usize s_MaxBinProbeCount = 16u;
constexpr usize s_FreeFlagBit = 1u;

static_assert((s_SmallBinMaxSpan & (s_SmallBinMaxSpan - 1u)) == 0u, "Small bin threshold must be a power of two");


struct alignas(MaxAlign) Block{
    usize spanAndFlags = 0u;
    usize requestedBytes = 0u;
    Block* previous = nullptr;
    Block* next = nullptr;


    [[nodiscard]] inline usize spanBytes()const noexcept{ return spanAndFlags & ~s_FreeFlagBit; }
    [[nodiscard]] inline b8 isFree()const noexcept{ return (spanAndFlags & s_FreeFlagBit) != 0u; }
    inline void setSpanBytes(const usize bytes)noexcept{ spanAndFlags = (bytes & ~s_FreeFlagBit) | (spanAndFlags & s_FreeFlagBit); }
    inline void setFree(const b8 freeState)noexcept{
        if(freeState)
            spanAndFlags |= s_FreeFlagBit;
        else
            spanAndFlags &= ~s_FreeFlagBit;
    }
};

struct FreeLinks{
    Block* previous = nullptr;
    Block* next = nullptr;
};

struct AllocationLayout{
    usize userOffset = 0u;
    usize spanBytes = 0u;
};

struct FreeBlockSelection{
    Block* block = nullptr;
    AllocationLayout layout;
    usize bin = 0u;
};


static_assert(alignof(Block) <= alignof(MaxAlign), "PersistentArena block alignment must fit CoreAlloc storage");
static_assert(sizeof(Block) == sizeof(usize) * 4u, "PersistentArena Block header must stay 4-word compact");
static_assert(sizeof(Block) % s_BlockAlignment == 0u, "PersistentArena block headers must preserve physical alignment");
static_assert((s_BlockAlignment & (s_BlockAlignment - 1u)) == 0u, "PersistentArena block alignment must be a power of two");
static_assert(alignof(FreeLinks) <= alignof(MaxAlign), "PersistentArena free links must fit native block storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr usize EffectiveAlignment(const usize align)noexcept{
    return align > s_BlockAlignment ? align : s_BlockAlignment;
}

[[nodiscard]] constexpr usize MinimumFreeSpan()noexcept{
    return AlignUp(sizeof(FreeLinks), s_BlockAlignment);
}

[[nodiscard]] inline NotNull<u8*> BlockData(Block& block)noexcept{
    return MakeNotNull(reinterpret_cast<u8*>(&block) + sizeof(Block));
}

[[nodiscard]] inline NotNull<const u8*> BlockData(const Block& block)noexcept{
    return MakeNotNull(reinterpret_cast<const u8*>(&block) + sizeof(Block));
}

[[nodiscard]] inline FreeLinks& Links(Block& block)noexcept{
    return *reinterpret_cast<FreeLinks*>(BlockData(block).get());
}

[[nodiscard]] inline Block* BlockFromAllocation(NotNull<void*> p)noexcept{
    Block* block = nullptr;
    FOUNDATION_MEMCPY(&block, sizeof(block), static_cast<u8*>(p.get()) - sizeof(block), sizeof(block));
    return block;
}

inline void StoreBlockForAllocation(NotNull<void*> p, Block& block)noexcept{
    Block* const blockPtr = &block;
    FOUNDATION_MEMCPY(static_cast<u8*>(p.get()) - sizeof(blockPtr), sizeof(blockPtr), &blockPtr, sizeof(blockPtr));
}

[[nodiscard]] inline b8 IsLiveBlock(const void* const bucket, const usize bucketSize, const Block* const block)noexcept{
    if(!bucket || !block || bucketSize < sizeof(Block))
        return false;

    const usize bucketBegin = reinterpret_cast<usize>(bucket);
    const usize blockBegin = reinterpret_cast<usize>(block);
    if(blockBegin < bucketBegin || blockBegin - bucketBegin > bucketSize - sizeof(Block))
        return false;

    return !block->isFree();
}

[[nodiscard]] inline Expected<AllocationLayout> BuildAllocationLayout(const Block& block, const usize alignment, const usize requestedBytes)noexcept{
    const usize dataAddress = reinterpret_cast<usize>(BlockData(block).get());
    if(AddOverflows<usize>(dataAddress, sizeof(Block*)))
        return MakeUnexpected(Failure{});

    usize userAddress = 0u;
    const auto userAddressResult = ::AlignUpPowerOfTwoChecked(dataAddress + sizeof(Block*), alignment);
    if(!userAddressResult)
        return MakeUnexpected(Failure{});
    userAddress = *userAddressResult;
    if(userAddress < dataAddress)
        return MakeUnexpected(Failure{});

    const usize userOffset = userAddress - dataAddress;
    if(AddOverflows<usize>(userOffset, requestedBytes))
        return MakeUnexpected(Failure{});

    usize spanBytes = 0u;
    const auto spanBytesResult = ::AlignUpPowerOfTwoChecked(userOffset + requestedBytes, s_BlockAlignment);
    if(!spanBytesResult)
        return MakeUnexpected(Failure{});
    spanBytes = *spanBytesResult;

    return AllocationLayout{ .userOffset = userOffset, .spanBytes = spanBytes, };
}

[[nodiscard]] inline Expected<AllocationLayout> BuildNormalAllocationLayout(const usize requestedBytes)noexcept{
    constexpr usize s_UserOffset = AlignUp(sizeof(Block*), s_BlockAlignment);
    if(AddOverflows<usize>(s_UserOffset, requestedBytes))
        return MakeUnexpected(Failure{});

    usize spanBytes = 0u;
    const auto spanBytesResult = ::AlignUpPowerOfTwoChecked(s_UserOffset + requestedBytes, s_BlockAlignment);
    if(!spanBytesResult)
        return MakeUnexpected(Failure{});
    spanBytes = *spanBytesResult;

    return AllocationLayout{ .userOffset = s_UserOffset, .spanBytes = spanBytes, };
}

[[nodiscard]] inline Expected<AllocationLayout> BuildExistingPointerLayout(const Block& block, const NotNull<void*> p, const usize requestedBytes)noexcept{
    const usize dataAddress = reinterpret_cast<usize>(BlockData(block).get());
    const usize pointerAddress = reinterpret_cast<usize>(p.get());
    if(pointerAddress < dataAddress || pointerAddress - dataAddress < sizeof(Block*))
        return MakeUnexpected(Failure{});

    const usize userOffset = pointerAddress - dataAddress;
    if(AddOverflows<usize>(userOffset, requestedBytes))
        return MakeUnexpected(Failure{});

    usize spanBytes = 0u;
    const auto spanBytesResult = ::AlignUpPowerOfTwoChecked(userOffset + requestedBytes, s_BlockAlignment);
    if(!spanBytesResult)
        return MakeUnexpected(Failure{});
    spanBytes = *spanBytesResult;

    return AllocationLayout{ .userOffset = userOffset, .spanBytes = spanBytes, };
}

[[nodiscard]] inline Block& CreateBlock(NotNull<void*> address, const usize spanBytes, Block* const previous, Block* const next, const b8 isFree)noexcept{
    return *new(address.get()) Block{ .spanAndFlags = (spanBytes & ~s_FreeFlagBit) | (isFree ? s_FreeFlagBit : 0u), .requestedBytes = 0u, .previous = previous, .next = next };
}

[[nodiscard]] inline usize BinForSpan(const usize spanBytes)noexcept{
    if(spanBytes <= s_BlockAlignment)
        return 0u;
    if(spanBytes <= s_SmallBinMaxSpan)
        return (spanBytes / s_BlockAlignment) - 1u;
    const usize bit = static_cast<usize>(std::bit_width(spanBytes) - 1);
    const usize exponent = bit - s_LogBinBaseShift;
    const usize bin = s_SmallBinCount + exponent;
    return bin < s_FreeBinCount ? bin : (s_FreeBinCount - 1u);
}

inline void InsertFreeBlock(Array<void*, s_FreeBinCount>& bins, u64& binMask, Block& block)noexcept{
    FOUNDATION_ASSERT(block.isFree());

    const usize bin = BinForSpan(block.spanBytes());
    Block* const next = static_cast<Block*>(bins[bin]);
    new(BlockData(block).get()) FreeLinks{ .previous = nullptr, .next = next, };
    if(next)
        Links(*next).previous = &block;
    bins[bin] = &block;
    binMask |= (1ull << bin);
}

inline void RemoveFreeBlockFromBin(Array<void*, s_FreeBinCount>& bins, u64& binMask, Block& block, const usize bin)noexcept{
    FOUNDATION_ASSERT(block.isFree());

    const FreeLinks links = Links(block);
    if(links.previous)
        Links(*links.previous).next = links.next;
    else
        bins[bin] = links.next;
    if(links.next)
        Links(*links.next).previous = links.previous;
    if(!bins[bin])
        binMask &= ~(1ull << bin);
    block.setFree(false);
}

inline void RemoveFreeBlock(Array<void*, s_FreeBinCount>& bins, u64& binMask, Block& block)noexcept{
    RemoveFreeBlockFromBin(bins, binMask, block, BinForSpan(block.spanBytes()));
}

inline void MergeNextBlock(Block& block, Block& next)noexcept{
    FOUNDATION_ASSERT(block.next == &next);

    block.setSpanBytes(block.spanBytes() + sizeof(Block) + next.spanBytes());
    block.next = next.next;
    if(block.next)
        block.next->previous = &block;
}

inline void SplitUsedBlock(Array<void*, s_FreeBinCount>& bins, u64& binMask, Block& block, const usize requestedSpan)noexcept{
    FOUNDATION_ASSERT(!block.isFree());
    FOUNDATION_ASSERT(requestedSpan <= block.spanBytes());

    const usize remainder = block.spanBytes() - requestedSpan;
    if(remainder < sizeof(Block) + MinimumFreeSpan())
        return;

    Block* const oldNext = block.next;
    Block& tail = CreateBlock(MakeNotNull(static_cast<void*>(BlockData(block).get() + requestedSpan)), remainder - sizeof(Block), &block, oldNext, true);
    block.setSpanBytes(requestedSpan);
    block.next = &tail;
    if(oldNext)
        oldNext->previous = &tail;

    if(oldNext && oldNext->isFree()){
        RemoveFreeBlock(bins, binMask, *oldNext);
        MergeNextBlock(tail, *oldNext);
    }
    InsertFreeBlock(bins, binMask, tail);
}

[[nodiscard]] inline Expected<FreeBlockSelection> FindFreeBlock(const Array<void*, s_FreeBinCount>& bins, const u64 binMask, const usize alignment, const usize requestedBytes)noexcept{
    if(binMask == 0ull)
        return MakeUnexpected(Failure{});

    FreeBlockSelection selection;
    if(alignment == s_BlockAlignment){
        const auto normalLayout = BuildNormalAllocationLayout(requestedBytes);
        if(!normalLayout)
            return MakeUnexpected(Failure{});

        const usize b0 = BinForSpan(normalLayout->spanBytes);
        const u64 higherMask = b0 < (s_FreeBinCount - 1u) ? (binMask & (~0ull << (b0 + 1u))) : 0ull;
        if((binMask & (1ull << b0)) != 0ull){
            if(b0 < s_SmallBinCount){
                selection.block = static_cast<Block*>(bins[b0]);
                selection.layout = *normalLayout;
                selection.bin = b0;
                return selection;
            }
            usize selectedSlackBytes = 0u;
            usize probed = 0u;
            for(Block* block = static_cast<Block*>(bins[b0]); block; block = Links(*block).next){
                if(probed >= s_MaxBinProbeCount && (selection.block != nullptr || higherMask != 0ull))
                    break;
                ++probed;
                const usize blockSpan = block->spanBytes();
                if(normalLayout->spanBytes > blockSpan)
                    continue;
                const usize slackBytes = blockSpan - normalLayout->spanBytes;
                if(!selection.block || slackBytes < selectedSlackBytes){
                    selection.block = block;
                    selection.layout = *normalLayout;
                    selection.bin = b0;
                    selectedSlackBytes = slackBytes;
                    if(slackBytes == 0u)
                        break;
                }
            }
            if(selection.block)
                return selection;
        }

        if(higherMask == 0ull)
            return MakeUnexpected(Failure{});

        const usize b1 = static_cast<usize>(CountTrailingZeros(higherMask));
        selection.block = static_cast<Block*>(bins[b1]);
        selection.layout = *normalLayout;
        selection.bin = b1;
        return selection;
    }

    if(AddOverflows<usize>(sizeof(Block*), requestedBytes))
        return MakeUnexpected(Failure{});
    const auto minSpan = ::AlignUpPowerOfTwoChecked(sizeof(Block*) + requestedBytes, s_BlockAlignment);
    if(!minSpan)
        return MakeUnexpected(Failure{});

    const usize minBin = BinForSpan(*minSpan);
    u64 candidateMask = binMask & (~0ull << minBin);
    while(candidateMask != 0ull){
        const usize bin = static_cast<usize>(CountTrailingZeros(candidateMask));
        candidateMask &= candidateMask - 1ull;

        usize selectedSlackBytes = 0u;
        usize probed = 0u;
        for(Block* block = static_cast<Block*>(bins[bin]); block; block = Links(*block).next){
            if(selection.block && probed >= s_MaxBinProbeCount)
                break;
            ++probed;
            const auto candidateLayout = BuildAllocationLayout(*block, alignment, requestedBytes);
            if(!candidateLayout)
                continue;
            const AllocationLayout& layout = *candidateLayout;
            const usize blockSpan = block->spanBytes();
            if(layout.spanBytes > blockSpan)
                continue;
            const usize slackBytes = blockSpan - layout.spanBytes;
            if(!selection.block || slackBytes < selectedSlackBytes){
                selection.block = block;
                selection.layout = layout;
                selection.bin = bin;
                selectedSlackBytes = slackBytes;
                if(slackBytes == 0u)
                    break;
            }
        }
        if(selection.block)
            return selection;
    }
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize PersistentArena::StructureAlignedSize(const usize byte, const usize align){
    if(!IsSupportedAlignmentValue(align)){
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("PersistentArena alignment must be zero, one, or a power of two"));
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
    FOUNDATION_FATAL_ASSERT(m_bucket || m_maxSize == 0u);

    m_memoryStats.reset(static_cast<u64>(m_maxSize));
    if(!m_bucket)
        return;

    FOUNDATION_FATAL_ASSERT(reinterpret_cast<usize>(m_bucket) % __hidden_persistent::s_BlockAlignment == 0u);
    const usize managedBytes = m_maxSize - (m_maxSize % __hidden_persistent::s_BlockAlignment);
    if(managedBytes < sizeof(__hidden_persistent::Block) + __hidden_persistent::MinimumFreeSpan())
        return;

    auto& first = __hidden_persistent::CreateBlock(MakeNotNull(m_bucket), managedBytes - sizeof(__hidden_persistent::Block), nullptr, nullptr, true);
    __hidden_persistent::InsertFreeBlock(m_freeBins, m_nonEmptyBinMask, first);
}

PersistentArena::~PersistentArena()noexcept{
    CoreFree(m_bucket);
    m_bucket = nullptr;
    m_freeBins.fill(nullptr);
    m_nonEmptyBinMask = 0u;
    m_memoryStats.releaseRetainedMemory();
}


void* PersistentArena::allocate(const usize align, usize size){
    size = Alignment(align, size);
    if(size == 0u)
        return nullptr;
    if(!IsSupportedAlignmentValue(align)){
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("PersistentArena alignment must be zero, one, or a power of two"));
        return nullptr;
    }

    NothrowScopedLock lock(m_mutex);

    const auto allocation = allocateLocked(align, size);
    if(!allocation)
        return nullptr;
    const auto& allocatedBlock = *static_cast<__hidden_persistent::Block*>(allocation->block.get());
    m_memoryStats.recordAllocationSingleThread(static_cast<u64>(allocatedBlock.spanBytes()));
    return allocation->pointer.get();
}

void* PersistentArena::reallocate(void* const p, const usize align, usize size){
    size = Alignment(align, size);
    if(!p)
        return size != 0u ? allocate(align, size) : nullptr;

    NothrowScopedLock lock(m_mutex);

    const NotNull<void*> nonNullPtr = MakeNotNull(p);
    auto* const blockPtr = __hidden_persistent::BlockFromAllocation(nonNullPtr);
    const b8 liveBlock = __hidden_persistent::IsLiveBlock(m_bucket, m_maxSize, blockPtr);
    FOUNDATION_ASSERT_MSG(liveBlock, FOUNDATION_TEXT("PersistentArena reallocation must reference a live block from this arena"));
    if(!liveBlock)
        return nullptr;

    const NotNull<__hidden_persistent::Block*> block = MakeNotNull(blockPtr);
    const u64 oldSpanBytes = static_cast<u64>(block->spanBytes());
    if(size == 0u){
        deallocateBlockLocked(MakeNotNull(static_cast<void*>(block.get())));
        m_memoryStats.recordReallocationSingleThread(oldSpanBytes, 0u);
        return nullptr;
    }
    if(!IsSupportedAlignmentValue(align)){
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("PersistentArena alignment must be zero, one, or a power of two"));
        return nullptr;
    }

    const usize alignment = __hidden_persistent::EffectiveAlignment(align);
    const usize pointerAddress = reinterpret_cast<usize>(nonNullPtr.get());
    const auto layout = (pointerAddress % alignment == 0u) ? __hidden_persistent::BuildExistingPointerLayout(*block, nonNullPtr, size) : Expected<__hidden_persistent::AllocationLayout>(MakeUnexpected(Failure{}));
    if(layout && layout->spanBytes <= block->spanBytes()){
        __hidden_persistent::SplitUsedBlock(m_freeBins, m_nonEmptyBinMask, *block, layout->spanBytes);
        block->requestedBytes = size;
        m_memoryStats.recordReallocationSingleThread(oldSpanBytes, static_cast<u64>(block->spanBytes()));
        return p;
    }

    if(layout && layout->spanBytes > block->spanBytes()){
        usize combinedSpanBytes = block->spanBytes();
        for(__hidden_persistent::Block* next = block->next; next && next->isFree() && combinedSpanBytes < layout->spanBytes; next = next->next){
            const usize nextTotalBytes = sizeof(__hidden_persistent::Block) + next->spanBytes();
            if(AddOverflows<usize>(combinedSpanBytes, nextTotalBytes))
                break;
            combinedSpanBytes += nextTotalBytes;
        }
        if(layout->spanBytes <= combinedSpanBytes){
            while(layout->spanBytes > block->spanBytes()){
                FOUNDATION_ASSERT(block->next && block->next->isFree());
                __hidden_persistent::Block* const next = block->next;
                __hidden_persistent::RemoveFreeBlock(m_freeBins, m_nonEmptyBinMask, *next);
                __hidden_persistent::MergeNextBlock(*block, *next);
            }
            __hidden_persistent::SplitUsedBlock(m_freeBins, m_nonEmptyBinMask, *block, layout->spanBytes);
            block->requestedBytes = size;
            m_memoryStats.recordReallocationSingleThread(oldSpanBytes, static_cast<u64>(block->spanBytes()));
            return p;
        }
    }

    const usize copyBytes = Min(block->requestedBytes, size);
    const auto replacement = allocateLocked(align, size);
    if(!replacement)
        return nullptr;

    auto& replacementBlock = *static_cast<__hidden_persistent::Block*>(replacement->block.get());
    FOUNDATION_MEMCPY(replacement->pointer.get(), replacementBlock.requestedBytes, nonNullPtr.get(), copyBytes);
    deallocateBlockLocked(MakeNotNull(static_cast<void*>(block.get())));
    m_memoryStats.recordReallocationSingleThread(oldSpanBytes, static_cast<u64>(replacementBlock.spanBytes()));
    return replacement->pointer.get();
}

void PersistentArena::deallocate(void* const p, [[maybe_unused]] const usize align, [[maybe_unused]] const usize size)noexcept{
    if(!p)
        return;

    NothrowScopedLock lock(m_mutex);

    auto* const blockPtr = __hidden_persistent::BlockFromAllocation(MakeNotNull(p));
    const b8 liveBlock = __hidden_persistent::IsLiveBlock(m_bucket, m_maxSize, blockPtr);
    FOUNDATION_ASSERT_MSG(liveBlock, FOUNDATION_TEXT("PersistentArena deallocation must reference a live block from this arena"));
    if(!liveBlock)
        return;

    const NotNull<__hidden_persistent::Block*> block = MakeNotNull(blockPtr);
    m_memoryStats.recordDeallocationSingleThread(static_cast<u64>(block->spanBytes()));
    deallocateBlockLocked(MakeNotNull(static_cast<void*>(block.get())));
}


Expected<PersistentArena::Allocation> PersistentArena::allocateLocked(const usize align, const usize size)noexcept{
    const usize alignment = __hidden_persistent::EffectiveAlignment(align);
    const auto selection = __hidden_persistent::FindFreeBlock(m_freeBins, m_nonEmptyBinMask, alignment, size);
    if(!selection)
        return MakeUnexpected(Failure{});
    auto& selected = *selection->block;
    const auto& selectedLayout = selection->layout;

    __hidden_persistent::RemoveFreeBlockFromBin(m_freeBins, m_nonEmptyBinMask, selected, selection->bin);
    __hidden_persistent::SplitUsedBlock(m_freeBins, m_nonEmptyBinMask, selected, selectedLayout.spanBytes);
    selected.requestedBytes = size;
    const auto p = MakeNotNull(__hidden_persistent::BlockData(selected).get() + selectedLayout.userOffset);
    __hidden_persistent::StoreBlockForAllocation(MakeNotNull(static_cast<void*>(p.get())), selected);
    return Allocation{ .pointer = MakeNotNull(static_cast<void*>(p.get())), .block = MakeNotNull(static_cast<void*>(&selected)) };
}

void PersistentArena::deallocateBlockLocked(NotNull<void*> blockPointer)noexcept{
    auto& block = *static_cast<__hidden_persistent::Block*>(blockPointer.get());
    block.requestedBytes = 0u;

    __hidden_persistent::Block* merged = &block;
    if(merged->previous && merged->previous->isFree()){
        __hidden_persistent::Block* const prev = merged->previous;
        __hidden_persistent::RemoveFreeBlock(m_freeBins, m_nonEmptyBinMask, *prev);
        __hidden_persistent::MergeNextBlock(*prev, *merged);
        merged = prev;
    }
    if(merged->next && merged->next->isFree()){
        __hidden_persistent::Block* const next = merged->next;
        __hidden_persistent::RemoveFreeBlock(m_freeBins, m_nonEmptyBinMask, *next);
        __hidden_persistent::MergeNextBlock(*merged, *next);
    }
    merged->setFree(true);
    __hidden_persistent::InsertFreeBlock(m_freeBins, m_nonEmptyBinMask, *merged);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

