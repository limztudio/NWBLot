// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture state-ingress readiness, policy lifetime, and ownership atomicity coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_texture_gpu_readiness_state{


namespace TextureStateIngress{
    enum Enum : u8{
        UavPolicy = 0u,
        BeginTracking,
        SetState,
        SetPermanentState,
        ReleaseOwnership,
        Count,
    };
}


static void ApplyTextureStateIngress(
    CommandList& commandList,
    Texture* const texture,
    const TextureStateIngress::Enum ingress
){
    switch(ingress){
    case TextureStateIngress::UavPolicy:
        commandList.setEnableUavBarriersForTexture(texture, false);
        break;
    case TextureStateIngress::BeginTracking:
        commandList.beginTrackingTextureState(texture, s_AllSubresources, ResourceStates::Common);
        break;
    case TextureStateIngress::SetState:
        commandList.setTextureState(texture, s_AllSubresources, ResourceStates::Common);
        break;
    case TextureStateIngress::SetPermanentState:
        commandList.setPermanentTextureState(texture, ResourceStates::Common);
        break;
    case TextureStateIngress::ReleaseOwnership:
        commandList.releaseTextureOwnership(
            texture,
            s_AllSubresources,
            commandList.getDescription().physicalQueue
        );
        break;
    default:
        NWB_ASSERT(false);
        break;
    }
}


static void ExpectEveryTextureStateIngressRejects(
    GraphicsBackend::Device& device,
    Texture* const texture
){
    for(u8 ingress = 0u; ingress < TextureStateIngress::Count; ++ingress){
        SCOPED_TRACE(static_cast<u32>(ingress));
        CommandListHandle commandList = device.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        ApplyTextureStateIngress(
            *commandList,
            texture,
            static_cast<TextureStateIngress::Enum>(ingress)
        );
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
    }
}


};


class TextureGpuReadinessStateTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Texture GPU-readiness state tests: no validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "Texture GPU-readiness state tests emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static TextureDesc ordinaryTextureDesc(){
        return TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
        ;
    }

    [[nodiscard]] static TextureHandle createUnboundTexture(){
        TextureDesc desc = ordinaryTextureDesc();
        desc.isVirtual = true;
        return device().createTexture(desc);
    }

    [[nodiscard]] static bool submitAndWait(CommandList& commandList){
        CommandList* const commandLists[]{ &commandList };
        const QueueSubmissionToken token = device().executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList.getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
        return token.valid() && device().waitForIdle();
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool TextureGpuReadinessStateTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> TextureGpuReadinessStateTest::s_scope;
Optional<CapturingLogger> TextureGpuReadinessStateTest::s_logger;
Optional<Common::LoggerRegistrationGuard> TextureGpuReadinessStateTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextureGpuReadinessStateTest, NullTextureStateIngressRemainsANoOp){
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);

    for(
        u8 ingress = 0u;
        ingress < __hidden_texture_gpu_readiness_state::TextureStateIngress::Count;
        ++ingress
    )
        __hidden_texture_gpu_readiness_state::ApplyTextureStateIngress(
            *commandList,
            nullptr,
            static_cast<__hidden_texture_gpu_readiness_state::TextureStateIngress::Enum>(ingress)
        );

    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasCommandBuffer());
}


TEST_F(TextureGpuReadinessStateTest, UnboundTextureRejectsEveryStateIngress){
    TextureHandle unboundTexture = createUnboundTexture();
    ASSERT_TRUE(unboundTexture);
    ASSERT_FALSE(device().isTextureReadyForGpuUse(unboundTexture.get()));
    __hidden_texture_gpu_readiness_state::ExpectEveryTextureStateIngressRejects(
        device(),
        unboundTexture.get()
    );
}


TEST_F(TextureGpuReadinessStateTest, ForeignTextureRejectsEveryStateIngress){
    HeadlessGraphicsScope foreignScope;
    if(!foreignScope.initialize())
        GTEST_SKIP() << "Texture GPU-readiness state tests: second headless Vulkan device is unavailable.";
    TextureHandle foreignTexture = foreignScope.graphics().getDevice().createTexture(ordinaryTextureDesc());
    ASSERT_TRUE(foreignTexture);
    ASSERT_FALSE(device().isTextureReadyForGpuUse(foreignTexture.get()));
    __hidden_texture_gpu_readiness_state::ExpectEveryTextureStateIngressRejects(
        device(),
        foreignTexture.get()
    );
}


TEST_F(TextureGpuReadinessStateTest, BeginTrackingRejectsUnknownRangeAndPermanentConflictAtomically){
    TextureHandle texture = device().createTexture(ordinaryTextureDesc());
    ASSERT_TRUE(texture);

    CommandListHandle unknownList = device().createCommandList();
    ASSERT_TRUE(unknownList);
    const u32 unknownReferences = texture->getReferenceCount();
    unknownList->open();
    unknownList->beginTrackingTextureState(texture.get(), s_AllSubresources, ResourceStates::Unknown);
    EXPECT_TRUE(unknownList->commandRecordingFailed());
    EXPECT_FALSE(unknownList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    EXPECT_EQ(texture->getReferenceCount(), unknownReferences);
    unknownList->close();

    CommandListHandle rangeList = device().createCommandList();
    ASSERT_TRUE(rangeList);
    const u32 rangeReferences = texture->getReferenceCount();
    rangeList->open();
    rangeList->beginTrackingTextureState(
        texture.get(),
        TextureSubresourceSet(7u, 1u, 0u, 1u),
        ResourceStates::Common
    );
    EXPECT_TRUE(rangeList->commandRecordingFailed());
    EXPECT_FALSE(rangeList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    EXPECT_EQ(texture->getReferenceCount(), rangeReferences);
    rangeList->close();

    CommandListHandle permanentList = device().createCommandList();
    ASSERT_TRUE(permanentList);
    permanentList->open();
    permanentList->setPermanentTextureState(texture.get(), ResourceStates::Common);
    permanentList->close();
    ASSERT_FALSE(permanentList->commandRecordingFailed());
    ASSERT_TRUE(submitAndWait(*permanentList));
    ASSERT_EQ(permanentList->getPermanentTextureState(texture.get()), ResourceStates::Common);

    permanentList->open();
    const u32 permanentReferences = texture->getReferenceCount();
    permanentList->beginTrackingTextureState(
        texture.get(),
        s_AllSubresources,
        ResourceStates::ShaderResource
    );
    EXPECT_TRUE(permanentList->commandRecordingFailed());
    EXPECT_EQ(permanentList->getPermanentTextureState(texture.get()), ResourceStates::Common);
    EXPECT_EQ(texture->getReferenceCount(), permanentReferences);
    permanentList->close();
}


TEST_F(TextureGpuReadinessStateTest, TextureUavPolicyRetainsOnceAndPersistsAcrossReopen){
    TextureHandle texture = device().createTexture(ordinaryTextureDesc());
    ASSERT_TRUE(texture);
    Texture* const rawTexture = texture.get();

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->setEnableUavBarriersForTexture(rawTexture, false);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(rawTexture->getReferenceCount(), 1u);

    commandList->open();
    commandList->setEnableUavBarriersForTexture(rawTexture, false);
    const u32 retainedReferences = rawTexture->getReferenceCount();
    EXPECT_EQ(retainedReferences, 2u);
    commandList->setEnableUavBarriersForTexture(rawTexture, true);
    EXPECT_EQ(rawTexture->getReferenceCount(), retainedReferences);
    texture.reset();
    EXPECT_EQ(rawTexture->getReferenceCount(), 1u);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    commandList->open();
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(rawTexture->getReferenceCount(), 1u);
    commandList->setEnableUavBarriersForTexture(rawTexture, false);
    EXPECT_EQ(rawTexture->getReferenceCount(), 1u);
    commandList->close();
}


TEST_F(TextureGpuReadinessStateTest, OwnershipLateConflictPublishesNoEarlierSubresource){
    const GpuPhysicalQueueTopology topology = device().getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount < 2u)
        GTEST_SKIP() << "Texture ownership atomicity needs two distinct physical queues.";

    GpuPhysicalQueueId firstDestination = topology.queues[0u].id;
    GpuPhysicalQueueId secondDestination;
    for(usize queueIndex = 1u; queueIndex < topology.queueCount; ++queueIndex){
        if(topology.queues[queueIndex].id != firstDestination){
            secondDestination = topology.queues[queueIndex].id;
            break;
        }
    }
    if(!secondDestination.valid())
        GTEST_SKIP() << "Texture ownership atomicity needs two distinct physical queue identities.";

    TextureDesc desc = ordinaryTextureDesc().setMipLevels(2u).setKeepInitialState(true);
    TextureHandle texture = device().createTexture(desc);
    ASSERT_TRUE(texture);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);

    commandList->open();
    commandList->beginTrackingTextureState(texture.get(), s_AllSubresources, ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(submitAndWait(*commandList));

    commandList->open();
    commandList->releaseTextureOwnership(
        texture.get(),
        TextureSubresourceSet(1u, 1u, 0u, 1u),
        firstDestination
    );
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    ASSERT_TRUE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 1u));

    commandList->releaseTextureOwnership(texture.get(), s_AllSubresources, secondDestination);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

