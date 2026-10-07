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

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/presentation_contributor.h>
#include <core/task/gpu/task_graph.h>

#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuPaintPushConstants{
#define NWB_UI_FLOAT2_FIELD(name) Point name;
#define NWB_UI_UINT_FIELD(name, value) u32 name = value;
    NWB_UI_PUSH_CONSTANTS_FIELDS(NWB_UI_FLOAT2_FIELD, NWB_UI_UINT_FIELD)
#undef NWB_UI_UINT_FIELD
#undef NWB_UI_FLOAT2_FIELD
};
struct GpuOutputPushConstants{
#define NWB_UI_UINT_FIELD(name, value) u32 name = value;
    NWB_UI_OUTPUT_PUSH_CONSTANTS_FIELDS(NWB_UI_UINT_FIELD)
#undef NWB_UI_UINT_FIELD
};
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
static_assert(offsetof(GpuPaintPushConstants, reserved) == NWB_UI_PUSH_CONSTANTS_RESERVED_BYTE_OFFSET);
static_assert(offsetof(GpuPaintPushConstants, reserved1) == NWB_UI_PUSH_CONSTANTS_RESERVED1_BYTE_OFFSET);
static_assert(sizeof(GpuOutputPushConstants) == NWB_UI_OUTPUT_PUSH_CONSTANTS_BYTE_SIZE);
static_assert(offsetof(GpuOutputPushConstants, textureSlot) == NWB_UI_OUTPUT_PUSH_CONSTANTS_TEXTURE_SLOT_BYTE_OFFSET);
static_assert(offsetof(GpuOutputPushConstants, samplerSlot) == NWB_UI_OUTPUT_PUSH_CONSTANTS_SAMPLER_SLOT_BYTE_OFFSET);
static_assert(offsetof(GpuOutputPushConstants, presentationMode) == NWB_UI_OUTPUT_PUSH_CONSTANTS_PRESENTATION_MODE_BYTE_OFFSET);
static_assert(offsetof(GpuOutputPushConstants, reserved) == NWB_UI_OUTPUT_PUSH_CONSTANTS_RESERVED_BYTE_OFFSET);
static_assert(static_cast<u32>(PaintMaterial::Solid) == NWB_UI_MATERIAL_SOLID);
static_assert(static_cast<u32>(PaintMaterial::Skin) == NWB_UI_MATERIAL_SKIN);
static_assert(static_cast<u32>(PaintMaterial::Glyph) == NWB_UI_MATERIAL_GLYPH);
static_assert(static_cast<u32>(PaintMaterial::SdfGlyph) == NWB_UI_MATERIAL_SDF_GLYPH);
static_assert(static_cast<u32>(PaintMaterial::Image) == NWB_UI_MATERIAL_IMAGE);
static_assert(s_SdfDistanceEncodingFreeTypeU8 == NWB_UI_SDF_DISTANCE_ENCODING_FREETYPE_U8);

inline constexpr usize s_GpuMaxGlyphPages = s_PaintMaxImages;
inline constexpr usize s_GpuMaxSdfPages = s_PaintMaxImages;
inline constexpr usize s_GpuMaxTextureImages = s_PaintMaxImages;
using GpuRasterResourceUses = FixedVector<Core::GpuTaskResourceUse, s_GpuMaxGlyphPages + 4u>;
using GpuGlyphGraphResources = FixedVector<Core::GpuGraphResourceId, s_GpuMaxGlyphPages>;
using GpuSdfGraphResources = FixedVector<Core::GpuGraphResourceId, s_GpuMaxSdfPages>;
using GpuTextureGraphResources = FixedVector<Core::GpuGraphResourceId, s_GpuMaxTextureImages>;


template<typename T>
using GpuVersion = RefCountPtr<RefCounter<T>, ArenaRefDeleter<RefCounter<T>, Core::Alloc::GlobalArena>>;

template<typename T, typename... Args>
[[nodiscard]] GpuVersion<T> MakeGpuVersion(Core::Alloc::GlobalArena& arena, Args&&... args){
    return GpuVersion<T>(
        NewArenaObject<RefCounter<T>>(arena, Forward<Args>(args)...),
        ArenaRefDeleter<RefCounter<T>, Core::Alloc::GlobalArena>(&arena),
        s_AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuSkinVersion : NoCopy{
    explicit GpuSkinVersion(Core::GraphicsRuntime& graphics);
    ~GpuSkinVersion()noexcept;

    Core::GraphicsRuntime& m_graphics;
    SkinBinding m_binding;
    TextureGpuResource m_texture;
};

struct GpuGlyphVersion : NoCopy{
    GpuGlyphVersion(Core::GraphicsRuntime& graphics, SharedGlyphPage page);
    ~GpuGlyphVersion()noexcept;

    Core::GraphicsRuntime& m_graphics;
    SharedGlyphPage m_page;
    Core::TextureHandle m_texture;
    Core::GpuDescriptorHandle m_sampledImage;
    Core::QueueSubmissionToken m_readinessToken;
};

struct GpuTargetVersion : NoCopy{
    explicit GpuTargetVersion(Core::GraphicsRuntime& graphics);
    ~GpuTargetVersion()noexcept;

    Core::GraphicsRuntime& m_graphics;
    Core::TextureHandle m_color;
    Core::FramebufferHandle m_framebuffer;
    Core::GpuDescriptorHandle m_sampledImage;
    Core::QueueSubmissionToken m_readinessToken;
};

struct GpuSharedResources : NoCopy{
    explicit GpuSharedResources(Core::GraphicsRuntime& graphics);
    ~GpuSharedResources()noexcept;

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
};

struct GpuFrameData : NoCopy{
    GpuFrameData(Core::Alloc::GlobalArena& arena, DrawSnapshot&& snapshot, GpuVersion<GpuSkinVersion> skin);
    [[nodiscard]] bool prefixComplete(Core::Device& device)const;
    [[nodiscard]] bool complete(Core::Device& device)const;

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
    u32 m_presentationMode = NWB_UI_PRESENTATION_SDR;
    bool m_prepared = false;
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


// Resolve material descriptors in place; non-SDF draws preserve the previous SDF push fields.
[[nodiscard]] NWB_INLINE bool ResolvePaintPushConstants(
    const GpuFrameData& frame,
    const DrawCommand& draw,
    GpuPaintPushConstants& push
)noexcept{
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
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context);
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
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context);
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuRendererState final : NoCopy{
    GpuRendererState(
        Core::Alloc::GlobalArena& arena,
        Core::GraphicsRuntime& graphics,
        Core::Assets::AssetManager& assets,
        GpuRenderer::ShaderPathResolveCallback resolver
    );
    [[nodiscard]] static bool MatchesAcquired(
        const Core::AcquiredPresentationFrame& first,
        const Core::AcquiredPresentationFrame& second
    );
    [[nodiscard]] static bool ValidateGlyphPages(const DrawSnapshot& snapshot);
    [[nodiscard]] static bool ValidateSdfPages(const DrawSnapshot& snapshot);
    [[nodiscard]] static bool ValidateTextureImages(const DrawSnapshot& snapshot);
    [[nodiscard]] bool createResources();
    [[nodiscard]] GpuVersion<GpuTargetVersion> createTarget(u32 width, u32 height);
    [[nodiscard]] GpuVersion<GpuGlyphVersion> prepareGlyphPage(const SharedGlyphPage& page);
    [[nodiscard]] bool prepareGlyphPages(GpuFrameData& frame);
    [[nodiscard]] bool declareGlyphPages(
        Core::GpuTaskGraph& graph,
        const GpuFrame& frame,
        GpuGlyphGraphResources& resources,
        GpuRasterResourceUses& uses
    );
    [[nodiscard]] GpuVersion<GpuSdfAtlasVersion> prepareSdfPage(const SharedSdfAtlasPage& page);
    [[nodiscard]] bool prepareSdfPages(GpuFrameData& frame);
    [[nodiscard]] bool declareSdfPages(
        Core::GpuTaskGraph& graph,
        const GpuFrame& frame,
        GpuSdfGraphResources& resources,
        GpuRasterResourceUses& uses
    );
    [[nodiscard]] GpuVersion<GpuTextureImageVersion> prepareTextureImage(const SharedImageSource& source);
    [[nodiscard]] bool prepareTextureImages(GpuFrameData& frame);
    [[nodiscard]] bool declareTextureImages(
        Core::GpuTaskGraph& graph,
        const GpuFrame& frame,
        GpuTextureGraphResources& resources,
        GpuRasterResourceUses& uses
    );
    void trimImageCache(const DrawSnapshot& snapshot);
    void evictImageCacheEntry();
    [[nodiscard]] bool prepareBuffers(GpuFrameSlot& slot, const DrawSnapshot& snapshot);
    [[nodiscard]] bool prepareOutputPipeline(const Core::AcquiredPresentationFrame& acquired);
    [[nodiscard]] bool prepare(const Core::AcquiredPresentationFrame& acquired);
    [[nodiscard]] bool declare(Core::GpuTaskGraph& graph, Core::GpuTaskGraphOutputLayer& outLayer, bool allowEmptySceneLayer);
    [[nodiscard]] Core::GpuTaskId declareStandalone(
        Core::GpuTaskGraph& graph,
        Core::GpuTimingFrameTransaction& frameTimingTransaction
    );

    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assets;
    GpuRenderer::ShaderPathResolveCallback m_resolver;
    GpuVersion<GpuSharedResources> m_resources;
    GpuVersion<GpuSkinVersion> m_skin;
    Vector<GpuVersion<GpuGlyphVersion>, Core::Alloc::GlobalArena> m_glyphCache;
    Vector<GpuVersion<GpuSdfAtlasVersion>, Core::Alloc::GlobalArena> m_sdfCache;
    Vector<GpuVersion<GpuTextureImageVersion>, Core::Alloc::GlobalArena> m_textureCache;
    Array<GpuFrameSlot, 3u> m_slots;
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
    bool m_frameTimingScopePrepared = false;
    bool m_claimed = false;
    bool m_readyToDeclare = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

