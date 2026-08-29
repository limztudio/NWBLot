// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Immutable native VkImage provenance and exact queue-admission coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/graphics/task_graph/task_graph.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_texture_native_provenance{


struct NativeQueueFamilySet{
    Vector<u32, Alloc::ScratchArena> familyIndices;


    explicit NativeQueueFamilySet(Alloc::ScratchArena& scratchArena)
        : familyIndices(scratchArena)
    {}


    void append(const u32 familyIndex){
        for(const u32 existingFamilyIndex : familyIndices){
            if(existingFamilyIndex == familyIndex)
                return;
        }
        familyIndices.push_back(familyIndex);
    }
    [[nodiscard]] VkSharingMode sharingMode()const noexcept{
        return familyIndices.size() >= 2u ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    }
    [[nodiscard]] u32 size()const noexcept{
        NWB_ASSERT(familyIndices.size() <= Limit<u32>::s_Max);
        return static_cast<u32>(familyIndices.size());
    }
    [[nodiscard]] const u32* data()const noexcept{
        return familyIndices.empty() ? nullptr : familyIndices.data();
    }
};


static void GatherQueueFamilies(
    GraphicsBackend::Device& device,
    const ResourceQueueSharing::Mask sharing,
    NativeQueueFamilySet& result
){
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    result.familyIndices.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(ResourceQueueSharing::IncludesQueueClass(sharing, queue.queueClass))
            result.append(queue.familyIndex);
    }
}


};


class TextureNativeProvenanceTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Texture native provenance: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled texture native-provenance smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }
    [[nodiscard]] static Alloc::GlobalArena& arena(){
        return s_scope->arena();
    }


protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool TextureNativeProvenanceTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> TextureNativeProvenanceTest::s_scope;
Optional<CapturingLogger> TextureNativeProvenanceTest::s_logger;
Optional<Common::LoggerRegistrationGuard> TextureNativeProvenanceTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextureNativeProvenanceTest, MalformedProvenanceRejectsWithoutTopologySkipsOrIdentityPublication){
    auto& device = TextureNativeProvenanceTest::device();
    constexpr VkImageUsageFlags s_NativeUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
    const TextureDesc exclusiveDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureDesc graphicsDesc = exclusiveDesc;
    graphicsDesc.setQueueSharing(ResourceQueueSharing::Graphics);
    const Object nativeImage(static_cast<u64>(0x22d00100u));
    const u32 queueFamilyIndex = 0u;
    const Array<u32, 2u> queueFamilies = { 0u, 1u };
    const Array<u32, 2u> duplicateQueueFamilies = { 0u, 0u };
    const Array<u32, 2u> outOfRangeQueueFamilies = { 0u, VK_QUEUE_FAMILY_IGNORED };
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            exclusiveDesc,
            GraphicsBackend::NativeTextureProvenance{}
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            graphicsDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .sharingMode = static_cast<VkSharingMode>(VK_SHARING_MODE_MAX_ENUM),
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            exclusiveDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .queueFamilyIndexCount = 1u,
                .queueFamilyIndices = &queueFamilyIndex,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            graphicsDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = 1u,
                .queueFamilyIndices = &queueFamilyIndex,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            graphicsDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = 2u,
                .queueFamilyIndices = nullptr,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            exclusiveDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = static_cast<u32>(queueFamilies.size()),
                .queueFamilyIndices = queueFamilies.data(),
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            graphicsDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = static_cast<u32>(duplicateQueueFamilies.size()),
                .queueFamilyIndices = duplicateQueueFamilies.data(),
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            graphicsDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = static_cast<u32>(outOfRangeQueueFamilies.size()),
                .queueFamilyIndices = outOfRangeQueueFamilies.data(),
            }
        ).get() != nullptr;
    });

    TextureHandle retry = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        exclusiveDesc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
    );
    ASSERT_TRUE(retry);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(retry.get(), s_NativeUsage));
}


TEST_F(TextureNativeProvenanceTest, ImageCreateFlagsRejectContradictionsAndAcceptExactCapabilities){
    auto& device = TextureNativeProvenanceTest::device();
    constexpr VkImageUsageFlags s_NativeUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
    const TextureDesc cubeDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setArraySize(6u)
        .setFormat(Format::RGBA8_UNORM)
        .setDimension(TextureDimension::TextureCube)
        .setInitialState(ResourceStates::Common)
    ;
    const Object cubeImage(static_cast<u64>(0x22d00101u));
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            cubeImage,
            cubeDesc,
            GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            cubeImage,
            cubeDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            cubeImage,
            cubeDesc,
            GraphicsBackend::NativeTextureProvenance{
                .usage = s_NativeUsage,
                .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_PROTECTED_BIT,
            }
        ).get() != nullptr;
    });

    TextureHandle cube = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        cubeImage,
        cubeDesc,
        GraphicsBackend::NativeTextureProvenance{
            .usage = s_NativeUsage,
            .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        }
    );
    ASSERT_TRUE(cube);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(cube.get(), s_NativeUsage));

    const TextureDesc mutableDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureHandle mutableImage = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        Object(static_cast<u64>(0x22d00102u)),
        mutableDesc,
        GraphicsBackend::NativeTextureProvenance{
            .usage = s_NativeUsage,
            .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        }
    );
    ASSERT_TRUE(mutableImage);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(mutableImage.get(), s_NativeUsage));
}


TEST_F(TextureNativeProvenanceTest, InitialStateKnowledgeSurvivesTypedTaskGraphImport){
    auto& device = TextureNativeProvenanceTest::device();
    constexpr VkImageUsageFlags s_NativeUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
    const TextureDesc desc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::ShaderResource)
        .setKeepInitialState(true)
    ;
    TextureHandle unknownState = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        Object(static_cast<u64>(0x22d00104u)),
        desc,
        GraphicsBackend::NativeTextureProvenance{
            .usage = s_NativeUsage,
            .initialStateKnown = false,
        }
    );
    TextureHandle knownState = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        Object(static_cast<u64>(0x22d00105u)),
        desc,
        GraphicsBackend::NativeTextureProvenance{
            .usage = s_NativeUsage,
            .initialStateKnown = true,
        }
    );
    ASSERT_TRUE(unknownState);
    ASSERT_TRUE(knownState);

    GpuTaskGraph graph(TextureNativeProvenanceTest::arena());
    const GpuGraphResourceId unknownResource = graph.importTexture(
        unknownState,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/texture_native_provenance/unknown_initial_state"))
            .setMarkerLabel("Unknown Native Initial State")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId knownResource = graph.importTexture(
        knownState,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/texture_native_provenance/known_initial_state"))
            .setMarkerLabel("Known Native Initial State")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(unknownResource.valid());
    ASSERT_TRUE(knownResource.valid());
    EXPECT_EQ(graph.resourceAt(unknownResource.index).initialState, ResourceStates::Unknown);
    EXPECT_EQ(graph.resourceAt(knownResource.index).initialState, ResourceStates::ShaderResource);
}


TEST_F(TextureNativeProvenanceTest, ConcurrentNativeSharingCopiesFamiliesAndDoesNotWidenLogicalQueueClasses){
    auto& device = TextureNativeProvenanceTest::device();
    Alloc::ScratchArena scratchArena(Name("tests/texture_native_provenance/queue_families"));
    __hidden_texture_native_provenance::NativeQueueFamilySet nativeQueueFamilies(scratchArena);
    __hidden_texture_native_provenance::GatherQueueFamilies(
        device,
        ResourceQueueSharing::Graphics,
        nativeQueueFamilies
    );
    ASSERT_GT(nativeQueueFamilies.size(), 0u);
    const u32 logicalFamilyCount = nativeQueueFamilies.size();
    const bool logicalSharingUsesExclusiveOwnership = logicalFamilyCount == 1u;
    const TextureDesc desc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Graphics)
    ;
    const Object nativeImage(static_cast<u64>(0x22d00103u));
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    if(logicalFamilyCount > 1u){
        expectDiagnosticRejection([&](){
            return device.createHandleForNativeTexture(
                GraphicsBackend::ObjectTypes::VK_Image,
                nativeImage,
                desc,
                GraphicsBackend::NativeTextureProvenance{ .usage = VK_IMAGE_USAGE_SAMPLED_BIT }
            ).get() != nullptr;
        });
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* unadmittedQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.queueClass == CommandQueue::Graphics)
            continue;
        bool familyAlreadyPresent = false;
        for(const u32 familyIndex : nativeQueueFamilies.familyIndices){
            if(familyIndex == candidate.familyIndex){
                familyAlreadyPresent = true;
                break;
            }
        }
        if(!familyAlreadyPresent){
            unadmittedQueue = &candidate;
            break;
        }
    }
    if(logicalFamilyCount >= 3u || (logicalFamilyCount == 2u && unadmittedQueue)){
        __hidden_texture_native_provenance::NativeQueueFamilySet omittedQueueFamilies(scratchArena);
        omittedQueueFamilies.familyIndices.reserve(logicalFamilyCount);
        for(u32 familyIndex = 0u; familyIndex + 1u < logicalFamilyCount; ++familyIndex)
            omittedQueueFamilies.append(nativeQueueFamilies.familyIndices[familyIndex]);
        if(logicalFamilyCount == 2u)
            omittedQueueFamilies.append(unadmittedQueue->familyIndex);
        const u32 expectedOmittedCount = logicalFamilyCount >= 3u ? logicalFamilyCount - 1u : logicalFamilyCount;
        ASSERT_EQ(omittedQueueFamilies.size(), expectedOmittedCount);
        ASSERT_EQ(omittedQueueFamilies.sharingMode(), VK_SHARING_MODE_CONCURRENT);
        expectDiagnosticRejection([&](){
            return device.createHandleForNativeTexture(
                GraphicsBackend::ObjectTypes::VK_Image,
                nativeImage,
                desc,
                GraphicsBackend::NativeTextureProvenance{
                    .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                    .sharingMode = VK_SHARING_MODE_CONCURRENT,
                    .queueFamilyIndexCount = omittedQueueFamilies.size(),
                    .queueFamilyIndices = omittedQueueFamilies.data(),
                }
            ).get() != nullptr;
        });
    }
    if(unadmittedQueue)
        nativeQueueFamilies.append(unadmittedQueue->familyIndex);
    if(nativeQueueFamilies.sharingMode() != VK_SHARING_MODE_CONCURRENT)
        GTEST_SKIP() << "Texture native provenance: no second queue family is available for concurrent import.";

    TextureHandle texture = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_CONCURRENT,
            .queueFamilyIndexCount = nativeQueueFamilies.size(),
            .queueFamilyIndices = nativeQueueFamilies.data(),
        }
    );
    ASSERT_TRUE(texture);

    for(u32& familyIndex : nativeQueueFamilies.familyIndices)
        familyIndex = VK_QUEUE_FAMILY_IGNORED;
    CommandListHandle admittedList = device.createCommandList();
    ASSERT_TRUE(admittedList);
    admittedList->open();
    admittedList->setEnableUavBarriersForTexture(texture.get(), false);
    EXPECT_FALSE(admittedList->commandRecordingFailed());
    admittedList->close();

    if(logicalSharingUsesExclusiveOwnership){
        CommandListHandle ownershipList = device.createCommandList();
        ASSERT_TRUE(ownershipList);
        ownershipList->open();
        ownershipList->beginTrackingTextureState(texture.get(), s_AllSubresources, ResourceStates::Common);
        ASSERT_FALSE(ownershipList->commandRecordingFailed());
        ASSERT_TRUE(ownershipList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
        const u32 ownershipTextureReferences = texture->getReferenceCount();
        ownershipList->releaseTextureOwnership(
            texture.get(),
            s_AllSubresources,
            ownershipList->getDescription().physicalQueue
        );
        EXPECT_TRUE(ownershipList->commandRecordingFailed());
        EXPECT_EQ(texture->getReferenceCount(), ownershipTextureReferences);
        ownershipList->close();
        EXPECT_FALSE(ownershipList->hasCommandBuffer());
    }

    if(!unadmittedQueue)
        return;

    CommandListParameters policyParameters;
    policyParameters.setPhysicalQueue(unadmittedQueue->id);
    CommandListHandle policyList = device.createCommandList(policyParameters);
    ASSERT_TRUE(policyList);
    const u32 policyTextureReferences = texture->getReferenceCount();
    policyList->open();
    policyList->setEnableUavBarriersForTexture(texture.get(), false);
    EXPECT_TRUE(policyList->commandRecordingFailed());
    EXPECT_EQ(texture->getReferenceCount(), policyTextureReferences);
    policyList->close();
    EXPECT_FALSE(policyList->hasCommandBuffer());

    CommandListParameters commandParameters;
    commandParameters.setPhysicalQueue(unadmittedQueue->id);
    CommandListHandle commandList = device.createCommandList(commandParameters);
    ASSERT_TRUE(commandList);
    const u32 textureReferences = texture->getReferenceCount();
    commandList->open();
    commandList->beginTrackingTextureState(texture.get(), s_AllSubresources, ResourceStates::Common);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    EXPECT_EQ(texture->getReferenceCount(), textureReferences);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
}


TEST_F(TextureNativeProvenanceTest, CreationDescriptorAndNativeUsageRemainImmutable){
    auto& device = TextureNativeProvenanceTest::device();
    constexpr VkImageUsageFlags s_SampledUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
    const TextureDesc desc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setName(Name("tests/texture_native_provenance/immutable_creation"))
    ;
    TextureHandle managed = device.createTexture(desc);
    ASSERT_TRUE(managed);

    const TextureDesc& creationDesc = managed->getCreationDescription();
    EXPECT_EQ(creationDesc.width, 16u);
    EXPECT_EQ(creationDesc.name, desc.name);
    EXPECT_TRUE(managed->descriptionMatchesCreation());
    EXPECT_TRUE(device.isTextureReadyForGpuUse(managed.get()));
    EXPECT_TRUE(device.isTextureReadyForGpuUse(managed.get(), VK_IMAGE_USAGE_SAMPLED_BIT));
    EXPECT_TRUE(device.isTextureReadyForGpuUse(managed.get(), VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    EXPECT_TRUE(device.isTextureReadyForGpuUse(managed.get(), VK_IMAGE_USAGE_TRANSFER_DST_BIT));

    TextureDesc& mutableDesc = const_cast<TextureDesc&>(managed->getDescription());
    mutableDesc.setWidth(32u);
    EXPECT_EQ(creationDesc.width, 16u);
    EXPECT_FALSE(managed->descriptionMatchesCreation());
    EXPECT_FALSE(device.isTextureReadyForGpuUse(managed.get()));
    const FramebufferInfoEx framebufferInfo(FramebufferDesc().addColorAttachment(managed.get()));
    ASSERT_EQ(framebufferInfo.colorFormats.size(), 1u);
    EXPECT_EQ(framebufferInfo.colorFormats[0u], Format::RGBA8_UNORM);
    EXPECT_EQ(framebufferInfo.width, 16u);
    EXPECT_EQ(framebufferInfo.height, 16u);

    mutableDesc = desc;
    EXPECT_TRUE(managed->descriptionMatchesCreation());
    EXPECT_TRUE(device.isTextureReadyForGpuUse(managed.get()));
    mutableDesc.setKeepInitialState(true);
    EXPECT_FALSE(managed->descriptionMatchesCreation());
    EXPECT_FALSE(device.isTextureReadyForGpuUse(managed.get()));

    mutableDesc = desc;
    mutableDesc.setName(Name("tests/texture_native_provenance/descriptor_drift"));
    EXPECT_FALSE(managed->descriptionMatchesCreation());
    EXPECT_FALSE(device.isTextureReadyForGpuUse(managed.get()));
    mutableDesc = desc;
    EXPECT_TRUE(managed->descriptionMatchesCreation());

    TextureHandle managedUav = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::UnorderedAccess)
    );
    ASSERT_TRUE(managedUav);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(managedUav.get(), VK_IMAGE_USAGE_STORAGE_BIT));

    const auto expectDiagnosticRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };
    TextureDesc invalidUavDesc = desc;
    invalidUavDesc.setInitialState(ResourceStates::UnorderedAccess);
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            Object(static_cast<u64>(0x22d00006u)),
            invalidUavDesc,
            GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage }
        ).get() != nullptr;
    });
    TextureDesc invalidDepthDesc = desc;
    invalidDepthDesc.setInitialState(ResourceStates::DepthRead);
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            Object(static_cast<u64>(0x22d00007u)),
            invalidDepthDesc,
            GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage }
        ).get() != nullptr;
    });
    TextureDesc transferSourceDesc = desc;
    transferSourceDesc.setInitialState(ResourceStates::CopySource);
    const Object transferSourceImage(static_cast<u64>(0x22d00008u));
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            transferSourceImage,
            transferSourceDesc,
            GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage }
        ).get() != nullptr;
    });
    TextureHandle transferSource = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        transferSourceImage,
        transferSourceDesc,
        GraphicsBackend::NativeTextureProvenance{
            .usage = s_SampledUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        }
    );
    ASSERT_TRUE(transferSource);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(transferSource.get(), VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    EXPECT_FALSE(device.isTextureReadyForGpuUse(transferSource.get(), VK_IMAGE_USAGE_TRANSFER_DST_BIT));

    TextureDesc invalidStateDesc = desc;
    invalidStateDesc.setInitialState(ResourceStates::VertexBuffer);
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            Object(static_cast<u64>(0x22d00009u)),
            invalidStateDesc,
            GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage }
        ).get() != nullptr;
    });

    TextureDesc storageDesc = desc;
    storageDesc.setInUAV(true);
    const Object storageImage(static_cast<u64>(0x22d00010u));
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            storageImage,
            storageDesc,
            GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage }
        ).get() != nullptr;
    });
    TextureHandle storage = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        storageImage,
        storageDesc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage | VK_IMAGE_USAGE_STORAGE_BIT }
    );
    ASSERT_TRUE(storage);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(storage.get(), VK_IMAGE_USAGE_STORAGE_BIT));

    const Object zeroUsageImage(static_cast<u64>(0x22d00011u));
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            zeroUsageImage,
            desc,
            GraphicsBackend::NativeTextureProvenance{}
        ).get() != nullptr;
    });
    TextureHandle zeroUsageRetry = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        zeroUsageImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage }
    );
    ASSERT_TRUE(zeroUsageRetry);

    const Object nativeImage(static_cast<u64>(0x22d00005u));
    TextureHandle unmanaged = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_SampledUsage }
    );
    ASSERT_TRUE(unmanaged);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(unmanaged.get()));
    EXPECT_TRUE(device.isTextureReadyForGpuUse(unmanaged.get(), VK_IMAGE_USAGE_SAMPLED_BIT));
    EXPECT_FALSE(device.isTextureReadyForGpuUse(unmanaged.get(), VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    EXPECT_FALSE(device.isTextureReadyForGpuUse(unmanaged.get(), VK_IMAGE_USAGE_TRANSFER_DST_BIT));

    const u32 unmanagedReferences = unmanaged->getReferenceCount();
    CommandListHandle copySourceList = device.createCommandList();
    ASSERT_TRUE(copySourceList);
    copySourceList->open();
    copySourceList->beginTrackingTextureState(unmanaged.get(), s_AllSubresources, ResourceStates::CopySource);
    EXPECT_TRUE(copySourceList->commandRecordingFailed());
    EXPECT_EQ(copySourceList->getTextureSubresourceState(unmanaged.get(), 0u, 0u), ResourceStates::Unknown);
    EXPECT_EQ(unmanaged->getReferenceCount(), unmanagedReferences);
    copySourceList->close();
    EXPECT_FALSE(copySourceList->hasCommandBuffer());

    CommandListHandle copyDestinationList = device.createCommandList();
    ASSERT_TRUE(copyDestinationList);
    copyDestinationList->open();
    copyDestinationList->setTextureState(unmanaged.get(), s_AllSubresources, ResourceStates::CopyDest);
    EXPECT_TRUE(copyDestinationList->commandRecordingFailed());
    EXPECT_EQ(copyDestinationList->getTextureSubresourceState(unmanaged.get(), 0u, 0u), ResourceStates::Unknown);
    EXPECT_EQ(unmanaged->getReferenceCount(), unmanagedReferences);
    copyDestinationList->close();
    EXPECT_FALSE(copyDestinationList->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

