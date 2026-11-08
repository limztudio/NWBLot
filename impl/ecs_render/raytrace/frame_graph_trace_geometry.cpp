// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_trace_geometry.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/raytrace/types.adhd.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphSoftwareBvhBuildStateResult> FrameGraphSoftwareBvhBuildStateImporter::declare(const FrameGraphSoftwareBvhBuildStateInputs& inputs, Core::Alloc::ScratchArena& scratchArena){
    FrameGraphSoftwareBvhBuildStateResult result(scratchArena);
    using namespace RendererTaskGraphDetail;
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources = inputs.rayTracingShadowResources;
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareBvhBuildStateResources{ scratchArena };
    Vector<NotNull<Core::Buffer*>, Core::Alloc::ScratchArena> softwareBvhBuildStateBuffers{ scratchArena };
    if(inputs.softwareTraceResourcesPrepared){
        const auto meshSoftwareBvhParentBuildStates = m_meshSystem.collectSoftwareBvhParentBuildStates(scratchArena);
        if(!meshSoftwareBvhParentBuildStates)
            return MakeUnexpected(Failure{});
        // Sort keys, sort payload, and visit counter follow the parent build states.
        softwareBvhBuildStateResources.reserve(meshSoftwareBvhParentBuildStates->size() + s_SortBuildStateCapacity);
        softwareBvhBuildStateBuffers.reserve(meshSoftwareBvhParentBuildStates->size() + s_SortBuildStateCapacity);
        const auto appendSoftwareBvhBuildState = [&](const Core::BufferHandle& buffer, const Name identity, const AStringView label){
            if(!buffer || !identity)
                return false;
            for(const NotNull<Core::Buffer*> existing : softwareBvhBuildStateBuffers){
                if(existing.get() == buffer.get())
                    return true;
            }
            const auto resourceResult = ImportExternalBuffer(m_graph, buffer, identity, label);
            if(!resourceResult)
                return false;
            const Core::GpuGraphResourceId resource = *resourceResult;
            softwareBvhBuildStateBuffers.push_back(MakeNotNull(buffer.get()));
            softwareBvhBuildStateResources.push_back(resource);
            return true;
        };
        for(const ECSRenderDetail::MeshSoftwareBvhParentBuildState& state : *meshSoftwareBvhParentBuildStates){
            if(!appendSoftwareBvhBuildState(state.buffer, state.identity, "Software BVH Parent")){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software BVH parent build state"));
                return MakeUnexpected(Failure{});
            }
        }
        if(
            !rayTracingShadowResources.bvhSortKeysBuffer
            || !rayTracingShadowResources.bvhSortPayloadBuffer
            || !rayTracingShadowResources.bvhVisitCounterBuffer
            || !appendSoftwareBvhBuildState(rayTracingShadowResources.bvhSortKeysBuffer, Name(s_RenderNameRenderShadowTraceSwBvhSortKeys), "Software BVH Sort Keys")
            || !appendSoftwareBvhBuildState(rayTracingShadowResources.bvhSortPayloadBuffer, Name(s_RenderNameRenderShadowTraceSwBvhSortPayload), "Software BVH Sort Payload")
            || !appendSoftwareBvhBuildState(rayTracingShadowResources.bvhVisitCounterBuffer, Name(s_RenderNameRenderShadowTraceSwBvhVisitCounter), "Software BVH Visit Counter")
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared software BVH build state"));
            return MakeUnexpected(Failure{});
        }
    }
    result.buildStateResources = Move(softwareBvhBuildStateResources);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphTraceGeometryResult> FrameGraphTraceGeometryImporter::declare(Core::Alloc::ScratchArena& scratchArena, const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources){
    using namespace RendererTaskGraphDetail;

    // Preflight froze the mesh tables; import each buffer once and fan out IDs.
    if(!m_raytracingSystem.freezePreparedShadowTraceGeometryBuffers(scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow-trace geometry buffers"));
        return MakeUnexpected(Failure{});
    }
    const Span<const PreparedShadowTraceGeometryBuffer> preparedTraceGeometry = m_raytracingSystem.preparedShadowTraceGeometryBuffers();
    const Span<const Core::TextureHandle> preparedTraceMaterialSampledTextures = m_raytracingSystem.preparedShadowTraceMaterialSampledTextures();

    FrameGraphTraceGeometryResult result(scratchArena);
    result.traceGeometry.reserve(preparedTraceGeometry.size());
    result.hardwareTraceGeometry.reserve(preparedTraceGeometry.size());
    result.softwareTraceGeometry.reserve(preparedTraceGeometry.size());
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        Core::GpuGraphResourceDesc desc = BufferResourceDesc(preparedBuffer.identity, "Prepared Shadow Trace Geometry");
        desc.setInitialState(preparedBuffer.initialState);
        const auto resourceResult = m_graph.importBuffer(preparedBuffer.buffer, desc);
        if(!resourceResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-trace geometry buffer"));
            return MakeUnexpected(Failure{});
        }
        const Core::GpuGraphResourceId resource = *resourceResult;
        result.traceGeometry.push_back(resource);
        if(preparedBuffer.roles & (PreparedShadowTraceGeometryRole::HardwarePosition | PreparedShadowTraceGeometryRole::HardwareIndex | PreparedShadowTraceGeometryRole::HardwareAttribute))
            result.hardwareTraceGeometry.push_back(resource);
        if(preparedBuffer.roles & (PreparedShadowTraceGeometryRole::SoftwareNode | PreparedShadowTraceGeometryRole::SoftwarePosition | PreparedShadowTraceGeometryRole::SoftwareIndex | PreparedShadowTraceGeometryRole::SoftwareAttribute))
            result.softwareTraceGeometry.push_back(resource);
    }
    // Trace dispatchers select textures via frozen material context; reuse typed preflight import when owned.
    const auto traceMaterialSampledTextureResources = ImportMaterialSampledTextureResources(preparedTraceMaterialSampledTextures, "Prepared Trace Material Sampled Texture", m_graph, scratchArena);
    if(!traceMaterialSampledTextureResources){
        switch(traceMaterialSampledTextureResources.error()){
        case SampledTextureImportFailure::GraphUnavailable:
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared trace material texture graph was unavailable"));
            return MakeUnexpected(Failure{});
        case SampledTextureImportFailure::MissingIdentity:
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared trace material texture has no stable identity"));
            return MakeUnexpected(Failure{});
        case SampledTextureImportFailure::ImportFailed:
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared trace material sampled texture"));
            return MakeUnexpected(Failure{});
        }
    }
    result.shadowTraceGeometrySet = importSet(result.traceGeometry, Name(s_RenderNameRenderPostGbufferTraceGeometry), "Post-G-Buffer Trace Geometry");
    result.softwareTraceGeometrySet = importSet(result.softwareTraceGeometry, Name(s_RenderNameRenderSoftwareTraceGeometry), "Software Trace Geometry");
    if(!traceMaterialSampledTextureResources->empty()){
        const auto traceMaterialSampledTextureSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_RenderNameRenderTraceMaterialSampledTextures))
                .setMarkerLabel("Trace Material Sampled Textures")
                .setMembers(*traceMaterialSampledTextureResources)
        );
        if(!traceMaterialSampledTextureSetResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared trace material sampled textures"));
            return MakeUnexpected(Failure{});
        }
        result.traceMaterialSampledTextureSet = *traceMaterialSampledTextureSetResult;
    }
    result.hardwareTraceGeometrySet = importSet(result.hardwareTraceGeometry, Name(s_RenderNameRenderHardwareTraceGeometry), "Hardware Trace Geometry");

    result.softwareTraceResourcesPrepared = !result.softwareTraceGeometry.empty();
    FrameGraphSoftwareBvhBuildStateImporter softwareBvhBuildStateImporter(m_graph, m_meshSystem);
    auto softwareBvhBuildStateResult = softwareBvhBuildStateImporter.declare(
        FrameGraphSoftwareBvhBuildStateInputs{ .rayTracingShadowResources = rayTracingShadowResources, .softwareTraceResourcesPrepared = result.softwareTraceResourcesPrepared, },
        scratchArena
    );
    if(!softwareBvhBuildStateResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared software BVH build state"));
        return MakeUnexpected(Failure{});
    }
    result.softwareBvhBuildState = Move(softwareBvhBuildStateResult->buildStateResources);
    return result;
}

// Empty member lists and failed imports leave the set invalid.
Core::GpuGraphResourceSetId FrameGraphTraceGeometryImporter::importSet(const Span<const Core::GpuGraphResourceId> members, const Name& identity, const AStringView markerLabel){
    if(members.empty())
        return {};

    const auto setResult = m_graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(identity).setMarkerLabel(markerLabel).setMembers(members));
    if(!setResult)
        return {};
    return *setResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

