// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
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

template<typename Operation>
void ExpectManagerWriteRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
    EXPECT_FALSE(operation());
#endif
}

template<typename Operation>
void ExpectManagerFreeRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({ operation(); }, "");
#else
    operation();
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DescriptorBufferManagerIngressTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize())
            return;

        s_runtimeInitialized = true;
        auto& localDevice = s_scope->graphics().getDevice();
        auto& localManager = localDevice.getDescriptorBufferManager();
        if(!localManager.isEnabled())
            return;

        // Descriptor-manager ingress tests own the manager's segments. The renderer heap is a separate client that may
        // consume an entire device-limited range, so release it before direct manager allocation tests.
        auto& heap = localDevice.getDescriptorHeap();
        heap.shutdown();
        ASSERT_FALSE(heap.isInitialized());
        ASSERT_TRUE(localManager.isEnabled());
        s_ready = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_runtimeInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled descriptor-manager ingress tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeInitialized = false;
        s_ready = false;
    }

    virtual void SetUp()override{
        if(!s_ready)
            GTEST_SKIP() << "Descriptor-manager ingress: no usable descriptor-buffer headless device.";
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static GraphicsBackend::DescriptorBufferManager& manager(){
        return device().getDescriptorBufferManager();
    }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }


protected:
    static bool s_runtimeInitialized;
    static bool s_ready;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool DescriptorBufferManagerIngressTest::s_runtimeInitialized = false;
bool DescriptorBufferManagerIngressTest::s_ready = false;
UniquePtr<HeadlessGraphicsScope> DescriptorBufferManagerIngressTest::s_scope;
Optional<CapturingLogger> DescriptorBufferManagerIngressTest::s_logger;
Optional<Common::LoggerRegistrationGuard> DescriptorBufferManagerIngressTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferManagerIngressTest, RejectsForeignRetaggedAndStaleStorageIdentities){
    auto& localDevice = device();
    auto& localManager = manager();

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    auto& foreignManager = foreignDevice.getDescriptorBufferManager();
    if(!foreignManager.isEnabled())
        GTEST_SKIP() << "Second device has no descriptor-buffer manager.";
    auto& foreignHeap = foreignDevice.getDescriptorHeap();
    foreignHeap.shutdown();
    ASSERT_FALSE(foreignHeap.isInitialized());

    const u32 storageSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_EQ(storageSize, foreignManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
    const auto localBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageSize,
        localManager.getOffsetAlignmentBytes()
    );
    const auto foreignBlock = foreignManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageSize,
        foreignManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(localBlock.valid());
    ASSERT_TRUE(foreignBlock.valid());
    EXPECT_NE(localBlock.storageIdentity, foreignBlock.storageIdentity);
    auto foreignForgery = foreignBlock;
    foreignForgery.storageIdentity = localBlock.storageIdentity;
    auto localForgery = localBlock;
    localForgery.storageIdentity = foreignBlock.storageIdentity;

    const BufferDesc storageDesc = BufferDesc()
        .setByteSize(4096u)
        .setStructStride(16u)
        .setCanHaveUAVs(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    BufferHandle localBuffer = localDevice.createBuffer(storageDesc);
    BufferHandle foreignBuffer = foreignDevice.createBuffer(storageDesc);
    ASSERT_TRUE(localBuffer);
    ASSERT_TRUE(foreignBuffer);
    const DescriptorWriteItem localItem = DescriptorWriteItem::StructuredBuffer_SRV(0u, localBuffer.get());
    const DescriptorWriteItem foreignItem = DescriptorWriteItem::StructuredBuffer_SRV(0u, foreignBuffer.get());

    ExpectManagerFreeRejection([&](){ foreignManager.free(foreignForgery); });
    ExpectManagerWriteRejection([&](){
        return foreignManager.writeDescriptor(
            foreignItem,
            foreignForgery,
            foreignForgery.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
    });
    ExpectManagerFreeRejection([&](){ localManager.free(localForgery); });
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            localItem,
            localForgery,
            localForgery.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        localItem,
        localBlock,
        localBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    ));
    EXPECT_TRUE(foreignManager.writeDescriptor(
        foreignItem,
        foreignBlock,
        foreignBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    ));

    const u32 samplerSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER);
    const auto samplerBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Sampler,
        samplerSize,
        localManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(samplerBlock.valid());
    SamplerHandle sampler = localDevice.createSampler(SamplerDesc());
    ASSERT_TRUE(sampler);

    auto retagged = localBlock;
    retagged.kind = GraphicsBackend::DescriptorBufferSegmentKind::Sampler;
    retagged.offsetBytes = samplerBlock.offsetBytes;
    retagged.sizeBytes = samplerBlock.sizeBytes;
    retagged.allocationSerial = samplerBlock.allocationSerial;
    ASSERT_NE(retagged.storageIdentity, samplerBlock.storageIdentity);
    const DescriptorWriteItem samplerItem = DescriptorWriteItem::Sampler(0u, sampler.get());
    ExpectManagerFreeRejection([&](){ localManager.free(retagged); });
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            samplerItem,
            retagged,
            retagged.offsetBytes,
            VK_DESCRIPTOR_TYPE_SAMPLER
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        samplerItem,
        samplerBlock,
        samplerBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_SAMPLER
    ));

    localManager.free(localBlock);
    const auto replacement = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageSize,
        localManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.offsetBytes, localBlock.offsetBytes);
    EXPECT_EQ(replacement.storageIdentity, localBlock.storageIdentity);
    EXPECT_NE(replacement.allocationSerial, localBlock.allocationSerial);
    ExpectManagerFreeRejection([&](){ localManager.free(localBlock); });
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            localItem,
            localBlock,
            localBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        localItem,
        replacement,
        replacement.offsetBytes,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    ));

    localManager.free(replacement);
    localManager.free(samplerBlock);
    foreignManager.free(foreignBlock);

    const u64 foreignStorageIdentity = foreignBlock.storageIdentity;
    const u64 foreignAllocationSerial = foreignBlock.allocationSerial;
    ASSERT_TRUE(foreignManager.shutdown());
    ASSERT_TRUE(foreignManager.initialize());
    const auto reinitializedBlock = foreignManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageSize,
        foreignManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(reinitializedBlock.valid());
    EXPECT_EQ(reinitializedBlock.storageIdentity, foreignStorageIdentity);
    EXPECT_GT(reinitializedBlock.allocationSerial, foreignAllocationSerial);
    ExpectManagerWriteRejection([&](){
        return foreignManager.writeDescriptor(
            foreignItem,
            foreignBlock,
            foreignBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
    });
    EXPECT_TRUE(foreignManager.writeDescriptor(
        foreignItem,
        reinitializedBlock,
        reinitializedBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    ));
    foreignManager.free(reinitializedBlock);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferManagerIngressTest, RejectsForeignAndUnboundResourcesBeforeLocalRetry){
    auto& localDevice = device();
    auto& localManager = manager();

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();

    const BufferDesc bufferDesc = BufferDesc()
        .setByteSize(4096u)
        .setStructStride(16u)
        .setCanHaveUAVs(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    BufferHandle localBuffer = localDevice.createBuffer(bufferDesc);
    BufferHandle foreignBuffer = foreignDevice.createBuffer(bufferDesc);
    BufferDesc virtualBufferDesc = bufferDesc;
    virtualBufferDesc.setIsVirtual(true);
    BufferHandle virtualBuffer = localDevice.createBuffer(virtualBufferDesc);
    ASSERT_TRUE(localBuffer);
    ASSERT_TRUE(foreignBuffer);
    ASSERT_TRUE(virtualBuffer);

    const u32 storageSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const auto storageBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageSize,
        localManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(storageBlock.valid());
    const u32 foreignBufferReferences = foreignBuffer->getReferenceCount();
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::StructuredBuffer_SRV(0u, foreignBuffer.get()),
            storageBlock,
            storageBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
    });
    EXPECT_EQ(foreignBuffer->getReferenceCount(), foreignBufferReferences);
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::StructuredBuffer_SRV(0u, virtualBuffer.get()),
            storageBlock,
            storageBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        DescriptorWriteItem::StructuredBuffer_SRV(0u, localBuffer.get()),
        storageBlock,
        storageBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    ));

    TextureDesc textureDesc;
    textureDesc
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureHandle localTexture = localDevice.createTexture(textureDesc);
    TextureHandle foreignTexture = foreignDevice.createTexture(textureDesc);
    TextureDesc virtualTextureDesc = textureDesc;
    virtualTextureDesc.isVirtual = true;
    TextureHandle virtualTexture = localDevice.createTexture(virtualTextureDesc);
    ASSERT_TRUE(localTexture);
    ASSERT_TRUE(foreignTexture);
    ASSERT_TRUE(virtualTexture);

    const u32 imageSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    const auto imageBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        imageSize,
        localManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(imageBlock.valid());
    const u32 foreignTextureReferences = foreignTexture->getReferenceCount();
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::Texture_SRV(0u, foreignTexture.get()),
            imageBlock,
            imageBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
        );
    });
    EXPECT_EQ(foreignTexture->getReferenceCount(), foreignTextureReferences);
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::Texture_SRV(0u, virtualTexture.get()),
            imageBlock,
            imageBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        DescriptorWriteItem::Texture_SRV(0u, localTexture.get()),
        imageBlock,
        imageBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    ));

    SamplerHandle localSampler = localDevice.createSampler(SamplerDesc());
    SamplerHandle foreignSampler = foreignDevice.createSampler(SamplerDesc());
    ASSERT_TRUE(localSampler);
    ASSERT_TRUE(foreignSampler);
    const u32 samplerSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER);
    const auto samplerBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Sampler,
        samplerSize,
        localManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(samplerBlock.valid());
    const u32 foreignSamplerReferences = foreignSampler->getReferenceCount();
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::Sampler(0u, foreignSampler.get()),
            samplerBlock,
            samplerBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_SAMPLER
        );
    });
    EXPECT_EQ(foreignSampler->getReferenceCount(), foreignSamplerReferences);
    EXPECT_TRUE(localManager.writeDescriptor(
        DescriptorWriteItem::Sampler(0u, localSampler.get()),
        samplerBlock,
        samplerBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_SAMPLER
    ));

    if(
        localDevice.queryFeatureSupport(Feature::RayTracingAccelStruct)
        && foreignDevice.queryFeatureSupport(Feature::RayTracingAccelStruct)
    ){
        RayTracingAccelStructDesc tlasDesc(arena());
        tlasDesc.setTopLevelMaxInstances(1u);
        RayTracingAccelStructHandle localTlas = localDevice.createAccelStruct(tlasDesc);
        RayTracingAccelStructHandle foreignTlas = foreignDevice.createAccelStruct(tlasDesc);
        RayTracingAccelStructHandle localBlas = localDevice.createAccelStruct(RayTracingAccelStructDesc(arena()));
        ASSERT_TRUE(localTlas && foreignTlas && localBlas);
        const_cast<RayTracingAccelStructDesc&>(localBlas->getDescription()).isTopLevel = true;
        ASSERT_TRUE(localBlas->getDescription().isTopLevel);
        const u32 tlasSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        const auto tlasBlock = localManager.allocate(
            GraphicsBackend::DescriptorBufferSegmentKind::Resource,
            tlasSize,
            localManager.getOffsetAlignmentBytes()
        );
        ASSERT_TRUE(tlasBlock.valid());
        const u32 foreignTlasReferences = foreignTlas->getReferenceCount();
        ExpectManagerWriteRejection([&](){
            return localManager.writeDescriptor(
                DescriptorWriteItem::RayTracingAccelStruct(0u, foreignTlas.get()),
                tlasBlock,
                tlasBlock.offsetBytes,
                VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
            );
        });
        EXPECT_EQ(foreignTlas->getReferenceCount(), foreignTlasReferences);
        ExpectManagerWriteRejection([&](){
            return localManager.writeDescriptor(
                DescriptorWriteItem::RayTracingAccelStruct(0u, localBlas.get()),
                tlasBlock, tlasBlock.offsetBytes, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
            );
        });
        EXPECT_TRUE(localManager.writeDescriptor(
            DescriptorWriteItem::RayTracingAccelStruct(0u, localTlas.get()),
            tlasBlock,
            tlasBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
        ));
        localManager.free(tlasBlock);
    }

    localManager.free(storageBlock);
    localManager.free(imageBlock);
    localManager.free(samplerBlock);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferManagerIngressTest, RejectsMissingUsageCapabilitiesAndInvalidAddresses){
    auto& localDevice = device();
    auto& localManager = manager();

    const u32 uniformSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    const u32 storageSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const u32 texelSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    const u32 sampledImageSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    const u32 storageImageSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    const u32 segmentAlignment = localManager.getOffsetAlignmentBytes();
    const auto uniformBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        uniformSize,
        segmentAlignment
    );
    const auto storageBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageSize,
        segmentAlignment
    );
    const auto texelBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        texelSize,
        segmentAlignment
    );
    const auto sampledImageBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        sampledImageSize,
        segmentAlignment
    );
    const auto storageImageBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageImageSize,
        segmentAlignment
    );
    ASSERT_TRUE(uniformBlock.valid());
    ASSERT_TRUE(storageBlock.valid());
    ASSERT_TRUE(texelBlock.valid());
    ASSERT_TRUE(sampledImageBlock.valid());
    ASSERT_TRUE(storageImageBlock.valid());

    BufferHandle constantBuffer = localDevice.createBuffer(
        BufferDesc().setByteSize(4096u).setIsConstantBuffer(true)
    );
    BufferHandle missingUniform = localDevice.createBuffer(BufferDesc().setByteSize(4096u));
    BufferHandle volatileBuffer = localDevice.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setIsConstantBuffer(true)
            .setIsVolatile(true)
            .setMaxVersions(2u)
            .setCpuAccess(CpuAccessMode::Write)
    );
    ASSERT_TRUE(constantBuffer);
    ASSERT_TRUE(missingUniform);
    ASSERT_TRUE(volatileBuffer);
    const DescriptorWriteItem goodUniform = DescriptorWriteItem::ConstantBuffer(0u, constantBuffer.get());
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::ConstantBuffer(0u, missingUniform.get()),
            uniformBlock,
            uniformBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        );
    });
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::ConstantBuffer(0u, volatileBuffer.get()),
            uniformBlock,
            uniformBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        );
    });
    if(localManager.getUniformBufferAddressAlignmentBytes() > 1u){
        ExpectManagerWriteRejection([&](){
            return localManager.writeDescriptor(
                DescriptorWriteItem::ConstantBuffer(0u, constantBuffer.get(), BufferRange(1u, 16u)),
                uniformBlock,
                uniformBlock.offsetBytes,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
            );
        });
    }
    EXPECT_TRUE(localManager.writeDescriptor(
        goodUniform,
        uniformBlock,
        uniformBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    ));

    BufferHandle goodStorage = localDevice.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
    );
    BufferHandle missingStorage = localDevice.createBuffer(BufferDesc().setByteSize(4096u));
    BufferHandle rawOnly = localDevice.createBuffer(
        BufferDesc().setByteSize(4096u).setCanHaveRawViews(true).setCanHaveUAVs(true)
    );
    BufferHandle structuredNoUav = localDevice.createBuffer(
        BufferDesc().setByteSize(4096u).setStructStride(16u)
    );
    BufferHandle rawNoUav = localDevice.createBuffer(
        BufferDesc().setByteSize(4096u).setCanHaveRawViews(true)
    );
    ASSERT_TRUE(goodStorage);
    ASSERT_TRUE(missingStorage);
    ASSERT_TRUE(rawOnly);
    ASSERT_TRUE(structuredNoUav);
    ASSERT_TRUE(rawNoUav);
    const DescriptorWriteItem goodStorageItem = DescriptorWriteItem::StructuredBuffer_UAV(0u, goodStorage.get());
    const DescriptorWriteItem rejectedStorageItems[] = {
        DescriptorWriteItem::StructuredBuffer_SRV(0u, missingStorage.get()),
        DescriptorWriteItem::StructuredBuffer_SRV(0u, rawOnly.get()),
        DescriptorWriteItem::StructuredBuffer_UAV(0u, structuredNoUav.get()),
        DescriptorWriteItem::RawBuffer_UAV(0u, rawNoUav.get()),
    };
    for(const DescriptorWriteItem& rejectedItem : rejectedStorageItems){
        ExpectManagerWriteRejection([&](){
            return localManager.writeDescriptor(
                rejectedItem,
                storageBlock,
                storageBlock.offsetBytes,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
            );
        });
        EXPECT_TRUE(localManager.writeDescriptor(
            goodStorageItem,
            storageBlock,
            storageBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        ));
    }
    if(localManager.getStorageBufferAddressAlignmentBytes() > 1u){
        ExpectManagerWriteRejection([&](){
            return localManager.writeDescriptor(
                DescriptorWriteItem::StructuredBuffer_SRV(0u, goodStorage.get(), Format::UNKNOWN, BufferRange(1u, 16u)),
                storageBlock,
                storageBlock.offsetBytes,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
            );
        });
    }

    BufferHandle goodTyped = localDevice.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setFormat(Format::R32_UINT)
            .setCanHaveTypedViews(true)
            .setCanHaveUAVs(true)
    );
    BufferHandle missingTyped = localDevice.createBuffer(
        BufferDesc().setByteSize(4096u).setFormat(Format::R32_UINT)
    );
    BufferHandle typedNoUav = localDevice.createBuffer(
        BufferDesc().setByteSize(4096u).setFormat(Format::R32_UINT).setCanHaveTypedViews(true)
    );
    ASSERT_TRUE(goodTyped);
    ASSERT_TRUE(missingTyped);
    ASSERT_TRUE(typedNoUav);
    BufferDesc& forgedTypedDesc = const_cast<BufferDesc&>(missingTyped->getDescription());
    forgedTypedDesc.canHaveTypedViews = true;
    ASSERT_TRUE(missingTyped->getDescription().canHaveTypedViews);
    const DescriptorWriteItem goodTypedItem = DescriptorWriteItem::TypedBuffer_SRV(
        0u,
        goodTyped.get(),
        Format::R32_UINT,
        BufferRange(0u, 4u)
    );
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::TypedBuffer_SRV(0u, missingTyped.get(), Format::R32_UINT),
            texelBlock,
            texelBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
        );
    });
    const u32 storageTexelSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    const auto storageTexelBlock = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        storageTexelSize,
        segmentAlignment
    );
    ASSERT_TRUE(storageTexelBlock.valid());
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::TypedBuffer_UAV(0u, typedNoUav.get(), Format::R32_UINT),
            storageTexelBlock,
            storageTexelBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
        );
    });
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::TypedBuffer_SRV(0u, goodTyped.get(), Format::R32_UINT, BufferRange(1u, 4u)),
            texelBlock,
            texelBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
        );
    });
    if((localDevice.queryFormatSupport(Format::D32) & FormatSupport::Buffer) == 0u){
        ExpectManagerWriteRejection([&](){
            return localManager.writeDescriptor(
                DescriptorWriteItem::TypedBuffer_SRV(0u, goodTyped.get(), Format::D32, BufferRange(0u, 4u)),
                texelBlock,
                texelBlock.offsetBytes,
                VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
            );
        });
    }
    EXPECT_TRUE(localManager.writeDescriptor(
        goodTypedItem,
        texelBlock,
        texelBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
    ));

    TextureDesc missingSampledDesc;
    missingSampledDesc.setWidth(8u).setHeight(8u).setFormat(Format::RGBA8_UNORM);
    missingSampledDesc.isShaderResource = false;
    TextureDesc missingStorageDesc;
    missingStorageDesc.setWidth(8u).setHeight(8u).setFormat(Format::RGBA8_UNORM);
    TextureDesc goodTextureDesc = missingStorageDesc;
    goodTextureDesc.setInUAV(true);
    TextureHandle missingSampled = localDevice.createTexture(missingSampledDesc);
    TextureHandle missingStorageImage = localDevice.createTexture(missingStorageDesc);
    TextureHandle goodTexture = localDevice.createTexture(goodTextureDesc);
    ASSERT_TRUE(missingSampled);
    ASSERT_TRUE(missingStorageImage);
    ASSERT_TRUE(goodTexture);
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::Texture_SRV(0u, missingSampled.get()),
            sampledImageBlock,
            sampledImageBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        DescriptorWriteItem::Texture_SRV(0u, goodTexture.get()),
        sampledImageBlock,
        sampledImageBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    ));
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::Texture_UAV(0u, missingStorageImage.get()),
            storageImageBlock,
            storageImageBlock.offsetBytes,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        DescriptorWriteItem::Texture_UAV(0u, goodTexture.get()),
        storageImageBlock,
        storageImageBlock.offsetBytes,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    ));

    localManager.free(uniformBlock);
    localManager.free(storageBlock);
    localManager.free(texelBlock);
    localManager.free(storageTexelBlock);
    localManager.free(sampledImageBlock);
    localManager.free(storageImageBlock);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferManagerIngressTest, RejectsTexelElementOverflowWhenPracticalToAllocate){
    auto& localDevice = device();
    auto& localManager = manager();
    const u64 elementCount = static_cast<u64>(localManager.getMaxTexelBufferElements()) + 1u;
    const u64 byteSize = elementCount * sizeof(u32);
    constexpr u64 s_PracticalAllocationLimitBytes = 64u * 1024u * 1024u;
    if(byteSize > s_PracticalAllocationLimitBytes)
        GTEST_SKIP() << "Texel element overflow needs a buffer larger than the practical smoke-test cap.";

    BufferHandle buffer = localDevice.createBuffer(
        BufferDesc()
            .setByteSize(byteSize)
            .setFormat(Format::R32_UINT)
            .setCanHaveTypedViews(true)
    );
    ASSERT_TRUE(buffer);
    const u32 descriptorSize = localManager.getDescriptorSize(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    const auto block = localManager.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        descriptorSize,
        localManager.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(block.valid());
    ExpectManagerWriteRejection([&](){
        return localManager.writeDescriptor(
            DescriptorWriteItem::TypedBuffer_SRV(0u, buffer.get(), Format::R32_UINT),
            block,
            block.offsetBytes,
            VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
        );
    });
    EXPECT_TRUE(localManager.writeDescriptor(
        DescriptorWriteItem::TypedBuffer_SRV(0u, buffer.get(), Format::R32_UINT, BufferRange(0u, 4u)),
        block,
        block.offsetBytes,
        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
    ));
    localManager.free(block);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

