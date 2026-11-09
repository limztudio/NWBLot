// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "allocation_size.h"
#include "assert.h"
#include "span.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Replacement text must not borrow from the source. Ranges must be disjoint; equal-start order is unspecified.
template<typename ReplacementT, typename StringT>
void ApplyTextReplacements(const Span<ReplacementT> inOutReplacements, StringT& inOutSource){
    Sort(inOutReplacements.begin(), inOutReplacements.end(), [](const ReplacementT& lhs, const ReplacementT& rhs)noexcept(noexcept(static_cast<b8>(lhs.begin > rhs.begin))) -> b8{
        return static_cast<b8>(lhs.begin > rhs.begin);
    });

    usize capacity = inOutSource.size();
    for(const ReplacementT& replacement : inOutReplacements){
        FOUNDATION_ASSERT(replacement.begin <= replacement.end && replacement.end <= inOutSource.size());
        const usize oldSize = replacement.end - replacement.begin;
        if(replacement.text.size() > oldSize)
            capacity = AddSize(capacity, replacement.text.size() - oldSize);
    }
    inOutSource.reserve(capacity);
    for(const ReplacementT& replacement : inOutReplacements)
        inOutSource.replace(replacement.begin, replacement.end - replacement.begin, replacement.text);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

