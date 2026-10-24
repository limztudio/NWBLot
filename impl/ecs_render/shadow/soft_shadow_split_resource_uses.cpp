// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "soft_shadow_split_resource_uses.h"

#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SoftShadowSplitResourceUseBuilder::build(const SoftShadowSplitResourceInputs& inputs, SoftShadowSplitResourceUses& uses){
    using namespace RendererTaskGraphDetail;

    const SoftShadowSplitRoute& route = inputs.route;
    const bool hardwareTransparentTrace = inputs.rayTracingPlan.hardwareTransparentTrace;
    const bool softShadowHistoryReadable = inputs.rayTracingPlan.softShadowHistoryReadable;
    const bool opaqueHistoryFrontIsA = inputs.rayTracingPlan.historyFrontIsA;
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_graph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_graph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };

    const auto shadowSoftHalfAResult = importTexture(inputs.targets.shadowSoftHalfA, Name(RendererFramePipelineDetail::s_ShadowSoftHalfAName), RendererFramePipelineDetail::s_ShadowSoftHalfALabel);
    const auto shadowSoftHalfBResult = importTexture(inputs.targets.shadowSoftHalfB, Name(RendererFramePipelineDetail::s_ShadowSoftHalfBName), RendererFramePipelineDetail::s_ShadowSoftHalfBLabel);
    const auto shadowSoftGeometryResult = importTexture(inputs.targets.shadowSoftGeometry, Name(RendererFramePipelineDetail::s_ShadowSoftGeometryName), RendererFramePipelineDetail::s_ShadowSoftGeometryLabel);
    const auto shadowSoftGeometryPreviousResult = importTexture(inputs.targets.shadowSoftGeometryPrev, Name(RendererFramePipelineDetail::s_ShadowSoftGeometryPreviousName), RendererFramePipelineDetail::s_ShadowSoftGeometryPreviousLabel);
    const auto opaqueHistoryAResult = importTexture(inputs.targets.shadowHistA, Name(RendererFramePipelineDetail::s_ShadowHistoryAName), RendererFramePipelineDetail::s_ShadowHistoryALabel);
    const auto opaqueHistoryBResult = importTexture(inputs.targets.shadowHistB, Name(RendererFramePipelineDetail::s_ShadowHistoryBName), RendererFramePipelineDetail::s_ShadowHistoryBLabel);
    const auto opaqueMomentsAResult = importTexture(inputs.targets.shadowMomentsA, Name(RendererFramePipelineDetail::s_ShadowMomentsAName), RendererFramePipelineDetail::s_ShadowMomentsALabel);
    const auto opaqueMomentsBResult = importTexture(inputs.targets.shadowMomentsB, Name(RendererFramePipelineDetail::s_ShadowMomentsBName), RendererFramePipelineDetail::s_ShadowMomentsBLabel);
    const auto transparentSoftHalfResult = importTexture(inputs.targets.transparentSoftHalf, Name(RendererFramePipelineDetail::s_TransparentSoftHalfName), RendererFramePipelineDetail::s_TransparentSoftHalfLabel);
    const auto transparentHistoryAResult = importTexture(inputs.targets.transparentHistA, Name(RendererFramePipelineDetail::s_TransparentHistoryAName), RendererFramePipelineDetail::s_TransparentHistoryALabel);
    const auto transparentHistoryBResult = importTexture(inputs.targets.transparentHistB, Name(RendererFramePipelineDetail::s_TransparentHistoryBName), RendererFramePipelineDetail::s_TransparentHistoryBLabel);
    const auto transparentMomentsAResult = importTexture(inputs.targets.transparentMomentsA, Name(RendererFramePipelineDetail::s_TransparentMomentsAName), RendererFramePipelineDetail::s_TransparentMomentsALabel);
    const auto transparentMomentsBResult = importTexture(inputs.targets.transparentMomentsB, Name(RendererFramePipelineDetail::s_TransparentMomentsBName), RendererFramePipelineDetail::s_TransparentMomentsBLabel);
    Expected<Optional<Core::GpuGraphResourceId>> sceneBvhNodesResult = Optional<Core::GpuGraphResourceId>{};
    if(!(hardwareTransparentTrace)){
        const auto importedResource = importBuffer(inputs.rayTracingResources.sceneBvhNodeBuffer, Name(RendererFramePipelineDetail::s_SceneBvhNodesName), RendererFramePipelineDetail::s_SceneBvhNodesLabel);
        if(!importedResource)
            sceneBvhNodesResult = MakeUnexpected(importedResource.error());
        else
            sceneBvhNodesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    Expected<Optional<Core::GpuGraphResourceId>> sceneInstancesResult = Optional<Core::GpuGraphResourceId>{};
    if(!(hardwareTransparentTrace)){
        const auto importedResource = importBuffer(inputs.rayTracingResources.sceneInstanceBuffer, Name(RendererFramePipelineDetail::s_SceneInstancesName), RendererFramePipelineDetail::s_SceneInstancesLabel);
        if(!importedResource)
            sceneInstancesResult = MakeUnexpected(importedResource.error());
        else
            sceneInstancesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    const auto shadowInstanceMaterialsResult = importBuffer(inputs.rayTracingResources.shadowInstanceMaterialBuffer, Name(RendererFramePipelineDetail::s_SceneShadowInstanceMaterialName), RendererFramePipelineDetail::s_SceneShadowInstanceMaterialLabel);
    const auto shadowTypedMaterialsResult = importBuffer(inputs.rayTracingResources.shadowMaterialTypedBuffer, Name(RendererFramePipelineDetail::s_SceneShadowMaterialTypedName), RendererFramePipelineDetail::s_SceneShadowMaterialTypedLabel);
    const auto shadowInstancesResult = importBuffer(inputs.rayTracingResources.shadowInstanceBuffer, Name(RendererFramePipelineDetail::s_SceneShadowInstancesName), RendererFramePipelineDetail::s_SceneShadowInstancesLabel);
    if(
        !shadowSoftHalfAResult
        || !shadowSoftHalfBResult
        || !shadowSoftGeometryResult
        || !shadowSoftGeometryPreviousResult
        || !opaqueHistoryAResult
        || !opaqueHistoryBResult
        || !opaqueMomentsAResult
        || !opaqueMomentsBResult
        || !transparentSoftHalfResult
        || !transparentHistoryAResult
        || !transparentHistoryBResult
        || !transparentMomentsAResult
        || !transparentMomentsBResult
        || (hardwareTransparentTrace ? !inputs.sceneTlas.valid() : (!sceneBvhNodesResult || !sceneInstancesResult))
        || !shadowInstanceMaterialsResult
        || !shadowTypedMaterialsResult
        || !shadowInstancesResult
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared soft-transparent shadow-fold resources"));
        return false;
    }
    const Core::GpuGraphResourceId sceneInstances = sceneInstancesResult->value_or(Core::GpuGraphResourceId{});
    const Core::GpuGraphResourceId sceneBvhNodes = sceneBvhNodesResult->value_or(Core::GpuGraphResourceId{});
    const Core::GpuGraphResourceId shadowSoftHalfA = *shadowSoftHalfAResult;
    const Core::GpuGraphResourceId shadowSoftHalfB = *shadowSoftHalfBResult;
    const Core::GpuGraphResourceId shadowSoftGeometry = *shadowSoftGeometryResult;
    const Core::GpuGraphResourceId shadowSoftGeometryPrevious = *shadowSoftGeometryPreviousResult;
    const Core::GpuGraphResourceId opaqueHistoryA = *opaqueHistoryAResult;
    const Core::GpuGraphResourceId opaqueHistoryB = *opaqueHistoryBResult;
    const Core::GpuGraphResourceId opaqueMomentsA = *opaqueMomentsAResult;
    const Core::GpuGraphResourceId opaqueMomentsB = *opaqueMomentsBResult;
    const Core::GpuGraphResourceId transparentSoftHalf = *transparentSoftHalfResult;
    const Core::GpuGraphResourceId transparentHistoryA = *transparentHistoryAResult;
    const Core::GpuGraphResourceId transparentHistoryB = *transparentHistoryBResult;
    const Core::GpuGraphResourceId transparentMomentsA = *transparentMomentsAResult;
    const Core::GpuGraphResourceId transparentMomentsB = *transparentMomentsBResult;
    const Core::GpuGraphResourceId shadowInstanceMaterials = *shadowInstanceMaterialsResult;
    const Core::GpuGraphResourceId shadowTypedMaterials = *shadowTypedMaterialsResult;
    const Core::GpuGraphResourceId shadowInstances = *shadowInstancesResult;

    // The opaque callback has no temporal or coarse scratch access. Keep its serial writes separate from the unsplit adaptive declaration so a fresh target never becomes a synthetic first read.
    constexpr usize s_OpaqueResourceUseCapacity = 18u;
    uses.opaque.reserve(s_OpaqueResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResources.size()));
    uses.opaque.push_back(ReadUse(inputs.worldPosition));
    uses.opaque.push_back(ReadUse(inputs.normal));
    uses.opaque.push_back(ReadUse(inputs.depth, Core::ResourceStates::ShaderResource));
    uses.opaque.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    uses.opaque.push_back(WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess));
    uses.opaque.push_back(ReadUse(inputs.sceneGeometryDomain));
    // The map/fallback pair reads only values it has written inside this task; fresh graph entry stays write-only.
    uses.opaque.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
    uses.opaque.push_back(WriteUse(shadowSoftGeometry, Core::ResourceStates::UnorderedAccess));
    uses.opaque.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    uses.opaque.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
    if(route.hardwareShadowSupported){
        if(inputs.sceneTlas.valid())
            uses.opaque.push_back(ReadUse(inputs.sceneTlas, Core::ResourceStates::AccelStructRead));
        if(route.hardwareCsgShadow){
            uses.opaque.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
            uses.opaque.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
            uses.opaque.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
            uses.opaque.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
            if(!route.traceGeometryStatesGraphOwned){
                for(const Core::GpuGraphResourceId resource : inputs.traceGeometryResources)
                    uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
        }
    }else{
        uses.opaque.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        if(inputs.materialContextSlots.valid())
            uses.opaque.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
        if(!route.traceGeometryStatesGraphOwned){
            for(const Core::GpuGraphResourceId resource : inputs.traceGeometryResources)
                uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }

    uses.opaqueFirstWaveletCount = 0u;
    // The compiler lowers the opaque trace from UAV to the exact sampled state required by temporal merge or the first wavelet. The first wavelet then publishes half-B for the resolve tail.
    uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource);
    if(!route.combinedSoftWavelet)
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
    uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    if(route.graphOwnsOpaqueTemporalMergeEntryStates){
        // The frozen selector permits exact selected input/output declarations for the opaque temporal merge.
        const Core::GpuGraphResourceId opaqueHistoryIn = opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB;
        const Core::GpuGraphResourceId opaqueMomentsIn = opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB;
        const Core::GpuGraphResourceId opaqueHistoryOut = opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA;
        const Core::GpuGraphResourceId opaqueMomentsOut = opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA;
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
        if(softShadowHistoryReadable){
            uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource);
            uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(opaqueHistoryIn, Core::ResourceStates::ShaderResource);
            uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(opaqueMomentsIn, Core::ResourceStates::ShaderResource);
        }
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = WriteUse(opaqueHistoryOut, Core::ResourceStates::UnorderedAccess);
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = WriteUse(opaqueMomentsOut, Core::ResourceStates::UnorderedAccess);
    }

    uses.opaqueResolveCount = 0u;
    if(!route.combinedSoftUpsample){
        // With the current one-wavelet opaque resolve, the tail only samples the first-wavelet half-B result for upsample. Keep a conservative ping-pong declaration if that pass count grows.
        if(NWB_SHADOW_RESOLVE_PASS_COUNT == 1u)
            uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource);
        else{
            uses.opaqueResolve[uses.opaqueResolveCount++] = ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
            uses.opaqueResolve[uses.opaqueResolveCount++] = ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
        }
        uses.opaqueResolve[uses.opaqueResolveCount++] = WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.normal, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.depth, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer);
    }

    constexpr usize s_TransparentTraceResourceUseCapacity = 16u;
    uses.transparentTrace.reserve(s_TransparentTraceResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResources.size()));
    uses.transparentTrace.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadUse(inputs.normal, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadUse(inputs.depth, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    uses.transparentTrace.push_back(ReadUse(inputs.sceneGeometryDomain));
    uses.transparentTrace.push_back(WriteUse(transparentSoftHalf, Core::ResourceStates::UnorderedAccess));
    if(hardwareTransparentTrace){
        for(const Core::GpuTaskResourceUse& use : inputs.hardwareSceneReads.uses)
            uses.transparentTrace.push_back(use);
        for(const Core::GpuGraphResourceId scratch : inputs.hardwareTransparentScratch)
            uses.transparentTrace.push_back(ReadWriteUse(scratch, Core::ResourceStates::UnorderedAccess));
    }
    else{
        uses.transparentTrace.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    }
    uses.transparentTrace.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    uses.transparentTrace.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
    if(!route.traceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : inputs.traceGeometryResources)
            uses.transparentTrace.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

    uses.graphOwnsTransparentTemporalMergeEntryStates = inputs.rayTracingPlan.transparentTemporalMergeReady;
    const bool transparentHistoryFrontIsA = inputs.rayTracingPlan.historyFrontIsA;
    const Core::GpuGraphResourceId transparentHistoryIn = transparentHistoryFrontIsA ? transparentHistoryA : transparentHistoryB;
    const Core::GpuGraphResourceId transparentMomentsIn = transparentHistoryFrontIsA ? transparentMomentsA : transparentMomentsB;
    const Core::GpuGraphResourceId transparentHistoryOut = transparentHistoryFrontIsA ? transparentHistoryB : transparentHistoryA;
    const Core::GpuGraphResourceId transparentMomentsOut = transparentHistoryFrontIsA ? transparentMomentsB : transparentMomentsA;
    if(!inputs.rayTracingPlan.hardwareTransparentTrace && !inputs.rayTracingPlan.lightSpace.ready && uses.graphOwnsTransparentTemporalMergeEntryStates && softShadowHistoryReadable){
        uses.transparentTrace.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(transparentMomentsIn, Core::ResourceStates::ShaderResource));
    }
    uses.transparentTemporalMergeCount = 0u;
    uses.transparentFirstWaveletCount = 0u;
    if(uses.graphOwnsTransparentTemporalMergeEntryStates){
        // Selection is frozen with the compiled frame. The merge samples the current front pair and publishes the opposite pair, which the following wavelet receives as graph-owned sampled inputs.
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource);
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
        if(softShadowHistoryReadable){
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource);
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(transparentHistoryIn, Core::ResourceStates::ShaderResource);
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(transparentMomentsIn, Core::ResourceStates::ShaderResource);
        }
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(transparentHistoryOut, Core::ResourceStates::UnorderedAccess);
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(transparentMomentsOut, Core::ResourceStates::UnorderedAccess);
        if(route.combinedSoftTemporal){
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource);
            if(softShadowHistoryReadable){
                uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB, Core::ResourceStates::ShaderResource);
                uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB, Core::ResourceStates::ShaderResource);
            }
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA, Core::ResourceStates::UnorderedAccess);
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA, Core::ResourceStates::UnorderedAccess);
        }

        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(transparentHistoryOut, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(transparentMomentsOut, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    }else{
        // Inactive temporal frames keep their existing trace-to-wavelet route and do not acquire stale-history declarations or a no-op merge callback.
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    }
    if(route.combinedSoftWavelet){
        // Both temporal merges publish their selected output pair before the fused wavelet reads either pair.
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
    }
    uses.transparentFoldCount = 0u;
    if(route.combinedSoftUpsample)
        uses.transparentFold[uses.transparentFoldCount++] = ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource);
    // With one RGB wavelet the terminal task only samples half-A before multiplying visibility. Preserve a native ping-pong declaration if the compile-time pass count grows.
    if(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u)
        uses.transparentFold[uses.transparentFoldCount++] = ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource);
    else{
        uses.transparentFold[uses.transparentFoldCount++] = ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
        uses.transparentFold[uses.transparentFoldCount++] = ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
    }
    uses.transparentFold[uses.transparentFoldCount++] = route.combinedSoftUpsample ? WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess) : ReadWriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.normal, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.depth, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

