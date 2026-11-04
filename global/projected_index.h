// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "algorithm.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Indices, typename Records, typename Selected, typename RecordLess>
void BuildProjectedRecordIndex(Indices& indices, const Records& records, const Selected selected, const RecordLess recordLess){
    indices.clear();
    indices.reserve(records.size());
    for(usize index = 0u; index < records.size(); ++index){
        if(selected(records[index]))
            indices.push_back(index);
    }
    Sort(indices.begin(), indices.end(), [&](const usize lhs, const usize rhs)noexcept(
        noexcept(recordLess(records[lhs], records[rhs]))
        && (!IsReference_V<decltype(recordLess(records[lhs], records[rhs]))> || IsNothrowConvertible_V<decltype(recordLess(records[lhs], records[rhs])), Decay_T<decltype(recordLess(records[lhs], records[rhs]))>>)
    ){ return recordLess(records[lhs], records[rhs]); }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

