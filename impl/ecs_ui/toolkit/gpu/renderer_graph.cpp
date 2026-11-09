// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"
#include "image_resources.h"
#include "renderer_frame_timing_task.h"

#include <core/common/log.h>
#include <core/task/gpu/frame_timing_begin_task.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

