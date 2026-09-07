// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// ImGui's leaf set contains only the 32-byte per-draw push block (scale/translate plus sampled-image and sampler
// slots). The font atlas and dynamic ImTextureData images use the global SampledImage/Sampler tables, so this shape
// must remain descriptor-buffer compatible with both heap layouts.
TEST_F(DescriptorBufferRoundTripTest, ImguiHeapTextureAndSamplerLayoutBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/imgui_heap_desc_arena"};
    static constexpr u32 kImguiPushConstantBytes = sizeof(f32) * 4u + sizeof(u32) * 4u;
    Alloc::GlobalArena descArena{kDescArenaName};

    // The production leaf carries push constants only and consumes no descriptor set. The persistent pure
    // resource/sampler heap tables occupy sets 0/1.
    BindingLayoutDesc leafDesc(descArena);
    leafDesc.setVisibility(ShaderType::AllGraphics);
    leafDesc.addItem(BindingLayoutItem::PushConstants(0u, kImguiPushConstantBytes));
    auto leafLayout = device.createBindingLayout(leafDesc);
    ASSERT_NE(leafLayout.get(), nullptr);
    EXPECT_TRUE(leafLayout->isDescriptorBufferCompatible())
        << "ImGui push-only leaf layout is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());

    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(texture);

    SamplerDesc samplerDesc;
    samplerDesc.setAllFilters(true).setAllAddressModes(SamplerAddressMode::Clamp);
    auto sampler = device.createSampler(samplerDesc);
    ASSERT_TRUE(sampler);

    EXPECT_EQ(heap.getRegisterSlot(GpuDescriptorClass::SampledImage), NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE);
    EXPECT_EQ(heap.getRegisterSlot(GpuDescriptorClass::Sampler), NWB_BINDLESS_HEAP_BINDING_SAMPLER);
    const GpuDescriptorHandle textureHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    const GpuDescriptorHandle samplerHandle = heap.allocate(GpuDescriptorClass::Sampler);
    ASSERT_TRUE(textureHandle.valid());
    ASSERT_TRUE(samplerHandle.valid());
    EXPECT_TRUE(heap.write(
        textureHandle,
        DescriptorWriteItem::Texture_SRV(0u, texture.get(), Format::RGBA8_UNORM, s_AllSubresources, TextureDimension::Texture2D)
    ));
    EXPECT_TRUE(heap.write(samplerHandle, DescriptorWriteItem::Sampler(0u, sampler.get())));

    // Free mirrors ImGui texture destruction. This headless test records no UI command buffer, so both heap uses can
    // retire immediately.
    heap.free(textureHandle);
    heap.free(samplerHandle);
}


[[nodiscard]] static u64 DecodeNativeImageViewBits(const Object& view)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
    const VkImageView imageView = static_cast<VkImageView>(view.pointer());
    return static_cast<u64>(reinterpret_cast<usize>(imageView));
#else
    const VkImageView imageView = static_cast<VkImageView>(view.integer);
    return static_cast<u64>(imageView);
#endif
}


// Parallel packet recording can request one texture's lazy native views from multiple workers. Keep the owning handle
// alive through every join, then prove same-key reuse and disjoint-key insertion both converge on one native handle.
TEST_F(DescriptorBufferRoundTripTest, TextureNativeViewCacheIsStableAcrossConcurrentRequests){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_WorkerCount = 8u;
    static constexpr u32 s_MipCount = 4u;
    static constexpr u32 s_ArraySize = 4u;
    static constexpr u32 s_ViewCount = s_MipCount * s_ArraySize;
    static constexpr u32 s_RepetitionCount = 64u;

    TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setMipLevels(s_MipCount)
            .setArraySize(s_ArraySize)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(texture);
    Texture* const textureResource = texture.get();

    u64 observedViews[s_WorkerCount][s_ViewCount] = {};
    bool workerViewsStable[s_WorkerCount];
    for(u32 workerIndex = 0u; workerIndex < s_WorkerCount; ++workerIndex)
        workerViewsStable[workerIndex] = true;

    Latch workersReady(s_WorkerCount);
    Thread workers[s_WorkerCount];
    for(u32 workerIndex = 0u; workerIndex < s_WorkerCount; ++workerIndex){
        workers[workerIndex] = Thread([&, workerIndex](){
            workersReady.count_down();
            workersReady.wait();

            const Object sharedView = textureResource->getNativeView(
                GraphicsBackend::ObjectTypes::VK_ImageView,
                Format::RGBA8_UNORM,
                TextureSubresourceSet(0u, 1u, 0u, 1u),
                TextureDimension::Texture2DArray,
                false
            );
            const u64 sharedViewBits = DecodeNativeImageViewBits(sharedView);
            observedViews[workerIndex][0u] = sharedViewBits;

            for(u32 repetition = 0u; repetition < s_RepetitionCount; ++repetition){
                for(u32 viewOffset = 0u; viewOffset < s_ViewCount; ++viewOffset){
                    const u32 viewIndex = (workerIndex + viewOffset + repetition) % s_ViewCount;
                    const u32 mipLevel = viewIndex % s_MipCount;
                    const u32 arraySlice = viewIndex / s_MipCount;
                    const Object view = textureResource->getNativeView(
                        GraphicsBackend::ObjectTypes::VK_ImageView,
                        Format::RGBA8_UNORM,
                        TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
                        TextureDimension::Texture2DArray,
                        false
                    );
                    const u64 viewBits = DecodeNativeImageViewBits(view);
                    u64& observedView = observedViews[workerIndex][viewIndex];
                    if(observedView == 0u)
                        observedView = viewBits;
                    else if(observedView != viewBits)
                        workerViewsStable[workerIndex] = false;
                }
            }
        });
    }

    for(Thread& worker : workers)
        worker.join();

    for(u32 workerIndex = 0u; workerIndex < s_WorkerCount; ++workerIndex)
        EXPECT_TRUE(workerViewsStable[workerIndex]) << "worker observed a changing native view for one cache key";

    for(u32 viewIndex = 0u; viewIndex < s_ViewCount; ++viewIndex){
        const u32 mipLevel = viewIndex % s_MipCount;
        const u32 arraySlice = viewIndex / s_MipCount;
        const Object cachedView = textureResource->getNativeView(
            GraphicsBackend::ObjectTypes::VK_ImageView,
            Format::RGBA8_UNORM,
            TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
            TextureDimension::Texture2DArray,
            false
        );
        const u64 cachedViewBits = DecodeNativeImageViewBits(cachedView);
        ASSERT_NE(cachedViewBits, 0u);
        for(u32 workerIndex = 0u; workerIndex < s_WorkerCount; ++workerIndex)
            EXPECT_EQ(observedViews[workerIndex][viewIndex], cachedViewBits);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

