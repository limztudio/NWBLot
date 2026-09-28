// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRendererState::declare(Core::GpuTaskGraph& graph, Core::GpuTaskGraphOutputLayer& outLayer){
    outLayer = {};
    if(!m_pending || !m_pending->m_prepared || !m_readyToDeclare)
        return true;
    const Core::AcquiredPresentationFrame& current = m_graphics.acquiredPresentationFrame();
    const Core::AcquiredPresentationFrame& prepared = m_pending->m_acquired;
    if(!matchesAcquired(current, prepared))
        return true;
    u64 generation = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        if(!view.valid())
            return false;
        generation = view.generation();
    }
    if(m_claimed && m_declaredGraph == &graph && m_graphGeneration == generation){
        outLayer = m_declaredLayer;
        return true;
    }
    if(!m_pending->prefixComplete(m_graphics.getDevice()))
        return true;
    const GpuFrame frame = m_pending;
    frame->m_vertexUpload = {};
    frame->m_indexUpload = {};
    frame->m_clear = {};
    frame->m_raster = {};
    const Core::GpuExternalCompletionId targetReady = graph.importExternalCompletion(
        Core::GpuExternalCompletionDesc().setIdentity(Name("ui.layer_ready")).setMarkerLabel("UI Layer Initialization Completion")
            .setToken(frame->m_target->m_readinessToken)
    );
    if(!targetReady.valid())
        return false;
    const Core::GpuGraphResourceId color = graph.importTexture(
        frame->m_target->m_color,
        Core::GpuGraphResourceDesc().setIdentity(Name("ui.layer")).setMarkerLabel("UI Linear Layer").setType(Core::GpuGraphResourceType::Texture)
            .setInitialAvailabilityCompletion(targetReady).setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    const Core::GpuExternalCompletionId skinReady = graph.importExternalCompletion(
        Core::GpuExternalCompletionDesc().setIdentity(Name("ui.atlas_ready")).setMarkerLabel("UI Atlas Upload Completion")
            .setToken(frame->m_skin->m_texture.readinessToken)
    );
    if(!color.valid() || !skinReady.valid())
        return false;
    const Core::GpuGraphResourceId skin = graph.importTexture(
        frame->m_skin->m_texture.texture,
        Core::GpuGraphResourceDesc().setIdentity(Name("ui.atlas")).setMarkerLabel("UI Atlas").setType(Core::GpuGraphResourceType::Texture)
            .setInitialAvailabilityCompletion(skinReady).setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    const Core::GpuGraphResourceVersionId version = graph.declareResourceVersion(
        Core::GpuGraphResourceVersionDesc().setResource(color).setOrigin(Core::GpuGraphResourceVersionOrigin::TaskProduced)
    );
    const Core::GpuGraphPipelineId pipeline = graph.importGraphicsPipeline(
        frame->m_resources->m_pipeline, Core::GpuGraphPipelineDesc().setIdentity(Name("ui.raster_pipeline")).setMarkerLabel("UI Raster Pipeline")
            .setType(Core::GpuGraphPipelineType::Graphics)
    );
    if(!skin.valid() || !version.valid() || !pipeline.valid())
        return false;
    Core::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Core::GpuTaskCostHint::Small;
    uploadScheduling.allowParallelRecording = true;
    uploadScheduling.allowSameClassQueueRouting = true;
    uploadScheduling.preferNonPrimarySameClassQueue = true;
    FixedVector<Core::GpuTaskId, 3u> dependencies;
    GpuRasterResourceUses uses;
    GpuGlyphGraphResources glyphPages;
    GpuSdfGraphResources sdfPages;
    uses.push_back({ color, {}, Core::ResourceStates::RenderTarget, Core::GpuTaskResourceAccess::Write });
    uses.push_back({ skin, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read });
    if(!declareGlyphPages(graph, frame, glyphPages, uses) || !declareSdfPages(graph, frame, sdfPages, uses))
        return false;
    if(!frame->m_snapshot.vertices().empty()){
        const Core::GpuGraphResourceId vertices = graph.importBuffer(
            frame->m_vertices, Core::GpuGraphResourceDesc().setIdentity(Name("ui.vertices")).setMarkerLabel("UI Vertices").setType(Core::GpuGraphResourceType::Buffer)
                .setExternalFinalState(Core::ResourceStates::Common)
        );
        const Core::GpuGraphResourceId indices = graph.importBuffer(
            frame->m_indices, Core::GpuGraphResourceDesc().setIdentity(Name("ui.indices")).setMarkerLabel("UI Indices").setType(Core::GpuGraphResourceType::Buffer)
                .setExternalFinalState(Core::ResourceStates::Common)
        );
        if(!vertices.valid() || !indices.valid())
            return false;
        const Core::GpuUploadBlobId vertexBytes = graph.copyUploadData(
            frame->m_snapshot.vertices().data(), frame->m_snapshot.vertices().size() * sizeof(Vertex), alignof(Vertex)
        );
        const Core::GpuUploadBlobId indexBytes = graph.copyUploadData(
            frame->m_snapshot.indices().data(), frame->m_snapshot.indices().size() * sizeof(u32), alignof(u32)
        );
        if(!vertexBytes.valid() || !indexBytes.valid())
            return false;
        const Core::GpuTaskId vertexUpload = graph.addUploadBufferTask(
            Core::GpuTaskDesc().setIdentity(Name("ui.vertex_upload")).setMarkerLabel("UI Vertex Upload").setScheduling(uploadScheduling),
            Core::GpuUploadBufferTaskDesc{
                .source = vertexBytes,
                .destination = vertices,
                .finalState = Core::ResourceStates::Common,
                .acceptedToken = &frame->m_vertexUpload,
            }
        );
        const Core::GpuTaskId indexUpload = graph.addUploadBufferTask(
            Core::GpuTaskDesc().setIdentity(Name("ui.index_upload")).setMarkerLabel("UI Index Upload").setScheduling(uploadScheduling),
            Core::GpuUploadBufferTaskDesc{
                .source = indexBytes,
                .destination = indices,
                .finalState = Core::ResourceStates::Common,
                .acceptedToken = &frame->m_indexUpload,
            }
        );
        if(!vertexUpload.valid() || !indexUpload.valid())
            return false;
        dependencies.push_back(vertexUpload);
        dependencies.push_back(indexUpload);
        uses.push_back({ vertices, {}, Core::ResourceStates::VertexBuffer, Core::GpuTaskResourceAccess::Read });
        uses.push_back({ indices, {}, Core::ResourceStates::IndexBuffer, Core::GpuTaskResourceAccess::Read });
    }
    const Core::GpuTaskId clear = graph.addClearTextureTask(
        Core::GpuTaskDesc().setIdentity(Name("ui.clear")).setMarkerLabel("UI Transparent Clear").setScheduling(uploadScheduling),
        Core::GpuClearTextureTaskDesc{
            .acceptedToken = &frame->m_clear,
            .destination = color,
            .valueType = Core::GpuClearTextureTaskValueType::Float,
            .floatValue = Core::Color(0.0f, 0.0f, 0.0f, 0.0f),
        }
    );
    if(!clear.valid())
        return false;
    dependencies.push_back(clear);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.allowParallelRecording = true;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    const Core::GpuTaskResourceVersionUse produce{ version, Core::GpuTaskResourceVersionRole::Produce };
    const Core::GpuTaskId raster = graph.addTask<GpuRasterTask>(
        Core::GpuTaskDesc().setIdentity(Name("ui.raster")).setMarkerLabel("UI Raster").setScheduling(scheduling)
            .setDependencies(dependencies.data(), dependencies.size()).setResourceUses(uses.data(), uses.size())
            .setResourceVersionUses(&produce, 1u)
            .setTimingMetadata({ 0u, m_width ^ (m_height << 16u), Core::GpuTaskTimingPolicy::Task }),
        GpuRasterTask::Payload{ frame, color, skin, Move(glyphPages), Move(sdfPages) }
    );
    if(!raster.valid())
        return false;
    outLayer = { raster, color, version, frame->m_target->m_sampledImage, frame->m_snapshot.generation() };
    m_declaredLayer = outLayer;
    m_declaredGraph = &graph;
    m_graphGeneration = generation;
    m_claimed = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

