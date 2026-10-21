// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_trace_geometry.h"

#include <impl/ecs_render/graph/frame_graph_software_bvh_build_state.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphTraceGeometryResult> FrameGraphTraceGeometryImporter::declare(Core::Alloc::ScratchArena& scratchArena, const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources){
    using namespace RendererTaskGraphDetail;

    // Preflight froze the mesh tables; import each buffer once and fan out IDs.
    if(!m_raytracingSystem.freezePreparedShadowTraceGeometryBuffers(scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow-trace geometry buffers"));
        return MakeUnexpected(Failure{});
    }
    const PreparedShadowTraceGeometryBufferVector& preparedTraceGeometry = m_raytracingSystem.preparedShadowTraceGeometryBuffers();
    const PreparedShadowTraceMaterialSampledTextureVector& preparedTraceMaterialSampledTextures = m_raytracingSystem.preparedShadowTraceMaterialSampledTextures();

    FrameGraphTraceGeometryResult result(scratchArena);
    result.traceGeometry.reserve(preparedTraceGeometry.size());
    result.hardwareTraceGeometry.reserve(preparedTraceGeometry.size());
    result.hardwareTraceAttributes.reserve(preparedTraceGeometry.size());
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
        if(preparedBuffer.roles & (PreparedShadowTraceGeometryRole::s_HardwarePosition | PreparedShadowTraceGeometryRole::s_HardwareIndex | PreparedShadowTraceGeometryRole::s_HardwareAttribute))
            result.hardwareTraceGeometry.push_back(resource);
        if(preparedBuffer.roles & PreparedShadowTraceGeometryRole::s_HardwareAttribute)
            result.hardwareTraceAttributes.push_back(resource);
        if(preparedBuffer.roles & (PreparedShadowTraceGeometryRole::s_SoftwareNode | PreparedShadowTraceGeometryRole::s_SoftwarePosition | PreparedShadowTraceGeometryRole::s_SoftwareIndex | PreparedShadowTraceGeometryRole::s_SoftwareAttribute))
            result.softwareTraceGeometry.push_back(resource);
    }
    // Trace dispatchers select textures via frozen material context; reuse typed preflight import when owned.
    const auto traceMaterialSampledTextureResources = ImportMaterialSampledTextureResources(
        m_graph,
        preparedTraceMaterialSampledTextures.data(),
        preparedTraceMaterialSampledTextures.size(),
        "Prepared Trace Material Sampled Texture",
        scratchArena
    );
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
    result.shadowTraceGeometrySet = importSet(result.traceGeometry, Name("render.post_gbuffer_trace_geometry"), "Post-G-Buffer Trace Geometry");
    result.softwareTraceGeometrySet = importSet(result.softwareTraceGeometry, Name("render.software_trace_geometry"), "Software Trace Geometry");
    if(!traceMaterialSampledTextureResources->empty()){
        const auto traceMaterialSampledTextureSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.trace_material_sampled_textures"))
                .setMarkerLabel("Trace Material Sampled Textures")
                .setMembers(traceMaterialSampledTextureResources->data(), traceMaterialSampledTextureResources->size())
        );
        if(!traceMaterialSampledTextureSetResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared trace material sampled textures"));
            return MakeUnexpected(Failure{});
        }
        result.traceMaterialSampledTextureSet = *traceMaterialSampledTextureSetResult;
    }
    result.hardwareTraceGeometrySet = importSet(result.hardwareTraceGeometry, Name("render.hardware_trace_geometry"), "Hardware Trace Geometry");
    result.hardwareTraceAttributeSet = importSet(result.hardwareTraceAttributes, Name("render.hardware_trace_attributes"), "Hardware Trace Attributes");

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
Core::GpuGraphResourceSetId FrameGraphTraceGeometryImporter::importSet(const Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& members, const Name& identity, const AStringView markerLabel){
    if(members.empty())
        return {};

    const auto setResult = m_graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(identity).setMarkerLabel(markerLabel).setMembers(members.data(), members.size()));
    if(!setResult)
        return {};
    return *setResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

