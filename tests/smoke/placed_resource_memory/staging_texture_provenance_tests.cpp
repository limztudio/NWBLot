// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Immutable staging-texture layout provenance, checked-range, and queue-admission coverage.


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


namespace __hidden_staging_texture_provenance_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ExpectZeroRange(const GraphicsBackend::VulkanDetail::StagingTextureRange& range){
    EXPECT_EQ(range.byteOffset, 0u);
    EXPECT_EQ(range.byteSize, 0u);
    EXPECT_EQ(range.rowPitch, 0u);
    EXPECT_EQ(range.bufferRowLength, 0u);
    EXPECT_EQ(range.bufferImageHeight, 0u);
}

static void ExpectRangeRejected(
    const TextureSlice& slice,
    const GraphicsBackend::VulkanDetail::StagingTextureMipLayout& mipLayout,
    const GraphicsBackend::VulkanDetail::TextureFormatBlockLayout& formatLayout,
    const u64 arrayByteSize,
    const u64 totalByteSize,
    const u32 requiredOffsetAlignment
){
    GraphicsBackend::VulkanDetail::StagingTextureRange range;
    range.byteOffset = 1u;
    range.byteSize = 1u;
    range.rowPitch = 1u;
    range.bufferRowLength = 1u;
    range.bufferImageHeight = 1u;
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::BuildStagingTextureRange(
        slice,
        mipLayout,
        formatLayout,
        arrayByteSize,
        totalByteSize,
        requiredOffsetAlignment,
        false,
        range
    ));
    ExpectZeroRange(range);
}

template<typename RecordOperation>
static void ExpectAtomicCopyRejection(
    GraphicsBackend::Device& device,
    Framebuffer& framebuffer,
    Texture& renderTarget,
    StagingTexture& staging,
    Texture& texture,
    RecordOperation&& recordOperation
){
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(&framebuffer));
    ASSERT_TRUE(commandList->isRenderPassActive());
    ASSERT_FALSE(commandList->commandRecordingFailed());

    const u32 stagingReferences = staging.getReferenceCount();
    const u32 textureReferences = texture.getReferenceCount();
    recordOperation(*commandList);

    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->isRenderPassActive());
    EXPECT_EQ(
        commandList->getTextureSubresourceState(&renderTarget, 0u, 0u),
        ResourceStates::RenderTarget
    );
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(&texture, 0u, 0u));
    EXPECT_EQ(staging.getReferenceCount(), stagingReferences);
    EXPECT_EQ(texture.getReferenceCount(), textureReferences);

    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    commandList->open();
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_TRUE(commandList->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


TEST(StagingTextureProvenanceCpuTest, CheckedAlignmentAndRangeArithmeticRejectWithoutPublishingOutputs){
    using namespace GraphicsBackend::VulkanDetail;

    u32 alignment = 0x5a5a5a5au;
    EXPECT_FALSE(TryComputeCommonAlignment(0u, 4u, alignment));
    EXPECT_EQ(alignment, 0u);
    EXPECT_TRUE(TryComputeCommonAlignment(4u, 3u, alignment));
    EXPECT_EQ(alignment, 12u);
    EXPECT_TRUE(TryComputeCommonAlignment(4u, 6u, alignment));
    EXPECT_EQ(alignment, 12u);
    EXPECT_TRUE(TryComputeCommonAlignment(4u, 12u, alignment));
    EXPECT_EQ(alignment, 12u);
    EXPECT_FALSE(TryComputeCommonAlignment(4u, Limit<u32>::s_Max, alignment));
    EXPECT_EQ(alignment, 0u);

    EXPECT_TRUE(StagingTextureSharingIncludesQueueClass(
        ResourceQueueSharing::GraphicsAndTransfer,
        CommandQueue::Graphics
    ));
    EXPECT_FALSE(StagingTextureSharingIncludesQueueClass(
        ResourceQueueSharing::GraphicsAndTransfer,
        CommandQueue::Compute
    ));
    EXPECT_TRUE(StagingTextureSharingIncludesQueueClass(
        ResourceQueueSharing::GraphicsAndTransfer,
        CommandQueue::Transfer
    ));

    const TextureFormatBlockLayout sixByteFormat{ 1u, 1u, 6u };
    const StagingTextureMipLayout validMip{ 0u, 24u, 48u, 4u, 2u };
    const TextureSlice validSlice = TextureSlice()
        .setOrigin(2u, 1u, 1u)
        .setSize(2u, 2u, 2u)
        .setArraySlice(1u)
    ;
    StagingTextureRange range;
    ASSERT_TRUE(BuildStagingTextureRange(
        validSlice,
        validMip,
        sixByteFormat,
        240u,
        480u,
        12u,
        false,
        range
    ));
    EXPECT_EQ(range.byteOffset, 324u);
    EXPECT_EQ(range.byteSize, 84u);
    EXPECT_EQ(range.rowPitch, 24u);
    EXPECT_EQ(range.bufferRowLength, 4u);
    EXPECT_EQ(range.bufferImageHeight, 2u);

    ASSERT_TRUE(BuildStagingTextureRange(
        validSlice,
        validMip,
        sixByteFormat,
        240u,
        408u,
        12u,
        false,
        range
    ));
    EXPECT_EQ(range.byteOffset + range.byteSize, 408u);

    SCOPED_TRACE("range tail exceeds immutable allocation");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        validSlice, validMip, sixByteFormat, 240u, 407u, 12u
    );
    SCOPED_TRACE("array stride exceeds immutable allocation");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        validSlice, validMip, sixByteFormat, 240u, 200u, 12u
    );
    SCOPED_TRACE("range offset violates required alignment");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        validSlice, validMip, sixByteFormat, 240u, 480u, 16u
    );

    const TextureFormatBlockLayout oneByteFormat{ 1u, 1u, 1u };
    const StagingTextureMipLayout unitMip{ 0u, 1u, 1u, 1u, 1u };
    SCOPED_TRACE("array offset multiplication overflows");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        TextureSlice().setArraySlice(2u),
        unitMip,
        oneByteFormat,
        UINT64_MAX / 2u + 1u,
        UINT64_MAX,
        0u
    );

    StagingTextureMipLayout overflowMip = unitMip;
    overflowMip.slicePitch = UINT64_MAX;
    SCOPED_TRACE("z offset multiplication overflows");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        TextureSlice().setOrigin(0u, 0u, 2u),
        overflowMip,
        oneByteFormat,
        UINT64_MAX,
        UINT64_MAX,
        0u
    );
    SCOPED_TRACE("tail size overflows");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        TextureSlice().setDepth(2u),
        overflowMip,
        oneByteFormat,
        UINT64_MAX,
        UINT64_MAX,
        0u
    );

    overflowMip = unitMip;
    overflowMip.rowPitch = UINT64_MAX;
    SCOPED_TRACE("y offset multiplication overflows");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        TextureSlice().setOrigin(0u, 2u, 0u),
        overflowMip,
        oneByteFormat,
        UINT64_MAX,
        UINT64_MAX,
        0u
    );

    const TextureFormatBlockLayout twoByteFormat{ 1u, 1u, 2u };
    overflowMip = unitMip;
    overflowMip.byteOffset = UINT64_MAX - 1u;
    SCOPED_TRACE("x offset addition overflows");
    __hidden_staging_texture_provenance_tests::ExpectRangeRejected(
        TextureSlice().setOrigin(1u, 0u, 0u),
        overflowMip,
        twoByteFormat,
        UINT64_MAX,
        UINT64_MAX,
        0u
    );
}


class StagingTextureProvenanceTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif

        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(
            !s_scope->setAsyncComputeLaneEnabled(true)
            || !s_scope->setTransferQueueEnabled(true)
            || !s_scope->setSameClassMultiQueueEnabled(true)
        ){
            GTEST_SKIP() << "Staging texture provenance: queue configuration is unavailable.";
            return;
        }
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Staging texture provenance: no validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "staging-texture provenance tests emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static TextureDesc arrayTextureDesc(){
        TextureDesc desc = TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setDimension(TextureDimension::Texture2DArray)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        desc.arraySize = 1u;
        desc.mipLevels = 1u;
        return desc;
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool StagingTextureProvenanceTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> StagingTextureProvenanceTest::s_scope;
Optional<CapturingLogger> StagingTextureProvenanceTest::s_logger;
Optional<Common::LoggerRegistrationGuard> StagingTextureProvenanceTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(StagingTextureProvenanceTest, CreationRejectsUnknownQueueSharingBits){
    TextureDesc desc = arrayTextureDesc();
    desc.queueSharing = static_cast<ResourceQueueSharing::Mask>(1u << 7u);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        const StagingTextureHandle rejected = device().createStagingTexture(desc, CpuAccessMode::Write);
        EXPECT_FALSE(rejected);
    }, "");
#else
    EXPECT_FALSE(device().createStagingTexture(desc, CpuAccessMode::Write));
#endif
}


TEST_F(StagingTextureProvenanceTest, MappingUsesImmutableMipAndArrayBoundsAndRecovers){
    const TextureDesc desc = arrayTextureDesc();
    StagingTextureHandle upload = device().createStagingTexture(desc, CpuAccessMode::Write);
    StagingTextureHandle readback = device().createStagingTexture(desc, CpuAccessMode::Read);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(readback);

    static constexpr usize s_UnchangedPitch = 0x5a5a5a5au;
    const auto expectImmutableRejectionsAndRetry = [&](StagingTexture& staging, const CpuAccessMode::Enum access){
        TextureDesc& publicDesc = const_cast<TextureDesc&>(staging.getDescription());
        const auto forgedMapWasAccepted = [&](const TextureSlice& slice, const bool forgeMip){
            if(forgeMip)
                publicDesc.mipLevels = 2u;
            else
                publicDesc.arraySize = 2u;
            usize pitch = s_UnchangedPitch;
            void* const memory = device().mapStagingTexture(&staging, slice, access, &pitch);
            publicDesc.mipLevels = 1u;
            publicDesc.arraySize = 1u;
            if(memory)
                device().unmapStagingTexture(&staging);
            return memory != nullptr || pitch != s_UnchangedPitch;
        };
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(forgedMapWasAccepted(TextureSlice().setMipLevel(1u), true));
        }, "");
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(forgedMapWasAccepted(TextureSlice().setArraySlice(1u), false));
        }, "");
#else
        EXPECT_FALSE(forgedMapWasAccepted(TextureSlice().setMipLevel(1u), true));
        EXPECT_FALSE(forgedMapWasAccepted(TextureSlice().setArraySlice(1u), false));
#endif

        usize edgePitch = s_UnchangedPitch;
        void* const edgeMemory = device().mapStagingTexture(
            &staging,
            TextureSlice().setOrigin(7u, 7u, 0u).setSize(1u, 1u, 1u),
            access,
            &edgePitch
        );
        ASSERT_NE(edgeMemory, nullptr);
        EXPECT_NE(edgePitch, s_UnchangedPitch);
        device().unmapStagingTexture(&staging);

        usize retryPitch = s_UnchangedPitch;
        ASSERT_NE(device().mapStagingTexture(&staging, TextureSlice{}, access, &retryPitch), nullptr);
        EXPECT_NE(retryPitch, s_UnchangedPitch);
        device().unmapStagingTexture(&staging);
    };

    expectImmutableRejectionsAndRetry(*upload, CpuAccessMode::Write);
    expectImmutableRejectionsAndRetry(*readback, CpuAccessMode::Read);
}


TEST_F(StagingTextureProvenanceTest, ForgedArrayCopyDirectionsRejectAtomicallyAndValidOperandsRetry){
    const TextureDesc desc = arrayTextureDesc();
    TextureHandle texture = device().createTexture(desc);
    StagingTextureHandle upload = device().createStagingTexture(desc, CpuAccessMode::Write);
    StagingTextureHandle readback = device().createStagingTexture(desc, CpuAccessMode::Read);

    TextureDesc renderTargetDesc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setInRenderTarget(true)
    ;
    TextureHandle renderTarget = device().createTexture(renderTargetDesc);
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(renderTarget.get())
    );
    ASSERT_TRUE(texture);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(readback);
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(framebuffer);

    const TextureSlice forgedStagingSlice = TextureSlice().setArraySlice(1u);
    const TextureSlice imageSlice = TextureSlice().setArraySlice(0u);
    {
        SCOPED_TRACE("staging upload forged one-past creation array");
        __hidden_staging_texture_provenance_tests::ExpectAtomicCopyRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *upload,
            *texture,
            [&](CommandList& commandList){
                TextureDesc& publicDesc = const_cast<TextureDesc&>(upload->getDescription());
                publicDesc.arraySize = 2u;
                commandList.copyTexture(texture.get(), imageSlice, upload.get(), forgedStagingSlice);
                publicDesc.arraySize = 1u;
            }
        );
    }
    {
        SCOPED_TRACE("staging readback forged one-past creation array");
        __hidden_staging_texture_provenance_tests::ExpectAtomicCopyRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *readback,
            *texture,
            [&](CommandList& commandList){
                TextureDesc& publicDesc = const_cast<TextureDesc&>(readback->getDescription());
                publicDesc.arraySize = 2u;
                commandList.copyTexture(readback.get(), forgedStagingSlice, texture.get(), imageSlice);
                publicDesc.arraySize = 1u;
            }
        );
    }

    CommandListHandle retry = device().createCommandList();
    ASSERT_TRUE(retry);
    retry->open();
    retry->copyTexture(texture.get(), imageSlice, upload.get(), imageSlice);
    retry->copyTexture(readback.get(), imageSlice, texture.get(), imageSlice);
    EXPECT_FALSE(retry->commandRecordingFailed());
    retry->close();
    ASSERT_TRUE(retry->hasCommandBuffer());
    CommandList* const retryLists[] = { retry.get() };
    const QueueSubmissionToken retryToken = device().executeCommandLists(
        retryLists,
        LengthOf(retryLists),
        retry->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(retryToken.valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(StagingTextureProvenanceTest, ImmutableQueueAdmissionRejectsMutationAndForgedActiveLease){
    const GpuPhysicalQueueInfo* const primaryGraphics = device().getPhysicalQueueInfo(
        device().getPrimaryPhysicalQueue(CommandQueue::Graphics)
    );
    ASSERT_NE(primaryGraphics, nullptr);
    const GpuPhysicalQueueTopology topology = device().getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* leaseTarget = nullptr;
    const GpuPhysicalQueueInfo* differentFamilyTarget = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id != primaryGraphics->id
            && (candidate.capabilities & GpuQueueCapability::Transfer) != GpuQueueCapability::None
        ){
            if(!leaseTarget)
                leaseTarget = &candidate;
            if(candidate.familyIndex != primaryGraphics->familyIndex && !differentFamilyTarget)
                differentFamilyTarget = &candidate;
        }
    }
    if(!leaseTarget)
        GTEST_SKIP() << "Staging texture provenance: no distinct transfer-capable exact queue.";

    TextureDesc sharedDesc = arrayTextureDesc();
    sharedDesc.queueSharing = ResourceQueueSharing::GraphicsAsyncComputeAndTransfer;
    TextureHandle sharedTexture = device().createTexture(sharedDesc);
    StagingTextureHandle sharedUpload = device().createStagingTexture(sharedDesc, CpuAccessMode::Write);

    TextureDesc renderTargetDesc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setInRenderTarget(true)
    ;
    TextureHandle renderTarget = device().createTexture(renderTargetDesc);
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(renderTarget.get())
    );
    ASSERT_TRUE(sharedTexture);
    ASSERT_TRUE(sharedUpload);
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(framebuffer);

    {
        SCOPED_TRACE("opened Graphics lease with forged distinct exact queue descriptor");
        __hidden_staging_texture_provenance_tests::ExpectAtomicCopyRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *sharedUpload,
            *sharedTexture,
            [&](CommandList& commandList){
                CommandListParameters& publicDesc = const_cast<CommandListParameters&>(commandList.getDescription());
                const GpuPhysicalQueueId graphicsQueue = publicDesc.physicalQueue;
                const CommandQueue::Enum graphicsQueueClass = publicDesc.queueType;
                publicDesc.physicalQueue = leaseTarget->id;
                publicDesc.queueType = leaseTarget->queueClass;
                commandList.copyTexture(
                    sharedTexture.get(),
                    TextureSlice{},
                    sharedUpload.get(),
                    TextureSlice{}
                );
                publicDesc.physicalQueue = graphicsQueue;
                publicDesc.queueType = graphicsQueueClass;
            }
        );
    }

    if(!differentFamilyTarget)
        return;

    const TextureDesc exclusiveDesc = arrayTextureDesc();
    StagingTextureHandle exclusiveUpload = device().createStagingTexture(exclusiveDesc, CpuAccessMode::Write);
    ASSERT_TRUE(exclusiveUpload);
    CommandListParameters transferParameters;
    transferParameters.setPhysicalQueue(differentFamilyTarget->id);
    CommandListHandle transferList = device().createCommandList(transferParameters);
    ASSERT_TRUE(transferList);
    const u32 stagingReferences = exclusiveUpload->getReferenceCount();
    const u32 textureReferences = sharedTexture->getReferenceCount();

    transferList->open();
    TextureDesc& publicDesc = const_cast<TextureDesc&>(exclusiveUpload->getDescription());
    publicDesc.queueSharing = ResourceQueueSharing::GraphicsAsyncComputeAndTransfer;
    transferList->copyTexture(sharedTexture.get(), TextureSlice{}, exclusiveUpload.get(), TextureSlice{});
    publicDesc.queueSharing = ResourceQueueSharing::Exclusive;
    EXPECT_TRUE(transferList->commandRecordingFailed());
    EXPECT_FALSE(transferList->hasExplicitTextureSubresourceState(sharedTexture.get(), 0u, 0u));
    EXPECT_EQ(exclusiveUpload->getReferenceCount(), stagingReferences);
    EXPECT_EQ(sharedTexture->getReferenceCount(), textureReferences);
    transferList->close();
    EXPECT_FALSE(transferList->hasCommandBuffer());

    CommandListHandle retry = device().createCommandList();
    ASSERT_TRUE(retry);
    retry->open();
    retry->copyTexture(sharedTexture.get(), TextureSlice{}, exclusiveUpload.get(), TextureSlice{});
    EXPECT_FALSE(retry->commandRecordingFailed());
    retry->close();
    ASSERT_TRUE(retry->hasCommandBuffer());
    CommandList* const retryLists[] = { retry.get() };
    const QueueSubmissionToken retryToken = device().executeCommandLists(
        retryLists,
        LengthOf(retryLists),
        retry->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(retryToken.valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(StagingTextureProvenanceTest, ConcurrentSharingRequiresImmutableQueueClassAsWellAsFamily){
    const GpuPhysicalQueueTopology topology = device().getPhysicalQueueTopology();
    const ResourceQueueSharing::Mask masks[] = {
        ResourceQueueSharing::GraphicsAndAsyncCompute,
        ResourceQueueSharing::GraphicsAndTransfer,
        ResourceQueueSharing::AsyncComputeAndTransfer,
    };
    ResourceQueueSharing::Mask selectedMask = ResourceQueueSharing::Exclusive;
    const GpuPhysicalQueueInfo* unmaskedQueue = nullptr;
    for(const ResourceQueueSharing::Mask mask : masks){
        usize uniqueFamilyCount = 0u;
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
            if(!GraphicsBackend::VulkanDetail::StagingTextureSharingIncludesQueueClass(mask, queue.queueClass))
                continue;
            bool firstInFamily = true;
            for(usize earlier = 0u; earlier < queueIndex; ++earlier){
                const GpuPhysicalQueueInfo& previous = topology.queues[earlier];
                if(
                    previous.familyIndex == queue.familyIndex
                    && GraphicsBackend::VulkanDetail::StagingTextureSharingIncludesQueueClass(
                        mask,
                        previous.queueClass
                    )
                ){
                    firstInFamily = false;
                    break;
                }
            }
            if(firstInFamily)
                ++uniqueFamilyCount;
        }
        if(uniqueFamilyCount < 2u)
            continue;

        for(usize candidateIndex = 0u; candidateIndex < topology.queueCount; ++candidateIndex){
            const GpuPhysicalQueueInfo& candidate = topology.queues[candidateIndex];
            if(GraphicsBackend::VulkanDetail::StagingTextureSharingIncludesQueueClass(mask, candidate.queueClass))
                continue;
            for(usize admittedIndex = 0u; admittedIndex < topology.queueCount; ++admittedIndex){
                const GpuPhysicalQueueInfo& admitted = topology.queues[admittedIndex];
                if(
                    admitted.familyIndex == candidate.familyIndex
                    && GraphicsBackend::VulkanDetail::StagingTextureSharingIncludesQueueClass(
                        mask,
                        admitted.queueClass
                    )
                ){
                    selectedMask = mask;
                    unmaskedQueue = &candidate;
                    break;
                }
            }
            if(unmaskedQueue)
                break;
        }
        if(unmaskedQueue)
            break;
    }
    if(!unmaskedQueue)
        GTEST_SKIP() << "Staging texture provenance: topology has no concurrent unmasked class in an admitted family.";

    TextureDesc stagingDesc = arrayTextureDesc();
    stagingDesc.queueSharing = selectedMask;
    TextureDesc imageDesc = arrayTextureDesc();
    imageDesc.queueSharing = ResourceQueueSharing::GraphicsAsyncComputeAndTransfer;
    StagingTextureHandle upload = device().createStagingTexture(stagingDesc, CpuAccessMode::Write);
    TextureHandle texture = device().createTexture(imageDesc);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(texture);
#if !defined(NWB_FINAL)
    EXPECT_EQ(upload->getNativeQueueFamilySharingModeForTesting(), VK_SHARING_MODE_CONCURRENT);
    EXPECT_GE(upload->getAdmittedQueueFamilyCountForTesting(), 2u);
#endif

    CommandListParameters parameters;
    parameters.setPhysicalQueue(unmaskedQueue->id);
    CommandListHandle commandList = device().createCommandList(parameters);
    ASSERT_TRUE(commandList);
    const u32 stagingReferences = upload->getReferenceCount();
    const u32 textureReferences = texture->getReferenceCount();
    commandList->open();
    commandList->copyTexture(texture.get(), TextureSlice{}, upload.get(), TextureSlice{});
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    EXPECT_EQ(upload->getReferenceCount(), stagingReferences);
    EXPECT_EQ(texture->getReferenceCount(), textureReferences);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
}


TEST_F(StagingTextureProvenanceTest, OneFamilyNonzeroSharingCollapsesAndAllowsAnExactSiblingQueue){
    const GpuPhysicalQueueInfo* const primaryGraphics = device().getPhysicalQueueInfo(
        device().getPrimaryPhysicalQueue(CommandQueue::Graphics)
    );
    ASSERT_NE(primaryGraphics, nullptr);
    const GpuPhysicalQueueTopology topology = device().getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* siblingQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id != primaryGraphics->id
            && candidate.familyIndex == primaryGraphics->familyIndex
            && (candidate.capabilities & GpuQueueCapability::Transfer) != GpuQueueCapability::None
        ){
            siblingQueue = &candidate;
            break;
        }
    }
    if(!siblingQueue)
        GTEST_SKIP() << "Staging texture provenance: no exact sibling queue in the Graphics family.";

    TextureDesc stagingDesc = arrayTextureDesc();
    stagingDesc.queueSharing = ResourceQueueSharing::Graphics;
    TextureDesc imageDesc = arrayTextureDesc();
    imageDesc.queueSharing = ResourceQueueSharing::GraphicsAsyncComputeAndTransfer;
    StagingTextureHandle upload = device().createStagingTexture(stagingDesc, CpuAccessMode::Write);
    TextureHandle texture = device().createTexture(imageDesc);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(texture);
#if !defined(NWB_FINAL)
    EXPECT_EQ(upload->getNativeQueueFamilySharingModeForTesting(), VK_SHARING_MODE_EXCLUSIVE);
    ASSERT_EQ(upload->getAdmittedQueueFamilyCountForTesting(), 1u);
    EXPECT_EQ(upload->getAdmittedQueueFamilyForTesting(0u), primaryGraphics->familyIndex);
    EXPECT_EQ(upload->getAdmittedQueueFamilyForTesting(1u), Limit<u32>::s_Max);
#endif

    CommandListParameters siblingParameters;
    siblingParameters.setPhysicalQueue(siblingQueue->id);
    CommandListHandle commandList = device().createCommandList(siblingParameters);
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->copyTexture(texture.get(), TextureSlice{}, upload.get(), TextureSlice{});
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        siblingQueue->id,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(token.valid());
    EXPECT_TRUE(device().waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

