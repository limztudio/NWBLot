// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Allocator-scoped native Texture identity and lifecycle coverage.


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
        ordinaryDesc
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
        virtualDesc
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


TEST_F(TextureNativeIdentityTest, ConcurrentUnmanagedDuplicatesChooseOneOwnerAndReleaseAllowsRewrap){
    auto& device = TextureNativeIdentityTest::device();
    static constexpr u32 s_WorkerCount = 8u;
    const Object nativeImage(static_cast<u64>(0x22d00002u));
    const TextureDesc desc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;

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
                    desc
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
    EXPECT_EQ(
        canonicalOwner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Image).integer,
        nativeImage.integer
    );

    TextureHandle blockedDuplicate = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    EXPECT_FALSE(blockedDuplicate);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(canonicalOwner));

    for(TextureHandle& wrapper : wrappers)
        wrapper.reset();

    TextureHandle rewrapped = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    ASSERT_TRUE(rewrapped);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(rewrapped.get()));
    rewrapped.reset();

    TextureHandle secondRewrap = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    ASSERT_TRUE(secondRewrap);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(secondRewrap.get()));
}


#if !defined(NWB_FINAL)
TEST_F(TextureNativeIdentityTest, RevokedTombstoneReservesIdentityUntilNativeRelease){
    auto& device = TextureNativeIdentityTest::device();
    const Object nativeImage(static_cast<u64>(0x22d00004u));
    const TextureDesc desc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureHandle revokedOwner = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    ASSERT_TRUE(revokedOwner);
    ASSERT_TRUE(device.isTextureReadyForGpuUse(revokedOwner.get()));

    ASSERT_TRUE(device.revokeUnmanagedNativeTextureForTesting(revokedOwner.get(), nativeImage));
    EXPECT_FALSE(device.isTextureReadyForGpuUse(revokedOwner.get()));
    EXPECT_EQ(
        revokedOwner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Image).integer,
        Object(nullptr).integer
    );
    TextureHandle blockedByTombstone = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    EXPECT_FALSE(blockedByTombstone);

    device.releaseRevokedNativeTextureIdentityForTesting(revokedOwner.get(), nativeImage);
    TextureHandle replacement = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    ASSERT_TRUE(replacement);
    ASSERT_TRUE(device.isTextureReadyForGpuUse(replacement.get()));

    revokedOwner.reset();
    TextureHandle duplicateAfterOldDestruction = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    EXPECT_FALSE(duplicateAfterOldDestruction);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(replacement.get()));
}
#endif


TEST_F(TextureNativeIdentityTest, IdenticalNativeBitsRemainIndependentAcrossAllocators){
    auto& device = TextureNativeIdentityTest::device();
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
        desc
    );
    TextureHandle foreignWrapper = foreignDevice.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
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

