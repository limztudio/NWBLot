// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer.h"
#include "renderer_internal.h"
#include "image_resources.h"
#include "renderer_frame_timing_task.h"
#include <core/common/log.h>
#include <global/termination.h>
#include <core/task/gpu/frame_timing_begin_task.h>
#include <core/task/gpu/capture/command_ir_raster.h>
#include <core/task/gpu/compiled_graph.h>
#include "target_factory.h"
#include "shared_resources_factory.h"
#include <core/graphics/rhi/format.h>
#include <global/binary.h>
#include <global/range_algorithms.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::GlobalUniquePtr<GpuRendererState> CreateState(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assets, GpuRenderer::ShaderPathResolveCallback resolver){
    Core::GlobalUniquePtr<GpuRendererState> state = Core::MakeGlobalUnique<GpuRendererState>(arena, arena, graphics, assets, Move(resolver));
    if(!state)
        TerminateInvariant();
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuFrameData::reset(DrawSnapshot&& snapshot, GpuVersion<GpuSkinVersion> skin)noexcept{
    m_snapshot = Move(snapshot);
    m_skin = Move(skin);
    m_glyphPages.clear();
    m_sdfPages.clear();
    m_textureImages.clear();
    m_target.reset();
    m_resources.reset();
    m_vertices.reset();
    m_indices.reset();
    m_outputPipeline.reset();
    m_acquired = {};
    m_vertexUpload = {};
    m_indexUpload = {};
    m_clear = {};
    m_raster = {};
    m_finalConsumer = {};
    m_recordingMode = GpuCommandRecordingMode::Direct;
    m_presentationMode = NWB_UI_PRESENTATION_SDR;
    m_prepared = false;
}

b8 GpuFrameData::prefixComplete(Core::Device& device)const{
    return GpuTokenComplete(device, m_vertexUpload) && GpuTokenComplete(device, m_indexUpload) && GpuTokenComplete(device, m_clear) && GpuTokenComplete(device, m_raster);
}

b8 GpuFrameData::recyclable(Core::Device& device)const{
    if(!GpuTokenComplete(device, m_finalConsumer) || !prefixComplete(device))
        return false;
    for(const auto& page : m_glyphPages){
        if(!GpuTokenComplete(device, page->m_readinessToken))
            return false;
    }
    for(const auto& page : m_sdfPages){
        if(!GpuTokenComplete(device, page->m_readinessToken))
            return false;
    }
    for(const auto& image : m_textureImages){
        if(!GpuTokenComplete(device, image->m_texture.readinessToken))
            return false;
    }
    return true;
}

b8 GpuFrameData::complete(Core::Device& device)const{
    return m_finalConsumer.valid() && recyclable(device);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuRendererState::GpuRendererState(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assets, GpuRenderer::ShaderPathResolveCallback resolver)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_assets(assets)
    , m_resolver(Move(resolver))
    , m_imageCache(arena)
{
    m_imageCache.reserve();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuRenderer::GpuRenderer(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, ShaderPathResolveCallback shaderPathResolver)
    : m_state(MakeNotNullUnique(__hidden_ui_gpu::CreateState(arena, graphics, assetManager, Move(shaderPathResolver))))
{}

GpuRenderer::~GpuRenderer()noexcept{
    invalidateResources();
}

void GpuRenderer::setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{
    m_state->m_activeSceneSubsystems = subsystems;
}

b8 GpuRenderer::validateResources(const u32 width, const u32 height){
    if(!IsValidGpuRendererExtent(width, height))
        return false;
    m_state->m_frameTimingScopePrepared = m_state->m_graphics.gpuTiming().prepareScopeQueries(GpuRendererTimingScope::s_Frame.identity, m_state->m_graphics.getDevice(), s_GpuFrameTimingQueryCount);
    if(!m_state->m_frameTimingScopePrepared)
        NWB_LOGGER_WARNING(NWB_TEXT("GpuRenderer: could not prepare standalone frame timing scope"));
    if(m_state->m_width == width && m_state->m_height == height && m_state->m_resources)
        return true;
    if(m_state->m_pending)
        return false;
    if(!m_state->createResources())
        return false;
    Core::Framebuffer* const framebuffer = m_state->m_graphics.getFramebuffer(0u);
    if(!framebuffer || !m_state->prepareOutputPipeline(framebuffer->getFramebufferInfo()))
        return false;
    Array<GpuFrame, s_GpuFrameCount> frames;
    for(auto& frame : frames){
        auto created = MakeGpuVersion<GpuFrameData>(m_state->m_arena, m_state->m_arena);
        if(!created)
            return false;
        frame = Move(*created);
    }
    Array<GpuVersion<GpuTargetVersion>, s_GpuTargetSlotCount> targets;
    for(auto& target : targets){
        auto created = m_state->createTarget(width, height);
        if(!created)
            return false;
        target = Move(*created);
    }
    for(usize i = 0u; i < targets.size(); ++i){
        m_state->m_slots[i] = {};
        m_state->m_slots[i].target = Move(targets[i]);
    }
    m_state->m_frames = Move(frames);
    m_state->m_width = width;
    m_state->m_height = height;
    return true;
}

void GpuRenderer::invalidateResources()noexcept{
    m_state->m_pending.reset();
    for(auto& slot : m_state->m_slots)
        slot = {};
    for(auto& frame : m_state->m_frames)
        frame.reset();
    m_state->m_skin.reset();
    m_state->m_imageCache.clear();
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

b8 GpuRenderer::setSkin(const Core::Assets::AssetRef<UiSkin>& identity, const UiSkin& skin, const u64 skinGeneration){
    if(!identity.valid() || skinGeneration == 0u || !m_state->m_resources || !skin.validatePayload())
        return false;
    const SkinBinding binding{ identity, skin.texture(), skinGeneration, skin.atlasWidth(), skin.atlasHeight(), skin.referenceDensity() };
    if(m_state->m_skin){
        const SkinBinding& existing = m_state->m_skin->m_binding;
        if(SameSkinBindingKey(existing, binding))
            return SameSkinBinding(existing, binding);
    }
    auto loadedAsset = m_state->m_assets.loadTypedSync<Texture>(binding.texture.name(), s_GpuRendererContext);
    const Texture* texture = loadedAsset ? loadedAsset->get() : nullptr;
    if(!texture || !skin.validateTexture(*texture))
        return false;
    auto versionResult = MakeGpuVersion<GpuSkinVersion>(m_state->m_arena, m_state->m_graphics);
    if(!versionResult)
        return false;
    GpuVersion<GpuSkinVersion> version = Move(*versionResult);
    version->m_binding = binding;
    if(!TextureAssetLoader::Create(*texture, s_GpuAtlasTextureName, s_GpuRendererContext, m_state->m_graphics, version->m_texture))
        return false;
    if(!version->m_texture.readinessToken.valid() || !version->m_texture.readinessToken.hasPhysicalQueueIdentity())
        return false;
    m_state->m_skin = Move(version);
    return true;
}

b8 GpuRenderer::submit(DrawSnapshot&& snapshot)noexcept{
    if(m_state->m_pending || !m_state->m_resources || !m_state->m_skin || snapshot.generation() == 0u)
        return false;
    if(!SameSkinBinding(snapshot.skinBinding(), m_state->m_skin->m_binding))
        return false;
    if(!IsValidDisplayMetricsForGpuSubmit(snapshot.displayMetrics()))
        return false;
    if(!GpuImageResources::ValidateGlyphPages(snapshot) || !GpuImageResources::ValidateSdfPages(snapshot) || !GpuImageResources::ValidateTextureImages(snapshot))
        return false;
    Core::Device& device = m_state->m_graphics.getDevice();
    for(auto& slot : m_state->m_slots){
        if(slot.inFlight && slot.inFlight->complete(device))
            slot.inFlight.reset();
    }
    for(auto& frame : m_state->m_frames){
        // Completed GPU work can still have graph payload owners; mutate only an exclusively retained frame.
        if(!frame || frame->getReferenceCount() != 1u)
            continue;
        AtomicThreadFence(MemoryOrder::acquire);
        if(!frame->recyclable(device))
            continue;
        if(m_state->m_pending){
            if(frame->m_snapshot.generation() != 0u)
                frame->reset(DrawSnapshot(m_state->m_arena), {});
            continue;
        }
        frame->reset(Move(snapshot), m_state->m_skin);
        m_state->m_pending = frame;
    }
    if(!m_state->m_pending){
        m_state->m_claimed = false;
        return false;
    }
    m_state->m_pending->m_recordingMode = m_state->m_recordingMode;
    m_state->m_claimed = false;
    return true;
}

void GpuRenderer::setCommandRecordingMode(const GpuCommandRecordingMode::Enum mode)noexcept{
    m_state->m_recordingMode = mode;
}

b8 GpuRenderer::hasPendingFrame()const noexcept{
    return m_state->m_pending != nullptr;
}

u64 GpuRenderer::lastAcceptedGeneration()const noexcept{
    return m_state->m_lastAcceptedGeneration;
}

Core::PresentationReceiptStatus::Enum GpuRenderer::lastAcceptedPresentationStatus()const noexcept{
    return m_state->m_graphics.lastPresentationReceipt().status(m_state->m_lastAcceptedAcquired.backBuffer);
}

b8 GpuRenderer::prepareTaskGraphOutputLayer(const Core::AcquiredPresentationFrame& frame){
    return m_state->prepare(frame);
}

Expected<Core::GpuTaskGraphOutputLayer> GpuRenderer::declareTaskGraphOutputLayer(Core::GpuTaskGraph& graph){
    return m_state->declare(graph, true);
}

void GpuRenderer::acceptTaskGraphOutputLayer(const u64 frameGeneration, const Core::QueueSubmissionToken& submissionToken)noexcept{
    if(!m_state->m_pending || m_state->m_pending->m_snapshot.generation() != frameGeneration)
        return;
    NWB_FATAL_ASSERT(submissionToken.valid() && submissionToken.hasPhysicalQueueIdentity());
    m_state->m_pending->m_finalConsumer = submissionToken;
    m_state->m_lastAcceptedAcquired = m_state->m_pending->m_acquired;
    m_state->m_lastAcceptedGeneration = frameGeneration;
    m_state->m_pending.reset();
    m_state->m_claimed = false;
    m_state->m_declaredGraph = nullptr;
    m_state->m_graphGeneration = 0u;
    m_state->m_declaredLayer = {};
    m_state->m_readyToDeclare = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskGraphOutputLayer> GpuRendererState::declare(Core::GpuTaskGraph& graph, b8 allowEmptySceneLayer){
    Core::GpuTaskGraphOutputLayer layer;
    if(!m_pending || !m_pending->m_prepared || !m_readyToDeclare)
        return layer;
    const Core::AcquiredPresentationFrame& current = m_graphics.acquiredPresentationFrame();
    const Core::AcquiredPresentationFrame& prepared = m_pending->m_acquired;
    if(!MatchesAcquired(current, prepared))
        return layer;
    u64 generation = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        if(!view.valid())
            return MakeUnexpected(Failure{});
        generation = view.generation();
    }
    if(m_claimed && m_declaredGraph == &graph && m_graphGeneration == generation){
        layer = m_declaredLayer;
        return layer;
    }
    if(!m_pending->prefixComplete(m_graphics.getDevice()))
        return layer;
    const GpuFrame frame = m_pending;
    frame->m_vertexUpload = {};
    frame->m_indexUpload = {};
    frame->m_clear = {};
    frame->m_raster = {};
    const DrawSnapshot& snapshot = frame->m_snapshot;
    if(allowEmptySceneLayer && IsEmptyGpuSceneSnapshot(snapshot.commands().size(), snapshot.vertices().size(), snapshot.indices().size(), snapshot.glyphPages().size(), snapshot.sdfPages().size(), snapshot.textureImages().size())){
        layer.frameGeneration = snapshot.generation();
        m_declaredLayer = layer;
        m_declaredGraph = &graph;
        m_graphGeneration = generation;
        m_claimed = true;
        return layer;
    }
    const auto targetReadyResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc().setIdentity(s_GpuLayerReadyName).setMarkerLabel(s_GpuLayerReadyMarkerLabel).setToken(frame->m_target->m_readinessToken));
    if(!targetReadyResult)
        return MakeUnexpected(Failure{});
    const Core::GpuExternalCompletionId targetReady = *targetReadyResult;
    const auto colorResult = graph.importTexture(
        frame->m_target->m_color,
        Core::GpuGraphResourceDesc().setIdentity(s_GpuLayerTextureName).setMarkerLabel(s_GpuLayerMarkerLabel).setType(Core::GpuGraphResourceType::Texture)
            .setInitialAvailabilityCompletion(targetReady).setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    const auto skinReadyResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc().setIdentity(s_GpuAtlasReadyName).setMarkerLabel(s_GpuAtlasReadyMarkerLabel).setToken(frame->m_skin->m_texture.readinessToken));
    if(!colorResult || !skinReadyResult)
        return MakeUnexpected(Failure{});
    const Core::GpuExternalCompletionId skinReady = *skinReadyResult;
    const Core::GpuGraphResourceId color = *colorResult;
    const auto skinResult = graph.importTexture(
        frame->m_skin->m_texture.texture,
        Core::GpuGraphResourceDesc().setIdentity(s_GpuAtlasTextureName).setMarkerLabel(s_GpuAtlasMarkerLabel).setType(Core::GpuGraphResourceType::Texture)
            .setInitialAvailabilityCompletion(skinReady).setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    const auto versionResult = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc().setResource(color).setOrigin(Core::GpuGraphResourceVersionOrigin::TaskProduced));
    const auto pipelineResult = graph.importGraphicsPipeline(frame->m_resources->m_pipeline, Core::GpuGraphPipelineDesc().setIdentity(s_GpuRasterPipelineName).setMarkerLabel(s_GpuRasterPipelineMarkerLabel).setType(Core::GpuGraphPipelineType::Graphics));
    if(!skinResult || !versionResult || !pipelineResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceVersionId version = *versionResult;
    const Core::GpuGraphPipelineId pipeline = *pipelineResult;
    const Core::GpuGraphResourceId skin = *skinResult;
    Core::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Core::GpuTaskCostHint::Small;
    uploadScheduling.allowParallelRecording = true;
    uploadScheduling.allowSameClassQueueRouting = true;
    uploadScheduling.preferNonPrimarySameClassQueue = true;
    // Vertex upload, index upload, and clear.
    FixedVector<Core::GpuTaskId, s_GpuRasterDependencyCapacity> dependencies;
    GpuRasterResourceUses uses;
    Core::GpuGraphResourceId vertices;
    Core::GpuGraphResourceId indices;
    uses.push_back({ color, {}, Core::ResourceStates::RenderTarget, Core::GpuTaskResourceAccess::Write });
    uses.push_back({ skin, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read });
    GpuImageResources imageResources(m_arena, m_graphics, m_imageCache);
    auto glyphPages = imageResources.declareGlyphPages(graph, frame, uses);
    if(!glyphPages)
        return MakeUnexpected(Failure{});
    auto sdfPages = imageResources.declareSdfPages(graph, frame, uses);
    if(!sdfPages)
        return MakeUnexpected(Failure{});
    auto textureImages = imageResources.declareTextureImages(graph, frame, uses);
    if(!textureImages)
        return MakeUnexpected(Failure{});
    if(!frame->m_snapshot.vertices().empty()){
        const auto verticesResult = graph.importBuffer(
            frame->m_vertices,
            Core::GpuGraphResourceDesc().setIdentity(s_GpuVertexBufferName).setMarkerLabel(s_GpuVerticesMarkerLabel).setType(Core::GpuGraphResourceType::Buffer).setExternalFinalState(Core::ResourceStates::Common)
        );
        const auto indicesResult = graph.importBuffer(
            frame->m_indices,
            Core::GpuGraphResourceDesc().setIdentity(s_GpuIndexBufferName).setMarkerLabel(s_GpuIndicesMarkerLabel).setType(Core::GpuGraphResourceType::Buffer).setExternalFinalState(Core::ResourceStates::Common)
        );
        if(!verticesResult || !indicesResult)
            return MakeUnexpected(Failure{});
        vertices = *verticesResult;
        indices = *indicesResult;
        const auto vertexBytesResult = graph.copyUploadData(frame->m_snapshot.vertices().data(), frame->m_snapshot.vertices().size() * sizeof(Vertex), alignof(Vertex));
        const auto indexBytesResult = graph.copyUploadData(frame->m_snapshot.indices().data(), frame->m_snapshot.indices().size() * sizeof(u32), alignof(u32));
        if(!vertexBytesResult || !indexBytesResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId vertexBytes = *vertexBytesResult;
        const Core::GpuUploadBlobId indexBytes = *indexBytesResult;
        const auto vertexUploadResult = graph.addUploadBufferTask(
            Core::GpuTaskDesc().setIdentity(s_GpuVertexUploadTaskName).setMarkerLabel(s_GpuVertexUploadMarkerLabel).setScheduling(uploadScheduling),
            Core::GpuUploadBufferTaskDesc{ .source = vertexBytes, .destination = vertices, .finalState = Core::ResourceStates::Common, .acceptedToken = &frame->m_vertexUpload, });
        const auto indexUploadResult = graph.addUploadBufferTask(
            Core::GpuTaskDesc().setIdentity(s_GpuIndexUploadTaskName).setMarkerLabel(s_GpuIndexUploadMarkerLabel).setScheduling(uploadScheduling),
            Core::GpuUploadBufferTaskDesc{ .source = indexBytes, .destination = indices, .finalState = Core::ResourceStates::Common, .acceptedToken = &frame->m_indexUpload, });
        if(!vertexUploadResult || !indexUploadResult)
            return MakeUnexpected(Failure{});
        const Core::GpuTaskId vertexUpload = *vertexUploadResult;
        const Core::GpuTaskId indexUpload = *indexUploadResult;
        dependencies.push_back(vertexUpload);
        dependencies.push_back(indexUpload);
        uses.push_back({ vertices, {}, Core::ResourceStates::VertexBuffer, Core::GpuTaskResourceAccess::Read });
        uses.push_back({ indices, {}, Core::ResourceStates::IndexBuffer, Core::GpuTaskResourceAccess::Read });
    }
    const auto clearResult = graph.addClearTextureTask(
        Core::GpuTaskDesc().setIdentity(s_GpuClearTaskName).setMarkerLabel(s_GpuClearMarkerLabel).setScheduling(uploadScheduling),
        Core::GpuClearTextureTaskDesc{ .acceptedToken = &frame->m_clear, .destination = color, .valueType = Core::GpuClearTextureTaskValueType::Float, .floatValue = Core::Color(0.0f, 0.0f, 0.0f, 0.0f), });
    if(!clearResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId clear = *clearResult;
    dependencies.push_back(clear);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.allowParallelRecording = true;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    const Core::GpuTaskResourceVersionUse produce{ version, Core::GpuTaskResourceVersionRole::Produce };
    const auto rasterResult = graph.addTask<GpuRasterTask>(
        Core::GpuTaskDesc().setIdentity(s_GpuRasterTaskName).setMarkerLabel(s_GpuRasterMarkerLabel).setScheduling(scheduling)
            .setDependencies(dependencies).setResourceUses(uses)
            .setResourceVersionUses({ &produce, 1u })
            .setTiming({ 0u, ComputeGpuTimingResolutionHash(m_width, m_height), Core::GpuTaskTimingPolicy::Task }),
        GpuRasterTask::Payload{ frame, color, skin, pipeline, vertices, indices, Move(*glyphPages), Move(*sdfPages), Move(*textureImages) });
    if(!rasterResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId raster = *rasterResult;
    layer = { raster, color, version, frame->m_target->m_sampledImage, frame->m_snapshot.generation() };
    m_declaredLayer = layer;
    m_declaredGraph = &graph;
    m_graphGeneration = generation;
    m_claimed = true;
    return layer;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_standalone{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StandaloneDeclarationContext{
    GpuRendererState& state;
    Core::GpuTimingFrameTransaction& frameTimingTransaction;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> GpuRendererState::declareStandalone(Core::GpuTaskGraph& graph, Core::GpuTimingFrameTransaction& frameTimingTransaction){
    if(!m_pending || !m_pending->m_prepared || !m_readyToDeclare)
        return MakeUnexpected(Failure{});
    Core::GpuTaskSchedulingHint beginScheduling;
    beginScheduling.cost = Core::GpuTaskCostHint::Tiny;
    beginScheduling.forceSubmissionBoundary = true;
    beginScheduling.allowPacketMerge = false;
    const auto beginResult = graph.addTask<Core::FrameTimingBeginGraphTask>(
        Core::GpuTaskDesc().setIdentity(s_GpuFrameTimingBeginTaskName).setMarkerLabel(s_GpuFrameTimingBeginMarkerLabel)
            .setScheduling(beginScheduling)
            .setTiming({ 0u, 0u, Core::GpuTaskTimingPolicy::PacketOnly }),
        Core::FrameTimingBeginGraphTask::Payload{ .frameTimingTransaction = frameTimingTransaction, .device = m_graphics.getDevice(), .scopeDefinition = m_frameTimingScopePrepared ? GpuRendererTimingScope::s_Frame : Core::GpuTimingScopeDefinition{}, });
    if(!beginResult || !graph.setNormalExecutionPrelude(*beginResult))
        return MakeUnexpected(Failure{});
    const auto layer = declare(graph, false);
    if(!layer)
        return MakeUnexpected(layer.error());
    if(!layer->color.valid())
        return MakeUnexpected(Failure{});
    const GpuFrame frame = m_pending;
    const auto acquiredResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc().setIdentity(s_GpuAcquiredReadyName).setMarkerLabel(s_GpuAcquiredMarkerLabel).setToken(frame->m_acquired.backBuffer.availabilityCompletion));
    if(!acquiredResult)
        return MakeUnexpected(Failure{});
    const Core::GpuExternalCompletionId acquired = *acquiredResult;
    const auto backBufferResult = graph.importTexture(
        frame->m_acquired.backBuffer.texture,
        Core::GpuGraphResourceDesc().setIdentity(s_GpuBackBufferTextureName).setMarkerLabel(s_GpuBackBufferMarkerLabel).setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(frame->m_acquired.backBuffer.nativeInitialState)
            .setExternalFinalState(Core::ResourceStates::Present)
            .setInitialAvailabilityCompletion(acquired)
    );
    const auto pipelineResult = graph.importGraphicsPipeline(frame->m_outputPipeline, Core::GpuGraphPipelineDesc().setIdentity(s_GpuOutputPipelineName).setMarkerLabel(s_GpuOutputPipelineMarkerLabel).setType(Core::GpuGraphPipelineType::Graphics));
    if(!backBufferResult || !pipelineResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphPipelineId pipeline = *pipelineResult;
    const Core::GpuGraphResourceId backBuffer = *backBufferResult;
    const Array<Core::GpuTaskResourceUse, s_GpuOutputResourceUseCount> uses{{
        { layer->color, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read },
        { backBuffer, {}, Core::ResourceStates::RenderTarget, Core::GpuTaskResourceAccess::Write },
    }};
    const Core::GpuTaskResourceVersionUse consume{ layer->colorVersion, Core::GpuTaskResourceVersionRole::Consume };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.avoidQueueCrossing = true;
    const auto outputResult = graph.addTask<GpuOutputTask>(
        Core::GpuTaskDesc().setIdentity(s_GpuOutputTaskName).setMarkerLabel(s_GpuStandaloneOutputMarkerLabel).setScheduling(scheduling)
            .setDependencies({ &layer->readyTask, 1u }).setResourceUses({ uses.data(), s_GpuOutputResourceUseCount }).setResourceVersionUses({ &consume, 1u })
            .setTiming({ 0u, ComputeGpuTimingResolutionHash(m_width, m_height), Core::GpuTaskTimingPolicy::Task }),
        GpuOutputTask::Payload{ frame, backBuffer, layer->color, pipeline, frame->m_acquired, frame->m_outputPipeline, frame->m_presentationMode });
    if(!outputResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId output = *outputResult;
    Core::GpuTaskId timingEndDependency = output;
    if(m_presentationContributor && m_presentationContributor->hasTaskGraphPresentationWork()){
        const auto timingEndDependencyResult = m_presentationContributor->declareTaskGraphPresentation(graph, frame->m_acquired, backBuffer, output);
        if(!timingEndDependencyResult){
            timingEndDependency = {};
            return MakeUnexpected(Failure{});
        }
        timingEndDependency = *timingEndDependencyResult;
    }
    Core::GpuTaskSchedulingHint timingEndScheduling;
    timingEndScheduling.cost = Core::GpuTaskCostHint::Tiny;
    timingEndScheduling.overlapPreferred = false;
    timingEndScheduling.avoidQueueCrossing = true;
    timingEndScheduling.forceSubmissionBoundary = true;
    timingEndScheduling.allowPacketMerge = false;
    const auto terminalResult = graph.addTask<GpuFrameTimingEndTask>(
        Core::GpuTaskDesc().setIdentity(s_GpuFrameTimingEndTaskName).setMarkerLabel(s_GpuFrameTimingEndMarkerLabel)
            .setScheduling(timingEndScheduling).setDependencies({ &timingEndDependency, 1u })
            .setTiming({ 0u, 0u, Core::GpuTaskTimingPolicy::PacketOnly }),
        GpuFrameTimingEndTask::Payload{ frameTimingTransaction });
    if(!terminalResult || !graph.declarePresentEndpoint({ *terminalResult, backBuffer }))
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId terminal = *terminalResult;
    return terminal;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRenderer::renderStandalone(const Core::AcquiredPresentationFrame& frame){
    if(!GpuRendererState::MatchesAcquired(m_state->m_graphics.acquiredPresentationFrame(), frame))
        return false;
    // Only the exact accepted acquired image is already produced. A rejected claimed scene graph owns its recovery tail.
    if(GpuRendererState::MatchesAcquired(m_state->m_lastAcceptedAcquired, frame))
        return true;
    if(!m_state->m_pending)
        return false;
    const Core::AcquiredPresentationFrame& prepared = m_state->m_pending->m_acquired;
    if(m_state->m_claimed && GpuRendererState::MatchesAcquired(prepared, frame))
        return true;
    if(!m_state->prepare(frame))
        return false;
    if(!m_state->m_pending->m_prepared || !m_state->m_readyToDeclare)
        return false;
    m_state->m_presentationContributor = nullptr;
    if(Core::IGpuTaskGraphPresentationContributor* const contributor = m_state->m_graphics.taskGraphPresentationContributor()){
        if(contributor->prepareTaskGraphPresentation(frame))
            m_state->m_presentationContributor = contributor;
        else if(m_state->m_graphics.isDeviceRecreationRequested())
            return false;
        else
            NWB_LOGGER_WARNING(NWB_TEXT("GpuRenderer: presentation contributor preparation failed; presenting UI output without its contribution"));
    }
    const Core::GpuPhysicalQueueId queue = m_state->m_graphics.getDevice().getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(!queue.valid())
        return false;
    const GpuFrame pending = m_state->m_pending;
    Core::GpuTimingFrameTransaction frameTimingTransaction(m_state->m_graphics.gpuTiming());
    __hidden_ui_gpu_standalone::StandaloneDeclarationContext declarationContext{ *m_state, frameTimingTransaction };
    const auto submitted = m_state->m_graphics.standaloneGraph().submitStandaloneTaskGraph(&declarationContext, [](void* context, Core::GpuTaskGraph& graph){
            auto& declaration = *MakeNotNull(static_cast<__hidden_ui_gpu_standalone::StandaloneDeclarationContext*>(context));
            return declaration.state.declareStandalone(graph, declaration.frameTimingTransaction);
        },
        queue,
        &m_state->m_graphics.gpuTiming(),
        &frameTimingTransaction
    );
    m_state->m_presentationContributor = nullptr;
    if(pending->m_finalConsumer.valid())
        acceptTaskGraphOutputLayer(pending->m_snapshot.generation(), pending->m_finalConsumer);
    if(!submitted || !submitted->valid()){
        // Retain immutable inputs and accepted-prefix tokens; a later acquired frame can retry after completion.
        m_state->m_claimed = false;
        m_state->m_declaredGraph = nullptr;
        return false;
    }
    return pending->m_finalConsumer.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_command_ir{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ReplayCaptured(const GpuFrame& frame, const Core::GpuTaskRecordContext& context, const Core::GpuCommandIrCapture& capture, Core::CommandList& commands){
    if(capture.recordCount() == 0u)
        return !commands.commandRecordingFailed();
    const auto stream = capture.exportOwned(frame->m_arena);
    if(!stream)
        return false;
    const Core::GpuCommandIrReplayResult replay = Core::ReplayGpuCommandIrPacket(*stream, context.declarations, context.compiledPlan, context.packet, commands);
    return replay.valid() && !commands.commandRecordingFailed();
}

[[nodiscard]] static b8 CaptureRasterState(
    const GpuRasterTask::Payload& payload,
    const Core::GpuTaskRecordContext& context,
    const Core::GpuCommandIrOwnerAnchor& descriptorOwner,
    const Core::Viewport& viewport,
    const Core::Rect& scissor,
    Core::GpuDescriptorHeap& heap,
    Core::GpuCommandIrCapture& capture
){
    const GpuFrame& frame = payload.frame;
    Core::GpuCommandIrRasterStateDesc state;
    state.pipeline = payload.pipeline;
    state.pipelineOwner = frame->m_resources->m_pipeline;
    state.colorAttachment = payload.color;
    state.framebufferOwner = frame->m_target->m_framebuffer;
    state.viewport = viewport;
    state.scissor = scissor;
    state.hasScissor = true;
    state.vertexBuffers.push_back({ payload.vertices, frame->m_vertices, NWB_UI_VERTEX_BUFFER_INDEX, 0u });
    state.indexResource = payload.indices;
    state.indexBuffer = frame->m_indices;
    state.indexFormat = Core::Format::R32_UINT;
    if(!capture.captureSetGraphicsState(context.task, context.packet, context.queue, state))
        return false;
    return capture.captureBindGraphicsHeap(heap, frame->m_arena, context.task, context.packet, context.queue, payload.pipeline, frame->m_resources->m_pipeline, descriptorOwner);
}

[[nodiscard]] static b8 CaptureOutputState(
    const GpuOutputTask::Payload& payload,
    const Core::GpuTaskRecordContext& context,
    const Core::GpuCommandIrOwnerAnchor& descriptorOwner,
    const Core::Viewport& viewport,
    Core::GpuDescriptorHeap& heap,
    Core::GpuCommandIrCapture& capture
){
    Core::GpuCommandIrRasterStateDesc state;
    state.pipeline = payload.graphPipeline;
    state.pipelineOwner = payload.pipeline;
    state.colorAttachment = payload.backBuffer;
    state.framebufferOwner = payload.acquired.framebuffer;
    state.viewport = viewport;
    if(!capture.captureSetGraphicsState(context.task, context.packet, context.queue, state))
        return false;
    return capture.captureBindGraphicsHeap(heap, payload.frame->m_arena, context.task, context.packet, context.queue, payload.graphPipeline, payload.pipeline, descriptorOwner);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RecordRasterCommandIr(const GpuRasterTask::Payload& payload, const Core::GpuTaskRecordContext& context, Core::CommandList& commands){
    // A task-local stream must not silently disappear from a caller's separate packet capture.
    if(context.commandIrCapture)
        return false;
    const GpuFrame& frame = payload.frame;
    if(!frame || !frame->m_target || !frame->m_resources || !payload.pipeline.valid())
        return false;
    if(context.declarations.graphicsPipelineFor(payload.pipeline) != frame->m_resources->m_pipeline.get())
        return false;
    if(frame->m_snapshot.vertices().empty())
        return frame->m_snapshot.commands().empty() && !commands.commandRecordingFailed();
    if(
        !frame->m_vertices || !frame->m_indices || !payload.vertices.valid() || !payload.indices.valid()
        || context.declarations.bufferForResource(payload.vertices) != frame->m_vertices.get()
        || context.declarations.bufferForResource(payload.indices) != frame->m_indices.get()
    )
        return false;
    Core::GpuCommandIrCapture capture(frame->m_arena);
    if(!capture.beginRecordingAttempt(context.recordingAttemptGeneration))
        return false;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const auto descriptorOwner = Core::MakeGpuCommandIrOwnerAnchor(frame->m_arena, GpuFrame(frame));
    if(!descriptorOwner)
        return false;
    const Core::TextureDesc& target = frame->m_target->m_color->getDescription();
    const GpuRasterGeometry geometry(frame->m_snapshot.displayMetrics(), target.width, target.height);
    const auto initialPush = TryInitializeGpuPaintPushConstants(geometry, frame->m_resources->m_samplerDescriptor.slot());
    if(!initialPush)
        return false;
    GpuPaintPushConstants push = *initialPush;
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    Core::Rect boundScissor;
    b8 hasRasterState = false;
    b8 hasDraw = false;
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const auto resolvedScissor = ResolveGpuScissorRect(geometry, draw);
        if(!resolvedScissor || draw.indexCount == 0u)
            continue;
        const Core::Rect& scissor = *resolvedScissor;
        if(!hasRasterState || scissor != boundScissor){
            if(!__hidden_ui_gpu_command_ir::CaptureRasterState(payload, context, *descriptorOwner, viewport, scissor, heap, capture))
                return false;
            boundScissor = scissor;
            hasRasterState = true;
        }
        if(!ResolvePaintPushConstants(*frame, draw, push))
            return false;
        if(!capture.capturePushConstants(context.task, context.packet, context.queue, BinaryByteView{ reinterpret_cast<const u8*>(&push), sizeof(push) }))
            return false;
        Core::DrawArguments arguments;
        arguments.setVertexCount(draw.indexCount).setStartIndexLocation(draw.firstIndex);
        if(!capture.captureDraw(context.task, context.packet, context.queue, arguments, true))
            return false;
        hasDraw = true;
    }
    if(hasDraw && !capture.captureEndRenderPass(context.task, context.packet, context.queue))
        return false;
    if(!__hidden_ui_gpu_command_ir::ReplayCaptured(frame, context, capture, commands))
        return false;
    if(hasDraw)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiGpuRenderer: raster command IR replay active"));
    return true;
}

b8 RecordOutputCommandIr(const GpuOutputTask::Payload& payload, const Core::GpuTaskRecordContext& context, Core::CommandList& commands){
    if(context.commandIrCapture)
        return false;
    const GpuFrame& frame = payload.frame;
    if(!frame || !frame->m_target || !frame->m_resources || !payload.graphPipeline.valid() || context.declarations.graphicsPipelineFor(payload.graphPipeline) != payload.pipeline.get())
        return false;
    Core::GpuCommandIrCapture capture(frame->m_arena);
    if(!capture.beginRecordingAttempt(context.recordingAttemptGeneration))
        return false;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const auto descriptorOwner = Core::MakeGpuCommandIrOwnerAnchor(frame->m_arena, GpuFrame(frame));
    if(!descriptorOwner)
        return false;
    const Core::TextureDesc& target = payload.acquired.backBuffer.texture->getDescription();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    if(!__hidden_ui_gpu_command_ir::CaptureOutputState(payload, context, *descriptorOwner, viewport, heap, capture))
        return false;
    const GpuOutputPushConstants push = MakeGpuOutputPushConstants(frame->m_target->m_sampledImage.slot(), frame->m_resources->m_samplerDescriptor.slot(), payload.presentationMode);
    if(!capture.capturePushConstants(context.task, context.packet, context.queue, BinaryByteView{ reinterpret_cast<const u8*>(&push), sizeof(push) }))
        return false;
    if(!capture.captureDraw(context.task, context.packet, context.queue, Core::DrawArguments().setVertexCount(s_GpuFullscreenTriangleVertexCount), false) || !capture.captureEndRenderPass(context.task, context.packet, context.queue))
        return false;
    return __hidden_ui_gpu_command_ir::ReplayCaptured(frame, context, capture, commands);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Optional<Core::Rect> ResolveGpuScissorRect(const GpuRasterGeometry& geometry, const DrawCommand& draw)noexcept{
    const auto bounds = TryComputeGpuScissorBounds(geometry, draw);
    if(!bounds)
        return {};
    return Core::Rect(bounds->x, bounds->y, bounds->z, bounds->w);
}

b8 ResolvePaintPushConstants(const GpuFrameData& frame, const DrawCommand& draw, GpuPaintPushConstants& push)noexcept{
    u32 textureSlot = NWB_UI_INVALID_HEAP_SLOT;
    u32 sdfSpreadPixels = 0u;
    u32 sdfDistanceEncoding = 0u;
    if(draw.material == PaintMaterial::Skin)
        textureSlot = frame.m_skin->m_texture.sampledImageHeapHandle.slot();
    else if(draw.material == PaintMaterial::Glyph){
        if(draw.glyphPageIndex >= frame.m_glyphPages.size())
            return false;
        textureSlot = frame.m_glyphPages[draw.glyphPageIndex]->m_sampledImage.slot();
    }
    else if(draw.material == PaintMaterial::SdfGlyph){
        if(draw.sdfPageIndex >= frame.m_sdfPages.size())
            return false;
        const auto& page = frame.m_sdfPages[draw.sdfPageIndex];
        textureSlot = page->m_sampledImage.slot();
        sdfSpreadPixels = page->m_page->binding().spreadPixels;
        sdfDistanceEncoding = page->m_page->binding().distanceEncoding;
    }
    else if(draw.material == PaintMaterial::Image){
        if(draw.textureImageIndex >= frame.m_textureImages.size())
            return false;
        textureSlot = frame.m_textureImages[draw.textureImageIndex]->m_texture.sampledImageHeapHandle.slot();
    }
    AssignGpuPaintMaterialPushConstants(draw.material, textureSlot, draw.sdfChannel, sdfSpreadPixels, sdfDistanceEncoding, push);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRasterTask::Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !frame->m_prepared || !frame->m_target || !frame->m_resources
        || context.declarations.textureForResource(payload.color) != frame->m_target->m_color.get()
        || context.declarations.textureForResource(payload.skin) != frame->m_skin->m_texture.texture.get()
    )
        return false;
    if(payload.glyphPages.size() != frame->m_glyphPages.size())
        return false;
    for(usize index = 0u; index < payload.glyphPages.size(); ++index){
        if(context.declarations.textureForResource(payload.glyphPages[index]) != frame->m_glyphPages[index]->m_texture.get())
            return false;
    }
    if(payload.sdfPages.size() != frame->m_sdfPages.size())
        return false;
    for(usize index = 0u; index < payload.sdfPages.size(); ++index){
        if(context.declarations.textureForResource(payload.sdfPages[index]) != frame->m_sdfPages[index]->m_texture.get())
            return false;
    }
    if(payload.textureImages.size() != frame->m_textureImages.size())
        return false;
    for(usize index = 0u; index < payload.textureImages.size(); ++index){
        if(context.declarations.textureForResource(payload.textureImages[index]) != frame->m_textureImages[index]->m_texture.texture.get())
            return false;
    }
    if(frame->m_recordingMode == GpuCommandRecordingMode::CommandIrReplay)
        return RecordRasterCommandIr(payload, context, commands);
    const Core::TextureDesc& target = frame->m_target->m_color->getDescription();
    const GpuRasterGeometry geometry(frame->m_snapshot.displayMetrics(), target.width, target.height);
    const auto initialPush = TryInitializeGpuPaintPushConstants(geometry, frame->m_resources->m_samplerDescriptor.slot());
    if(!initialPush)
        return false;
    GpuPaintPushConstants push = *initialPush;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    Core::Rect boundScissor;
    b8 heapBound = false;
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const auto resolvedScissor = ResolveGpuScissorRect(geometry, draw);
        if(!resolvedScissor)
            continue;
        const Core::Rect& scissor = *resolvedScissor;
        if(!heapBound || scissor != boundScissor){
            Core::ViewportState view;
            view.addViewport(viewport).addScissorRect(scissor);
            Core::GraphicsState state;
            state
                .setPipeline(frame->m_resources->m_pipeline.get())
                .setFramebuffer(frame->m_target->m_framebuffer.get())
                .setViewport(view)
                .addVertexBuffer(Core::VertexBufferBinding().setBuffer(frame->m_vertices.get()).setSlot(NWB_UI_VERTEX_BUFFER_INDEX))
                .setIndexBuffer(Core::IndexBufferBinding().setBuffer(frame->m_indices.get()).setFormat(Core::Format::R32_UINT))
            ;
            commands.setGraphicsState(state);
            if(!heapBound){
                heap.bindGraphics(commands, *frame->m_resources->m_pipeline);
                heapBound = true;
            }
            boundScissor = scissor;
        }
        if(!ResolvePaintPushConstants(*frame, draw, push))
            return false;
        commands.setPushConstants(&push, sizeof(push));
        Core::DrawArguments arguments;
        arguments.setVertexCount(draw.indexCount).setStartIndexLocation(draw.firstIndex);
        commands.drawIndexed(arguments);
    }
    commands.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuOutputTask::Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !payload.pipeline || !payload.acquired.valid()
        || context.declarations.textureForResource(payload.backBuffer) != payload.acquired.backBuffer.texture.get()
        || context.declarations.textureForResource(payload.color) != frame->m_target->m_color.get()
    )
        return false;
    if(frame->m_recordingMode == GpuCommandRecordingMode::CommandIrReplay)
        return RecordOutputCommandIr(payload, context, commands);
    const Core::TextureDesc& target = payload.acquired.backBuffer.texture->getDescription();
    Core::ViewportState viewport;
    viewport.addViewport(Core::Viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f));
    Core::GraphicsState state;
    state.setPipeline(payload.pipeline.get()).setFramebuffer(payload.acquired.framebuffer.get()).setViewport(viewport);
    commands.setGraphicsState(state);
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    heap.bindGraphics(commands, *payload.pipeline);
    const GpuOutputPushConstants push = MakeGpuOutputPushConstants(frame->m_target->m_sampledImage.slot(), frame->m_resources->m_samplerDescriptor.slot(), payload.presentationMode);
    commands.setPushConstants(&push, sizeof(push));
    commands.draw(Core::DrawArguments().setVertexCount(s_GpuFullscreenTriangleVertexCount));
    commands.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_ImageLoadContext = NWB_TEXT("UI Texture Image");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ValidVersion(const GpuTextureImageVersion& version)noexcept{
    if(!version.m_source || !ValidUiTextureImageSource(*version.m_source) || !version.m_texture.valid())
        return false;
    const TextureGpuResource& resource = version.m_texture;
    if(resource.format == Core::Format::UNKNOWN || resource.format >= Core::Format::Count)
        return false;
    const Core::FormatInfo& format = Core::GetFormatInfo(resource.format);
    const Core::TextureDesc& description = resource.texture->getCreationDescription();
    const Texture& texture = version.m_source->texture();
    return IsValidGpuTextureImageResourceDesc(
        format.kind == Core::FormatKind::Normalized || format.kind == Core::FormatKind::Float,
        format.hasDepth,
        format.hasStencil,
        format.hasRed || format.hasGreen || format.hasBlue || format.hasAlpha,
        resource.sampledImageHeapHandle.descriptorClass() == Core::GpuDescriptorClass::SampledImage,
        resource.readinessToken.valid(),
        resource.readinessToken.hasPhysicalQueueIdentity(),
        description.dimension == Core::TextureDimension::Texture2D,
        description.depth,
        description.arraySize,
        description.sampleCount,
        description.format == resource.format,
        description.width,
        texture.width(),
        description.height,
        texture.height(),
        description.mipLevels,
        texture.mipLevels().size()
    );
}

template<typename TElement>
[[nodiscard]] static b8 EnsureGeometryBuffer(
    Core::Device& device,
    const usize requiredCount,
    const usize initialCapacity,
    const Name& debugName,
    const b8 isVertexBuffer,
    Core::BufferHandle& inOutBuffer,
    usize& inOutCapacity
){
    if(inOutCapacity >= requiredCount)
        return true;
    const usize capacity = ComputeGpuGrowingBufferCapacity(inOutCapacity, requiredCount, initialCapacity, sizeof(TElement));
    Core::BufferDesc description;
    description.setByteSize(static_cast<u64>(capacity * sizeof(TElement))).setIsVertexBuffer(isVertexBuffer).setIsIndexBuffer(!isVertexBuffer)
        .setDebugName(debugName).enableAutomaticStateTracking(Core::ResourceStates::Common)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    auto bufferResult = device.createBuffer(description);
    if(!bufferResult)
        return false;
    inOutBuffer = Move(*bufferResult);
    inOutCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuImageResources::ValidateGlyphPages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCounts(snapshot.glyphPages().size(), snapshot.sdfPages().size(), snapshot.textureImages().size()))
        return false;
    const auto& pages = snapshot.glyphPages();
    for(usize index = 0u; index < pages.size(); ++index){
        if(!pages[index] || !ValidGlyphPageBinding(pages[index]->binding(), pages[index]->pixels().size()))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(SameGlyphPageKey(pages[index]->binding(), pages[previous]->binding()))
                return false;
        }
    }
    return ValidateDrawCommandGlyphBindings(snapshot.commands(), pages.size());
}

Expected<GpuVersion<GpuGlyphVersion>> GpuImageResources::prepareGlyphPage(const SharedGlyphPage& page){
    usize replaceIndex = m_imageCache.glyphs().size();
    for(usize index = 0u; index < m_imageCache.glyphs().size(); ++index){
        const auto& existing = m_imageCache.glyphs()[index];
        if(!SameGlyphPageKey(existing->m_page->binding(), page->binding()))
            continue;
        replaceIndex = index;
        if(existing->m_page->binding().generation != page->binding().generation)
            break;
        if(!SameGlyphPageVersion(existing->m_page->binding(), existing->m_page->pixels(), page->binding(), page->pixels(), existing->m_page.get() == page.get()))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuGlyphVersion> reused = existing;
        m_imageCache.glyphs().erase(m_imageCache.glyphs().begin() + static_cast<isize>(index));
        m_imageCache.glyphs().push_back(reused);
        if(!GpuSampledTextures::PrepareDescriptor(reused->m_graphics, reused->m_texture, Core::Format::R8_UNORM, reused->m_sampledImage))
            return MakeUnexpected(Failure{});
        return Move(reused);
    }
    Core::Device& device = m_graphics.getDevice();
    if((device.queryFormatSupport(Core::Format::R8_UNORM) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport)
        return MakeUnexpected(Failure{});
    auto versionResult = MakeGpuVersion<GpuGlyphVersion>(m_arena, m_graphics, page);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuGlyphVersion> version = Move(*versionResult);
    const GlyphPageBinding& binding = page->binding();
    // Upload only a fresh immutable image. Accepted readiness stays with this version across presentation retries.
    if(!GpuSampledTextures::CreateAndUploadPage(
        m_graphics,
        GpuPageTextureUploadDesc{
            .name = s_GpuGlyphPageTextureName,
            .data = page->pixels().data(),
            .dataSize = page->pixels().size(),
            .rowPitch = binding.width,
            .width = binding.width,
            .height = binding.height,
            .format = Core::Format::R8_UNORM,
        },
        version->m_texture,
        version->m_readinessToken
    ))
        return MakeUnexpected(Failure{});
    // Frames retain superseded versions; evicting a cache entry never overwrites an image or frees a live frame's descriptor.
    if(replaceIndex < m_imageCache.glyphs().size())
        m_imageCache.glyphs().erase(m_imageCache.glyphs().begin() + static_cast<isize>(replaceIndex));
    else if(ShouldEvictGpuImageCacheBeforeInsert(m_imageCache.glyphs().size(), m_imageCache.sdfs().size(), m_imageCache.textures().size()))
        m_imageCache.evict();
    m_imageCache.glyphs().push_back(version);
    // Cache accepted uploads before descriptor publication so descriptor exhaustion retries cannot discard their readiness.
    if(!GpuSampledTextures::PrepareDescriptor(version->m_graphics, version->m_texture, Core::Format::R8_UNORM, version->m_sampledImage))
        return MakeUnexpected(Failure{});
    return Move(version);
}

b8 GpuImageResources::prepareGlyphPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.glyphPages();
    frame.m_glyphPages.reserve(pages.size());
    // Retain successful earlier uploads if preparation fails partway through the page set.
    while(frame.m_glyphPages.size() < pages.size()){
        auto version = prepareGlyphPage(pages[frame.m_glyphPages.size()]);
        if(!version)
            return false;
        frame.m_glyphPages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSampledGraphResources> GpuImageResources::declareGlyphPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_glyphPages.size() != frame->m_snapshot.glyphPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared glyph page count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_glyphPages.size(); ++index){
        const auto& page = frame->m_glyphPages[index];
        const auto texture = GpuSampledTextures::ImportTexture(
            GpuSampledTextureImportDesc{
                .readyPrefix = s_GpuGlyphPageReadyPrefix,
                .readyMarkerLabel = s_GpuGlyphPageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("glyph page"),
                .readinessToken = page->m_readinessToken,
                .texturePrefix = s_GpuGlyphPagePrefix,
                .textureMarkerLabel = s_GpuGlyphPageMarkerLabel,
                .texture = page->m_texture,
                .index = index,
            },
            graph,
            uses
        );
        if(!texture){
            if(texture.error() == GpuSampledTextureImportError::Texture)
                NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: glyph page {} texture import failed (atlas={}, page={}, generation={})"), index, page->m_page->binding().atlasIdentity, page->m_page->binding().index, page->m_page->binding().generation);
            return MakeUnexpected(Failure{});
        }
        resources->push_back(*texture);
    }
    return resources;
}

b8 GpuImageResources::ValidateSdfPages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCounts(snapshot.glyphPages().size(), snapshot.sdfPages().size(), snapshot.textureImages().size()))
        return false;
    const auto& pages = snapshot.sdfPages();
    if(!AllValidAndDistinct(Span<const SharedSdfAtlasPage>(pages.data(), pages.size()), [](const SharedSdfAtlasPage& current)noexcept{ return current && ValidSdfAtlasPageBinding(current->binding(), current->pixels().size()); }, [](const SharedSdfAtlasPage& current, const SharedSdfAtlasPage& previous)noexcept{ return SameSdfAtlasPageKey(current->binding(), previous->binding()); }))
        return false;
    for(const DrawCommand& command : snapshot.commands()){
        const u32 channelCount = command.sdfPageIndex < pages.size() ? pages[command.sdfPageIndex]->binding().channelCount : 0u;
        if(!ValidDrawCommandSdfBinding(command, pages.size(), channelCount))
            return false;
    }
    return true;
}

Expected<GpuVersion<GpuSdfAtlasVersion>> GpuImageResources::prepareSdfPage(Core::Alloc::ScratchArena& scratchArena, const SharedSdfAtlasPage& page){
    for(usize index = 0u; index < m_imageCache.sdfs().size(); ++index){
        const auto& existing = m_imageCache.sdfs()[index];
        if(!SameSdfAtlasPageKey(existing->m_page->binding(), page->binding()))
            continue;
        if(!(existing->m_page->binding() == page->binding()))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuSdfAtlasVersion> reused = existing;
        m_imageCache.sdfs().erase(m_imageCache.sdfs().begin() + static_cast<isize>(index));
        m_imageCache.sdfs().push_back(reused);
        if(!GpuSampledTextures::PrepareDescriptor(reused->m_graphics, reused->m_texture, reused->m_texture->getDescription().format, reused->m_sampledImage))
            return MakeUnexpected(Failure{});
        return Move(reused);
    }
    Core::Device& device = m_graphics.getDevice();
    const SdfAtlasPageBinding& binding = page->binding();
    constexpr Array<Core::Format::Enum, 4> s_Formats{ Core::Format::R8_UNORM, Core::Format::RG8_UNORM, Core::Format::RGB8_UNORM, Core::Format::RGBA8_UNORM };
    Core::Format::Enum format = s_Formats[binding.channelCount - 1u];
    u32 uploadChannels = binding.channelCount;
    if(binding.channelCount == s_GpuRgbChannelCount && (device.queryFormatSupport(format) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport){
        format = Core::Format::RGBA8_UNORM;
        uploadChannels = s_GpuRgbaChannelCount;
    }
    if((device.queryFormatSupport(format) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport)
        return MakeUnexpected(Failure{});
    auto versionResult = MakeGpuVersion<GpuSdfAtlasVersion>(m_arena, m_graphics, page);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuSdfAtlasVersion> version = Move(*versionResult);
    Vector<u8, Core::Alloc::ScratchArena> expanded(scratchArena);
    NotNull<const u8*> uploadData(page->pixels().data());
    usize uploadBytes = page->pixels().size();
    if(uploadChannels != binding.channelCount){
        ExpandGpuSdfPagePixels(binding.width, binding.height, binding.channelCount, uploadChannels, page->pixels(), expanded);
        uploadData = expanded.data();
        uploadBytes = expanded.size();
    }
    if(!GpuSampledTextures::CreateAndUploadPage(
        m_graphics,
        GpuPageTextureUploadDesc{
            .name = s_GpuSdfPageTextureName,
            .data = uploadData.get(),
            .dataSize = uploadBytes,
            .rowPitch = static_cast<usize>(binding.width) * uploadChannels,
            .width = binding.width,
            .height = binding.height,
            .format = format,
        },
        version->m_texture,
        version->m_readinessToken
    ))
        return MakeUnexpected(Failure{});
    // Native accepted upload commands retain their destination image until the physical queue completes.
    // Frame owners retain sampled descriptors; cache eviction therefore releases neither a live image nor a live draw view.
    if(ShouldEvictGpuImageCacheBeforeInsert(m_imageCache.glyphs().size(), m_imageCache.sdfs().size(), m_imageCache.textures().size()))
        m_imageCache.evict();
    m_imageCache.sdfs().push_back(version);
    // Preserve every accepted upload before descriptor publication; retrying a failed descriptor never uploads again.
    if(!GpuSampledTextures::PrepareDescriptor(version->m_graphics, version->m_texture, version->m_texture->getDescription().format, version->m_sampledImage))
        return MakeUnexpected(Failure{});
    return Move(version);
}

b8 GpuImageResources::prepareSdfPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.sdfPages();
    frame.m_sdfPages.reserve(pages.size());
    Core::Alloc::ScratchArena scratchArena(s_GpuSdfUploadArenaName);
    while(frame.m_sdfPages.size() < pages.size()){
        auto version = prepareSdfPage(scratchArena, pages[frame.m_sdfPages.size()]);
        if(!version)
            return false;
        frame.m_sdfPages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSampledGraphResources> GpuImageResources::declareSdfPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_sdfPages.size() != frame->m_snapshot.sdfPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared SDF page count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_sdfPages.size(); ++index){
        const auto& page = frame->m_sdfPages[index];
        const auto texture = GpuSampledTextures::ImportTexture(
            GpuSampledTextureImportDesc{
                .readyPrefix = s_GpuSdfPageReadyPrefix,
                .readyMarkerLabel = s_GpuSdfPageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("SDF page"),
                .readinessToken = page->m_readinessToken,
                .texturePrefix = s_GpuSdfPagePrefix,
                .textureMarkerLabel = s_GpuSdfPageMarkerLabel,
                .texture = page->m_texture,
                .index = index,
            },
            graph,
            uses
        );
        if(!texture){
            if(texture.error() == GpuSampledTextureImportError::Texture)
                NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: SDF page {} texture import failed (atlas={}, group={}, generation={})"), index, page->m_page->binding().atlasIdentity, page->m_page->binding().index, page->m_page->binding().generation);
            return MakeUnexpected(Failure{});
        }
        resources->push_back(*texture);
    }
    return resources;
}

b8 GpuImageResources::ValidateTextureImages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCounts(snapshot.glyphPages().size(), snapshot.sdfPages().size(), snapshot.textureImages().size()))
        return false;
    const auto& images = snapshot.textureImages();
    for(usize index = 0u; index < images.size(); ++index){
        if(!images[index] || !ValidUiTextureImageSource(*images[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(images[index]->generation() == images[previous]->generation())
                return false;
        }
    }
    return ValidateDrawCommandImageBindings(snapshot.commands(), images.size());
}

Expected<GpuVersion<GpuTextureImageVersion>> GpuImageResources::prepareTextureImage(const SharedImageSource& source){
    if(!source || !ValidUiTextureImageSource(*source))
        return MakeUnexpected(Failure{});
    for(usize index = 0u; index < m_imageCache.textures().size(); ++index){
        const auto& existing = m_imageCache.textures()[index];
        if(existing->m_source->generation() != source->generation())
            continue;
        if(!SameTextureImageKey(*existing->m_source, *source) || !__hidden_ui_gpu_resources::ValidVersion(*existing))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuTextureImageVersion> reused = existing;
        m_imageCache.textures().erase(m_imageCache.textures().begin() + static_cast<isize>(index));
        m_imageCache.textures().push_back(reused);
        return Move(reused);
    }
    // The source factory owns structural payload validation; immutable cache insertion keeps a debug-only invariant.
    NWB_ASSERT(source->texture().validatePayload());
    auto versionResult = MakeGpuVersion<GpuTextureImageVersion>(m_arena, m_graphics, source);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuTextureImageVersion> version = Move(*versionResult);
    if(!TextureAssetLoader::Create(source->texture(), source->identity().name(), __hidden_ui_gpu_resources::s_ImageLoadContext, m_graphics, version->m_texture))
        return MakeUnexpected(Failure{});
    if(!__hidden_ui_gpu_resources::ValidVersion(*version)){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture image {} has invalid GPU format, geometry, or readiness"), source->generation());
        return MakeUnexpected(Failure{});
    }
    if(ShouldEvictGpuImageCacheBeforeInsert(m_imageCache.glyphs().size(), m_imageCache.sdfs().size(), m_imageCache.textures().size()))
        m_imageCache.evict();
    // Retain each successful immutable upload before a later image or frame preparation can fail.
    m_imageCache.textures().push_back(version);
    return Move(version);
}

b8 GpuImageResources::prepareTextureImages(GpuFrameData& frame){
    const auto& images = frame.m_snapshot.textureImages();
    frame.m_textureImages.reserve(images.size());
    while(frame.m_textureImages.size() < images.size()){
        auto version = prepareTextureImage(images[frame.m_textureImages.size()]);
        if(!version)
            return false;
        frame.m_textureImages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSampledGraphResources> GpuImageResources::declareTextureImages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_textureImages.size() != frame->m_snapshot.textureImages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared texture image count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_textureImages.size(); ++index){
        const auto& image = frame->m_textureImages[index];
        const auto& source = frame->m_snapshot.textureImages()[index];
        if(!image || !source || !image->m_source || !SameTextureImageKey(*image->m_source, *source) || !__hidden_ui_gpu_resources::ValidVersion(*image))
            return MakeUnexpected(Failure{});
        const auto texture = GpuSampledTextures::ImportTexture(
            GpuSampledTextureImportDesc{
                .readyPrefix = s_GpuTextureImageReadyPrefix,
                .readyMarkerLabel = s_GpuTextureImageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("texture"),
                .readinessToken = image->m_texture.readinessToken,
                .texturePrefix = s_GpuTextureImagePrefix,
                .textureMarkerLabel = s_GpuTextureImageMarkerLabel,
                .texture = image->m_texture.texture,
                .index = index,
            },
            graph,
            uses
        );
        if(!texture){
            if(texture.error() == GpuSampledTextureImportError::Texture)
                NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture image {} import failed (source generation={})"), index, source->generation());
            return MakeUnexpected(Failure{});
        }
        resources->push_back(*texture);
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FreeGpuDescriptor(Core::GraphicsRuntime& graphics, const Core::GpuDescriptorHandle& descriptor)noexcept{
    if(descriptor.valid())
        graphics.getDevice().getDescriptorHeap().free(descriptor);
}

b8 GpuTokenComplete(Core::Device& device, const Core::QueueSubmissionToken& token){
    if(!token.valid())
        return true;
    if(!token.hasPhysicalQueueIdentity())
        return false;
    const Core::GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
    const auto timeline = device.getQueueTimelineSnapshot(queue);
    return timeline && timeline->valid() && timeline->queue == queue && timeline->completedValue >= token.value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRendererState::MatchesAcquired(const Core::AcquiredPresentationFrame& first, const Core::AcquiredPresentationFrame& second)noexcept{
    return
        first.valid() && second.valid()
        && first.backBuffer.texture == second.backBuffer.texture && first.framebuffer == second.framebuffer
        && first.backBuffer.index == second.backBuffer.index && first.backBuffer.nativeInitialState == second.backBuffer.nativeInitialState
        && first.backBuffer.availabilityCompletion == second.backBuffer.availabilityCompletion
    ;
}

b8 GpuRendererState::createResources(){
    return GpuSharedResourcesFactory{}.create(*this);
}

Expected<GpuVersion<GpuTargetVersion>> GpuRendererState::createTarget(const u32 width, const u32 height){
    return GpuTargetFactory{}.create(m_arena, m_graphics, width, height);
}

b8 GpuRendererState::prepareBuffers(GpuFrameSlot& slot, const DrawSnapshot& snapshot){
    const usize vertexCount = snapshot.vertices().size();
    const usize indexCount = snapshot.indices().size();
    if(vertexCount == 0u && indexCount == 0u)
        return true;
    if(!IsValidGpuSnapshotBufferCounts(vertexCount, indexCount, sizeof(Vertex), sizeof(u32)))
        return false;
    Core::Device& device = m_graphics.getDevice();
    return __hidden_ui_gpu_resources::EnsureGeometryBuffer<Vertex>(device, vertexCount, s_GpuInitialVertexCapacity, s_GpuVertexBufferName, true, slot.vertices, slot.vertexCapacity)
        && __hidden_ui_gpu_resources::EnsureGeometryBuffer<u32>(device, indexCount, s_GpuInitialIndexCapacity, s_GpuIndexBufferName, false, slot.indices, slot.indexCapacity);
}

b8 GpuRendererState::prepareOutputPipeline(const Core::FramebufferInfo& info){
    return GpuSharedResourcesFactory{}.prepareOutputPipeline(*this, info);
}

b8 GpuRendererState::prepare(const Core::AcquiredPresentationFrame& acquired){
    m_readyToDeclare = false;
    if(!m_pending)
        return true;
    if(!acquired.valid() || !m_resources || m_width == 0u || m_height == 0u)
        return false;
    const Core::FramebufferDesc& attachments = acquired.framebuffer->getDescription();
    const Core::TextureDesc& backBuffer = acquired.backBuffer.texture->getDescription();
    if(!IsValidGpuPresentationAttachments(
        attachments.colorAttachments.size(),
        attachments.colorAttachments.size() == 1u && attachments.colorAttachments[0].texture == acquired.backBuffer.texture.get(),
        attachments.depthAttachment.valid(),
        attachments.shadingRateAttachment.valid(),
        backBuffer.width,
        m_width,
        backBuffer.height,
        m_height,
        backBuffer.sampleCount
    ))
        return false;
    if(!m_resources->m_outputPipeline || m_resources->m_outputPipeline->getFramebufferInfo() != acquired.framebuffer->getFramebufferInfo())
        return false;
    if(m_pending->m_prepared && MatchesAcquired(m_pending->m_acquired, acquired)){
        m_readyToDeclare = true;
        return true;
    }
    Core::Device& device = m_graphics.getDevice();
    if(m_pending->m_prepared){
        // A failed graph may have accepted uploads or raster. Rebuild only after that exact prefix completes.
        if(!m_pending->prefixComplete(device))
            return true;
        m_pending->m_acquired = acquired;
        m_readyToDeclare = true;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Retire all completed owners before cache eviction or descriptor allocation can need their heap slots.
    for(auto& slot : m_slots){
        if(slot.inFlight && slot.inFlight->complete(device))
            slot.inFlight.reset();
    }
    m_imageCache.trim(m_pending->m_snapshot);
    for(auto& slot : m_slots){
        if(slot.inFlight)
            continue;
        GpuImageResources imageResources(m_arena, m_graphics, m_imageCache);
        if(!imageResources.prepareGlyphPages(*m_pending) || !imageResources.prepareSdfPages(*m_pending) || !imageResources.prepareTextureImages(*m_pending) || !prepareBuffers(slot, m_pending->m_snapshot))
            return false;
        m_pending->m_target = slot.target;
        m_pending->m_resources = m_resources;
        m_pending->m_vertices = slot.vertices;
        m_pending->m_indices = slot.indices;
        m_pending->m_outputPipeline = m_resources->m_outputPipeline;
        m_pending->m_acquired = acquired;
        m_pending->m_presentationMode = m_graphics.isHDR10OutputActive() ? NWB_UI_PRESENTATION_HDR10 : NWB_UI_PRESENTATION_SDR;
        m_pending->m_prepared = true;
        m_readyToDeclare = true;
        slot.inFlight = m_pending;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Admission is bounded. The pending immutable CPU generation survives until a slot becomes available.
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
