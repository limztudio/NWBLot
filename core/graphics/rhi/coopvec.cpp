// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coopvec.h"

#include <core/graphics/rhi/coopvec_size.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type)noexcept{
    const usize result = MapCooperativeVectorTypeSize(type);
    if(result == 0u)
        NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Unknown CooperativeVectorDataType::Enum value"));
    return result;
}

Expected<usize> GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, u32 rows, u32 columns)noexcept{
    if(type > CooperativeVectorDataType::Float64 || rows == 0u || columns == 0u)
        return MakeUnexpected(Failure{});

    usize minorElementCount = 0u;
    switch(layout){
    case CooperativeVectorMatrixLayout::RowMajor:
        minorElementCount = static_cast<usize>(columns);
        break;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        minorElementCount = static_cast<usize>(rows);
        break;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        return 0u;
    default:
        return MakeUnexpected(Failure{});
    }

    const usize dataTypeSize = GetCooperativeVectorDataTypeSize(type);
    const auto minorByteSize = TryMultiply<usize>(minorElementCount, dataTypeSize);
    if(!minorByteSize)
        return MakeUnexpected(Failure{});
    if(AddOverflows<usize>(*minorByteSize, dataTypeSize))
        return MakeUnexpected(Failure{});
    return *minorByteSize + dataTypeSize;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

