// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_visibility_resource_planner.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityResourcePlanner::prepare(const ShadowVisibilityResourceInputs& inputs, ShadowVisibilityResourcePlan& result){
    using namespace RendererTaskGraphDetail;
    const auto& deferredTargets = inputs.deferredTargets;
    const auto& rayTracingShadowResources = inputs.rayTracingShadowResources;
    const auto& rayTracingResources = inputs.rayTracingResources;
    const auto& rayTracingPlan = inputs.rayTracingPlan;
    const auto& hardwareSceneReads = inputs.hardwareSceneReads;
    const auto worldPosition = inputs.worldPosition;
    const auto normal = inputs.normal;
    const auto depth = inputs.depth;
    const auto shadowVisibility = inputs.shadowVisibility;
    const auto currentBindlessSlots = inputs.currentBindlessSlots;
    const auto sceneShading = inputs.sceneShading;
    const auto lights = inputs.lights;
    const auto materialContextSlots = inputs.materialContextSlots;
    const auto& traceGeometryResources = inputs.traceGeometryResources;
    const auto traceGeometryStatesGraphOwned = inputs.traceGeometryStatesGraphOwned;
    const auto splitSoftTransparentFold = inputs.splitSoftTransparentFold;
    const auto graphOwnsOpaqueTemporalMergeEntryStates = inputs.graphOwnsOpaqueTemporalMergeEntryStates;
    const auto softShadowHistoryReadable = inputs.softShadowHistoryReadable;
    const auto opaqueHistoryFrontIsA = inputs.opaqueHistoryFrontIsA;
    const auto hardwareTransparentTrace = inputs.hardwareTransparentTrace;
    const auto adaptiveCompact = inputs.adaptiveCompact;
    auto& sceneGeometryDomain = result.sceneGeometryDomain;
    auto& sceneTlas = result.sceneTlas;
    auto& hardwareTransparentScratch = result.hardwareTransparentScratch;
    auto& adaptiveEdgeCounter = result.adaptiveEdgeCounter;
    auto& resourceUses = result.resourceUses;

    const auto sceneGeometryDomainResult = m_graph.importHazardDomain(HazardDomainDesc(Name("render.shadow_visibility.scene_geometry"), s_SceneGeometryDomainLabel));
    if(!sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred shadow-visibility graph resources"));
        return false;
    }
    sceneGeometryDomain = *sceneGeometryDomainResult;

    resourceUses.reserve(s_ShadowVisibilityResourceUseCapacity + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResources.size()));
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    // Shadow visibility samples the bindless depth image, so its declared layout must match the native shader read.
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // The split opaque producer overwrites visibility; the software monolith also folds transparency in place.
    resourceUses.push_back(splitSoftTransparentFold ? WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess) : ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const b8 isInput){
        if(!texture)
            return true;
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(isInput ? ReadUse(resource, Core::ResourceStates::ShaderResource) : WriteUse(resource, Core::ResourceStates::UnorderedAccess));
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
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(softShadowHistoryReadable)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        else if(splitSoftTransparentFold && !graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalTransparentTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const b8 isInput){
        if(!texture)
            return true;
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
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
    const auto appendOptionalReadWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const auto resourceResult = ImportExternalBuffer(m_graph, buffer, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(ReadWriteUse(resource, state));
        return true;
    };
    b8 optionalResourcesImported =
        (
            // The monolith picks adaptive fallback after runtime slot checks. Declare coarse across that route; split callbacks omit it.
            splitSoftTransparentFold
            || appendOptionalWriteTexture(deferredTargets.shadowCoarseTransmittance, Name("render.shadow_visibility.coarse_transmittance"), "Shadow Coarse Transmittance")
        )
        // The retained callback first overwrites these scratch targets, then resolves them locally within the same command list.
        // Its graph entry must therefore preserve Unknown -> UAV first writes rather than fabricate a ReadWrite native source for a fresh target generation.
        && appendOptionalWriteTexture(deferredTargets.shadowSoftHalfA, Name(s_ShadowSoftHalfAName), s_ShadowSoftHalfALabel)
        && appendOptionalWriteTexture(deferredTargets.shadowSoftHalfB, Name(s_ShadowSoftHalfBName), s_ShadowSoftHalfBLabel)
        && appendOptionalWriteTexture(deferredTargets.shadowSoftGeometry, Name(s_ShadowSoftGeometryName), s_ShadowSoftGeometryLabel)
        && appendOptionalOpaqueTemporalStaticReadTexture(deferredTargets.shadowSoftGeometryPrev, Name(s_ShadowSoftGeometryPreviousName), s_ShadowSoftGeometryPreviousLabel)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowHistA, Name(s_ShadowHistoryAName), s_ShadowHistoryALabel, opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowHistB, Name(s_ShadowHistoryBName), s_ShadowHistoryBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowMomentsA, Name(s_ShadowMomentsAName), s_ShadowMomentsALabel, opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowMomentsB, Name(s_ShadowMomentsBName), s_ShadowMomentsBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalWriteTexture(deferredTargets.transparentSoftHalf, Name(s_TransparentSoftHalfName), s_TransparentSoftHalfLabel)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentHistA, Name(s_TransparentHistoryAName), s_TransparentHistoryALabel, opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentHistB, Name(s_TransparentHistoryBName), s_TransparentHistoryBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentMomentsA, Name(s_TransparentMomentsAName), s_TransparentMomentsALabel, opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentMomentsB, Name(s_TransparentMomentsBName), s_TransparentMomentsBLabel, !opaqueHistoryFrontIsA)
        && (hardwareTransparentTrace || RendererFramePipelineDetail::AppendRayTracingSceneShadowBuffers(
            rayTracingResources,
            m_graph,
            resourceUses
        ))
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowEdgeCounterBuffer, Name(s_ShadowEdgeCounterName), s_ShadowEdgeCounterLabel, Core::ResourceStates::UnorderedAccess)
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowEdgeListBuffer, Name("render.shadow_visibility.edge_list"), "Shadow Edge List", Core::ResourceStates::UnorderedAccess)
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowIndirectArgsBuffer, Name("render.shadow_visibility.indirect_args"), "Shadow Indirect Arguments", Core::ResourceStates::UnorderedAccess)
    ;
    if(rayTracingShadowResources.sceneTlas){
        const auto sceneTlasResult = m_graph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel)
                .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
        );
        if(sceneTlasResult)
            sceneTlas = *sceneTlasResult;
        optionalResourcesImported = optionalResourcesImported && sceneTlasResult.has_value();
        if(sceneTlas.valid() && !hardwareTransparentTrace)
            resourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
    }
    if(hardwareTransparentTrace){
        const Array<ReferenceWrapper<const Core::BufferHandle>, 3u> buffers = { rayTracingResources.hardwareTransparentCrossingsBuffer, rayTracingResources.hardwareTransparentOverflowListBuffer, rayTracingResources.hardwareTransparentOverflowArgsBuffer, };
        for(usize index = 0u; index < buffers.size(); ++index){
            const Core::BufferHandle& buffer = buffers[index].get();
            if(!buffer)
                return false;
            const Core::BufferDesc& bufferDesc = buffer->getCreationDescription();
            if(!bufferDesc.debugName)
                return false;
            const auto hardwareTransparentScratchResult = m_graph.importBuffer(buffer, BufferResourceDesc(bufferDesc.debugName, "Hardware Transparent Shadow Scratch").setInitialState(bufferDesc.initialState));
            if(!hardwareTransparentScratchResult){
                hardwareTransparentScratch[index] = {};
                return false;
            }
            hardwareTransparentScratch[index] = *hardwareTransparentScratchResult;
            resourceUses.push_back(ReadWriteUse(hardwareTransparentScratch[index], Core::ResourceStates::UnorderedAccess));
        }
        for(const Core::GpuTaskResourceUse& use : hardwareSceneReads.uses)
            resourceUses.push_back(use);
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred shadow-visibility dynamic resource"));
        return false;
    }
    if(adaptiveCompact){
        const auto adaptiveEdgeCounterResult = ImportExternalBuffer(m_graph, rayTracingShadowResources.swShadowEdgeCounterBuffer, Name(s_ShadowEdgeCounterName), s_ShadowEdgeCounterLabel);
        if(!adaptiveEdgeCounterResult){
            adaptiveEdgeCounter = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graph-owned adaptive shadow counter resource"));
            return false;
        }
        adaptiveEdgeCounter = *adaptiveEdgeCounterResult;
    }
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid() && !hardwareTransparentTrace)
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(!traceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : traceGeometryResources){
            if(!resource.valid())
                return false;
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

