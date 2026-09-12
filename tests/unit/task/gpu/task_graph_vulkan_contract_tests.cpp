// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_vulkan_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(TextureUploadAspect, ResolvesExactDepthStencilPlanesAndLayouts){
    const Graphics::FormatInfo& d24s8 = Graphics::GetFormatInfo(Graphics::Format::D24S8);
    const Graphics::FormatInfo& d32s8 = Graphics::GetFormatInfo(Graphics::Format::D32S8);
    const Graphics::FormatInfo& rgba = Graphics::GetFormatInfo(Graphics::Format::RGBA8_UNORM);
    Graphics::TextureUploadAspect::Enum resolvedAspect = Graphics::TextureUploadAspect::Automatic;
    Graphics::TextureUploadAspectLayout layout;

    EXPECT_FALSE(Graphics::ResolveTextureUploadAspect(
        d24s8,
        Graphics::TextureUploadAspect::Automatic,
        resolvedAspect
    ));
    EXPECT_FALSE(Graphics::ResolveTextureUploadAspect(
        d24s8,
        Graphics::TextureUploadAspect::Color,
        resolvedAspect
    ));

    ASSERT_TRUE(Graphics::ResolveTextureUploadAspect(
        d24s8,
        Graphics::TextureUploadAspect::Depth,
        resolvedAspect
    ));
    EXPECT_EQ(resolvedAspect, Graphics::TextureUploadAspect::Depth);
    ASSERT_TRUE(Graphics::GetTextureUploadAspectLayout(
        d24s8,
        Graphics::TextureUploadAspect::Depth,
        layout
    ));
    EXPECT_EQ(layout.blockWidth, 1u);
    EXPECT_EQ(layout.blockHeight, 1u);
    EXPECT_EQ(layout.bytesPerBlock, sizeof(u32));

    ASSERT_TRUE(Graphics::ResolveTextureUploadAspect(
        d24s8,
        Graphics::TextureUploadAspect::Stencil,
        resolvedAspect
    ));
    EXPECT_EQ(resolvedAspect, Graphics::TextureUploadAspect::Stencil);
    ASSERT_TRUE(Graphics::GetTextureUploadAspectLayout(
        d24s8,
        Graphics::TextureUploadAspect::Stencil,
        layout
    ));
    EXPECT_EQ(layout.bytesPerBlock, sizeof(u8));

    ASSERT_TRUE(Graphics::GetTextureUploadAspectLayout(
        d32s8,
        Graphics::TextureUploadAspect::Depth,
        layout
    ));
    EXPECT_EQ(layout.bytesPerBlock, sizeof(u32));
    ASSERT_TRUE(Graphics::GetTextureUploadAspectLayout(
        d32s8,
        Graphics::TextureUploadAspect::Stencil,
        layout
    ));
    EXPECT_EQ(layout.bytesPerBlock, sizeof(u8));

    ASSERT_TRUE(Graphics::ResolveTextureUploadAspect(
        rgba,
        Graphics::TextureUploadAspect::Automatic,
        resolvedAspect
    ));
    EXPECT_EQ(resolvedAspect, Graphics::TextureUploadAspect::Color);
    EXPECT_FALSE(Graphics::ResolveTextureUploadAspect(
        rgba,
        Graphics::TextureUploadAspect::Depth,
        resolvedAspect
    ));
}

TEST(VulkanStateTracking, NormalizesBarrierScopesFromExactPhysicalQueueCapabilities){
    const auto normalize = [](
        const Graphics::GpuQueueCapability::Mask capabilities,
        const VkPipelineStageFlags2 initialStage,
        const VkAccessFlags2 initialAccess
    ){
        VkPipelineStageFlags2 stage = initialStage;
        VkAccessFlags2 access = initialAccess;
        Graphics::GraphicsBackend::VulkanStateTrackingDetail::NormalizeBarrierScopeForQueueCapabilities(capabilities, stage, access);
        return MakePair(stage, access);
    };
    const auto expectNormalized = [&normalize](
        const Graphics::GpuQueueCapability::Mask capabilities,
        const VkPipelineStageFlags2 initialStage,
        const VkAccessFlags2 initialAccess,
        const VkPipelineStageFlags2 expectedStage,
        const VkAccessFlags2 expectedAccess
    ){
        const auto normalized = normalize(capabilities, initialStage, initialAccess);
        EXPECT_EQ(normalized.first(), expectedStage);
        EXPECT_EQ(normalized.second(), expectedAccess);
    };
    const Graphics::GpuQueueCapability::Mask graphicsCompute = QueueCapabilities(
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueueCapability::Compute
    );

    expectNormalized(
        graphicsCompute,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
        VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
        VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
        VK_ACCESS_2_INDEX_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
        VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
        VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_CONVERT_COOPERATIVE_VECTOR_MATRIX_BIT_NV,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_CONVERT_COOPERATIVE_VECTOR_MATRIX_BIT_NV,
        VK_ACCESS_2_TRANSFER_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR,
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
        VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR,
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
            | VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
            | VK_ACCESS_2_TRANSFER_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT,
        VK_ACCESS_2_SHADER_READ_BIT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT,
        VK_ACCESS_2_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MICROMAP_READ_BIT_EXT | VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT,
        VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT,
        VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT,
        VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_INVOCATION_MASK_READ_BIT_HUAWEI,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_INVOCATION_MASK_BIT_HUAWEI,
        VK_ACCESS_2_INVOCATION_MASK_READ_BIT_HUAWEI,
        VK_PIPELINE_STAGE_2_INVOCATION_MASK_BIT_HUAWEI,
        VK_ACCESS_2_INVOCATION_MASK_READ_BIT_HUAWEI
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_DECOMPRESSION_READ_BIT_EXT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Compute,
        VK_PIPELINE_STAGE_2_MEMORY_DECOMPRESSION_BIT_EXT,
        VK_ACCESS_2_MEMORY_DECOMPRESSION_READ_BIT_EXT | VK_ACCESS_2_MEMORY_DECOMPRESSION_WRITE_BIT_EXT,
        VK_PIPELINE_STAGE_2_MEMORY_DECOMPRESSION_BIT_EXT,
        VK_ACCESS_2_MEMORY_DECOMPRESSION_READ_BIT_EXT | VK_ACCESS_2_MEMORY_DECOMPRESSION_WRITE_BIT_EXT
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Graphics,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::Transfer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
    expectNormalized(
        Graphics::GpuQueueCapability::None,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0u
    );
}

TEST(VulkanCommandValidation, PureValidatorsRejectInvalidRangesCountsAndPushConstants){
    using namespace Graphics::GraphicsBackend::VulkanDetail;

    constexpr u32 s_Value = 1u;
    EXPECT_TRUE(AreAllPointersValid(&s_Value));
    EXPECT_FALSE(AreAllPointersValid(&s_Value, static_cast<const u32*>(nullptr)));

    Graphics::BufferDesc bufferDesc;
    bufferDesc.byteSize = 16u;
    EXPECT_TRUE(IsBufferRangeInBounds(bufferDesc, 0u, 16u));
    EXPECT_TRUE(IsBufferRangeInBounds(bufferDesc, 16u, 0u));
    EXPECT_FALSE(IsBufferRangeInBounds(bufferDesc, 16u, 1u));
    EXPECT_FALSE(IsBufferRangeInBounds(bufferDesc, Limit<u64>::s_Max, 1u));
    EXPECT_FALSE(BufferRangesOverlap(0u, 4u, 4u, 4u));
    EXPECT_TRUE(BufferRangesOverlap(0u, 8u, 4u, 4u));
    EXPECT_TRUE(BufferRangesOverlap(Limit<u64>::s_Max - 1u, 4u, 0u, 4u));

    constexpr u32 s_MaximumGroupCounts[] = { 4u, 5u, 6u };
    EXPECT_TRUE(AreDispatchGroupCountsValid(4u, 5u, 6u, s_MaximumGroupCounts));
    EXPECT_FALSE(AreDispatchGroupCountsValid(5u, 5u, 6u, s_MaximumGroupCounts));
    EXPECT_FALSE(AreDispatchGroupCountsValid(4u, 6u, 6u, s_MaximumGroupCounts));
    EXPECT_FALSE(AreDispatchGroupCountsValid(4u, 5u, 7u, s_MaximumGroupCounts));
    EXPECT_FALSE(AreDispatchGroupCountsValid(1u, 1u, 1u, nullptr));

    EXPECT_TRUE(IsPushConstantByteSizeValid(4u, 128u));
    EXPECT_FALSE(IsPushConstantByteSizeValid(0u, 128u));
    EXPECT_FALSE(IsPushConstantByteSizeValid(2u, 128u));
    EXPECT_FALSE(IsPushConstantByteSizeValid(132u, 128u));
    EXPECT_TRUE(IsTextureSubresourceRangeValid(Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u)));
    EXPECT_FALSE(IsTextureSubresourceRangeValid(Graphics::TextureSubresourceSet(0u, 0u, 0u, 1u)));
}

TEST(VulkanCommandValidation, PureGraphicsAndMeshValidatorsCoverExactVulkanBoundaries){
    using namespace Graphics::GraphicsBackend::VulkanDetail;

    EXPECT_EQ(GetPrimitiveTopology(Graphics::PrimitiveType::PointList), VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    EXPECT_EQ(GetPrimitiveTopology(Graphics::PrimitiveType::LineStrip), VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
    EXPECT_EQ(GetPrimitiveTopology(Graphics::PrimitiveType::PatchList), VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
    EXPECT_EQ(
        GetPrimitiveTopology(static_cast<Graphics::PrimitiveType::Enum>(Limit<u8>::s_Max)),
        VK_PRIMITIVE_TOPOLOGY_MAX_ENUM
    );

    VkPhysicalDeviceLimits limits{};
    limits.maxViewportDimensions[0u] = 4096u;
    limits.maxViewportDimensions[1u] = 4096u;
    limits.viewportBoundsRange[0u] = -32768.0f;
    limits.viewportBoundsRange[1u] = 32767.0f;
    EXPECT_TRUE(IsViewportValid(Graphics::Viewport(-16.0f, 16.0f, -8.0f, 8.0f, 1.0f, 0.0f), limits));
    EXPECT_TRUE(IsViewportValid(Graphics::Viewport(0.0f, 16.0f, 4.0f, 4.0f, 0.0f, 1.0f), limits));
    EXPECT_FALSE(IsViewportValid(Graphics::Viewport(0.0f, 0.0f, 0.0f, 8.0f, 0.0f, 1.0f), limits));
    EXPECT_FALSE(IsViewportValid(Graphics::Viewport(-32769.0f, 16.0f, 0.0f, 8.0f, 0.0f, 1.0f), limits));
    EXPECT_FALSE(IsViewportValid(Graphics::Viewport(0.0f, 16.0f, 8.0f, 4.0f, 0.0f, 1.0f), limits));
    EXPECT_FALSE(IsViewportValid(Graphics::Viewport(0.0f, 16.0f, 0.0f, 8.0f, -0.1f, 1.0f), limits));
    EXPECT_FALSE(IsViewportValid(Graphics::Viewport(0.0f, 16.0f, 0.0f, 8.0f, 0.0f, 1.1f), limits));
    EXPECT_FALSE(IsViewportValid(
        Graphics::Viewport(0.0f, Limit<f32>::s_QuietNaN, 0.0f, 8.0f, 0.0f, 1.0f),
        limits
    ));
    VkPhysicalDeviceLimits exactIntegerLimits = limits;
    exactIntegerLimits.maxViewportDimensions[0u] = 16777219u;
    exactIntegerLimits.viewportBoundsRange[1u] = 33554432.0f;
    EXPECT_FALSE(IsViewportValid(
        Graphics::Viewport(0.0f, 16777220.0f, 0.0f, 8.0f, 0.0f, 1.0f),
        exactIntegerLimits
    ));

    EXPECT_TRUE(IsScissorRectValid(Graphics::Rect(0, 0, 0, 0)));
    EXPECT_TRUE(IsScissorRectValid(Graphics::Rect(1, 8, 2, 9)));
    EXPECT_FALSE(IsScissorRectValid(Graphics::Rect(-1, 8, 0, 9)));
    EXPECT_TRUE(IsImplicitScissorValid(Graphics::Viewport(0.0f, 16.0f, 0.0f, 8.0f, 0.0f, 1.0f)));
    EXPECT_FALSE(IsImplicitScissorValid(Graphics::Viewport(-1.0f, 16.0f, 0.0f, 8.0f, 0.0f, 1.0f)));
    EXPECT_FALSE(IsImplicitScissorValid(
        Graphics::Viewport(0.0f, 2147483648.0f, 0.0f, 8.0f, 0.0f, 1.0f)
    ));
    VkRect2D implicitScissor{};
    EXPECT_TRUE(BuildImplicitScissor(
        Graphics::Viewport(0.5f, 10.5f, 1.25f, 7.25f, 0.0f, 1.0f),
        implicitScissor
    ));
    EXPECT_EQ(implicitScissor.offset.x, 0);
    EXPECT_EQ(implicitScissor.offset.y, 1);
    EXPECT_EQ(implicitScissor.extent.width, 11u);
    EXPECT_EQ(implicitScissor.extent.height, 7u);

    const Graphics::TextureSubresourceSet firstRange(0u, 1u, 0u, 2u);
    EXPECT_TRUE(TextureSubresourceRangesOverlap(firstRange, Graphics::TextureSubresourceSet(0u, 1u, 1u, 1u)));
    EXPECT_FALSE(TextureSubresourceRangesOverlap(firstRange, Graphics::TextureSubresourceSet(1u, 1u, 0u, 2u)));

    Graphics::TextureDesc textureDesc;
    textureDesc.setDimension(Graphics::TextureDimension::TextureCube);
    EXPECT_EQ(
        GetFramebufferAttachmentViewDimension(textureDesc, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u)),
        Graphics::TextureDimension::Texture2D
    );
    EXPECT_EQ(
        GetFramebufferAttachmentViewDimension(textureDesc, Graphics::TextureSubresourceSet(0u, 1u, 0u, 6u)),
        Graphics::TextureDimension::Texture2DArray
    );
    textureDesc.setDimension(Graphics::TextureDimension::Texture3D);
    EXPECT_EQ(
        GetFramebufferAttachmentViewDimension(textureDesc, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u)),
        Graphics::TextureDimension::Texture2D
    );
    EXPECT_EQ(
        GetFramebufferAttachmentViewDimension(textureDesc, Graphics::TextureSubresourceSet(0u, 1u, 0u, 2u)),
        Graphics::TextureDimension::Unknown
    );
    EXPECT_TRUE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc(),
        Graphics::s_AllSubresources
    ));
    EXPECT_TRUE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc().setDimension(Graphics::TextureDimension::Texture2DArray).setArraySize(4u),
        Graphics::TextureSubresourceSet(0u, 1u, 1u, 3u)
    ));
    EXPECT_TRUE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc().setDimension(Graphics::TextureDimension::Texture2DArray).setArraySize(4u),
        Graphics::TextureSubresourceSet(0u, 1u, 1u, Graphics::TextureSubresourceSet::AllArraySlices)
    ));
    EXPECT_FALSE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc().setDimension(Graphics::TextureDimension::Texture2DArray).setArraySize(4u),
        Graphics::TextureSubresourceSet(0u, 1u, 3u, 2u)
    ));
    EXPECT_FALSE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc().setDimension(Graphics::TextureDimension::Texture2D),
        Graphics::TextureSubresourceSet(0u, 1u, 1u, 1u)
    ));
    EXPECT_FALSE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc().setMipLevels(2u),
        Graphics::TextureSubresourceSet(2u, 1u, 0u, 1u)
    ));
    EXPECT_FALSE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc().setMipLevels(2u),
        Graphics::TextureSubresourceSet(0u, Graphics::TextureSubresourceSet::AllMipLevels, 0u, 1u)
    ));
    EXPECT_TRUE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc().setMipLevels(2u),
        Graphics::TextureSubresourceSet(1u, Graphics::TextureSubresourceSet::AllMipLevels, 0u, 1u)
    ));
    EXPECT_FALSE(IsFramebufferAttachmentSubresourceSetValid(
        Graphics::TextureDesc(),
        Graphics::TextureSubresourceSet(0u, 1u, 0u, 2u)
    ));

    Graphics::BufferDesc bufferDesc;
    bufferDesc.byteSize = 64u;
    EXPECT_TRUE(IsStridedBufferRangeValid(bufferDesc, 0u, 0u, 4u, 16u, 12u));
    EXPECT_FALSE(IsStridedBufferRangeValid(bufferDesc, 0u, 0u, 5u, 16u, 12u));
    EXPECT_FALSE(IsStridedBufferRangeValid(bufferDesc, Limit<u64>::s_Max, 1u, 1u, 16u, 12u));
    EXPECT_TRUE(IsIndexFormatSupported(Graphics::Format::R16_UINT, false));
    EXPECT_FALSE(IsIndexFormatSupported(Graphics::Format::R32_UINT, false));
    EXPECT_TRUE(IsIndexFormatSupported(Graphics::Format::R32_UINT, true));
    EXPECT_FALSE(IsIndexFormatSupported(Graphics::Format::RGBA8_UNORM, true));
    EXPECT_TRUE(IsIndexDrawRangeValid(bufferDesc, 0u, 4u, 8u, sizeof(u32)));
    EXPECT_FALSE(IsIndexDrawRangeValid(bufferDesc, 0u, 12u, 8u, sizeof(u32)));
    EXPECT_FALSE(IsIndexDrawRangeValid(bufferDesc, Limit<u64>::s_Max, 1u, 1u, sizeof(u32)));
    EXPECT_TRUE(IsIndirectCommandRangeValid(bufferDesc, 0u, 16u, 4u));
    EXPECT_TRUE(IsIndirectCommandRangeValid(bufferDesc, 48u, 16u, 1u));
    EXPECT_FALSE(IsIndirectCommandRangeValid(bufferDesc, 2u, 16u, 1u));
    EXPECT_FALSE(IsIndirectCommandRangeValid(bufferDesc, 52u, 16u, 1u));
    EXPECT_FALSE(IsIndirectCommandRangeValid(bufferDesc, 0u, Limit<u64>::s_Max, 2u));
    EXPECT_FALSE(IsIndirectDrawCountValid(0u, 8u, true));
    EXPECT_FALSE(IsIndirectDrawCountValid(2u, 8u, false));
    EXPECT_TRUE(IsIndirectDrawCountValid(2u, 8u, true));
    EXPECT_FALSE(IsIndirectDrawCountValid(9u, 8u, true));

    constexpr u32 s_MaximumMeshGroupCounts[] = { 4u, 5u, 6u };
    EXPECT_TRUE(AreMeshDispatchGroupCountsValid(4u, 5u, 1u, s_MaximumMeshGroupCounts, 20u));
    EXPECT_FALSE(AreMeshDispatchGroupCountsValid(0u, 5u, 1u, s_MaximumMeshGroupCounts, 20u));
    EXPECT_FALSE(AreMeshDispatchGroupCountsValid(1u, 1u, 1u, nullptr, 20u));
    EXPECT_FALSE(AreMeshDispatchGroupCountsValid(4u, 5u, 2u, s_MaximumMeshGroupCounts, 20u));
    EXPECT_FALSE(AreMeshDispatchGroupCountsValid(
        Limit<u32>::s_Max,
        Limit<u32>::s_Max,
        Limit<u32>::s_Max,
        s_MaximumMeshGroupCounts,
        Limit<u32>::s_Max
    ));
    VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{};
    meshProperties.maxTaskWorkGroupCount[0u] = 2u;
    meshProperties.maxTaskWorkGroupTotalCount = 3u;
    meshProperties.maxMeshWorkGroupCount[0u] = 5u;
    meshProperties.maxMeshWorkGroupTotalCount = 7u;
    const MeshDispatchLimits taskLimits = GetMeshDispatchLimits(meshProperties, true);
    const MeshDispatchLimits meshLimits = GetMeshDispatchLimits(meshProperties, false);
    EXPECT_EQ(taskLimits.maximumGroupCounts, meshProperties.maxTaskWorkGroupCount);
    EXPECT_EQ(taskLimits.maximumTotalGroupCount, 3u);
    EXPECT_EQ(meshLimits.maximumGroupCounts, meshProperties.maxMeshWorkGroupCount);
    EXPECT_EQ(meshLimits.maximumTotalGroupCount, 7u);

    Graphics::DepthStencilState depthStencilState;
    EXPECT_FALSE(IsDepthStencilReadOnlyCompatible(depthStencilState, VK_IMAGE_ASPECT_DEPTH_BIT));
    depthStencilState.disableDepthTest();
    EXPECT_TRUE(IsDepthStencilReadOnlyCompatible(depthStencilState, VK_IMAGE_ASPECT_DEPTH_BIT));
    depthStencilState.enableDepthTest();
    depthStencilState.disableDepthWrite().enableStencil();
    EXPECT_TRUE(IsDepthStencilReadOnlyCompatible(
        depthStencilState,
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
    ));
    depthStencilState.frontFaceStencil.setPassOp(Graphics::StencilOp::Replace);
    EXPECT_FALSE(IsDepthStencilReadOnlyCompatible(depthStencilState, VK_IMAGE_ASPECT_STENCIL_BIT));
    depthStencilState.frontFaceStencil.setPassOp(Graphics::StencilOp::Keep);
    depthStencilState.backFaceStencil.setFailOp(Graphics::StencilOp::Replace);
    EXPECT_FALSE(IsDepthStencilReadOnlyCompatible(depthStencilState, VK_IMAGE_ASPECT_STENCIL_BIT));
    depthStencilState.backFaceStencil.setFailOp(Graphics::StencilOp::Keep);
    depthStencilState.backFaceStencil.setDepthFailOp(Graphics::StencilOp::Replace);
    EXPECT_FALSE(IsDepthStencilReadOnlyCompatible(depthStencilState, VK_IMAGE_ASPECT_STENCIL_BIT));
    depthStencilState.setStencilWriteMask(0u);
    EXPECT_TRUE(IsDepthStencilReadOnlyCompatible(depthStencilState, VK_IMAGE_ASPECT_STENCIL_BIT));

    Graphics::RasterState rasterState;
    EXPECT_EQ(BuildPipelineRasterizationState(rasterState, VK_POLYGON_MODE_FILL, VK_FALSE).depthBiasEnable, VK_FALSE);
    rasterState.slopeScaledDepthBias = 1.0f;
    EXPECT_EQ(BuildPipelineRasterizationState(rasterState, VK_POLYGON_MODE_FILL, VK_FALSE).depthBiasEnable, VK_TRUE);
    EXPECT_EQ(BuildPipelineRasterizationState(rasterState, VK_POLYGON_MODE_FILL, VK_TRUE).depthClampEnable, VK_TRUE);

    EXPECT_TRUE(IsPipelineColorAttachmentFormatClassValid(Graphics::Format::RGBA8_UNORM));
    EXPECT_FALSE(IsPipelineColorAttachmentFormatClassValid(Graphics::Format::D24S8));
    EXPECT_FALSE(IsPipelineColorAttachmentFormatClassValid(
        static_cast<Graphics::Format::Enum>(Limit<u8>::s_Max)
    ));

    Graphics::Alloc::ScratchArena scratchArena(Name("tests/graphics/pipeline_rendering_validation_scratch"));
    Graphics::GraphicsBackend::PipelineRenderingFormatVector colorFormats{ scratchArena };
    VkPipelineRenderingCreateInfo renderingInfo{};
    EXPECT_TRUE(BuildPipelineRenderingInfo(
        Graphics::FramebufferInfo().addColorFormat(Graphics::Format::RGBA8_UNORM),
        NWB_TEXT("unit graphics pipeline"),
        renderingInfo,
        colorFormats
    ));
#if defined(NWB_FINAL)
    CapturingLogger logger;
    Graphics::Common::LoggerRegistrationGuard loggerGuard(logger);
    EXPECT_FALSE(BuildPipelineRenderingInfo(
        Graphics::FramebufferInfo().addColorFormat(Graphics::Format::D24S8),
        NWB_TEXT("unit graphics pipeline"),
        renderingInfo,
        colorFormats
    ));
#endif
}

TEST(VulkanCommandValidation, RenderPassAttachmentActionsLowerExactlyAndPreserveByDefault){
    using namespace Graphics::GraphicsBackend::VulkanDetail;

    const Graphics::RenderPassParameters defaultParameters;
    for(u32 attachmentIndex = 0u; attachmentIndex < Graphics::s_MaxRenderTargets; ++attachmentIndex){
        const Graphics::RenderPassAttachmentActions& actions =
            defaultParameters.colorAttachmentActions[attachmentIndex]
        ;
        EXPECT_TRUE(IsRenderPassAttachmentActionsValid(actions));
        const RenderPassAttachmentOperations operations = ConvertRenderPassAttachmentActions(actions);
        EXPECT_EQ(operations.loadOp, VK_ATTACHMENT_LOAD_OP_LOAD);
        EXPECT_EQ(operations.storeOp, VK_ATTACHMENT_STORE_OP_STORE);
    }
    EXPECT_EQ(
        ConvertRenderPassAttachmentActions(defaultParameters.depthAttachmentActions).loadOp,
        VK_ATTACHMENT_LOAD_OP_LOAD
    );
    EXPECT_EQ(
        ConvertRenderPassAttachmentActions(defaultParameters.stencilAttachmentActions).storeOp,
        VK_ATTACHMENT_STORE_OP_STORE
    );

    Graphics::RenderPassAttachmentActions clearAndDiscard;
    clearAndDiscard.loadAction = Graphics::RenderPassLoadAction::Clear;
    clearAndDiscard.storeAction = Graphics::RenderPassStoreAction::Discard;
    const RenderPassAttachmentOperations clearAndDiscardOperations =
        ConvertRenderPassAttachmentActions(clearAndDiscard)
    ;
    EXPECT_EQ(clearAndDiscardOperations.loadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
    EXPECT_EQ(clearAndDiscardOperations.storeOp, VK_ATTACHMENT_STORE_OP_DONT_CARE);

    Graphics::RenderPassAttachmentActions discardAndStore;
    discardAndStore.loadAction = Graphics::RenderPassLoadAction::Discard;
    discardAndStore.storeAction = Graphics::RenderPassStoreAction::Store;
    const RenderPassAttachmentOperations discardAndStoreOperations =
        ConvertRenderPassAttachmentActions(discardAndStore)
    ;
    EXPECT_EQ(discardAndStoreOperations.loadOp, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    EXPECT_EQ(discardAndStoreOperations.storeOp, VK_ATTACHMENT_STORE_OP_STORE);

    Graphics::RenderPassAttachmentActions invalidLoad;
    invalidLoad.loadAction = Graphics::RenderPassLoadAction::Count;
    EXPECT_FALSE(IsRenderPassAttachmentActionsValid(invalidLoad));
    EXPECT_EQ(ConvertRenderPassLoadAction(invalidLoad.loadAction), VK_ATTACHMENT_LOAD_OP_MAX_ENUM);

    Graphics::RenderPassAttachmentActions invalidStore;
    invalidStore.storeAction = Graphics::RenderPassStoreAction::Count;
    EXPECT_FALSE(IsRenderPassAttachmentActionsValid(invalidStore));
    EXPECT_EQ(ConvertRenderPassStoreAction(invalidStore.storeAction), VK_ATTACHMENT_STORE_OP_MAX_ENUM);
}

TEST(VulkanStateTracking, DetectsExactActiveAttachmentBarrierOverlap){
    using Graphics::GraphicsBackend::VulkanStateTrackingDetail::ImageBarrierOverlapsTextureSubresources;

    const VkImage image = reinterpret_cast<VkImage>(static_cast<usize>(1u));
    const VkImage otherImage = reinterpret_cast<VkImage>(static_cast<usize>(2u));
    constexpr Graphics::TextureSubresourceSet s_AttachmentSubresources(2u, 1u, 3u, 2u);
    auto barrier = Graphics::GraphicsBackend::VulkanDetail::MakeVkStruct<VkImageMemoryBarrier2>(
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2
    );
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 2u;
    barrier.subresourceRange.levelCount = 1u;
    barrier.subresourceRange.baseArrayLayer = 4u;
    barrier.subresourceRange.layerCount = 1u;

    EXPECT_TRUE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));
    EXPECT_FALSE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        otherImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    EXPECT_FALSE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    barrier.subresourceRange.baseMipLevel = 3u;
    EXPECT_FALSE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));
    barrier.subresourceRange.baseMipLevel = 1u;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 5u;
    barrier.subresourceRange.layerCount = 1u;
    EXPECT_FALSE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));
    barrier.subresourceRange.baseArrayLayer = 2u;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    EXPECT_TRUE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));
    barrier.subresourceRange.levelCount = 0u;
    EXPECT_FALSE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));
    barrier.subresourceRange.levelCount = 1u;
    barrier.subresourceRange.layerCount = 0u;
    EXPECT_FALSE(ImageBarrierOverlapsTextureSubresources(
        barrier,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        s_AttachmentSubresources
    ));
}

TEST(VulkanDevice, DeviceGenerationAllocationFailsClosedAtExhaustion){
    Graphics::GraphicsBackend::VulkanDetail::DeviceGenerationAllocator allocator;
    for(u32 expectedGeneration = 1u; expectedGeneration <= static_cast<u32>(Limit<u16>::s_Max); ++expectedGeneration)
        ASSERT_EQ(allocator.allocate(), static_cast<u16>(expectedGeneration));
    EXPECT_EQ(allocator.allocate(), 0u);
    EXPECT_EQ(allocator.allocate(), 0u);
}

TEST(VulkanDevice, MatchesExactCommandListSubmissionQueueIdentity){
    using Graphics::GraphicsBackend::VulkanDetail::SubmissionCommandListMatchesExecutionQueue;

    constexpr Graphics::GpuPhysicalQueueId s_ExactQueue{ 3u, 7u };
    constexpr Graphics::GpuPhysicalQueueId s_OtherIndex{ 4u, 7u };
    constexpr Graphics::GpuPhysicalQueueId s_OtherGeneration{ 3u, 8u };
    constexpr Graphics::CommandListParameters s_CommandList{
        .queueType = Graphics::CommandQueue::Graphics,
        .physicalQueue = s_ExactQueue,
    };
    EXPECT_TRUE(SubmissionCommandListMatchesExecutionQueue(
        s_CommandList,
        s_ExactQueue,
        Graphics::CommandQueue::Graphics
    ));
    EXPECT_FALSE(SubmissionCommandListMatchesExecutionQueue(
        s_CommandList,
        s_OtherIndex,
        Graphics::CommandQueue::Graphics
    ));
    EXPECT_FALSE(SubmissionCommandListMatchesExecutionQueue(
        s_CommandList,
        s_OtherGeneration,
        Graphics::CommandQueue::Graphics
    ));
    EXPECT_FALSE(SubmissionCommandListMatchesExecutionQueue(
        s_CommandList,
        s_ExactQueue,
        Graphics::CommandQueue::Compute
    ));
    EXPECT_FALSE(SubmissionCommandListMatchesExecutionQueue(
        Graphics::CommandListParameters{},
        s_ExactQueue,
        Graphics::CommandQueue::Graphics
    ));
}

TEST(VulkanStateTracking, MapsAccelerationStructureBuildInputsAndReadScopesExactly){
    using Graphics::GraphicsBackend::VulkanDetail::GetVkAccessFlags;
    using Graphics::GraphicsBackend::VulkanDetail::GetVkPipelineStageFlags;

    EXPECT_EQ(
        GetVkAccessFlags(Graphics::ResourceStates::AccelStructBuildInput),
        VK_ACCESS_2_SHADER_READ_BIT
    );
    EXPECT_EQ(
        GetVkAccessFlags(Graphics::ResourceStates::OpacityMicromapBuildInput),
        VK_ACCESS_2_SHADER_READ_BIT
    );
    EXPECT_EQ(
        GetVkPipelineStageFlags(Graphics::ResourceStates::AccelStructBuildInput, false),
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    );
    EXPECT_EQ(
        GetVkPipelineStageFlags(Graphics::ResourceStates::OpacityMicromapBuildInput, false),
        VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT
    );
    constexpr VkPipelineStageFlags2 s_AccelStructReadStages =
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    ;
    EXPECT_EQ(
        GetVkPipelineStageFlags(Graphics::ResourceStates::AccelStructRead, false),
        s_AccelStructReadStages
    );
    EXPECT_EQ(
        GetVkPipelineStageFlags(Graphics::ResourceStates::AccelStructRead, true),
        s_AccelStructReadStages | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
    );
    EXPECT_EQ(
        GetVkPipelineStageFlags(Graphics::ResourceStates::AccelStructWrite, true),
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    );
    EXPECT_EQ(
        GetVkPipelineStageFlags(Graphics::ResourceStates::AccelStructBuildBlas, true),
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    );
}

TEST(VulkanStateTracking, HonorsForcedSameStateMemoryDependenciesIndependentlyOfUavPolicy){
    using Graphics::GraphicsBackend::VulkanStateTrackingDetail::NeedsResourceStateBarrier;

    EXPECT_TRUE(NeedsResourceStateBarrier(
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::CopyDest,
        false,
        false
    ));
    EXPECT_FALSE(NeedsResourceStateBarrier(
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::CopyDest,
        false,
        false
    ));
    EXPECT_TRUE(NeedsResourceStateBarrier(
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::CopyDest,
        false,
        true
    ));
    EXPECT_TRUE(NeedsResourceStateBarrier(
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess,
        true,
        false
    ));
    EXPECT_FALSE(NeedsResourceStateBarrier(
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess,
        false,
        false
    ));
    EXPECT_TRUE(NeedsResourceStateBarrier(
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess,
        false,
        true
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

