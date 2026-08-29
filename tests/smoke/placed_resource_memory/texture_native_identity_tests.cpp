// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Allocator-scoped native Texture identity and lifecycle coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/vulkan_test_sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_texture_native_identity{


class CallerOwnedVulkanImage final : NoCopy{
private:
    [[nodiscard]] static Object encode(const VkImage image)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
        return Object(static_cast<void*>(image));
#else
        return Object(static_cast<u64>(image));
#endif
    }


public:
    CallerOwnedVulkanImage(
        GraphicsBackend::Device& device,
        const u32 width,
        const u32 height,
        const VkFormat format,
        const VkImageUsageFlags usage
    )
        : m_context(VulkanTestDeviceProbe::capture(device))
    {
        if(
            !m_context.valid()
            || width == 0u
            || height == 0u
            || format == VK_FORMAT_UNDEFINED
            || usage == 0u
            || !vkCreateImage
            || !vkDestroyImage
            || !vkGetImageMemoryRequirements2
            || !vkAllocateMemory
            || !vkFreeMemory
            || !vkBindImageMemory
        )
            return;

        const VkImageCreateInfo imageInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0u,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = VkExtent3D{ width, height, 1u },
            .mipLevels = 1u,
            .arrayLayers = 1u,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0u,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        if(vkCreateImage(m_context.device, &imageInfo, m_context.allocationCallbacks, &m_image) != VK_SUCCESS){
            m_image = VK_NULL_HANDLE;
            return;
        }

        VkMemoryDedicatedRequirements dedicatedRequirements{
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
            .pNext = nullptr,
            .prefersDedicatedAllocation = VK_FALSE,
            .requiresDedicatedAllocation = VK_FALSE,
        };
        VkMemoryRequirements2 memoryRequirements{
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            .pNext = &dedicatedRequirements,
            .memoryRequirements = {},
        };
        const VkImageMemoryRequirementsInfo2 requirementsInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .pNext = nullptr,
            .image = m_image,
        };
        vkGetImageMemoryRequirements2(m_context.device, &requirementsInfo, &memoryRequirements);
        u32 memoryTypeIndex = VK_MAX_MEMORY_TYPES;
        for(u32 candidateIndex = 0u; candidateIndex < VK_MAX_MEMORY_TYPES; ++candidateIndex){
            if((memoryRequirements.memoryRequirements.memoryTypeBits & (1u << candidateIndex)) != 0u){
                memoryTypeIndex = candidateIndex;
                break;
            }
        }
        if(memoryTypeIndex == VK_MAX_MEMORY_TYPES)
            return;

        const VkMemoryDedicatedAllocateInfo dedicatedAllocationInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext = nullptr,
            .image = m_image,
            .buffer = VK_NULL_HANDLE,
        };
        const VkMemoryAllocateInfo allocationInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = dedicatedRequirements.requiresDedicatedAllocation == VK_TRUE
                ? &dedicatedAllocationInfo
                : nullptr,
            .allocationSize = memoryRequirements.memoryRequirements.size,
            .memoryTypeIndex = memoryTypeIndex,
        };
        if(vkAllocateMemory(
            m_context.device,
            &allocationInfo,
            m_context.allocationCallbacks,
            &m_memory
        ) != VK_SUCCESS){
            m_memory = VK_NULL_HANDLE;
            return;
        }
        if(vkBindImageMemory(m_context.device, m_image, m_memory, 0u) != VK_SUCCESS)
            return;
        m_bound = true;
    }
    ~CallerOwnedVulkanImage(){
        if(m_image != VK_NULL_HANDLE){
            vkDestroyImage(m_context.device, m_image, m_context.allocationCallbacks);
            m_image = VK_NULL_HANDLE;
        }
        if(m_memory != VK_NULL_HANDLE){
            vkFreeMemory(m_context.device, m_memory, m_context.allocationCallbacks);
            m_memory = VK_NULL_HANDLE;
        }
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_bound; }
    [[nodiscard]] Object nativeHandle()const noexcept{
        return valid() ? encode(m_image) : Object(u64{0u});
    }


private:
    VulkanTestDeviceContext m_context;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    bool m_bound = false;
};


};


class ConcurrentDiscardLogger final : public Core::Common::ILogger{
private:
    using LogArena = Core::Common::LogArena;
    using LogString = Core::Common::LogString;


public:
    virtual LogArena& arena()override{
        thread_local LogArena threadArena(Name("tests/texture_native_identity/concurrent_logger"));
        return threadArena;
    }
    virtual void enqueue(LogString&&, Core::Common::LogType::Enum)override{}
    virtual void enqueue(const LogString&, Core::Common::LogType::Enum)override{}
};


class TextureNativeIdentityTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Texture native identity: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled texture native-identity smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool TextureNativeIdentityTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> TextureNativeIdentityTest::s_scope;
Optional<CapturingLogger> TextureNativeIdentityTest::s_logger;
Optional<Common::LoggerRegistrationGuard> TextureNativeIdentityTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextureNativeIdentityTest, ManagedOrdinaryAndVirtualTexturesKeepCanonicalNativeOwners){
    auto& device = TextureNativeIdentityTest::device();
    EXPECT_FALSE(device.isTextureReadyForGpuUse(nullptr));
    constexpr VkImageUsageFlags s_ManagedNativeUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ;

    const TextureDesc ordinaryDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureHandle ordinary = device.createTexture(ordinaryDesc);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(device.isTextureReadyForGpuUse(ordinary.get()));
    const Object ordinaryNativeImage = ordinary->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Image);
    ASSERT_NE(ordinaryNativeImage.integer, 0u);
    const u32 ordinaryReferences = ordinary->getReferenceCount();

    TextureHandle ordinaryDuplicate = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        ordinaryNativeImage,
        ordinaryDesc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_ManagedNativeUsage }
    );
    EXPECT_FALSE(ordinaryDuplicate);
    EXPECT_EQ(ordinary->getReferenceCount(), ordinaryReferences);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(ordinary.get()));

    TextureDesc virtualDesc = ordinaryDesc;
    virtualDesc.isVirtual = true;
    TextureHandle placed = device.createTexture(virtualDesc);
    ASSERT_TRUE(placed);
    ASSERT_FALSE(device.isTextureReadyForGpuUse(placed.get()));
    const Object virtualNativeImage = placed->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Image);
    ASSERT_NE(virtualNativeImage.integer, 0u);

    TextureHandle virtualDuplicate = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        virtualNativeImage,
        virtualDesc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_ManagedNativeUsage }
    );
    EXPECT_FALSE(virtualDuplicate);
    EXPECT_FALSE(device.isTextureReadyForGpuUse(placed.get()));

    const MemoryRequirements requirements = device.getTextureMemoryRequirements(placed.get());
    ASSERT_GT(requirements.size, 0u);
    HeapHandle heap = device.createHeap(HeapDesc{
        .capacity = requirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/texture_native_identity/virtual_heap"),
    });
    ASSERT_TRUE(heap);
    if(!device.bindTextureMemory(placed.get(), heap.get(), 0u))
        GTEST_SKIP() << "Texture native identity: DeviceLocal heap is incompatible with virtual textures.";
    EXPECT_TRUE(device.isTextureReadyForGpuUse(placed.get()));
}


TEST_F(TextureNativeIdentityTest, CreationRejectsUnknownQueueSharingBeforeAllocationOrNativeIdentity){
    auto& device = TextureNativeIdentityTest::device();
    constexpr VkImageUsageFlags s_NativeUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };
    constexpr u8 s_UnknownQueueSharingBit = 1u << 7u;
    constexpr ResourceQueueSharing::Mask s_UnknownQueueSharing =
        static_cast<ResourceQueueSharing::Mask>(s_UnknownQueueSharingBit);
    constexpr ResourceQueueSharing::Mask s_MixedQueueSharing = static_cast<ResourceQueueSharing::Mask>(
        static_cast<u8>(ResourceQueueSharing::GraphicsAndTransfer) | s_UnknownQueueSharingBit
    );
    const TextureDesc baseDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;

    TextureDesc managedDesc = baseDesc;
    managedDesc.setQueueSharing(s_UnknownQueueSharing);
    expectDiagnosticRejection([&](){ return device.createTexture(managedDesc).get() != nullptr; });

    TextureDesc virtualDesc = baseDesc;
    virtualDesc.setQueueSharing(s_MixedQueueSharing);
    virtualDesc.isVirtual = true;
    expectDiagnosticRejection([&](){ return device.createTexture(virtualDesc).get() != nullptr; });

    const Object nativeImage(static_cast<u64>(0x22d0000fu));
    TextureDesc invalidNativeDesc = baseDesc;
    invalidNativeDesc.setQueueSharing(s_MixedQueueSharing);
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            nativeImage,
            invalidNativeDesc,
            GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
        ).get() != nullptr;
    });

    TextureHandle retry = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        baseDesc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
    );
    ASSERT_TRUE(retry);
    EXPECT_EQ(retry->getCreationDescription().queueSharing, ResourceQueueSharing::Exclusive);
    EXPECT_TRUE(retry->descriptionMatchesCreation());
}


TEST_F(TextureNativeIdentityTest, ConcurrentUnmanagedDuplicatesChooseOneOwnerAndReleaseAllowsRewrap){
    auto& device = TextureNativeIdentityTest::device();
    static constexpr u32 s_WorkerCount = 8u;
    constexpr VkImageUsageFlags s_NativeUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ;
    const TextureDesc desc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Unknown)
    ;
    __hidden_texture_native_identity::CallerOwnedVulkanImage nativeOwner(
        device,
        desc.width,
        desc.height,
        VK_FORMAT_R8G8B8A8_UNORM,
        s_NativeUsage
    );
    if(!nativeOwner.valid())
        GTEST_SKIP() << "Texture native identity: unable to allocate a caller-owned Vulkan image.";
    const Object nativeImage = nativeOwner.nativeHandle();
    ASSERT_NE(nativeImage, nullptr);

    Latch workersReady(s_WorkerCount);
    TextureHandle wrappers[s_WorkerCount];
    Thread workers[s_WorkerCount];
    ConcurrentDiscardLogger concurrentLogger;
    {
        Common::LoggerRegistrationGuard concurrentLoggerGuard(concurrentLogger);
        for(u32 workerIndex = 0u; workerIndex < s_WorkerCount; ++workerIndex){
            workers[workerIndex] = Thread([&, workerIndex](){
                workersReady.count_down();
                workersReady.wait();
                wrappers[workerIndex] = device.createHandleForNativeTexture(
                    GraphicsBackend::ObjectTypes::VK_Image,
                    nativeImage,
                    desc,
                    GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
                );
            });
        }
        for(Thread& worker : workers)
            worker.join();
    }

    u32 ownerCount = 0u;
    Texture* canonicalOwner = nullptr;
    for(const TextureHandle& wrapper : wrappers){
        if(wrapper){
            ++ownerCount;
            canonicalOwner = wrapper.get();
        }
    }
    ASSERT_EQ(ownerCount, 1u);
    ASSERT_NE(canonicalOwner, nullptr);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(canonicalOwner));
    EXPECT_TRUE(device.isTextureReadyForGpuUse(canonicalOwner, s_NativeUsage));
    EXPECT_FALSE(device.isTextureReadyForGpuUse(canonicalOwner, VK_IMAGE_USAGE_STORAGE_BIT));
    EXPECT_FALSE(device.isTextureReadyForGpuUse(canonicalOwner, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
    EXPECT_EQ(
        canonicalOwner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Image).integer,
        nativeImage.integer
    );

    TextureHandle blockedDuplicate = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
    );
    EXPECT_FALSE(blockedDuplicate);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(canonicalOwner));

    for(TextureHandle& wrapper : wrappers)
        wrapper.reset();

    TextureHandle rewrapped = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
    );
    ASSERT_TRUE(rewrapped);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(rewrapped.get()));
    EXPECT_TRUE(device.isTextureReadyForGpuUse(rewrapped.get(), s_NativeUsage));
    rewrapped.reset();

    TextureHandle secondRewrap = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
    );
    ASSERT_TRUE(secondRewrap);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(secondRewrap.get()));
    secondRewrap.reset();
}


TEST_F(TextureNativeIdentityTest, IdenticalNativeBitsRemainIndependentAcrossAllocators){
    auto& device = TextureNativeIdentityTest::device();
    constexpr VkImageUsageFlags s_NativeUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
    HeadlessGraphicsScope foreignScope;
    if(!foreignScope.initialize())
        GTEST_SKIP() << "Texture native identity: second validation-backed headless device is unavailable.";
    auto& foreignDevice = foreignScope.graphics().getDevice();

    const Object nativeImage(static_cast<u64>(0x22d00003u));
    const TextureDesc desc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureHandle localWrapper = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
    );
    TextureHandle foreignWrapper = foreignDevice.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_NativeUsage }
    );
    ASSERT_TRUE(localWrapper);
    ASSERT_TRUE(foreignWrapper);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(localWrapper.get()));
    EXPECT_FALSE(device.isTextureReadyForGpuUse(foreignWrapper.get()));
    EXPECT_TRUE(foreignDevice.isTextureReadyForGpuUse(foreignWrapper.get()));
    EXPECT_FALSE(foreignDevice.isTextureReadyForGpuUse(localWrapper.get()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

