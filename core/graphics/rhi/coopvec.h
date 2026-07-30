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

// Describes a combination of input and output data types for matrix multiplication with Cooperative Vectors.
// Maps from VkCooperativeVectorPropertiesNV.
struct CooperativeVectorMatMulFormatCombo{
    CooperativeVectorDataType::Enum inputType;
    CooperativeVectorDataType::Enum inputInterpretation;
    CooperativeVectorDataType::Enum matrixInterpretation;
    CooperativeVectorDataType::Enum biasInterpretation;
    CooperativeVectorDataType::Enum outputType;
    bool transposeSupported;
};

struct CooperativeVectorDeviceFeatures{
    // Format combinations supported by the device for matrix multiplication with Cooperative Vectors.
    GraphicsVector<CooperativeVectorMatMulFormatCombo> matMulFormats;

    // True if cooperativeVectorTrainingFloat16Accumulation is supported.
    bool trainingFloat16 = false;

    // True if cooperativeVectorTrainingFloat32Accumulation is supported.
    bool trainingFloat32 = false;

    explicit CooperativeVectorDeviceFeatures(GraphicsArena& arena)
        : matMulFormats(arena)
    {}
};

struct CooperativeVectorMatrixLayoutDesc{
    // Buffer where the matrix is stored.
    Buffer* buffer = nullptr;

    // Offset in bytes from the start of the buffer where the matrix starts.
    u64 offset = 0;

    // Data type of the matrix elements.
    CooperativeVectorDataType::Enum type = CooperativeVectorDataType::UInt8;

    // Layout of the matrix in memory.
    CooperativeVectorMatrixLayout::Enum layout = CooperativeVectorMatrixLayout::RowMajor;

    // Size in bytes of the matrix.
    usize size = 0;

    // Stride in bytes between rows or coumns, depending on the layout.
    // For RowMajor and ColumnMajor layouts, stride may be zero, in which case it is computed automatically.
    // For InferencingOptimal and TrainingOptimal layouts, stride does not matter and should be zero.
    usize stride = 0;
};

// Describes a single matrix layout conversion operation.
// Used by CommandList::convertCoopVecMatrices(...)
struct CooperativeVectorConvertMatrixLayoutDesc{
    CooperativeVectorMatrixLayoutDesc src;
    CooperativeVectorMatrixLayoutDesc dst;

    u32 numRows = 0;
    u32 numColumns = 0;
};

// Returns the size in bytes of a given data type.
usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type);

// Returns the stride for a given matrix if it's stored in a RowMajor or ColumnMajor layout.
// For other layouts, returns 0.
usize GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, u32 rows, u32 columns);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

