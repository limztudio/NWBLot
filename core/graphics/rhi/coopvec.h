// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CooperativeVectorDataType{
    enum Enum : u8{
        UInt8,
        SInt8,
        UInt8Packed,
        SInt8Packed,
        UInt16,
        SInt16,
        UInt32,
        SInt32,
        UInt64,
        SInt64,
        FloatE4M3,
        FloatE5M2,
        Float16,
        BFloat16,
        Float32,
        Float64,
    };
};

namespace CooperativeVectorMatrixLayout{
    enum Enum : u8{
        RowMajor,
        ColumnMajor,
        InferencingOptimal,
        TrainingOptimal,
    };
};

struct CooperativeVectorMatMulFormatCombo{
    CooperativeVectorDataType::Enum inputType;
    CooperativeVectorDataType::Enum inputInterpretation;
    CooperativeVectorDataType::Enum matrixInterpretation;
    CooperativeVectorDataType::Enum biasInterpretation;
    CooperativeVectorDataType::Enum outputType;
    bool transposeSupported;
};

struct CooperativeVectorDeviceFeatures{
    GraphicsVector<CooperativeVectorMatMulFormatCombo> matMulFormats;

    bool trainingFloat16 = false;

    bool trainingFloat32 = false;


public:
    explicit CooperativeVectorDeviceFeatures(GraphicsArena& arena)noexcept
        : matMulFormats(arena)
    {}
};

struct CooperativeVectorMatrixLayoutDesc{
    Buffer* buffer = nullptr;

    u64 offset = 0;

    usize size = 0;

    // Byte stride; zero derives row/column stride. Optimal layouts ignore it and should use zero.
    usize stride = 0;

    CooperativeVectorDataType::Enum type = CooperativeVectorDataType::UInt8;

    CooperativeVectorMatrixLayout::Enum layout = CooperativeVectorMatrixLayout::RowMajor;
};

struct CooperativeVectorConvertMatrixLayoutDesc{
    CooperativeVectorMatrixLayoutDesc src;
    CooperativeVectorMatrixLayoutDesc dst;

    u32 numRows = 0;
    u32 numColumns = 0;
};

usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type)noexcept;

// Row/column byte stride; successful zero for InferencingOptimal and TrainingOptimal, failure for invalid dimensions/type/layout or overflow.
[[nodiscard]] Expected<usize> GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, u32 rows, u32 columns)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

