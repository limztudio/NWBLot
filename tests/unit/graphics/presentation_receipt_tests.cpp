// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/rhi/presentation.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_presentation_receipt_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::AcquiredBackBuffer;
using Core::PresentationReceipt;
namespace PresentationReceiptStatus = Core::PresentationReceiptStatus;

static_assert(IsStandardLayout_V<PresentationReceipt>);
static_assert(IsTriviallyCopyable_V<PresentationReceipt>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AcquiredBackBuffer Acquisition(const u64 value = 17u, const u32 imageIndex = 2u){
    AcquiredBackBuffer acquired;
    acquired.availabilityCompletion = { value, 3u, 5u, Core::CommandQueue::Graphics };
    acquired.nativeInitialState = Core::ResourceStates::Present;
    acquired.index = imageIndex;
    return acquired;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PresentationReceipt, RejectedAttemptCannotBeQualifiedByALaterDifferentSuccessfulFrame){
    const AcquiredBackBuffer rejected = Acquisition(17u, 2u);
    const AcquiredBackBuffer accepted = Acquisition(18u, 3u);
    PresentationReceipt receipt;
    receipt.record(rejected, false);
    EXPECT_EQ(receipt.status(rejected), PresentationReceiptStatus::Rejected);
    EXPECT_EQ(receipt.status(accepted), PresentationReceiptStatus::Pending);
    receipt.record(accepted, true);
    EXPECT_EQ(receipt.status(accepted), PresentationReceiptStatus::Accepted);
    EXPECT_EQ(receipt.status(rejected), PresentationReceiptStatus::Pending);
}

TEST(PresentationReceipt, ImageReuseRequiresItsNewAcquisitionTimelineIdentity){
    const AcquiredBackBuffer first = Acquisition(17u, 2u);
    const AcquiredBackBuffer reused = Acquisition(18u, 2u);
    PresentationReceipt receipt;
    receipt.record(first, true);
    EXPECT_EQ(receipt.status(first), PresentationReceiptStatus::Accepted);
    EXPECT_EQ(receipt.status(reused), PresentationReceiptStatus::Pending);
    receipt.record(reused, false);
    EXPECT_EQ(receipt.status(reused), PresentationReceiptStatus::Rejected);
    EXPECT_EQ(receipt.status(first), PresentationReceiptStatus::Pending);
}

TEST(PresentationReceipt, EqualTimelineValuesOnDifferentQueuesDevicesOrImagesDoNotMatch){
    const AcquiredBackBuffer acquired = Acquisition();
    PresentationReceipt receipt;
    receipt.record(acquired, true);
    AcquiredBackBuffer variants[5u];
    for(auto& variant : variants)
        variant = acquired;
    ++variants[0u].availabilityCompletion.physicalQueueIndex;
    ++variants[1u].availabilityCompletion.deviceGeneration;
    variants[2u].availabilityCompletion.queue = Core::CommandQueue::Compute;
    ++variants[3u].index;
    variants[4u].nativeInitialState = Core::ResourceStates::Unknown;
    for(const auto& variant : variants)
        EXPECT_EQ(receipt.status(variant), PresentationReceiptStatus::Pending);
    EXPECT_EQ(receipt.status(acquired), PresentationReceiptStatus::Accepted);
}

TEST(PresentationReceipt, InvalidTokensAndImageStatesNeverPublishEvidence){
    const AcquiredBackBuffer acquired = Acquisition();
    AcquiredBackBuffer variants[7u];
    for(auto& variant : variants)
        variant = acquired;
    variants[0u].availabilityCompletion.value = 0u;
    variants[1u].availabilityCompletion.physicalQueueIndex = Limit<u16>::s_Max;
    variants[2u].availabilityCompletion.deviceGeneration = 0u;
    variants[3u].availabilityCompletion.queue = Core::CommandQueue::kCount;
    variants[4u].index = Limit<u32>::s_Max;
    variants[5u].nativeInitialState = Core::ResourceStates::ShaderResource;
    variants[6u] = {};
    PresentationReceipt receipt;
    for(const auto& variant : variants){
        receipt.record(acquired, true);
        EXPECT_EQ(receipt.status(variant), PresentationReceiptStatus::Pending);
        receipt.record(variant, true);
        EXPECT_FALSE(receipt.valid());
        EXPECT_EQ(receipt.status(variant), PresentationReceiptStatus::Pending);
        EXPECT_EQ(receipt.status(acquired), PresentationReceiptStatus::Pending);
    }
}

TEST(PresentationReceipt, UnknownInitialStateIsAValidDistinctAcquisitionState){
    AcquiredBackBuffer acquired = Acquisition();
    acquired.nativeInitialState = Core::ResourceStates::Unknown;
    PresentationReceipt receipt;
    receipt.record(acquired, true);
    ASSERT_TRUE(receipt.valid());
    EXPECT_EQ(receipt.status(acquired), PresentationReceiptStatus::Accepted);
    acquired.nativeInitialState = Core::ResourceStates::Present;
    EXPECT_EQ(receipt.status(acquired), PresentationReceiptStatus::Pending);
}

TEST(PresentationReceipt, ResetAtLifetimeBoundaryRemovesEarlierAcquisitionEvidence){
    const AcquiredBackBuffer acquired = Acquisition();
    PresentationReceipt receipt;
    receipt.record(acquired, true);
    ASSERT_EQ(receipt.status(acquired), PresentationReceiptStatus::Accepted);
    receipt.reset();
    EXPECT_FALSE(receipt.valid());
    EXPECT_EQ(receipt.status(acquired), PresentationReceiptStatus::Pending);
    receipt.record(acquired, false);
    ASSERT_EQ(receipt.status(acquired), PresentationReceiptStatus::Rejected);
    receipt.reset();
    EXPECT_FALSE(receipt.valid());
    EXPECT_EQ(receipt.status(acquired), PresentationReceiptStatus::Pending);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

