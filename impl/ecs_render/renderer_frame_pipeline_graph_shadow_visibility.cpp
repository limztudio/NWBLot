// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <impl/ecs_render/raytrace/task_graph_shadow_visibility_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererFramePipeline::declareDeferredShadowVisibilityTask(
    DeferredFrameTargets& deferredTargets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan,
    const bool hardwareShadowSupported,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId shadowVisibility,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const softwareTraceGeometryResources,
    const usize softwareTraceGeometryResourceCount,
    const Core::GpuGraphResourceSetId softwareTraceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId prefixTask,
    const Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    bool& opaqueProduced,
    bool& transparentTraceProduced,
    u32& opaqueFrameIndex
){
    using namespace RendererTaskGraphDetail;

    m_deferredShadowVisibilityOpaqueTask = {};
    m_deferredShadowVisibilityOpaqueFirstWaveletTask = {};
    m_deferredShadowVisibilityOpaqueResolveTask = {};
    m_deferredShadowVisibilityTransparentTraceTask = {};
    m_deferredShadowVisibilityTransparentTemporalMergeTask = {};
    m_deferredShadowVisibilityTransparentFirstWaveletTask = {};
    m_deferredShadowVisibilityAdaptiveStatsClearTask = {};
    m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
    m_deferredShadowVisibilityAdaptiveStatsReadbackTask = {};
    m_deferredShadowVisibilityAllLitClearTask = {};
    m_deferredShadowVisibilityTask = {};
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !normal.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !prefixTask.valid()
        || (softwareTraceGeometryResourceCount != 0u && !softwareTraceGeometryResources)
    )
        return false;

    Core::GpuTaskExternalStateSource shadowVisibilityStateSources[2u] = {};
    usize shadowVisibilityStateSourceCount = 0u;
    const auto* const shadowScratchStates = m_shadowComputePersistentState.source();
    if(shadowScratchStates){
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = {
            .states = shadowScratchStates,
        };
    }
    const auto* const shadowReturnStates = m_shadowVisibilityReturnState.source();
    if(shadowReturnStates){
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = {
            .states = shadowReturnStates,
            .applicableConsumerQueueClass = Core::CommandQueue::Compute,
        };
    }
    const Core::GpuTaskExternalStateSource* const shadowVisibilityStateSourceData =
        shadowVisibilityStateSourceCount != 0u ? shadowVisibilityStateSources : nullptr
    ;

    const Core::GpuExternalCompletionId* const laggedLightingHistoryWriterDrainDependencies =
        laggedLightingHistoryWriterDrainCompletion.valid() ? &laggedLightingHistoryWriterDrainCompletion : nullptr
    ;
    const usize laggedLightingHistoryWriterDrainDependencyCount = laggedLightingHistoryWriterDrainCompletion.valid() ? 1u : 0u;

    opaqueProduced = false;
    transparentTraceProduced = false;
    opaqueFrameIndex = 0u;
    // Only the fully prepared soft-transparent route can expose this boundary. Direct, adaptive/hybrid, and
    // resource-degraded paths retain the established monolithic Shadow Visibility callback.
    const bool preparedSoftTransparentFoldCandidate =
        rayTracingPlan.softTransparentFoldReady
        && deferredTargets.shadowCoarseTransmittance
        && deferredTargets.shadowSoftHalfA
        && deferredTargets.shadowSoftHalfB
        && deferredTargets.shadowSoftGeometry
        && deferredTargets.shadowSoftGeometryPrev
        && deferredTargets.shadowHistA
        && deferredTargets.shadowHistB
        && deferredTargets.shadowMomentsA
        && deferredTargets.shadowMomentsB
        && deferredTargets.transparentSoftHalf
        && deferredTargets.transparentHistA
        && deferredTargets.transparentHistB
        && deferredTargets.transparentMomentsA
        && deferredTargets.transparentMomentsB
        && materialContextSlots.valid()
        && softwareTraceGeometryResourceCount != 0u
    ;
    const bool splitSoftTransparentFold = preparedSoftTransparentFoldCandidate;
    // The adaptive fallback remains in the monolithic callback, but its raw buffer primitives and acceptance-time
    // diagnostic lifecycle are deterministic from this frozen route.  A frame with no clear/copy work still owns
    // its tick through the semantic task's accepted hook without gaining empty graph nodes.
    const bool graphOwnedAdaptiveCandidate = !splitSoftTransparentFold && rayTracingPlan.adaptivePlan.enabled;
    GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan = graphOwnedAdaptiveCandidate
        ? rayTracingPlan.adaptivePlan
        : GraphOwnedAdaptiveShadowPlan{}
    ;
    // The merge pipeline is ready before its retained history has an accepted frame. Bootstrap still publishes the
    // selected output pair, but only a usable history may be sampled with previous geometry.
    const bool softShadowHistoryReadable = rayTracingPlan.softShadowHistoryReadable;
    // The opaque temporal merge runs before the transparent tail and shares its history selector. Freeze its exact
    // input/output pair while the compiled packet owns the prepared temporal route.
    const bool graphOwnsOpaqueTemporalMergeEntryStates =
        splitSoftTransparentFold
        && rayTracingPlan.opaqueTemporalMergeReady
    ;
    const bool opaqueHistoryFrontIsA = rayTracingPlan.historyFrontIsA;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.shadow_visibility.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(!sceneGeometryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred shadow-visibility graph resources"));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    const bool softwareTraceGeometryStatesGraphOwned = softwareTraceGeometrySet.valid();
    resourceUses.reserve(40u + (
        softwareTraceGeometryStatesGraphOwned
            ? 0u
            : softwareTraceGeometryResourceCount
    ));
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    // Shadow visibility samples the bindless depth image, so its declared layout must match the native shader read.
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // The split opaque producer overwrites visibility; the retained monolith may also fold hybrid transparency in
    // place, so it remains ReadWrite on every compatibility route.
    resourceUses.push_back(splitSoftTransparentFold
        ? WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess)
        : ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess)
    );
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const bool isInput){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        if(graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(isInput
                ? ReadUse(resource, Core::ResourceStates::ShaderResource)
                : WriteUse(resource, Core::ResourceStates::UnorderedAccess)
            );
        else if(!splitSoftTransparentFold){
            if(isInput){
                if(softShadowHistoryReadable)
                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
            else if(rayTracingPlan.opaqueTemporalMergeReady)
                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
        else
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalStaticReadTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        if(softShadowHistoryReadable)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        else if(splitSoftTransparentFold && !graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalTransparentTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const bool isInput){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        if(rayTracingPlan.transparentTemporalMergeReady){
            if(isInput){
                if(softShadowHistoryReadable)
                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
            else
                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
        return true;
    };
    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalReadWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, state));
        return true;
    };
    bool optionalResourcesImported =
        (
            // The retained monolith selects its adaptive fallback only after runtime slot checks. Declare coarse as
            // an output across that whole compatibility route, while prepared split callbacks omit it completely.
            splitSoftTransparentFold
            || appendOptionalWriteTexture(
                deferredTargets.shadowCoarseTransmittance,
                Name("render.shadow_visibility.coarse_transmittance"),
                "Shadow Coarse Transmittance"
            )
        )
        // The retained callback first overwrites these scratch targets, then resolves them locally within the same
        // command list.  Its graph entry must therefore preserve Unknown -> UAV first writes rather than fabricate
        // a ReadWrite native source for a fresh target generation.
        && appendOptionalWriteTexture(
            deferredTargets.shadowSoftHalfA,
            Name("render.shadow_visibility.soft_half_a"),
            "Shadow Soft Half A"
        )
        && appendOptionalWriteTexture(
            deferredTargets.shadowSoftHalfB,
            Name("render.shadow_visibility.soft_half_b"),
            "Shadow Soft Half B"
        )
        && appendOptionalWriteTexture(
            deferredTargets.shadowSoftGeometry,
            Name("render.shadow_visibility.soft_geometry"),
            "Shadow Soft Geometry"
        )
        && appendOptionalOpaqueTemporalStaticReadTexture(
            deferredTargets.shadowSoftGeometryPrev,
            Name("render.shadow_visibility.soft_geometry_previous"),
            "Previous Shadow Soft Geometry"
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowHistA,
            Name("render.shadow_visibility.history_a"),
            "Shadow History A",
            opaqueHistoryFrontIsA
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowHistB,
            Name("render.shadow_visibility.history_b"),
            "Shadow History B",
            !opaqueHistoryFrontIsA
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowMomentsA,
            Name("render.shadow_visibility.moments_a"),
            "Shadow Moments A",
            opaqueHistoryFrontIsA
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowMomentsB,
            Name("render.shadow_visibility.moments_b"),
            "Shadow Moments B",
            !opaqueHistoryFrontIsA
        )
        && appendOptionalWriteTexture(
            deferredTargets.transparentSoftHalf,
            Name("render.shadow_visibility.transparent_soft_half"),
            "Transparent Shadow Soft Half"
        )
        && appendOptionalTransparentTemporalHistoryTexture(
            deferredTargets.transparentHistA,
            Name("render.shadow_visibility.transparent_history_a"),
            "Transparent Shadow History A",
            opaqueHistoryFrontIsA
        )
        && appendOptionalTransparentTemporalHistoryTexture(
            deferredTargets.transparentHistB,
            Name("render.shadow_visibility.transparent_history_b"),
            "Transparent Shadow History B",
            !opaqueHistoryFrontIsA
        )
        && appendOptionalTransparentTemporalHistoryTexture(
            deferredTargets.transparentMomentsA,
            Name("render.shadow_visibility.transparent_moments_a"),
            "Transparent Shadow Moments A",
            opaqueHistoryFrontIsA
        )
        && appendOptionalTransparentTemporalHistoryTexture(
            deferredTargets.transparentMomentsB,
            Name("render.shadow_visibility.transparent_moments_b"),
            "Transparent Shadow Moments B",
            !opaqueHistoryFrontIsA
        )
        && appendOptionalReadBuffer(
            rayTracingResources.sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            rayTracingResources.sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            rayTracingResources.shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            rayTracingResources.shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            rayTracingResources.shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadWriteBuffer(
            rayTracingShadowResources.swShadowEdgeStatsBuffer,
            Name("render.shadow_visibility.edge_stats"),
            "Shadow Edge Statistics",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            rayTracingShadowResources.swShadowEdgeCounterBuffer,
            Name("render.shadow_visibility.edge_counter"),
            "Shadow Edge Counter",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            rayTracingShadowResources.swShadowEdgeListBuffer,
            Name("render.shadow_visibility.edge_list"),
            "Shadow Edge List",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            rayTracingShadowResources.swShadowIndirectArgsBuffer,
            Name("render.shadow_visibility.indirect_args"),
            "Shadow Indirect Arguments",
            Core::ResourceStates::UnorderedAccess
        )
    ;
    Core::GpuGraphResourceId sceneTlas;
    if(rayTracingShadowResources.sceneTlas){
        sceneTlas = m_deferredLightingTaskGraph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
                .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
        );
        optionalResourcesImported = optionalResourcesImported && sceneTlas.valid();
        if(sceneTlas.valid()){
            resourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred shadow-visibility dynamic resource"));
        return false;
    }
    Core::GpuGraphResourceId adaptiveEdgeStats;
    Core::GpuGraphResourceId adaptiveEdgeStatsReadback;
    Core::GpuGraphResourceId adaptiveEdgeCounter;
    if(graphOwnedAdaptivePlan.captureStatsSnapshot){
        adaptiveEdgeStats = importBuffer(
            rayTracingShadowResources.swShadowEdgeStatsBuffer,
            Name("render.shadow_visibility.edge_stats"),
            "Shadow Edge Statistics"
        );
        adaptiveEdgeStatsReadback = importBuffer(
            rayTracingShadowResources.swShadowEdgeStatsReadback,
            Name("render.shadow_visibility.edge_stats_readback"),
            "Shadow Edge Statistics Readback"
        );
        if(!adaptiveEdgeStats.valid() || !adaptiveEdgeStatsReadback.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graph-owned adaptive shadow statistics resources"));
            return false;
        }
    }
    if(graphOwnedAdaptivePlan.compact){
        adaptiveEdgeCounter = importBuffer(
            rayTracingShadowResources.swShadowEdgeCounterBuffer,
            Name("render.shadow_visibility.edge_counter"),
            "Shadow Edge Counter"
        );
        if(!adaptiveEdgeCounter.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graph-owned adaptive shadow counter resource"));
            return false;
        }
    }
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(!softwareTraceGeometryStatesGraphOwned){
        for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex){
            const Core::GpuGraphResourceId resource = softwareTraceGeometryResources[resourceIndex];
            if(!resource.valid())
                return false;
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    const Core::GpuTaskResourceSetUse softwareTraceGeometrySetUse{
        .resourceSet = softwareTraceGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse traceMaterialSampledTextureSetUse{
        .resourceSet = traceMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse traceResourceSetUses[2u] = {};
    usize traceResourceSetUseCount = 0u;
    if(softwareTraceGeometryStatesGraphOwned)
        traceResourceSetUses[traceResourceSetUseCount++] = softwareTraceGeometrySetUse;
    if(traceMaterialSampledTextureSet.valid())
        traceResourceSetUses[traceResourceSetUseCount++] = traceMaterialSampledTextureSetUse;


// The prepared soft path keeps the opaque first wavelet, resolve tail, transparent trace, and temporal/RGB
    // resolve as adjacent callbacks. Re-importing retains shared graph identities while making each graph-owned
    // handoff explicit.
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueFirstWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueResolveResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTraceResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTemporalMergeResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentFirstWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentFoldResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueResourceUses{ scratchArena };
    bool graphOwnsTransparentTemporalMergeEntryStates = false;
    if(splitSoftTransparentFold){
        const Core::GpuGraphResourceId shadowSoftHalfA = importTexture(
            deferredTargets.shadowSoftHalfA,
            Name("render.shadow_visibility.soft_half_a"),
            "Shadow Soft Half A"
        );
        const Core::GpuGraphResourceId shadowSoftHalfB = importTexture(
            deferredTargets.shadowSoftHalfB,
            Name("render.shadow_visibility.soft_half_b"),
            "Shadow Soft Half B"
        );
        const Core::GpuGraphResourceId shadowSoftGeometry = importTexture(
            deferredTargets.shadowSoftGeometry,
            Name("render.shadow_visibility.soft_geometry"),
            "Shadow Soft Geometry"
        );
        const Core::GpuGraphResourceId shadowSoftGeometryPrevious = importTexture(
            deferredTargets.shadowSoftGeometryPrev,
            Name("render.shadow_visibility.soft_geometry_previous"),
            "Previous Shadow Soft Geometry"
        );
        const Core::GpuGraphResourceId opaqueHistoryA = importTexture(
            deferredTargets.shadowHistA,
            Name("render.shadow_visibility.history_a"),
            "Shadow History A"
        );
        const Core::GpuGraphResourceId opaqueHistoryB = importTexture(
            deferredTargets.shadowHistB,
            Name("render.shadow_visibility.history_b"),
            "Shadow History B"
        );
        const Core::GpuGraphResourceId opaqueMomentsA = importTexture(
            deferredTargets.shadowMomentsA,
            Name("render.shadow_visibility.moments_a"),
            "Shadow Moments A"
        );
        const Core::GpuGraphResourceId opaqueMomentsB = importTexture(
            deferredTargets.shadowMomentsB,
            Name("render.shadow_visibility.moments_b"),
            "Shadow Moments B"
        );
        const Core::GpuGraphResourceId transparentSoftHalf = importTexture(
            deferredTargets.transparentSoftHalf,
            Name("render.shadow_visibility.transparent_soft_half"),
            "Transparent Shadow Soft Half"
        );
        const Core::GpuGraphResourceId transparentHistoryA = importTexture(
            deferredTargets.transparentHistA,
            Name("render.shadow_visibility.transparent_history_a"),
            "Transparent Shadow History A"
        );
        const Core::GpuGraphResourceId transparentHistoryB = importTexture(
            deferredTargets.transparentHistB,
            Name("render.shadow_visibility.transparent_history_b"),
            "Transparent Shadow History B"
        );
        const Core::GpuGraphResourceId transparentMomentsA = importTexture(
            deferredTargets.transparentMomentsA,
            Name("render.shadow_visibility.transparent_moments_a"),
            "Transparent Shadow Moments A"
        );
        const Core::GpuGraphResourceId transparentMomentsB = importTexture(
            deferredTargets.transparentMomentsB,
            Name("render.shadow_visibility.transparent_moments_b"),
            "Transparent Shadow Moments B"
        );
        const Core::GpuGraphResourceId sceneBvhNodes = importBuffer(
            rayTracingResources.sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes"
        );
        const Core::GpuGraphResourceId sceneInstances = importBuffer(
            rayTracingResources.sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances"
        );
        const Core::GpuGraphResourceId shadowInstanceMaterials = importBuffer(
            rayTracingResources.shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials"
        );
        const Core::GpuGraphResourceId shadowTypedMaterials = importBuffer(
            rayTracingResources.shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials"
        );
        const Core::GpuGraphResourceId shadowInstances = importBuffer(
            rayTracingResources.shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances"
        );
        if(
            !shadowSoftHalfA.valid()
            || !shadowSoftHalfB.valid()
            || !shadowSoftGeometry.valid()
            || !shadowSoftGeometryPrevious.valid()
            || !opaqueHistoryA.valid()
            || !opaqueHistoryB.valid()
            || !opaqueMomentsA.valid()
            || !opaqueMomentsB.valid()
            || !transparentSoftHalf.valid()
            || !transparentHistoryA.valid()
            || !transparentHistoryB.valid()
            || !transparentMomentsA.valid()
            || !transparentMomentsB.valid()
            || !sceneBvhNodes.valid()
            || !sceneInstances.valid()
            || !shadowInstanceMaterials.valid()
            || !shadowTypedMaterials.valid()
            || !shadowInstances.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared soft-transparent shadow-fold resources"));
            return false;
        }

        // The opaque callback has no temporal or coarse scratch access. Keep its direct serial writes separate from
        // the monolithic compatibility vector so a fresh retained target never becomes a synthetic first read.
        opaqueResourceUses.reserve(18u + (
            softwareTraceGeometryStatesGraphOwned
                ? 0u
                : softwareTraceGeometryResourceCount
        ));
        opaqueResourceUses.push_back(ReadUse(worldPosition));
        opaqueResourceUses.push_back(ReadUse(normal));
        opaqueResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        opaqueResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        opaqueResourceUses.push_back(WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
        opaqueResourceUses.push_back(ReadUse(sceneGeometryDomain));
        opaqueResourceUses.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
        opaqueResourceUses.push_back(WriteUse(shadowSoftGeometry, Core::ResourceStates::UnorderedAccess));
        opaqueResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        opaqueResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        if(hardwareShadowSupported){
            if(sceneTlas.valid())
                opaqueResourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
        }else{
            opaqueResourceUses.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
            opaqueResourceUses.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
            opaqueResourceUses.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
            opaqueResourceUses.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
            opaqueResourceUses.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
            if(materialContextSlots.valid())
                opaqueResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
            if(!softwareTraceGeometryStatesGraphOwned){
                for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex)
                    opaqueResourceUses.push_back(ReadUse(softwareTraceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
            }
        }

        opaqueFirstWaveletResourceUses.reserve(12u);
        // The compiler lowers the opaque trace from UAV to the exact sampled state required by temporal merge or
        // the first wavelet. The first wavelet then publishes half-B for the resolve tail.
        opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource));
        opaqueFirstWaveletResourceUses.push_back(WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        if(graphOwnsOpaqueTemporalMergeEntryStates){
            // The frozen selector permits exact selected input/output declarations for the opaque temporal merge.
            const Core::GpuGraphResourceId opaqueHistoryIn = opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB;
            const Core::GpuGraphResourceId opaqueMomentsIn = opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB;
            const Core::GpuGraphResourceId opaqueHistoryOut = opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA;
            const Core::GpuGraphResourceId opaqueMomentsOut = opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA;
            opaqueFirstWaveletResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
            if(softShadowHistoryReadable){
                opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
                opaqueFirstWaveletResourceUses.push_back(ReadUse(opaqueHistoryIn, Core::ResourceStates::ShaderResource));
                opaqueFirstWaveletResourceUses.push_back(ReadUse(opaqueMomentsIn, Core::ResourceStates::ShaderResource));
            }
            opaqueFirstWaveletResourceUses.push_back(WriteUse(opaqueHistoryOut, Core::ResourceStates::UnorderedAccess));
            opaqueFirstWaveletResourceUses.push_back(WriteUse(opaqueMomentsOut, Core::ResourceStates::UnorderedAccess));
        }

        opaqueResolveResourceUses.reserve(8u);
        // With the current one-wavelet opaque resolve, the tail only samples the first-wavelet half-B result for
        // upsample. Keep a conservative native ping-pong declaration if that compile-time pass count grows.
        if(NWB_SHADOW_RESOLVE_PASS_COUNT == 1u)
            opaqueResolveResourceUses.push_back(ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource));
        else{
            opaqueResolveResourceUses.push_back(ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            opaqueResolveResourceUses.push_back(ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        }
        opaqueResolveResourceUses.push_back(WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
        opaqueResolveResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));

        transparentTraceResourceUses.reserve(16u + (
            softwareTraceGeometryStatesGraphOwned
                ? 0u
                : softwareTraceGeometryResourceCount
        ));
        transparentTraceResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        transparentTraceResourceUses.push_back(ReadUse(sceneGeometryDomain));
        transparentTraceResourceUses.push_back(WriteUse(transparentSoftHalf, Core::ResourceStates::UnorderedAccess));
        transparentTraceResourceUses.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        transparentTraceResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
        if(!softwareTraceGeometryStatesGraphOwned){
            for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex)
                transparentTraceResourceUses.push_back(ReadUse(softwareTraceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
        }

        graphOwnsTransparentTemporalMergeEntryStates = rayTracingPlan.transparentTemporalMergeReady;
        const bool transparentHistoryFrontIsA = rayTracingPlan.historyFrontIsA;
        const Core::GpuGraphResourceId transparentHistoryIn = transparentHistoryFrontIsA
            ? transparentHistoryA
            : transparentHistoryB
        ;
        const Core::GpuGraphResourceId transparentMomentsIn = transparentHistoryFrontIsA
            ? transparentMomentsA
            : transparentMomentsB
        ;
        const Core::GpuGraphResourceId transparentHistoryOut = transparentHistoryFrontIsA
            ? transparentHistoryB
            : transparentHistoryA
        ;
        const Core::GpuGraphResourceId transparentMomentsOut = transparentHistoryFrontIsA
            ? transparentMomentsB
            : transparentMomentsA
        ;
        if(graphOwnsTransparentTemporalMergeEntryStates){
            // Selection is frozen with the compiled frame. The merge samples the current front pair and publishes
            // the opposite pair, which the following wavelet receives as graph-owned sampled inputs.
            transparentTemporalMergeResourceUses.reserve(8u);
            transparentTemporalMergeResourceUses.push_back(ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
            if(softShadowHistoryReadable){
                transparentTemporalMergeResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
                transparentTemporalMergeResourceUses.push_back(ReadUse(transparentHistoryIn, Core::ResourceStates::ShaderResource));
                transparentTemporalMergeResourceUses.push_back(ReadUse(transparentMomentsIn, Core::ResourceStates::ShaderResource));
            }
            transparentTemporalMergeResourceUses.push_back(WriteUse(transparentHistoryOut, Core::ResourceStates::UnorderedAccess));
            transparentTemporalMergeResourceUses.push_back(WriteUse(transparentMomentsOut, Core::ResourceStates::UnorderedAccess));

            transparentFirstWaveletResourceUses.reserve(4u);
            transparentFirstWaveletResourceUses.push_back(ReadUse(transparentHistoryOut, Core::ResourceStates::ShaderResource));
            transparentFirstWaveletResourceUses.push_back(ReadUse(transparentMomentsOut, Core::ResourceStates::ShaderResource));
            transparentFirstWaveletResourceUses.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            transparentFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        }else{
            // Inactive temporal frames keep their existing trace-to-wavelet route and do not acquire stale-history
            // declarations or a no-op merge callback.
            transparentFirstWaveletResourceUses.reserve(3u);
            transparentFirstWaveletResourceUses.push_back(ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource));
            transparentFirstWaveletResourceUses.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            transparentFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        }
        transparentFoldResourceUses.reserve(8u);
        // With one RGB wavelet the terminal task only samples half-A before multiplying visibility. Preserve a
        // native ping-pong declaration if the compile-time pass count grows.
        if(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u)
            transparentFoldResourceUses.push_back(ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource));
        else{
            transparentFoldResourceUses.push_back(ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            transparentFoldResourceUses.push_back(ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        }
        transparentFoldResourceUses.push_back(ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
        transparentFoldResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    }

    if(splitSoftTransparentFold){
        Core::GpuTaskSchedulingHint opaqueScheduling;
        opaqueScheduling.cost = Core::GpuTaskCostHint::Large;
        opaqueScheduling.forceSubmissionBoundary = false;
        opaqueScheduling.allowPacketMerge = true;
        opaqueScheduling.mergeWithPrevious = false;
        EnableSameFamilyComputeEffectRouting(opaqueScheduling, false);
        EnableCrossFamilyComputeEffectRouting(opaqueScheduling);
        Core::GpuTaskDesc opaqueDesc;
        opaqueDesc
            .setIdentity(Name("render.shadow_visibility.opaque"))
            .setMarkerLabel("Shadow Visibility Opaque")
            .setQueue(ComputeTransferPacketQueueRequest())
            .setScheduling(opaqueScheduling)
            .setDependencies(&prefixTask, 1u)
            .setExternalDependencies(
                laggedLightingHistoryWriterDrainDependencies,
                laggedLightingHistoryWriterDrainDependencyCount
            )
            .setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)
            .setResourceUses(opaqueResourceUses.data(), opaqueResourceUses.size())
            .setResourceSetUses(
                !hardwareShadowSupported && softwareTraceGeometryStatesGraphOwned ? &softwareTraceGeometrySetUse : nullptr,
                !hardwareShadowSupported && softwareTraceGeometryStatesGraphOwned ? 1u : 0u
            )
        ;
        m_deferredShadowVisibilityOpaqueTask = m_raytracingSystem.declareShadowVisibilityOpaqueTask(
            m_deferredLightingTaskGraph,
            opaqueDesc,
            deferredTargets,
            deferredLightingResources,
            &m_shadowPreparationOutcome.ready,
            hardwareShadowSupported,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &opaqueProduced,
            &opaqueFrameIndex,
            true,
            graphOwnsOpaqueTemporalMergeEntryStates
        );
        if(!m_deferredShadowVisibilityOpaqueTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque shadow-visibility graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint tailScheduling;
        tailScheduling.cost = Core::GpuTaskCostHint::Medium;
        tailScheduling.forceSubmissionBoundary = false;
        tailScheduling.allowPacketMerge = true;
        tailScheduling.mergeWithPrevious = true;
        EnableSameFamilyComputeEffectRouting(tailScheduling);
        EnableCrossFamilyComputeEffectRouting(tailScheduling);
        const Core::GpuTaskId opaqueFirstWaveletDependencies[] = { m_deferredShadowVisibilityOpaqueTask };
        Core::GpuTaskDesc opaqueFirstWaveletDesc;
        opaqueFirstWaveletDesc
            .setIdentity(Name("render.shadow_visibility.opaque_first_wavelet"))
            .setMarkerLabel("Shadow Opaque First Wavelet")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(opaqueFirstWaveletDependencies, LengthOf(opaqueFirstWaveletDependencies))
            .setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)
            .setResourceUses(opaqueFirstWaveletResourceUses.data(), opaqueFirstWaveletResourceUses.size())
        ;
        m_deferredShadowVisibilityOpaqueFirstWaveletTask = m_raytracingSystem.declareShadowVisibilityOpaqueFirstWaveletTask(
            m_deferredLightingTaskGraph,
            opaqueFirstWaveletDesc,
            deferredTargets,
            deferredLightingResources,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &opaqueResolveTiming,
            &opaqueProduced,
            &opaqueFrameIndex,
            hardwareShadowSupported,
            true,
            graphOwnsOpaqueTemporalMergeEntryStates
        );
        if(!m_deferredShadowVisibilityOpaqueFirstWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow first-wavelet graph task"));
            return false;
        }

        const Core::GpuTaskId opaqueResolveDependencies[] = { m_deferredShadowVisibilityOpaqueFirstWaveletTask };
        Core::GpuTaskDesc opaqueResolveDesc;
        opaqueResolveDesc
            .setIdentity(Name("render.shadow_visibility.opaque_soft_resolve"))
            .setMarkerLabel("Shadow Opaque Soft Resolve")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(opaqueResolveDependencies, LengthOf(opaqueResolveDependencies))
            .setResourceUses(opaqueResolveResourceUses.data(), opaqueResolveResourceUses.size())
        ;
        m_deferredShadowVisibilityOpaqueResolveTask = m_raytracingSystem.declareShadowVisibilityOpaqueResolveTailTask(
            m_deferredLightingTaskGraph,
            opaqueResolveDesc,
            deferredTargets,
            deferredLightingResources,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &opaqueResolveTiming,
            &opaqueProduced,
            &opaqueFrameIndex,
            hardwareShadowSupported,
            true
        );
        if(!m_deferredShadowVisibilityOpaqueResolveTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow resolve-tail graph task"));
            return false;
        }

        const Core::GpuTaskId traceDependencies[] = { m_deferredShadowVisibilityOpaqueResolveTask };
        Core::GpuTaskDesc traceDesc;
        traceDesc
            .setIdentity(Name("render.shadow_visibility.soft_transparent_trace"))
            .setMarkerLabel("Shadow Transparent Soft Trace")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(traceDependencies, LengthOf(traceDependencies))
            .setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)
            .setResourceUses(transparentTraceResourceUses.data(), transparentTraceResourceUses.size())
            .setResourceSetUses(
                traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
                traceResourceSetUseCount
            )
        ;
        m_deferredShadowVisibilityTransparentTraceTask = m_raytracingSystem.declareShadowTransparentSoftTraceTask(
            m_deferredLightingTaskGraph,
            traceDesc,
            deferredTargets,
            deferredLightingResources,
            timingTicket,
            &opaqueProduced,
            &opaqueFrameIndex,
            &transparentTraceProduced,
            true
        );
        if(!m_deferredShadowVisibilityTransparentTraceTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow trace graph task"));
            return false;
        }

        Core::GpuTaskId transparentFirstWaveletDependency = m_deferredShadowVisibilityTransparentTraceTask;
        if(graphOwnsTransparentTemporalMergeEntryStates){
            const Core::GpuTaskId transparentTemporalMergeDependencies[] = {
                m_deferredShadowVisibilityTransparentTraceTask,
            };
            Core::GpuTaskDesc transparentTemporalMergeDesc;
            transparentTemporalMergeDesc
                .setIdentity(Name("render.shadow_visibility.transparent_temporal_merge"))
                .setMarkerLabel("Shadow Transparent Temporal Merge")
                .setQueue(ComputeQueueRequest())
                .setScheduling(tailScheduling)
                .setDependencies(transparentTemporalMergeDependencies, LengthOf(transparentTemporalMergeDependencies))
                .setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)
                .setResourceUses(
                    transparentTemporalMergeResourceUses.data(),
                    transparentTemporalMergeResourceUses.size()
                )
            ;
            m_deferredShadowVisibilityTransparentTemporalMergeTask =
                m_raytracingSystem.declareShadowTransparentSoftTemporalMergeTask(
                    m_deferredLightingTaskGraph,
                    transparentTemporalMergeDesc,
                    deferredTargets,
                    deferredLightingResources,
                    timingTicket,
                    &transparentResolveTiming,
                    &opaqueProduced,
                    &transparentTraceProduced,
                    &opaqueFrameIndex,
                    true,
                    true
                )
            ;
            if(!m_deferredShadowVisibilityTransparentTemporalMergeTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow temporal-merge graph task"));
                return false;
            }
            transparentFirstWaveletDependency = m_deferredShadowVisibilityTransparentTemporalMergeTask;
        }

        const Core::GpuTaskId transparentFirstWaveletDependencies[] = { transparentFirstWaveletDependency };
        Core::GpuTaskDesc transparentFirstWaveletDesc;
        transparentFirstWaveletDesc
            .setIdentity(Name("render.shadow_visibility.transparent_first_wavelet"))
            .setMarkerLabel("Shadow Transparent First Wavelet")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(transparentFirstWaveletDependencies, LengthOf(transparentFirstWaveletDependencies))
            .setResourceUses(transparentFirstWaveletResourceUses.data(), transparentFirstWaveletResourceUses.size())
        ;
        m_deferredShadowVisibilityTransparentFirstWaveletTask = m_raytracingSystem.declareShadowTransparentSoftFirstWaveletTask(
            m_deferredLightingTaskGraph,
            transparentFirstWaveletDesc,
            deferredTargets,
            deferredLightingResources,
            timingTicket,
            &transparentResolveTiming,
            &opaqueProduced,
            &transparentTraceProduced,
            &opaqueFrameIndex,
            true,
            true,
            !graphOwnsTransparentTemporalMergeEntryStates
        );
        if(!m_deferredShadowVisibilityTransparentFirstWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow first-wavelet graph task"));
            return false;
        }

        const Core::GpuTaskId foldDependencies[] = { m_deferredShadowVisibilityTransparentFirstWaveletTask };
        Core::GpuTaskDesc foldDesc;
        foldDesc
            .setIdentity(Name("render.shadow_visibility.soft_transparent_fold"))
            .setMarkerLabel("Shadow Transparent Soft Fold")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(foldDependencies, LengthOf(foldDependencies))
            .setResourceUses(transparentFoldResourceUses.data(), transparentFoldResourceUses.size())
        ;
        // This terminal ID deliberately replaces the opaque producer and transparent trace as the effect/output
        // owner. Existing caustics, lighting, state-handoff, recovery, and acceptance paths therefore observe the
        // fully folded visibility.
        m_deferredShadowVisibilityTask = m_raytracingSystem.declareShadowTransparentSoftFoldTask(
            m_deferredLightingTaskGraph,
            foldDesc,
            deferredTargets,
            deferredLightingResources,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &transparentResolveTiming,
            &opaqueProduced,
            &transparentTraceProduced,
            &opaqueFrameIndex,
            true
        );
        if(!m_deferredShadowVisibilityTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred soft-transparent shadow-fold graph task"));
            return false;
        }
        return true;
    }

    Core::GpuTaskId shadowVisibilityDependency = prefixTask;
    bool adaptivePrimitivePrecedesVisibility = false;
    if(graphOwnedAdaptivePlan.enabled){
        // Counter/stat buffers are private adaptive scratch.  Their exact CopyDest -> UAV handoff is now lowered by
        // the compiler before the retained Shadow Visibility callback, while the callback still decides at record
        // time whether the adaptive producer actually ran.
        Core::GpuTaskSchedulingHint primitiveScheduling;
        primitiveScheduling.cost = Core::GpuTaskCostHint::Tiny;
        primitiveScheduling.forceSubmissionBoundary = false;
        primitiveScheduling.allowPacketMerge = true;
        EnableSameFamilyComputeEffectRouting(primitiveScheduling, false);
        EnableCrossFamilyComputeEffectRouting(primitiveScheduling);

        if(graphOwnedAdaptivePlan.captureStatsSnapshot){
            Core::GpuTaskDesc statsClearDesc;
            statsClearDesc
                .setIdentity(Name("render.shadow_visibility.adaptive_stats_clear"))
                .setMarkerLabel("Shadow Adaptive Statistics Clear")
                .setQueue(ComputeTransferPacketQueueRequest())
                .setScheduling(primitiveScheduling)
                .setDependencies(&shadowVisibilityDependency, 1u)
            ;
            m_deferredShadowVisibilityAdaptiveStatsClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                statsClearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = adaptiveEdgeStats,
                    .clearValue = 0u,
                }
            );
            if(!m_deferredShadowVisibilityAdaptiveStatsClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow statistics clear"));
                return false;
            }
            shadowVisibilityDependency = m_deferredShadowVisibilityAdaptiveStatsClearTask;
            adaptivePrimitivePrecedesVisibility = true;
        }

        if(graphOwnedAdaptivePlan.compact){
            Core::GpuTaskSchedulingHint counterClearScheduling = primitiveScheduling;
            counterClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
            EnableSameFamilyComputeEffectRouting(counterClearScheduling, adaptivePrimitivePrecedesVisibility);
            Core::GpuTaskDesc counterClearDesc;
            counterClearDesc
                .setIdentity(Name("render.shadow_visibility.adaptive_counter_clear"))
                .setMarkerLabel("Shadow Adaptive Counter Clear")
                .setQueue(ComputeTransferPacketQueueRequest())
                .setScheduling(counterClearScheduling)
                .setDependencies(&shadowVisibilityDependency, 1u)
            ;
            m_deferredShadowVisibilityAdaptiveCounterClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                counterClearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = adaptiveEdgeCounter,
                    .clearValue = 0u,
                }
            );
            if(!m_deferredShadowVisibilityAdaptiveCounterClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow counter clear"));
                return false;
            }
            shadowVisibilityDependency = m_deferredShadowVisibilityAdaptiveCounterClearTask;
            adaptivePrimitivePrecedesVisibility = true;
        }
    }


// Preparedness is established while Shadow Prepare records, after this graph is declared.  The normal
    // monolithic path therefore begins with an unconditional all-lit clear: real shadow producers overwrite it,
    // while a missing producer retains the same white fallback without a callback-local native state bridge.
    // Keep this Compute-native clear on the selected Compute packet with Shadow Visibility.
    Core::GpuTaskSchedulingHint allLitClearScheduling;
    allLitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    allLitClearScheduling.forceSubmissionBoundary = false;
    allLitClearScheduling.allowPacketMerge = true;
    allLitClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
    EnableSameFamilyComputeEffectRouting(allLitClearScheduling, adaptivePrimitivePrecedesVisibility);
    EnableCrossFamilyComputeEffectRouting(allLitClearScheduling);
    const Core::GpuTaskResourceUse allLitClearResourceUse = WriteTextureUse(
        shadowVisibility,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::ResourceStates::CopyDest
    );
    Core::GpuTaskDesc allLitClearDesc;
    allLitClearDesc
        .setIdentity(Name("render.shadow_visibility.all_lit_clear"))
        .setMarkerLabel("Shadow Visibility All-Lit Clear")
        .setQueue(ComputePacketQueueRequest())
        .setScheduling(allLitClearScheduling)
        .setDependencies(&shadowVisibilityDependency, 1u)
        .setExternalDependencies(
            laggedLightingHistoryWriterDrainDependencies,
            laggedLightingHistoryWriterDrainDependencyCount
        )
        .setResourceUses(&allLitClearResourceUse, 1u)
    ;
    m_deferredShadowVisibilityAllLitClearTask = m_deferredLightingTaskGraph.addTask<
        ECSRenderDetail::ShadowVisibilityAllLitClearGraphTask
    >(
        allLitClearDesc,
        ECSRenderDetail::ShadowVisibilityAllLitClearGraphTask::Payload{
            .destination = shadowVisibility,
        }
    );
    if(!m_deferredShadowVisibilityAllLitClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned all-lit shadow-visibility clear"));
        return false;
    }
    shadowVisibilityDependency = m_deferredShadowVisibilityAllLitClearTask;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    EnableSameFamilyComputeEffectRouting(scheduling);
    EnableCrossFamilyComputeEffectRouting(scheduling);
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(ComputeTransferPacketQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&shadowVisibilityDependency, 1u)
        .setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(
            traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
            traceResourceSetUseCount
        )
    ;
    m_deferredShadowVisibilityTask = m_raytracingSystem.declareShadowVisibilityTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        deferredLightingResources,
        &m_shadowPreparationOutcome.ready,
        hardwareShadowSupported,
        timingTicket,
        true,
        true,
        graphOwnedAdaptivePlan
    );
    if(!m_deferredShadowVisibilityTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred shadow-visibility graph task"));
        return false;
    }
    if(graphOwnedAdaptivePlan.captureStatsSnapshot){
        const Core::GpuCopyBufferTaskRegion statsReadbackRegion{
            .source = adaptiveEdgeStats,
            .destination = adaptiveEdgeStatsReadback,
            .dataSizeBytes = static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT),
        };
        Core::GpuTaskSchedulingHint statsReadbackScheduling;
        statsReadbackScheduling.cost = Core::GpuTaskCostHint::Tiny;
        statsReadbackScheduling.forceSubmissionBoundary = false;
        statsReadbackScheduling.allowPacketMerge = true;
        statsReadbackScheduling.mergeWithPrevious = true;
        EnableSameFamilyComputeEffectRouting(statsReadbackScheduling);
        EnableCrossFamilyComputeEffectRouting(statsReadbackScheduling);
        // Lighting/caustics consume Shadow Visibility on another physical queue.  This immediate dependent is the
        // semantic terminal of the same packet, so it may intentionally close that consumer frontier.
        statsReadbackScheduling.allowMergeAcrossConsumerFrontier = true;
        const Core::GpuTaskId statsReadbackDependencies[] = { m_deferredShadowVisibilityTask };
        Core::GpuTaskDesc statsReadbackDesc;
        statsReadbackDesc
            .setIdentity(Name("render.shadow_visibility.adaptive_stats_readback"))
            .setMarkerLabel("Shadow Adaptive Statistics Readback")
            .setQueue(ComputeTransferPacketQueueRequest())
            .setScheduling(statsReadbackScheduling)
            .setDependencies(statsReadbackDependencies, LengthOf(statsReadbackDependencies))
            .setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)
        ;
        m_deferredShadowVisibilityAdaptiveStatsReadbackTask = m_deferredLightingTaskGraph.addCopyBufferTask(
            statsReadbackDesc,
            Core::GpuCopyBufferTaskDesc{
                .regions = &statsReadbackRegion,
                .regionCount = 1u,
            }
        );
        if(!m_deferredShadowVisibilityAdaptiveStatsReadbackTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow statistics readback"));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

