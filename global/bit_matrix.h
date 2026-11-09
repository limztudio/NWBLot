// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "assert.h"
#include "limit.h"
#include "simplemath.h"
#include "span.h"
#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void TransposeBitMatrix64(Array<u64, s_BitsPerU64>& tile)noexcept{
    u64 mask = 0x00000000FFFFFFFFull;
    for(usize shift = s_BitsPerU64 / 2u; shift != 0u; shift /= 2u){
        // Exchange each row-address bit with the corresponding column-address bit, starting with the high bit.
        for(usize block = 0u; block < s_BitsPerU64; block += shift * 2u){
            for(usize offset = 0u; offset < shift; ++offset){
                u64& low = tile[block + offset];
                u64& high = tile[block + offset + shift];
                const u64 exchanged = ((low >> shift) ^ high) & mask;
                low ^= exchanged << shift;
                high ^= exchanged;
            }
        }
        if(shift > 1u)
            mask ^= mask << (shift / 2u);
    }
}


// Dense and compact storage must contain every transposed destination bit.
inline void SymmetrizeDenseBitMatrix(const Span<u64> words, const usize dimension, const usize wordsPerRow)noexcept{
    for(usize rowBlock = 0u; rowBlock < wordsPerRow; ++rowBlock){
        const usize rowBase = rowBlock * s_BitsPerU64;
        const usize rowCount = Min(s_BitsPerU64, dimension - rowBase);
        for(usize columnBlock = rowBlock; columnBlock < wordsPerRow; ++columnBlock){
            const usize columnBase = columnBlock * s_BitsPerU64;
            const usize columnCount = Min(s_BitsPerU64, dimension - columnBase);
            Array<u64, s_BitsPerU64> forward = {};
            u64 forwardBits = 0u;
            for(usize row = 0u; row < rowCount; ++row){
                forward[row] = words[(rowBase + row) * wordsPerRow + columnBlock];
                forwardBits |= forward[row];
            }
            if(rowBlock == columnBlock){
                if(forwardBits != 0u){
                    TransposeBitMatrix64(forward);
                    for(usize row = 0u; row < rowCount; ++row)
                        words[(rowBase + row) * wordsPerRow + columnBlock] |= forward[row];
                }
                continue;
            }
            Array<u64, s_BitsPerU64> backward = {};
            u64 backwardBits = 0u;
            for(usize row = 0u; row < columnCount; ++row){
                backward[row] = words[(columnBase + row) * wordsPerRow + rowBlock];
                backwardBits |= backward[row];
            }
            if(forwardBits != 0u){
                TransposeBitMatrix64(forward);
                for(usize row = 0u; row < columnCount; ++row)
                    words[(columnBase + row) * wordsPerRow + rowBlock] |= forward[row];
            }
            if(backwardBits != 0u){
                TransposeBitMatrix64(backward);
                for(usize row = 0u; row < rowCount; ++row)
                    words[(rowBase + row) * wordsPerRow + columnBlock] |= backward[row];
            }
        }
    }
}


template<typename RowRanges>
inline void SymmetrizeCompactBitMatrix(const Span<u64> words, const Span<const usize> offsets, const RowRanges& ranges, const usize dimension, const usize wordsPerRow)noexcept(
    []()constexpr noexcept{
        using Range = const RemoveReference_T<decltype(DeclVal<const RowRanges&>()[DeclVal<const usize&>()])>&;
        return
            noexcept(DeclVal<const RowRanges&>()[DeclVal<const usize&>()])
            && noexcept(static_cast<b8>(DeclVal<const usize&>() < DeclVal<Range>().begin || DeclVal<const usize&>() >= DeclVal<Range>().end))
            && noexcept(DeclVal<const Span<u64>&>()[DeclVal<const Span<const usize>&>()[DeclVal<const usize&>()] + DeclVal<const usize&>() - DeclVal<Range>().begin])
            && noexcept(static_cast<b8>(DeclVal<const usize&>() >= DeclVal<Range>().begin && DeclVal<const usize&>() < DeclVal<Range>().end))
            && noexcept(DeclVal<usize&>() = Max(DeclVal<usize&>(), DeclVal<const RowRanges&>()[DeclVal<usize>()].end))
        ;
    }()
){
    const auto readWord = [&](const usize task, const usize word)noexcept(
        []()constexpr noexcept{
            using Range = const RemoveReference_T<decltype(DeclVal<const RowRanges&>()[DeclVal<const usize&>()])>&;
            return
                noexcept(DeclVal<const RowRanges&>()[DeclVal<const usize&>()])
                && noexcept(
                    DeclVal<const usize&>() < DeclVal<Range>().begin || DeclVal<const usize&>() >= DeclVal<Range>().end
                    ? static_cast<u64>(0u)
                    : DeclVal<const Span<u64>&>()[DeclVal<const Span<const usize>&>()[DeclVal<const usize&>()] + DeclVal<const usize&>() - DeclVal<Range>().begin]
                )
            ;
        }()
    ){
        const auto& range = ranges[task];
        return word < range.begin || word >= range.end ? static_cast<u64>(0u) : words[offsets[task] + word - range.begin];
    };
    const auto appendWord = [&](const usize task, const usize word, const u64 bits)noexcept(
        []()constexpr noexcept{
            using Range = const RemoveReference_T<decltype(DeclVal<const RowRanges&>()[DeclVal<const usize&>()])>&;
            return
                noexcept(DeclVal<const RowRanges&>()[DeclVal<const usize&>()])
                && noexcept(static_cast<b8>(DeclVal<const usize&>() >= DeclVal<Range>().begin && DeclVal<const usize&>() < DeclVal<Range>().end))
                && noexcept(DeclVal<const Span<u64>&>()[DeclVal<const Span<const usize>&>()[DeclVal<const usize&>()] + DeclVal<const usize&>() - DeclVal<Range>().begin] |= DeclVal<const u64&>())
            ;
        }()
    ){
        if(bits == 0u)
            return;
        const auto& range = ranges[task];
        FOUNDATION_ASSERT(word >= range.begin && word < range.end);
        words[offsets[task] + word - range.begin] |= bits;
    };
    for(usize rowBlock = 0u; rowBlock < wordsPerRow; ++rowBlock){
        const usize rowBase = rowBlock * s_BitsPerU64;
        const usize rowCount = Min(s_BitsPerU64, dimension - rowBase);
        usize columnEnd = rowBlock + 1u;
        for(usize row = 0u; row < rowCount; ++row)
            columnEnd = Max(columnEnd, ranges[rowBase + row].end);
        for(usize columnBlock = rowBlock; columnBlock < columnEnd; ++columnBlock){
            const usize columnBase = columnBlock * s_BitsPerU64;
            const usize columnCount = Min(s_BitsPerU64, dimension - columnBase);
            Array<u64, s_BitsPerU64> forward = {};
            u64 forwardBits = 0u;
            for(usize row = 0u; row < rowCount; ++row){
                forward[row] = readWord(rowBase + row, columnBlock);
                forwardBits |= forward[row];
            }
            if(rowBlock == columnBlock){
                if(forwardBits != 0u){
                    TransposeBitMatrix64(forward);
                    for(usize row = 0u; row < rowCount; ++row)
                        appendWord(rowBase + row, columnBlock, forward[row]);
                }
                continue;
            }
            Array<u64, s_BitsPerU64> backward = {};
            u64 backwardBits = 0u;
            for(usize row = 0u; row < columnCount; ++row){
                backward[row] = readWord(columnBase + row, rowBlock);
                backwardBits |= backward[row];
            }
            if(forwardBits != 0u){
                TransposeBitMatrix64(forward);
                for(usize row = 0u; row < columnCount; ++row)
                    appendWord(columnBase + row, rowBlock, forward[row]);
            }
            if(backwardBits != 0u){
                TransposeBitMatrix64(backward);
                for(usize row = 0u; row < rowCount; ++row)
                    appendWord(rowBase + row, columnBlock, backward[row]);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

