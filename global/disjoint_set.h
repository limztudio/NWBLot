// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "algorithm.h"
#include "span.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 FindDisjointRoot(u32 index, const Span<u32> parents)noexcept{
    while(parents[index] != index){
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

inline void JoinDisjointSets(const u32 first, const u32 second, const Span<u32> parents, const Span<u8> ranks)noexcept{
    u32 firstRoot = FindDisjointRoot(first, parents);
    u32 secondRoot = FindDisjointRoot(second, parents);
    if(firstRoot == secondRoot)
        return;
    if(ranks[firstRoot] < ranks[secondRoot])
        Swap(firstRoot, secondRoot);
    parents[secondRoot] = firstRoot;
    if(ranks[firstRoot] == ranks[secondRoot])
        ++ranks[firstRoot];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

