// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/api.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


class CooperativeVectorQueryTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        s_runtimeReady = s_scope->initialize();
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_runtimeReady && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled cooperative-vector queries emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeReady = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }

    virtual void SetUp()override{
        if(!s_runtimeReady)
            GTEST_SKIP() << "Cooperative-vector query: no usable headless graphics device.";
    }


protected:
    static bool s_runtimeReady;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool CooperativeVectorQueryTest::s_runtimeReady = false;
UniquePtr<HeadlessGraphicsScope> CooperativeVectorQueryTest::s_scope;
Optional<CapturingLogger> CooperativeVectorQueryTest::s_logger;
Optional<Common::LoggerRegistrationGuard> CooperativeVectorQueryTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CooperativeVectorQueryTest, StandardLayoutsQueryWithoutTightlyPackedStride){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector query: VK_NV_cooperative_vector is unavailable.";

    EXPECT_GT(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::RowMajor,
        2,
        2
    ), 0u);
    EXPECT_GT(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::ColumnMajor,
        2,
        2
    ), 0u);
}

TEST_F(CooperativeVectorQueryTest, InvalidInputsFailClosed){
    EXPECT_EQ(device().getCoopVecMatrixSize(
        static_cast<CooperativeVectorDataType::Enum>(Limit<u8>::s_Max),
        CooperativeVectorMatrixLayout::RowMajor,
        2,
        2
    ), 0u);
    EXPECT_EQ(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::Float32,
        static_cast<CooperativeVectorMatrixLayout::Enum>(Limit<u8>::s_Max),
        2,
        2
    ), 0u);
    EXPECT_EQ(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::RowMajor,
        0,
        2
    ), 0u);
    EXPECT_EQ(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::ColumnMajor,
        2,
        0
    ), 0u);
}

TEST_F(CooperativeVectorQueryTest, PackedAndFloat8StandardLayoutsFailClosed){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector query: VK_NV_cooperative_vector is unavailable.";

    EXPECT_EQ(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::UInt8Packed,
        CooperativeVectorMatrixLayout::InferencingOptimal,
        2,
        2
    ), 0u);
    EXPECT_EQ(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::SInt8Packed,
        CooperativeVectorMatrixLayout::TrainingOptimal,
        2,
        2
    ), 0u);
    EXPECT_EQ(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::FloatE4M3,
        CooperativeVectorMatrixLayout::RowMajor,
        2,
        2
    ), 0u);
    EXPECT_EQ(device().getCoopVecMatrixSize(
        CooperativeVectorDataType::FloatE5M2,
        CooperativeVectorMatrixLayout::ColumnMajor,
        2,
        2
    ), 0u);
}

TEST_F(CooperativeVectorQueryTest, UnsupportedMatrixInterpretationsFailClosed){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector query: VK_NV_cooperative_vector is unavailable.";

    const CooperativeVectorDeviceFeatures features = device().queryCoopVecFeatures();
    for(u8 value = CooperativeVectorDataType::UInt8; value <= CooperativeVectorDataType::Float64; ++value){
        const auto type = static_cast<CooperativeVectorDataType::Enum>(value);
        if(
            type == CooperativeVectorDataType::UInt8Packed
            || type == CooperativeVectorDataType::SInt8Packed
            || type == CooperativeVectorDataType::Float32
        )
            continue;

        bool supported = false;
        for(const CooperativeVectorMatMulFormatCombo& format : features.matMulFormats){
            if(format.matrixInterpretation == type){
                supported = true;
                break;
            }
        }
        if(supported)
            continue;

        EXPECT_EQ(device().getCoopVecMatrixSize(
            type,
            CooperativeVectorMatrixLayout::InferencingOptimal,
            2,
            2
        ), 0u);
        return;
    }

    GTEST_SKIP() << "Cooperative-vector query: every non-packed matrix interpretation is supported.";
}

TEST_F(CooperativeVectorQueryTest, MissingConversionEntrypointFailsClosed){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector query: VK_NV_cooperative_vector is unavailable.";

    const PFN_vkConvertCooperativeVectorMatrixNV originalConvert = vkConvertCooperativeVectorMatrixNV;
    ASSERT_NE(originalConvert, nullptr);
    vkConvertCooperativeVectorMatrixNV = nullptr;
    const bool advertisedWithoutEntrypoint = device().queryFeatureSupport(Feature::CooperativeVectorInferencing);
    const usize resultWithoutEntrypoint = device().getCoopVecMatrixSize(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::RowMajor,
        2,
        2
    );
    vkConvertCooperativeVectorMatrixNV = originalConvert;

    EXPECT_FALSE(advertisedWithoutEntrypoint);
    EXPECT_EQ(resultWithoutEntrypoint, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

