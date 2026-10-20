// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskGraphOutputLayer> GpuRendererState::declare(Core::GpuTaskGraph& graph, bool allowEmptySceneLayer){
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
    if(allowEmptySceneLayer && snapshot.commands().empty() && snapshot.vertices().empty() && snapshot.indices().empty() && snapshot.glyphPages().empty() && snapshot.sdfPages().empty() && snapshot.textureImages().empty()){
        layer.frameGeneration = snapshot.generation();
        m_declaredLayer = layer;
        m_declaredGraph = &graph;
        m_graphGeneration = generation;
        m_claimed = true;
        return layer;
    }
    const auto targetReadyResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc().setIdentity(Name("ui.layer_ready")).setMarkerLabel("UI Layer Initialization Completion").setToken(frame->m_target->m_readinessToken));
    if(!targetReadyResult)
        return MakeUnexpected(Failure{});
    const Core::GpuExternalCompletionId targetReady = *targetReadyResult;
    const auto colorResult = graph.importTexture(
        frame->m_target->m_color,
        Core::GpuGraphResourceDesc().setIdentity(s_GpuLayerTextureName).setMarkerLabel("UI Linear Layer").setType(Core::GpuGraphResourceType::Texture)
            .setInitialAvailabilityCompletion(targetReady).setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    const auto skinReadyResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc().setIdentity(Name("ui.atlas_ready")).setMarkerLabel("UI Atlas Upload Completion").setToken(frame->m_skin->m_texture.readinessToken));
    if(!colorResult || !skinReadyResult)
        return MakeUnexpected(Failure{});
    const Core::GpuExternalCompletionId skinReady = *skinReadyResult;
    const Core::GpuGraphResourceId color = *colorResult;
    const auto skinResult = graph.importTexture(
        frame->m_skin->m_texture.texture,
        Core::GpuGraphResourceDesc().setIdentity(s_GpuAtlasTextureName).setMarkerLabel("UI Atlas").setType(Core::GpuGraphResourceType::Texture)
            .setInitialAvailabilityCompletion(skinReady).setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    const auto versionResult = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc().setResource(color).setOrigin(Core::GpuGraphResourceVersionOrigin::TaskProduced));
    const auto pipelineResult = graph.importGraphicsPipeline(frame->m_resources->m_pipeline, Core::GpuGraphPipelineDesc().setIdentity(Name("ui.raster_pipeline")).setMarkerLabel("UI Raster Pipeline").setType(Core::GpuGraphPipelineType::Graphics));
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
    constexpr usize s_RasterDependencyCapacity = 3u;
    FixedVector<Core::GpuTaskId, s_RasterDependencyCapacity> dependencies;
    GpuRasterResourceUses uses;
    Core::GpuGraphResourceId vertices;
    Core::GpuGraphResourceId indices;
    uses.push_back({ color, {}, Core::ResourceStates::RenderTarget, Core::GpuTaskResourceAccess::Write });
    uses.push_back({ skin, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read });
    auto glyphPages = declareGlyphPages(graph, frame, uses);
    if(!glyphPages)
        return MakeUnexpected(Failure{});
    auto sdfPages = declareSdfPages(graph, frame, uses);
    if(!sdfPages)
        return MakeUnexpected(Failure{});
    auto textureImages = declareTextureImages(graph, frame, uses);
    if(!textureImages)
        return MakeUnexpected(Failure{});
    if(!frame->m_snapshot.vertices().empty()){
        const auto verticesResult = graph.importBuffer(
            frame->m_vertices,
            Core::GpuGraphResourceDesc().setIdentity(s_GpuVertexBufferName).setMarkerLabel("UI Vertices").setType(Core::GpuGraphResourceType::Buffer).setExternalFinalState(Core::ResourceStates::Common)
        );
        const auto indicesResult = graph.importBuffer(frame->m_indices, Core::GpuGraphResourceDesc().setIdentity(s_GpuIndexBufferName).setMarkerLabel("UI Indices").setType(Core::GpuGraphResourceType::Buffer).setExternalFinalState(Core::ResourceStates::Common));
        if(!verticesResult || !indicesResult){
            if(verticesResult)
                vertices = *verticesResult;
            else
                vertices = {};
            if(indicesResult)
                indices = *indicesResult;
            else
                indices = {};
            return MakeUnexpected(Failure{});
        }
        vertices = *verticesResult;
        indices = *indicesResult;
        const auto vertexBytesResult = graph.copyUploadData(frame->m_snapshot.vertices().data(), frame->m_snapshot.vertices().size() * sizeof(Vertex), alignof(Vertex));
        const auto indexBytesResult = graph.copyUploadData(frame->m_snapshot.indices().data(), frame->m_snapshot.indices().size() * sizeof(u32), alignof(u32));
        if(!vertexBytesResult || !indexBytesResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId vertexBytes = *vertexBytesResult;
        const Core::GpuUploadBlobId indexBytes = *indexBytesResult;
        const auto vertexUploadResult = graph.addUploadBufferTask(
            Core::GpuTaskDesc().setIdentity(Name("ui.vertex_upload")).setMarkerLabel("UI Vertex Upload").setScheduling(uploadScheduling),
            Core::GpuUploadBufferTaskDesc{ .source = vertexBytes, .destination = vertices, .finalState = Core::ResourceStates::Common, .acceptedToken = &frame->m_vertexUpload, }
        );
        const auto indexUploadResult = graph.addUploadBufferTask(
            Core::GpuTaskDesc().setIdentity(Name("ui.index_upload")).setMarkerLabel("UI Index Upload").setScheduling(uploadScheduling),
            Core::GpuUploadBufferTaskDesc{ .source = indexBytes, .destination = indices, .finalState = Core::ResourceStates::Common, .acceptedToken = &frame->m_indexUpload, }
        );
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
        Core::GpuTaskDesc().setIdentity(Name("ui.clear")).setMarkerLabel("UI Transparent Clear").setScheduling(uploadScheduling),
        Core::GpuClearTextureTaskDesc{ .acceptedToken = &frame->m_clear, .destination = color, .valueType = Core::GpuClearTextureTaskValueType::Float, .floatValue = Core::Color(0.0f, 0.0f, 0.0f, 0.0f), }
    );
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
        Core::GpuTaskDesc().setIdentity(Name("ui.raster")).setMarkerLabel("UI Raster").setScheduling(scheduling)
            .setDependencies(dependencies.data(), dependencies.size()).setResourceUses(uses.data(), uses.size())
            .setResourceVersionUses(&produce, 1u)
            .setTimingMetadata({ 0u, m_width ^ (m_height << s_GpuTimingHeightShift), Core::GpuTaskTimingPolicy::Task }),
        GpuRasterTask::Payload{ frame, color, skin, pipeline, vertices, indices, Move(*glyphPages), Move(*sdfPages), Move(*textureImages) }
    );
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


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

