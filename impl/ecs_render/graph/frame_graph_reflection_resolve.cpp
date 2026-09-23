// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/graph/frame_graph_reflection_resolve.h>

#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/reflection/reflection_system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/task_graph_refraction_resolve.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphReflectionResolve::FrameGraphReflectionResolve(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    RendererRayTracingSystem& raytracingSystem,
    RendererReflectionSystem& reflectionSystem
)
    : m_graph(graph)
    , m_graphics(graphics)
    , m_raytracingSystem(raytracingSystem)
    , m_reflectionSystem(reflectionSystem){
}


bool FrameGraphReflectionResolve::declare(
    const FrameGraphReflectionResolveInputs& inputs,
    RayTracingSceneGraphReads& sceneReads,
    Core::Alloc::ScratchArena& scratchArena,
    FrameGraphReflectionResolveResult& outResult
){
    outResult = FrameGraphReflectionResolveResult{};
    using namespace RendererTaskGraphDetail;
    DeferredFrameTargets& deferredTargets = *inputs.targets;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = *inputs.meshViewSnapshot;
    const RayTracingSceneGraphResources& sceneResources = *inputs.sceneResources;
    const ReflectionSettings& reflectionSettings = *inputs.reflectionSettings;
    const CsgFrameState& csgFrameState = *inputs.csgFrameState;
    RayTracingRefractionGraphResources refractionResources = *inputs.refractionResources;
    const bool refractionActive = inputs.refractionActive;
    const bool useLaggedLightingHistory = inputs.useLaggedLightingHistory;
    const Core::GpuGraphResourceId specularRoughness = inputs.specularRoughness;
    const Core::GpuGraphResourceId refractionSpecularRoughness = inputs.refractionSpecularRoughness;
    const Core::GpuGraphResourceId normal = inputs.normal;
    const Core::GpuGraphResourceId depth = inputs.depth;
    const Core::GpuGraphResourceId worldPosition = inputs.worldPosition;
    const Core::GpuGraphResourceId refractionDepth = inputs.refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = inputs.refractionNormalIor;
    const Core::GpuGraphResourceId refractionTintCoverage = inputs.refractionTintCoverage;
    const Core::GpuGraphResourceId refractionInstance = inputs.refractionInstance;
    const Core::GpuGraphResourceId refractionResolve = inputs.refractionResolve;
    const Core::GpuGraphResourceId opaqueColor = inputs.opaqueColor;
    const Core::GpuGraphResourceId meshView = inputs.meshView;
    const Core::GpuGraphResourceId currentBindlessSlots = inputs.currentBindlessSlots;
    const Core::GpuGraphResourceId sceneShading = inputs.sceneShading;
    const Core::GpuGraphResourceId lights = inputs.lights;
    const Core::GpuGraphResourceId avboitAccumColor = inputs.avboitAccumColor;
    const Core::GpuGraphResourceId avboitAccumExtinction = inputs.avboitAccumExtinction;
    const Core::GpuGraphResourceId avboitForegroundColor = inputs.avboitForegroundColor;
    const Core::GpuGraphResourceId avboitForegroundExtinction = inputs.avboitForegroundExtinction;
    const Core::GpuGraphResourceSetId hardwareTraceGeometrySet = inputs.hardwareTraceGeometrySet;
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet = inputs.traceMaterialSampledTextureSet;
    ReflectionSceneContentStamp reflectionContentStamp = *inputs.contentStamp;
    Core::Alloc::ScratchArena& traceGeometryScratchArena = scratchArena;
    reflectionContentStamp.geometry = sceneResources.contentStamp.geometry;
    reflectionContentStamp.material = sceneResources.contentStamp.material;
    // CSG evaluation and lagged screen lighting do not have a matching content stamp for this first history policy.
    reflectionContentStamp.trusted = sceneResources.contentStamp.trusted && csgFrameState.empty() && !useLaggedLightingHistory;
    const ReflectionFrameSnapshot reflectionResources = m_reflectionSystem.snapshotFrameResources(
        deferredTargets, meshViewBufferSnapshot,
        inputs.reflectionSceneAvailable ? sceneResources : RayTracingSceneGraphResources{},
        reflectionSettings, inputs.reflectionFrameIndex, reflectionContentStamp
    );
    if(!reflectionResources.valid())
        return false;
    if(
        !sceneReads.valid()
        && (reflectionResources.hasHardwareWork() || (refractionActive && refractionResources.valid() && refractionResources.usesHardwareTrace))
    ){
        sceneReads = ImportRayTracingSceneGraphReads(
            m_graph, sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), traceGeometryScratchArena
        );
        if(!sceneReads.valid())
            return false;
    }
    const Core::GpuTaskResourceUse reflectionSurfaceReads[] = {
        ReadUse(specularRoughness), ReadUse(refractionSpecularRoughness), ReadUse(normal), ReadUse(depth), ReadUse(worldPosition),
        ReadUse(refractionDepth), ReadUse(refractionNormalIor),
        ReadUse(meshView, Core::ResourceStates::ConstantBuffer),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> reflectionHardwareReads{traceGeometryScratchArena};
    reflectionHardwareReads.reserve(LengthOf(sceneReads.uses) + 2u);
    if(sceneReads.valid()){
        for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
            reflectionHardwareReads.push_back(read);
        reflectionHardwareReads.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        reflectionHardwareReads.push_back(ReadUse(lights));
    }
    Core::GpuTaskResourceSetUse reflectionSets[2] = {};
    usize reflectionSetCount = 0u;
    for(const Core::GpuGraphResourceSetId set : {hardwareTraceGeometrySet, traceMaterialSampledTextureSet}){
        if(set.valid()){
            reflectionSets[reflectionSetCount++] = Core::GpuTaskResourceSetUse{
                .resourceSet = set, .range = {}, .requiredState = Core::ResourceStates::ShaderResource,
                .access = Core::GpuTaskResourceAccess::Read,
            };
        }
    }
    const ReflectionGraphInputs reflectionInputs{
        .opaqueDepth = depth,
        .opaqueColor = opaqueColor,
        .surfaceReads = reflectionSurfaceReads, .surfaceReadCount = LengthOf(reflectionSurfaceReads),
        .hardwareReads = reflectionHardwareReads.data(), .hardwareReadCount = reflectionHardwareReads.size(),
        .hardwareSetReads = reflectionSets, .hardwareSetReadCount = reflectionSetCount,
        .hardwarePreparationReady = const_cast<const bool*>(inputs.hardwarePreparationReady),
        .hardwareDispatchLogged = const_cast<bool*>(inputs.hardwareDispatchLogged),
        .fallbackDispatchLogged = const_cast<bool*>(inputs.fallbackDispatchLogged),
    };
    const ReflectionGraphResult reflectionGraph = DeclareReflectionTasks(
        m_graph, m_graphics, traceGeometryScratchArena,
        reflectionResources, reflectionInputs, inputs.lightingTask
    );
    if(!reflectionGraph.valid())
        return false;
    const ReflectionCompositeInputs reflectionCompositeInputs{
        .opaqueRadianceSlot = reflectionResources.parameters.opaqueRadianceSlot,
        .glassRadianceSlot = reflectionResources.parameters.glassRadianceSlot,
        .debugView = reflectionSettings.debugView,
    };
    refractionResources.opaqueReflectionSlot = reflectionCompositeInputs.opaqueRadianceSlot;

    Core::GpuTaskId refractionResolveTask;
    if(refractionActive && refractionResources.valid()){
        Core::Alloc::ScratchArena refractionScratch(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> refractionUses{refractionScratch};
        const Core::GpuGraphResourceId refractionInputs[] = {
            refractionDepth, refractionNormalIor, refractionTintCoverage, refractionInstance,
            opaqueColor, reflectionGraph.opaqueRadiance, worldPosition, depth, avboitAccumColor, avboitAccumExtinction,
            avboitForegroundColor, avboitForegroundExtinction
        };
        refractionUses.reserve(LengthOf(refractionInputs) + 5u + LengthOf(sceneReads.uses));
        for(const auto input : refractionInputs)
            refractionUses.push_back(ReadUse(input));
        refractionUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(lights));
        refractionUses.push_back(WriteUse(refractionResolve, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskResourceSetUse refractionSets[3] = {};
        usize refractionSetCount = 0;
        if(refractionResources.usesHardwareTrace){
            for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
                refractionUses.push_back(read);
            const Core::GpuGraphResourceSetId sets[] = {
                hardwareTraceGeometrySet, traceMaterialSampledTextureSet
            };
            for(const auto set : sets){
                if(set.valid())
                    refractionSets[refractionSetCount++] = Core::GpuTaskResourceSetUse{
                        .resourceSet = set, .range = {}, .requiredState = Core::ResourceStates::ShaderResource,
                        .access = Core::GpuTaskResourceAccess::Read,
                    };
            }
        }
        const Core::GpuTaskId refractionDependencies[] = {
            reflectionGraph.completion, inputs.avboitFinalTask, inputs.surfelGiTask
        };
        Core::GpuTaskDesc refractionDesc;
        refractionDesc.setIdentity(Name("render.avboit.refraction_resolve"))
            .setMarkerLabel("AVBOIT Refraction Resolve").setQueue(ComputeQueueRequest())
            .setDependencies(refractionDependencies, LengthOf(refractionDependencies))
            .setResourceUses(refractionUses.data(), refractionUses.size())
            .setResourceSetUses(refractionSets, refractionSetCount);
        refractionResolveTask = m_graph.addTask<RefractionResolveGraphTask>(refractionDesc,
            RefractionResolveGraphTask::Payload{
                .system = &m_raytracingSystem, .targets = &deferredTargets, .resources = refractionResources,
                .hardwarePreparationReady = const_cast<const bool*>(inputs.hardwarePreparationReady),
                .dispatchLogged = refractionResources.usesHardwareTrace
                    ? const_cast<bool*>(inputs.refractionHardwareLogged) : const_cast<bool*>(inputs.refractionScreenLogged),
                .screenFallbackDispatchLogged = const_cast<bool*>(inputs.refractionScreenLogged),
            });
    }
    else{
        Core::GpuTaskDesc clearDesc;
        clearDesc.setIdentity(Name("render.avboit.refraction_resolve_clear"))
            .setMarkerLabel("AVBOIT Refraction Resolve Clear").setQueue(GraphicsUploadQueueRequest())
            .setDependencies(&inputs.avboitFinalTask, 1u);
        Core::GpuClearTextureTaskDesc clear;
        clear.destination = refractionResolve;
        clear.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clear.valueType = Core::GpuClearTextureTaskValueType::Float;
        clear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        refractionResolveTask = m_graph.addClearTextureTask(
            clearDesc, clear);
    }
    if(!refractionResolveTask.valid())
        return false;
    outResult.reflectionGraph = reflectionGraph;
    outResult.reflectionCompositeInputs = reflectionCompositeInputs;
    outResult.refractionResolveTask = refractionResolveTask;
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
