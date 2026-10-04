// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer.h"
#include "renderer_internal.h"
#include "renderer_frame_timing_task.h"

#include <core/common/log.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::GlobalUniquePtr<GpuRendererState> CreateState(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assets,
    GpuRenderer::ShaderPathResolveCallback resolver){
    Core::GlobalUniquePtr<GpuRendererState> state = Core::MakeGlobalUnique<GpuRendererState>(arena, arena, graphics, assets, Move(resolver));
    if(!state)
        TerminateInvariant();
    return state;
}

[[nodiscard]] static bool TokenComplete(Core::Device& device, const Core::QueueSubmissionToken& token){
    if(!token.valid())
        return true;
    if(!token.hasPhysicalQueueIdentity())
        return false;
    const Core::GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
    const Core::GpuQueueTimelineSnapshot timeline = device.getQueueTimelineSnapshot(queue);
    return timeline.valid() && timeline.queue == queue && timeline.completedValue >= token.value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuSkinVersion::GpuSkinVersion(Core::GraphicsRuntime& graphics)
    : m_graphics(graphics)
{}

GpuSkinVersion::~GpuSkinVersion()noexcept{
    TextureAssetLoader::Release(m_texture, m_graphics);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTargetVersion::GpuTargetVersion(Core::GraphicsRuntime& graphics)
    : m_graphics(graphics)
{}

GpuTargetVersion::~GpuTargetVersion()noexcept{
    if(m_sampledImage.valid())
        m_graphics.getDevice().getDescriptorHeap().free(m_sampledImage);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuSharedResources::GpuSharedResources(Core::GraphicsRuntime& graphics)
    : m_graphics(graphics)
{}

GpuSharedResources::~GpuSharedResources()noexcept{
    if(m_samplerDescriptor.valid())
        m_graphics.getDevice().getDescriptorHeap().free(m_samplerDescriptor);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuFrameData::GpuFrameData(Core::Alloc::GlobalArena& arena, DrawSnapshot&& snapshot, GpuVersion<GpuSkinVersion> skin)
    : m_arena(arena)
    , m_snapshot(Move(snapshot))
    , m_skin(Move(skin))
    , m_glyphPages(arena)
    , m_sdfPages(arena)
    , m_textureImages(arena)
{}

bool GpuFrameData::prefixComplete(Core::Device& device)const{
    return
        __hidden_ui_gpu::TokenComplete(device, m_vertexUpload)
        && __hidden_ui_gpu::TokenComplete(device, m_indexUpload)
        && __hidden_ui_gpu::TokenComplete(device, m_clear)
        && __hidden_ui_gpu::TokenComplete(device, m_raster)
    ;
}

bool GpuFrameData::complete(Core::Device& device)const{
    if(!m_finalConsumer.valid() || !__hidden_ui_gpu::TokenComplete(device, m_finalConsumer) || !prefixComplete(device))
        return false;
    for(const auto& page : m_glyphPages){
        if(!__hidden_ui_gpu::TokenComplete(device, page->m_readinessToken))
            return false;
    }
    for(const auto& page : m_sdfPages){
        if(!__hidden_ui_gpu::TokenComplete(device, page->m_readinessToken))
            return false;
    }
    for(const auto& image : m_textureImages){
        if(!__hidden_ui_gpu::TokenComplete(device, image->m_texture.readinessToken))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuRendererState::GpuRendererState(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assets,
    GpuRenderer::ShaderPathResolveCallback resolver)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_assets(assets)
    , m_resolver(Move(resolver))
    , m_glyphCache(arena)
    , m_sdfCache(arena)
    , m_textureCache(arena)
{
    m_glyphCache.reserve(s_GpuMaxGlyphPages);
    m_sdfCache.reserve(s_GpuMaxSdfPages);
    m_textureCache.reserve(s_GpuMaxTextureImages);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuRenderer::GpuRenderer(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver)
    : m_state(MakeNotNullUnique(__hidden_ui_gpu::CreateState(arena, graphics, assetManager, Move(shaderPathResolver))))
{}

GpuRenderer::~GpuRenderer(){
    invalidateResources();
}

bool GpuRenderer::validateResources(const u32 width, const u32 height){
    if(width == 0u || height == 0u || width > static_cast<u32>(Limit<i32>::s_Max) || height > static_cast<u32>(Limit<i32>::s_Max))
        return false;
    m_state->m_frameTimingScopePrepared = m_state->m_graphics.gpuTiming().prepareScopeQueries(
        GpuRendererTimingScope::s_Frame.identity, m_state->m_graphics.getDevice(), 2u
    );
    if(!m_state->m_frameTimingScopePrepared)
        NWB_LOGGER_WARNING(GLB_TEXT("GpuRenderer: could not prepare standalone frame timing scope"));
    if(m_state->m_width == width && m_state->m_height == height && m_state->m_resources)
        return true;
    if(m_state->m_pending)
        return false;
    if(!m_state->createResources())
        return false;
    Array<GpuVersion<GpuTargetVersion>, 3u> targets;
    for(auto& target : targets){
        target = m_state->createTarget(width, height);
        if(!target)
            return false;
    }
    for(usize i = 0u; i < targets.size(); ++i){
        m_state->m_slots[i] = {};
        m_state->m_slots[i].target = Move(targets[i]);
    }
    m_state->m_width = width;
    m_state->m_height = height;
    return true;
}

void GpuRenderer::invalidateResources(){
    m_state->m_pending.reset();
    for(auto& slot : m_state->m_slots)
        slot = {};
    m_state->m_skin.reset();
    m_state->m_glyphCache.clear();
    m_state->m_sdfCache.clear();
    m_state->m_textureCache.clear();
    m_state->m_resources.reset();
    m_state->m_width = 0u;
    m_state->m_height = 0u;
    m_state->m_frameTimingScopePrepared = false;
    m_state->m_claimed = false;
    m_state->m_declaredGraph = nullptr;
    m_state->m_graphGeneration = 0u;
    m_state->m_declaredLayer = {};
    m_state->m_readyToDeclare = false;
    m_state->m_lastAcceptedAcquired = {};
    m_state->m_lastAcceptedGeneration = 0u;
    m_state->m_presentationContributor = nullptr;
}

bool GpuRenderer::setSkin(
    const Core::Assets::AssetRef<UiSkin>& identity,
    const UiSkin& skin,
    const u64 skinGeneration){
    if(!identity.valid() || skinGeneration == 0u || !m_state->m_resources || !skin.validatePayload())
        return false;
    const SkinBinding binding{ identity, skin.texture(), skinGeneration, skin.atlasWidth(), skin.atlasHeight(), skin.referenceDensity() };
    if(m_state->m_skin){
        const SkinBinding& existing = m_state->m_skin->m_binding;
        if(existing.skin == binding.skin && existing.texture == binding.texture && existing.generation == binding.generation)
            return
                existing.atlasWidth == binding.atlasWidth && existing.atlasHeight == binding.atlasHeight
                && existing.referenceDensity == binding.referenceDensity
            ;
    }
    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Texture* texture = m_state->m_assets.loadTypedSync<Texture>(
        binding.texture.name(), loadedAsset, GLB_TEXT("GpuRenderer"), "UI atlas texture"
    );
    if(!texture || !skin.validateTexture(*texture))
        return false;
    GpuVersion<GpuSkinVersion> version = MakeGpuVersion<GpuSkinVersion>(m_state->m_arena, m_state->m_graphics);
    if(!version)
        return false;
    version->m_binding = binding;
    if(!TextureAssetLoader::Create(
        version->m_texture, *texture, Name("ui.atlas"), m_state->m_graphics, GLB_TEXT("GpuRenderer")
    ))
        return false;
    if(!version->m_texture.readinessToken.valid() || !version->m_texture.readinessToken.hasPhysicalQueueIdentity())
        return false;
    m_state->m_skin = Move(version);
    return true;
}

bool GpuRenderer::submit(DrawSnapshot&& snapshot){
    if(m_state->m_pending || !m_state->m_resources || !m_state->m_skin || snapshot.generation() == 0u)
        return false;
    const SkinBinding& binding = snapshot.skinBinding();
    const SkinBinding& loaded = m_state->m_skin->m_binding;
    if(
        binding.skin != loaded.skin || binding.texture != loaded.texture || binding.generation != loaded.generation
        || binding.atlasWidth != loaded.atlasWidth || binding.atlasHeight != loaded.atlasHeight
        || binding.referenceDensity != loaded.referenceDensity
    )
        return false;
    const DisplayMetrics& display = snapshot.displayMetrics();
    if(
        !IsFinite(display.logicalWidth) || !IsFinite(display.logicalHeight) || !IsFinite(display.pixelScaleX)
        || !IsFinite(display.pixelScaleY) || display.logicalWidth <= 0.0f || display.logicalHeight <= 0.0f
        || display.pixelScaleX <= 0.0f || display.pixelScaleY <= 0.0f
    )
        return false;
    if(
        !GpuRendererState::validateGlyphPages(snapshot) || !GpuRendererState::validateSdfPages(snapshot)
        || !GpuRendererState::validateTextureImages(snapshot)
    )
        return false;
    m_state->m_pending = MakeGpuVersion<GpuFrameData>(
        m_state->m_arena, m_state->m_arena, Move(snapshot), m_state->m_skin
    );
    if(m_state->m_pending)
        m_state->m_pending->m_recordingMode = m_state->m_recordingMode;
    m_state->m_claimed = false;
    return m_state->m_pending != nullptr;
}

void GpuRenderer::setCommandRecordingMode(const GpuCommandRecordingMode::Enum mode){
    m_state->m_recordingMode = mode;
}

bool GpuRenderer::hasPendingFrame()const{
    return m_state->m_pending != nullptr;
}

u64 GpuRenderer::lastAcceptedGeneration()const{
    return m_state->m_lastAcceptedGeneration;
}

Core::PresentationReceiptStatus::Enum GpuRenderer::lastAcceptedPresentationStatus()const{
    return m_state->m_graphics.lastPresentationReceipt().status(m_state->m_lastAcceptedAcquired.backBuffer);
}

bool GpuRenderer::prepareTaskGraphOutputLayer(const Core::AcquiredPresentationFrame& frame){
    return m_state->prepare(frame);
}

bool GpuRenderer::declareTaskGraphOutputLayer(Core::GpuTaskGraph& graph, Core::GpuTaskGraphOutputLayer& outLayer){
    return m_state->declare(graph, outLayer, true);
}

void GpuRenderer::acceptTaskGraphOutputLayer(const u64 frameGeneration, const Core::QueueSubmissionToken& submissionToken){
    if(!m_state->m_pending || m_state->m_pending->m_snapshot.generation() != frameGeneration)
        return;
    GLB_FATAL_ASSERT(submissionToken.valid() && submissionToken.hasPhysicalQueueIdentity());
    m_state->m_pending->m_finalConsumer = submissionToken;
    m_state->m_lastAcceptedAcquired = m_state->m_pending->m_acquired;
    m_state->m_lastAcceptedGeneration = frameGeneration;
    m_state->m_pending.reset();
    m_state->m_claimed = false;
    m_state->m_declaredGraph = nullptr;
    m_state->m_declaredLayer = {};
    m_state->m_readyToDeclare = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

