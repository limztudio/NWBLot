// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <global/not_null.h>
#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stream_exception_contract_tests{
template<typename VectorT>
struct ThrowingStreamLoadResult{
    operator VectorT()const{ throw u32{ 0xE208u }; }
};

struct SingleStreamLoadInput{};
struct DoubleStreamLoadInput{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] ThrowingStreamLoadResult<SIMDVectorSingle> LoadFloat(const SingleStreamLoadInput&)noexcept{ return {}; }

[[nodiscard]] ThrowingStreamLoadResult<SIMDVectorDouble> LoadDouble(const DoubleStreamLoadInput&)noexcept{ return {}; }

};


TEST(CurrentMathContractBoundaries, TransformStreamCallbackFailureDoesNotTerminateOrPublishOutput){
    const auto singleFailure = [](SIMDVectorSingle, const SIMDMatrix&)->SIMDVectorSingle{ throw u32{ 0xE206u }; };
    const auto doubleFailure = [](SIMDVectorDouble, const SIMDMatrixDouble&)->SIMDVectorDouble{ throw u32{ 0xE207u }; };
    const auto singleControl = [](SIMDVectorSingle value, const SIMDMatrix&)noexcept{ return value; };
    const auto doubleControl = [](SIMDVectorDouble value, const SIMDMatrixDouble&)noexcept{ return value; };
    const Float4U input(1.0f, 2.0f, 3.0f, 4.0f);
    Float4U output(11.0f, 12.0f, 13.0f, 14.0f);
    const Double4U doubleInput(1.0, 2.0, 3.0, 4.0);
    Double4U doubleOutput(21.0, 22.0, 23.0, 24.0);
    const SIMDMatrix matrix = MatrixIdentity();
    const SIMDMatrixDouble doubleMatrix = MatrixDoubleIdentity();

    static_assert(!noexcept(SIMDVectorDetail::VectorTransformStreamImpl(MakeNotNull(&output), sizeof(output), MakeNotNull(&input), sizeof(input), 1u, matrix, singleFailure)));
    static_assert(noexcept(SIMDVectorDetail::VectorTransformStreamImpl(MakeNotNull(&output), sizeof(output), MakeNotNull(&input), sizeof(input), 1u, matrix, singleControl)));
    static_assert(!noexcept(SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(MakeNotNull(&doubleOutput), sizeof(doubleOutput), MakeNotNull(&doubleInput), sizeof(doubleInput), 1u, doubleMatrix, doubleFailure)));
    static_assert(noexcept(SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(MakeNotNull(&doubleOutput), sizeof(doubleOutput), MakeNotNull(&doubleInput), sizeof(doubleInput), 1u, doubleMatrix, doubleControl)));

    EXPECT_THROW(SIMDVectorDetail::VectorTransformStreamImpl(MakeNotNull(&output), sizeof(output), MakeNotNull(&input), sizeof(input), 1u, matrix, singleFailure), u32);
    EXPECT_EQ(output, Float4U(11.0f, 12.0f, 13.0f, 14.0f));
    EXPECT_THROW(SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(MakeNotNull(&doubleOutput), sizeof(doubleOutput), MakeNotNull(&doubleInput), sizeof(doubleInput), 1u, doubleMatrix, doubleFailure), u32);
    EXPECT_EQ(doubleOutput, Double4U(21.0, 22.0, 23.0, 24.0));
}

TEST(CurrentMathContractBoundaries, StreamLoadConversionFailureUnwindsBeforePublishingOutput){
    using namespace __hidden_stream_exception_contract_tests;
    const SingleStreamLoadInput input;
    const DoubleStreamLoadInput doubleInput;
    Float4U output(11.0f, 12.0f, 13.0f, 14.0f);
    Double4U doubleOutput(21.0, 22.0, 23.0, 24.0);
    const SIMDMatrix matrix = MatrixIdentity();
    const SIMDMatrixDouble doubleMatrix = MatrixDoubleIdentity();
    const auto singleControl = [](SIMDVectorSingle value, const SIMDMatrix&)noexcept{ return value; };
    const auto doubleControl = [](SIMDVectorDouble value, const SIMDMatrixDouble&)noexcept{ return value; };

    static_assert(noexcept(LoadFloat(input)));
    static_assert(noexcept(LoadDouble(doubleInput)));
    static_assert(!noexcept(SIMDVectorDetail::VectorTransformStreamImpl(MakeNotNull(&output), sizeof(output), MakeNotNull(&input), sizeof(input), 1u, matrix, singleControl)));
    static_assert(!noexcept(SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(MakeNotNull(&doubleOutput), sizeof(doubleOutput), MakeNotNull(&doubleInput), sizeof(doubleInput), 1u, doubleMatrix, doubleControl)));

    EXPECT_THROW(SIMDVectorDetail::VectorTransformStreamImpl(MakeNotNull(&output), sizeof(output), MakeNotNull(&input), sizeof(input), 1u, matrix, singleControl), u32);
    EXPECT_EQ(output, Float4U(11.0f, 12.0f, 13.0f, 14.0f));
    EXPECT_THROW(SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(MakeNotNull(&doubleOutput), sizeof(doubleOutput), MakeNotNull(&doubleInput), sizeof(doubleInput), 1u, doubleMatrix, doubleControl), u32);
    EXPECT_EQ(doubleOutput, Double4U(21.0, 22.0, 23.0, 24.0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
