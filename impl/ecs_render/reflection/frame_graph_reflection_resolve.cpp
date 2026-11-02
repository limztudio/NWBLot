// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_reflection_resolve.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphReflectionResolveResult> FrameGraphReflectionResolve::declare(const FrameGraphReflectionResolveInputs& inputs, Core::Alloc::ScratchArena& scratchArena, RayTracingSceneGraphReads& sceneReads){
    FrameGraphReflectionResolveResult result{};
    using namespace RendererTaskGraphDetail;
    DeferredFrameTargets& deferredTargets = inputs.targets;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = inputs.meshViewSnapshot;
    const RayTracingSceneGraphResources& sceneResources = inputs.sceneResources;
    const ReflectionSettings& reflectionSettings = inputs.reflectionSettings;
    const CsgFrameState& csgFrameState = inputs.csgFrameState;
    const b8 useLaggedLightingHistory = inputs.useLaggedLightingHistory;
    const Core::GpuGraphResourceId specularRoughness = inputs.specularRoughness;
    const Core::GpuGraphResourceId refractionSpecularRoughness = inputs.refractionSpecularRoughness;
    const Core::GpuGraphResourceId normal = inputs.normal;
    const Core::GpuGraphResourceId depth = inputs.depth;
    const Core::GpuGraphResourceId worldPosition = inputs.worldPosition;
    const Core::GpuGraphResourceId refractionDepth = inputs.refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = inputs.refractionNormalIor;
    const Core::GpuGraphResourceId opaqueColor = inputs.opaqueColor;
    const Core::GpuGraphResourceId meshView = inputs.meshView;
    const Core::GpuGraphResourceId currentBindlessSlots = inputs.currentBindlessSlots;
    const Core::GpuGraphResourceId sceneShading = inputs.sceneShading;
    const Core::GpuGraphResourceId lights = inputs.lights;
    const Core::GpuGraphResourceSetId hardwareTraceGeometrySet = inputs.hardwareTraceGeometrySet;
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet = inputs.traceMaterialSampledTextureSet;
    ReflectionSceneContentStamp reflectionContentStamp = inputs.contentStamp;
    reflectionContentStamp.geometry = sceneResources.contentStamp.geometry;
    reflectionContentStamp.material = sceneResources.contentStamp.material;
    // CSG evaluation and lagged screen lighting do not have a matching content stamp for this first history policy.
    reflectionContentStamp.trusted = sceneResources.contentStamp.trusted && csgFrameState.empty() && !useLaggedLightingHistory;
    const ReflectionFrameSnapshot reflectionResources = m_reflectionSystem.snapshotFrameResources(
        deferredTargets,
        meshViewBufferSnapshot,
        inputs.reflectionSceneAvailable ? sceneResources : RayTracingSceneGraphResources{},
        reflectionSettings,
        inputs.reflectionFrameIndex,
        reflectionContentStamp
    );
    if(!reflectionResources.valid())
        return MakeUnexpected(Failure{});
    if(!sceneReads.valid() && reflectionResources.hasHardwareWork()){
        auto sceneReadsResult = ImportRayTracingSceneGraphReads(sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), inputs.traceContextUploadTask, m_graph, scratchArena);
        if(!sceneReadsResult){
            sceneReads = RayTracingSceneGraphReads(scratchArena);
            return MakeUnexpected(Failure{});
        }
        sceneReads = Move(*sceneReadsResult);
    }
    const Array<Core::GpuTaskResourceUse, 9> reflectionSurfaceReads = {
        ReadUse(specularRoughness),
        ReadUse(refractionSpecularRoughness),
        ReadUse(normal),
        ReadUse(depth),
        ReadUse(worldPosition),
        ReadUse(refractionDepth),
        ReadUse(refractionNormalIor),
        ReadUse(meshView, Core::ResourceStates::ConstantBuffer),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> reflectionHardwareReads{scratchArena};
    // Scene shading and lights follow the scene reads.
    reflectionHardwareReads.reserve(sceneReads.uses.size() + sceneReads.csgUses.size() + s_ReflectionExtraHardwareReadCapacity);
    if(sceneReads.valid()){
        for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
            reflectionHardwareReads.push_back(read);
        for(const Core::GpuTaskResourceUse& read : sceneReads.csgUses)
            reflectionHardwareReads.push_back(read);
        reflectionHardwareReads.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        reflectionHardwareReads.push_back(ReadUse(lights));
    }
    const RendererFramePipelineDetail::TraceResourceSetUses reflectionSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(hardwareTraceGeometrySet, traceMaterialSampledTextureSet);
    const ReflectionGraphInputs reflectionInputs{
        .raytracingSystem = m_raytracingSystem,
        .opaqueDepth = depth,
        .opaqueColor = opaqueColor,
        .surfaceReads = reflectionSurfaceReads,
        .hardwareReads = reflectionHardwareReads,
        .hardwareSetReads = Span<const Core::GpuTaskResourceSetUse>(reflectionSets.uses.data(), reflectionSets.useCount),
        .hardwarePreparationReady = &inputs.hardwarePreparationReady,
        .hardwareDispatchLogged = &inputs.hardwareDispatchLogged,
        .fallbackDispatchLogged = &inputs.fallbackDispatchLogged,
    };
    auto reflectionGraphResult = DeclareReflectionTasks(reflectionResources, reflectionInputs, inputs.lightingTask, m_graph, m_graphics, m_reflectionSystem, scratchArena);
    if(!reflectionGraphResult)
        return MakeUnexpected(Failure{});
    result.reflectionGraph = Move(*reflectionGraphResult);
    result.reflectionCompositeInputs = ReflectionCompositeInputs{
        .opaqueRadianceSlot = reflectionResources.parameters.opaqueRadianceSlot,
        .glassRadianceSlot = reflectionResources.parameters.glassRadianceSlot,
        .debugView = reflectionSettings.debugView,
    };
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

