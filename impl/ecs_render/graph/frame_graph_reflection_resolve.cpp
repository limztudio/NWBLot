// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_reflection_resolve.h"

#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/reflection/reflection_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/task_graph_refraction_resolve.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphReflectionResolve::FrameGraphReflectionResolve(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, RendererRayTracingSystem& raytracingSystem, RendererReflectionSystem& reflectionSystem)noexcept
    : m_graph(graph)
    , m_graphics(graphics)
    , m_raytracingSystem(raytracingSystem)
    , m_reflectionSystem(reflectionSystem){
}


Expected<FrameGraphReflectionResolveResult> FrameGraphReflectionResolve::declare(const FrameGraphReflectionResolveInputs& inputs, RayTracingSceneGraphReads& sceneReads, Core::Alloc::ScratchArena& scratchArena){
    FrameGraphReflectionResolveResult result{};
    using namespace RendererTaskGraphDetail;
    DeferredFrameTargets& deferredTargets = inputs.targets;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = inputs.meshViewSnapshot;
    const RayTracingSceneGraphResources& sceneResources = inputs.sceneResources;
    const ReflectionSettings& reflectionSettings = inputs.reflectionSettings;
    const CsgFrameState& csgFrameState = inputs.csgFrameState;
    RayTracingRefractionGraphResources refractionResources = inputs.refractionResources;
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
    ReflectionSceneContentStamp reflectionContentStamp = inputs.contentStamp;
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
        return MakeUnexpected(Failure{});
    if(!sceneReads.valid() && (reflectionResources.hasHardwareWork() || (refractionActive && refractionResources.valid() && refractionResources.usesHardwareTrace))){
        auto sceneReadsResult = ImportRayTracingSceneGraphReads(m_graph, sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), traceGeometryScratchArena);
        if(!sceneReadsResult){
            sceneReads = RayTracingSceneGraphReads(scratchArena);
            return MakeUnexpected(Failure{});
        }
        sceneReads = Move(*sceneReadsResult);
    }
    const Core::GpuTaskResourceUse reflectionSurfaceReads[] = {
        ReadUse(specularRoughness), ReadUse(refractionSpecularRoughness), ReadUse(normal), ReadUse(depth), ReadUse(worldPosition),
        ReadUse(refractionDepth), ReadUse(refractionNormalIor),
        ReadUse(meshView, Core::ResourceStates::ConstantBuffer),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> reflectionHardwareReads{traceGeometryScratchArena};
    // Scene shading and lights follow the scene reads.
    constexpr usize s_ReflectionExtraHardwareReadCapacity = 2u;
    reflectionHardwareReads.reserve(LengthOf(sceneReads.uses) + sceneReads.csgUses.size() + s_ReflectionExtraHardwareReadCapacity);
    if(sceneReads.valid()){
        for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
            reflectionHardwareReads.push_back(read);
        for(const Core::GpuTaskResourceUse& read : sceneReads.csgUses)
            reflectionHardwareReads.push_back(read);
        reflectionHardwareReads.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        reflectionHardwareReads.push_back(ReadUse(lights));
    }
    Core::GpuTaskResourceSetUse reflectionSets[2] = {};
    usize reflectionSetCount = 0u;
    for(const Core::GpuGraphResourceSetId set : {hardwareTraceGeometrySet, traceMaterialSampledTextureSet}){
        if(set.valid()){
            reflectionSets[reflectionSetCount++] = Core::GpuTaskResourceSetUse{ .resourceSet = set, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
        }
    }
    const ReflectionGraphInputs reflectionInputs{
        .raytracingSystem = m_raytracingSystem,
        .opaqueDepth = depth,
        .opaqueColor = opaqueColor,
        .surfaceReads = reflectionSurfaceReads, .surfaceReadCount = LengthOf(reflectionSurfaceReads),
        .hardwareReads = reflectionHardwareReads.data(), .hardwareReadCount = reflectionHardwareReads.size(),
        .hardwareSetReads = reflectionSets, .hardwareSetReadCount = reflectionSetCount,
        .hardwarePreparationReady = inputs.hardwarePreparationReady,
        .hardwareDispatchLogged = inputs.hardwareDispatchLogged,
        .fallbackDispatchLogged = inputs.fallbackDispatchLogged,
    };
    auto reflectionGraphResult = DeclareReflectionTasks(m_graph, m_graphics, m_reflectionSystem, traceGeometryScratchArena, reflectionResources, reflectionInputs, inputs.lightingTask);
    if(!reflectionGraphResult)
        return MakeUnexpected(Failure{});
    const ReflectionGraphResult reflectionGraph = Move(*reflectionGraphResult);
    const ReflectionCompositeInputs reflectionCompositeInputs{ .opaqueRadianceSlot = reflectionResources.parameters.opaqueRadianceSlot, .glassRadianceSlot = reflectionResources.parameters.glassRadianceSlot, .debugView = reflectionSettings.debugView, };
    refractionResources.opaqueReflectionSlot = reflectionCompositeInputs.opaqueRadianceSlot;

    Expected<Core::GpuTaskId> refractionResolveTask = MakeUnexpected(Failure{});
    if(refractionActive && refractionResources.valid()){
        const auto tileListResult = m_graph.importBuffer(deferredTargets.avboit.refractionTileList, BufferResourceDesc(Name("render.refraction.tile_list"), "Refraction Tile List"));
        const auto tileArgumentsResult = m_graph.importBuffer(deferredTargets.avboit.refractionTileArguments, BufferResourceDesc(Name("render.refraction.tile_arguments"), "Refraction Tile Arguments"));
        if(!tileListResult || !tileArgumentsResult)
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId tileList = *tileListResult;
        const Core::GpuGraphResourceId tileArguments = *tileArgumentsResult;
        const Core::GpuTaskResourceUse tileClearUse = WriteUse(tileArguments, Core::ResourceStates::CopyDest);
        Core::GpuTaskDesc tileClearDesc;
        tileClearDesc.setIdentity(Name("render.avboit.refraction_tile_arguments_clear")).setMarkerLabel("Refraction Tile Arguments Clear").setDependencies(&inputs.avboitFinalTask, 1u).setResourceUses(&tileClearUse, 1u);
        const auto tileClearTask = m_graph.addTask<RefractionClearTileArgumentsGraphTask>(tileClearDesc, RefractionClearTileArgumentsGraphTask::Payload{ m_graphics, deferredTargets.avboit.refractionTileArguments, tileArguments });
        if(!tileClearTask)
            return MakeUnexpected(Failure{});
        const Core::GpuTaskResourceUse tileUses[] = {
            ReadUse(refractionDepth), ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            WriteUse(tileList, Core::ResourceStates::UnorderedAccess), ReadWriteUse(tileArguments, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskSchedulingHint refractionTileScheduling;
        refractionTileScheduling.mergeWithPrevious = true;
        refractionTileScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc tileDesc;
        tileDesc.setIdentity(Name("render.avboit.refraction_classify_tiles")).setMarkerLabel("Refraction Classify Tiles")
            .setScheduling(refractionTileScheduling)
            .setDependencies(&*tileClearTask, 1u).setResourceUses(tileUses, LengthOf(tileUses));
        const auto tileTask = m_graph.addTask<RefractionClassifyTilesGraphTask>(tileDesc, RefractionClassifyTilesGraphTask::Payload{ m_raytracingSystem, deferredTargets, refractionResources });
        if(!tileTask)
            return MakeUnexpected(Failure{});
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> refractionUses{scratchArena};
        const Core::GpuGraphResourceId refractionInputs[] = {
            refractionDepth, refractionNormalIor, refractionTintCoverage, refractionInstance,
            opaqueColor, reflectionGraph.opaqueRadiance, worldPosition, depth, avboitAccumColor, avboitAccumExtinction,
            avboitForegroundColor, avboitForegroundExtinction
        };
        // Mesh view, bindless slots, scene shading, lights, the resolve output, and tile scheduling buffers.
        constexpr usize s_RefractionFixedUseCapacity = 7u;
        refractionUses.reserve(LengthOf(refractionInputs) + s_RefractionFixedUseCapacity + LengthOf(sceneReads.uses) + sceneReads.csgUses.size());
        for(const auto input : refractionInputs)
            refractionUses.push_back(ReadUse(input));
        refractionUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        refractionUses.push_back(ReadUse(lights));
        refractionUses.push_back(WriteUse(refractionResolve, Core::ResourceStates::UnorderedAccess));
        refractionUses.push_back(ReadUse(tileList));
        refractionUses.push_back(ReadUse(tileArguments, Core::ResourceStates::ShaderResource | Core::ResourceStates::IndirectArgument));
        Core::GpuTaskResourceSetUse refractionSets[3] = {};
        usize refractionSetCount = 0;
        if(refractionResources.usesHardwareTrace){
            for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
                refractionUses.push_back(read);
            for(const Core::GpuTaskResourceUse& read : sceneReads.csgUses)
                refractionUses.push_back(read);
            const Core::GpuGraphResourceSetId sets[] = { hardwareTraceGeometrySet, traceMaterialSampledTextureSet };
            for(const auto set : sets){
                if(set.valid())
                    refractionSets[refractionSetCount++] = Core::GpuTaskResourceSetUse{ .resourceSet = set, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
            }
        }
        const Core::GpuTaskId refractionDependencies[] = { reflectionGraph.completion, *tileTask, inputs.surfelGiTask };
        Core::GpuTaskDesc refractionDesc;
        refractionDesc.setIdentity(Name("render.avboit.refraction_resolve"))
            .setMarkerLabel("AVBOIT Refraction Resolve")
            .setScheduling(refractionTileScheduling)
            .setDependencies(refractionDependencies, LengthOf(refractionDependencies))
            .setResourceUses(refractionUses.data(), refractionUses.size())
            .setResourceSetUses(refractionSets, refractionSetCount)
        ;
        refractionResolveTask = m_graph.addTask<RefractionResolveGraphTask>(
            refractionDesc,
            RefractionResolveGraphTask::Payload{
                .system = m_raytracingSystem, .targets = deferredTargets, .resources = refractionResources,
                .hardwarePreparationReady = inputs.hardwarePreparationReady,
                .dispatchLogged = refractionResources.usesHardwareTrace
                    ? inputs.refractionHardwareLogged : inputs.refractionScreenLogged,
                .screenFallbackDispatchLogged = inputs.refractionScreenLogged,
            }
        );
    }
    else{
        Core::GpuTaskDesc clearDesc;
        clearDesc.setIdentity(Name("render.avboit.refraction_resolve_clear")).setMarkerLabel("AVBOIT Refraction Resolve Clear").setDependencies(&inputs.avboitFinalTask, 1u);
        Core::GpuClearTextureTaskDesc clear;
        clear.destination = refractionResolve;
        clear.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clear.valueType = Core::GpuClearTextureTaskValueType::Float;
        clear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        refractionResolveTask = m_graph.addClearTextureTask(clearDesc, clear);
    }
    if(!refractionResolveTask)
        return MakeUnexpected(Failure{});
    result.reflectionGraph = reflectionGraph;
    result.reflectionCompositeInputs = reflectionCompositeInputs;
    result.refractionResolveTask = *refractionResolveTask;
    result.declared = true;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

