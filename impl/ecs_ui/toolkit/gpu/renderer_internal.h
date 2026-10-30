// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer.h"
#include "renderer_sdf_resources.h"
#include "renderer_texture_resources.h"

#include <impl/assets/graphics/ui/output_push_constants.h>
#include <impl/assets/graphics/ui/push_constants.h>
#include <impl/assets_texture/loader.h>
#include <impl/ecs_ui/toolkit/text/glyph_page.h>

#include <core/alloc/scratch.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/task/gpu/presentation_contributor.h>
#include <core/task/gpu/task_graph.h>

#include <global/refcount_ptr.h>
#include <global/simd/vector_arithmetic.h>
#include <global/simd/vector_rounding.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(Vertex) == NWB_UI_VERTEX_BYTE_SIZE);
static_assert(offsetof(Vertex, position) == NWB_UI_VERTEX_POSITION_BYTE_OFFSET);
static_assert(offsetof(Vertex, texCoord) == NWB_UI_VERTEX_UV_BYTE_OFFSET);
static_assert(offsetof(Vertex, color) == NWB_UI_VERTEX_COLOR_BYTE_OFFSET);
static_assert(sizeof(GpuPaintPushConstants) == NWB_UI_PUSH_CONSTANTS_BYTE_SIZE);
static_assert(offsetof(GpuPaintPushConstants, scale) == NWB_UI_PUSH_CONSTANTS_SCALE_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, translate) == NWB_UI_PUSH_CONSTANTS_TRANSLATE_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, textureSlot) == NWB_UI_PUSH_CONSTANTS_TEXTURE_SLOT_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, samplerSlot) == NWB_UI_PUSH_CONSTANTS_SAMPLER_SLOT_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, material) == NWB_UI_PUSH_CONSTANTS_MATERIAL_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, sdfChannel) == NWB_UI_PUSH_CONSTANTS_SDF_CHANNEL_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, sdfSpreadPixels) == NWB_UI_PUSH_CONSTANTS_SDF_SPREAD_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, sdfDistanceEncoding) == NWB_UI_PUSH_CONSTANTS_SDF_ENCODING_BYTE_OFFSET);
static_assert(sizeof(GpuOutputPushConstants) == NWB_UI_OUTPUT_PUSH_CONSTANTS_BYTE_SIZE);
static_assert(offsetof(GpuOutputPushConstants, textureSlot) == NWB_UI_OUTPUT_PUSH_CONSTANTS_TEXTURE_SLOT_BYTE_OFFSET);
static_assert(offsetof(GpuOutputPushConstants, samplerSlot) == NWB_UI_OUTPUT_PUSH_CONSTANTS_SAMPLER_SLOT_BYTE_OFFSET);
static_assert(offsetof(GpuOutputPushConstants, presentationMode) == NWB_UI_OUTPUT_PUSH_CONSTANTS_PRESENTATION_MODE_BYTE_OFFSET);
static_assert(static_cast<u32>(PaintMaterial::Solid) == NWB_UI_MATERIAL_SOLID);
static_assert(static_cast<u32>(PaintMaterial::Skin) == NWB_UI_MATERIAL_SKIN);
static_assert(static_cast<u32>(PaintMaterial::Glyph) == NWB_UI_MATERIAL_GLYPH);
static_assert(static_cast<u32>(PaintMaterial::SdfGlyph) == NWB_UI_MATERIAL_SDF_GLYPH);
static_assert(static_cast<u32>(PaintMaterial::Image) == NWB_UI_MATERIAL_IMAGE);
static_assert(s_FontAtlasDistanceEncoding == NWB_UI_SDF_DISTANCE_ENCODING_FREETYPE_U8);

inline constexpr TStringView s_GpuRendererContext = NWB_TEXT("GpuRenderer");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GpuRasterResourceUses = FixedVector<Core::GpuTaskResourceUse, s_GpuMaxGlyphPages + s_GpuRasterFixedResourceUses>;
using GpuGlyphGraphResources = FixedVector<Core::GpuGraphResourceId, s_GpuMaxGlyphPages>;
using GpuSdfGraphResources = FixedVector<Core::GpuGraphResourceId, s_GpuMaxSdfPages>;
using GpuTextureGraphResources = FixedVector<Core::GpuGraphResourceId, s_GpuMaxTextureImages>;

template<typename T>
using GpuVersion = RefCountPtr<RefCounter<T>, ArenaRefDeleter<RefCounter<T>, Core::Alloc::GlobalArena>>;

template<typename T, typename... Args>
[[nodiscard]] Expected<GpuVersion<T>> MakeGpuVersion(Core::Alloc::GlobalArena& arena, Args&&... args)noexcept(ArenaObjectDetail::IsNothrowArenaObjectConstruction<RefCounter<T>, Args...>()){
    auto* const value = NewArenaObject<RefCounter<T>>(arena, Forward<Args>(args)...);
    if(!value)
        return MakeUnexpected(Failure{});
    return GpuVersion<T>(value, ArenaRefDeleter<RefCounter<T>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}

inline void FreeGpuDescriptor(Core::GraphicsRuntime& graphics, const Core::GpuDescriptorHandle& descriptor)noexcept{
    if(descriptor.valid())
        graphics.getDevice().getDescriptorHeap().free(descriptor);
}

[[nodiscard]] inline b8 PrepareGpuSampledImageDescriptor(Core::GraphicsRuntime& graphics, const Core::TextureHandle& texture, const Core::Format::Enum format, Core::GpuDescriptorHandle& inOutDescriptor){
    if(inOutDescriptor.valid())
        return true;
    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    const Expected<Core::GpuDescriptorHandle> descriptorResult = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!descriptorResult)
        return false;
    const Core::GpuDescriptorHandle descriptor = *descriptorResult;
    Core::GpuDescriptorRetirementGuard descriptorGuard(heap, descriptor);
    if(!heap.write(descriptor, Core::DescriptorWriteItem::TextureSrv(0u, texture.get(), format, Core::s_AllSubresources, Core::TextureDimension::Texture2D)))
        return false;
    inOutDescriptor = descriptor;
    descriptorGuard.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuSkinVersion : NoCopy{
    Core::GraphicsRuntime& m_graphics;
    SkinBinding m_binding;
    TextureGpuResource m_texture;


public:
    explicit GpuSkinVersion(Core::GraphicsRuntime& graphics)noexcept;
    ~GpuSkinVersion()noexcept;
};

struct GpuGlyphVersion : NoCopy{
    Core::GraphicsRuntime& m_graphics;
    SharedGlyphPage m_page;
    Core::TextureHandle m_texture;
    Core::GpuDescriptorHandle m_sampledImage;
    Core::QueueSubmissionToken m_readinessToken;


public:
    GpuGlyphVersion(Core::GraphicsRuntime& graphics, SharedGlyphPage page)noexcept;
    ~GpuGlyphVersion()noexcept;
};

struct GpuTargetVersion : NoCopy{
    Core::GraphicsRuntime& m_graphics;
    Core::TextureHandle m_color;
    Core::FramebufferHandle m_framebuffer;
    Core::GpuDescriptorHandle m_sampledImage;
    Core::QueueSubmissionToken m_readinessToken;


public:
    explicit GpuTargetVersion(Core::GraphicsRuntime& graphics)noexcept;
    ~GpuTargetVersion()noexcept;
};

struct GpuSharedResources : NoCopy{
    Core::GraphicsRuntime& m_graphics;
    Core::ShaderHandle m_vertexShader;
    Core::ShaderHandle m_pixelShader;
    Core::ShaderHandle m_outputVertexShader;
    Core::ShaderHandle m_outputPixelShader;
    Core::InputLayoutHandle m_inputLayout;
    Core::BindingLayoutHandle m_layout;
    Core::BindingLayoutHandle m_outputLayout;
    Core::GraphicsPipelineHandle m_pipeline;
    Core::GraphicsPipelineHandle m_outputPipeline;
    Core::SamplerHandle m_sampler;
    Core::GpuDescriptorHandle m_samplerDescriptor;


public:
    explicit GpuSharedResources(Core::GraphicsRuntime& graphics)noexcept;
    ~GpuSharedResources()noexcept;
};

struct GpuFrameData : NoCopy{
    // Strong version owners keep descriptors allocated through pending recording and final GPU completion.
    Core::Alloc::GlobalArena& m_arena;
    DrawSnapshot m_snapshot;
    GpuVersion<GpuSkinVersion> m_skin;
    Vector<GpuVersion<GpuGlyphVersion>, Core::Alloc::GlobalArena> m_glyphPages;
    Vector<GpuVersion<GpuSdfAtlasVersion>, Core::Alloc::GlobalArena> m_sdfPages;
    Vector<GpuVersion<GpuTextureImageVersion>, Core::Alloc::GlobalArena> m_textureImages;
    GpuVersion<GpuTargetVersion> m_target;
    GpuVersion<GpuSharedResources> m_resources;
    Core::BufferHandle m_vertices;
    Core::BufferHandle m_indices;
    Core::GraphicsPipelineHandle m_outputPipeline;
    Core::AcquiredPresentationFrame m_acquired;
    Core::QueueSubmissionToken m_vertexUpload;
    Core::QueueSubmissionToken m_indexUpload;
    Core::QueueSubmissionToken m_clear;
    Core::QueueSubmissionToken m_raster;
    Core::QueueSubmissionToken m_finalConsumer;
    GpuCommandRecordingMode::Enum m_recordingMode = GpuCommandRecordingMode::Direct;
    b8 m_prepared = false;
    u32 m_presentationMode = NWB_UI_PRESENTATION_SDR;


public:
    explicit GpuFrameData(Core::Alloc::GlobalArena& arena)noexcept;


public:
    void reset(DrawSnapshot&& snapshot, GpuVersion<GpuSkinVersion> skin)noexcept;


public:
    [[nodiscard]] b8 prefixComplete(Core::Device& device)const;
    [[nodiscard]] b8 recyclable(Core::Device& device)const;
    [[nodiscard]] b8 complete(Core::Device& device)const;
};

using GpuFrame = GpuVersion<GpuFrameData>;

struct GpuFrameSlot{
    GpuVersion<GpuTargetVersion> target;
    Core::BufferHandle vertices;
    Core::BufferHandle indices;
    usize vertexCapacity = 0u;
    usize indexCapacity = 0u;
    GpuFrame inFlight;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Call-scoped calculation scratch shared by native commands and command IR; never retained or uploaded.
struct GpuRasterGeometry{
    SIMDVectorSingle pixelScale;
    SIMDVectorSingle targetSize;


public:
    GpuRasterGeometry(const DisplayMetrics& display, const Core::TextureDesc& target)noexcept
        : pixelScale(VectorSet(display.pixelScaleX, display.pixelScaleY, display.pixelScaleX, display.pixelScaleY))
        , targetSize(VectorSet(static_cast<f32>(target.width), static_cast<f32>(target.height), static_cast<f32>(target.width), static_cast<f32>(target.height)))
    {}


public:
    [[nodiscard]] Expected<GpuPaintPushConstants> initializePushConstants(const u32 samplerSlot)const noexcept{
        constexpr f32 s_ClipSpaceExtent = 2.0f;
        const SIMDVectorSingle projection = VectorMultiply(VectorSet(s_ClipSpaceExtent, -s_ClipSpaceExtent, s_ClipSpaceExtent, -s_ClipSpaceExtent), pixelScale);
        const SIMDVectorSingle projectionScale = VectorDivide(projection, targetSize);
        GpuPaintPushConstants push;
        push.scale = { VectorGetX(projectionScale), VectorGetY(projectionScale) };
        push.translate = { -1.0f, 1.0f };
        push.samplerSlot = samplerSlot;
        if(!IsFinite(push.scale.x) || !IsFinite(push.scale.y))
            return MakeUnexpected(Failure{});
        return push;
    }

    [[nodiscard]] Optional<Core::Rect> scissor(const DrawCommand& draw)const noexcept{
        const SIMDVectorSingle clip = VectorSet(draw.clip.x, draw.clip.y, draw.clip.x, draw.clip.y);
        const SIMDVectorSingle extent = VectorSet(0.0f, 0.0f, draw.clip.width, draw.clip.height);
        const SIMDVectorSingle ends = VectorAdd(clip, extent);
        const SIMDVectorSingle coordinates = VectorPermute<0, 1, 6, 7>(clip, ends);
        const SIMDVectorSingle physicalClip = VectorMultiply(coordinates, pixelScale);
        const SIMDVectorSingle first = VectorFloor(VectorSwizzle<0, 1, 0, 1>(physicalClip));
        const SIMDVectorSingle last = VectorCeiling(VectorSwizzle<2, 3, 2, 3>(physicalClip));
        const SIMDVectorSingle minimum = VectorSelect(first, VectorZero(), VectorGreater(VectorZero(), first));
        const SIMDVectorSingle maximum = VectorSelect(last, targetSize, VectorLess(targetSize, last));
        const f32 minX = VectorGetX(minimum);
        const f32 minY = VectorGetY(minimum);
        const f32 maxX = VectorGetX(maximum);
        const f32 maxY = VectorGetY(maximum);
        if(maxX <= minX || maxY <= minY)
            return {};
        return Core::Rect(static_cast<i32>(minX), static_cast<i32>(maxX), static_cast<i32>(minY), static_cast<i32>(maxY));
    }
};

// Resolve material descriptors in place; non-SDF draws preserve the previous SDF push fields.
[[nodiscard]] NWB_INLINE b8 ResolvePaintPushConstants(const GpuFrameData& frame, const DrawCommand& draw, GpuPaintPushConstants& push)noexcept{
    push.material = static_cast<u32>(draw.material);
    push.textureSlot = NWB_UI_INVALID_HEAP_SLOT;
    if(draw.material == PaintMaterial::Skin)
        push.textureSlot = frame.m_skin->m_texture.sampledImageHeapHandle.slot();
    else if(draw.material == PaintMaterial::Glyph){
        if(draw.glyphPageIndex >= frame.m_glyphPages.size())
            return false;
        push.textureSlot = frame.m_glyphPages[draw.glyphPageIndex]->m_sampledImage.slot();
    }
    else if(draw.material == PaintMaterial::SdfGlyph){
        if(draw.sdfPageIndex >= frame.m_sdfPages.size())
            return false;
        const auto& page = frame.m_sdfPages[draw.sdfPageIndex];
        push.textureSlot = page->m_sampledImage.slot();
        push.sdfChannel = draw.sdfChannel;
        push.sdfSpreadPixels = page->m_page->binding().spreadPixels;
        push.sdfDistanceEncoding = page->m_page->binding().distanceEncoding;
    }
    else if(draw.material == PaintMaterial::Image){
        if(draw.textureImageIndex >= frame.m_textureImages.size())
            return false;
        push.textureSlot = frame.m_textureImages[draw.textureImageIndex]->m_texture.sampledImageHeapHandle.slot();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuRasterTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics, false };
    struct Payload{
        GpuFrame frame;
        Core::GpuGraphResourceId color;
        Core::GpuGraphResourceId skin;
        Core::GpuGraphPipelineId pipeline;
        Core::GpuGraphResourceId vertices;
        Core::GpuGraphResourceId indices;
        GpuGlyphGraphResources glyphPages;
        GpuSdfGraphResources sdfPages;
        GpuTextureGraphResources textureImages;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context);
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;
};

struct GpuOutputTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics, true };
    struct Payload{
        GpuFrame frame;
        Core::GpuGraphResourceId backBuffer;
        Core::GpuGraphResourceId color;
        Core::GpuGraphPipelineId graphPipeline;
        Core::AcquiredPresentationFrame acquired;
        Core::GraphicsPipelineHandle pipeline;
        u32 presentationMode = NWB_UI_PRESENTATION_SDR;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context);
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuRendererState final : NoCopy{
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assets;
    GpuRenderer::ShaderPathResolveCallback m_resolver;
    const ActiveSceneSubsystems* m_activeSceneSubsystems = nullptr;
    GpuVersion<GpuSharedResources> m_resources;
    GpuVersion<GpuSkinVersion> m_skin;
    Vector<GpuVersion<GpuGlyphVersion>, Core::Alloc::GlobalArena> m_glyphCache;
    Vector<GpuVersion<GpuSdfAtlasVersion>, Core::Alloc::GlobalArena> m_sdfCache;
    Vector<GpuVersion<GpuTextureImageVersion>, Core::Alloc::GlobalArena> m_textureCache;
    Array<GpuFrameSlot, s_GpuTargetSlotCount> m_slots;
    Array<GpuFrame, s_GpuFrameCount> m_frames;
    GpuFrame m_pending;
    Core::AcquiredPresentationFrame m_lastAcceptedAcquired;
    Core::IGpuTaskGraphPresentationContributor* m_presentationContributor = nullptr;
    const Core::GpuTaskGraph* m_declaredGraph = nullptr;
    Core::GpuTaskGraphOutputLayer m_declaredLayer;
    u64 m_graphGeneration = 0u;
    u64 m_lastAcceptedGeneration = 0u;
    GpuCommandRecordingMode::Enum m_recordingMode = GpuCommandRecordingMode::Direct;
    u32 m_width = 0u;
    u32 m_height = 0u;
    b8 m_frameTimingScopePrepared = false;
    b8 m_claimed = false;
    b8 m_readyToDeclare = false;


public:
    [[nodiscard]] static b8 MatchesAcquired(const Core::AcquiredPresentationFrame& first, const Core::AcquiredPresentationFrame& second)noexcept;
    [[nodiscard]] static b8 ValidateGlyphPages(const DrawSnapshot& snapshot)noexcept;
    [[nodiscard]] static b8 ValidateSdfPages(const DrawSnapshot& snapshot)noexcept;
    [[nodiscard]] static b8 ValidateTextureImages(const DrawSnapshot& snapshot)noexcept;


public:
    GpuRendererState(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assets, GpuRenderer::ShaderPathResolveCallback resolver);


public:
    [[nodiscard]] b8 createResources();
    [[nodiscard]] Expected<GpuVersion<GpuTargetVersion>> createTarget(u32 width, u32 height);
    [[nodiscard]] Expected<GpuVersion<GpuGlyphVersion>> prepareGlyphPage(const SharedGlyphPage& page);
    [[nodiscard]] b8 prepareGlyphPages(GpuFrameData& frame);
    [[nodiscard]] Expected<GpuGlyphGraphResources> declareGlyphPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses);
    [[nodiscard]] Expected<GpuVersion<GpuSdfAtlasVersion>> prepareSdfPage(Core::Alloc::ScratchArena& scratchArena, const SharedSdfAtlasPage& page);
    [[nodiscard]] b8 prepareSdfPages(GpuFrameData& frame);
    [[nodiscard]] Expected<GpuSdfGraphResources> declareSdfPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses);
    [[nodiscard]] Expected<GpuVersion<GpuTextureImageVersion>> prepareTextureImage(const SharedImageSource& source);
    [[nodiscard]] b8 prepareTextureImages(GpuFrameData& frame);
    [[nodiscard]] Expected<GpuTextureGraphResources> declareTextureImages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses);
    void trimImageCache(const DrawSnapshot& snapshot);
    void evictImageCacheEntry()noexcept;
    [[nodiscard]] b8 prepareBuffers(GpuFrameSlot& slot, const DrawSnapshot& snapshot);
    [[nodiscard]] b8 prepareOutputPipeline(const Core::FramebufferInfo& info);
    [[nodiscard]] b8 prepare(const Core::AcquiredPresentationFrame& acquired);
    [[nodiscard]] Expected<Core::GpuTaskGraphOutputLayer> declare(Core::GpuTaskGraph& graph, b8 allowEmptySceneLayer);
    [[nodiscard]] Expected<Core::GpuTaskId> declareStandalone(Core::GpuTaskGraph& graph, Core::GpuTimingFrameTransaction& frameTimingTransaction);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

