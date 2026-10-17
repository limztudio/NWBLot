// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coopvec.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type)noexcept{
    switch(type){
    case CooperativeVectorDataType::UInt8:
    case CooperativeVectorDataType::SInt8:
        return 1;
    case CooperativeVectorDataType::UInt8Packed:
    case CooperativeVectorDataType::SInt8Packed:
        return 1;
    case CooperativeVectorDataType::UInt16:
    case CooperativeVectorDataType::SInt16:
        return 2;
    case CooperativeVectorDataType::UInt32:
    case CooperativeVectorDataType::SInt32:
        return 4;
    case CooperativeVectorDataType::UInt64:
    case CooperativeVectorDataType::SInt64:
        return 8;
    case CooperativeVectorDataType::FloatE4M3:
    case CooperativeVectorDataType::FloatE5M2:
        return 1;
    case CooperativeVectorDataType::Float16:
    case CooperativeVectorDataType::BFloat16:
        return 2;
    case CooperativeVectorDataType::Float32:
        return 4;
    case CooperativeVectorDataType::Float64:
        return 8;
    }
    NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Unknown CooperativeVectorDataType::Enum value"));
    return 0;
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

