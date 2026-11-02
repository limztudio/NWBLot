// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "soft_shadow_split_resource_uses.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SoftShadowSplitResourceUseBuilder::build(const SoftShadowSplitResourceInputs& inputs, SoftShadowSplitResourceUses& uses){
    using namespace RendererTaskGraphDetail;

    const SoftShadowSplitRoute& route = inputs.route;
    const b8 hardwareTransparentTrace = inputs.rayTracingPlan.hardwareTransparentTrace;
    const b8 softShadowHistoryReadable = inputs.rayTracingPlan.softShadowHistoryReadable;
    const b8 opaqueHistoryFrontIsA = inputs.rayTracingPlan.historyFrontIsA;

    const auto shadowSoftHalfAResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftHalfA, Name(s_ShadowSoftHalfAName), s_ShadowSoftHalfALabel);
    const auto shadowSoftHalfBResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftHalfB, Name(s_ShadowSoftHalfBName), s_ShadowSoftHalfBLabel);
    const auto shadowSoftGeometryResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftGeometry, Name(s_ShadowSoftGeometryName), s_ShadowSoftGeometryLabel);
    const auto shadowSoftGeometryPreviousResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftGeometryPrev, Name(s_ShadowSoftGeometryPreviousName), s_ShadowSoftGeometryPreviousLabel);
    const auto opaqueHistoryAResult = ImportExternalTexture(m_graph, inputs.targets.shadowHistA, Name(s_ShadowHistoryAName), s_ShadowHistoryALabel);
    const auto opaqueHistoryBResult = ImportExternalTexture(m_graph, inputs.targets.shadowHistB, Name(s_ShadowHistoryBName), s_ShadowHistoryBLabel);
    const auto opaqueMomentsAResult = ImportExternalTexture(m_graph, inputs.targets.shadowMomentsA, Name(s_ShadowMomentsAName), s_ShadowMomentsALabel);
    const auto opaqueMomentsBResult = ImportExternalTexture(m_graph, inputs.targets.shadowMomentsB, Name(s_ShadowMomentsBName), s_ShadowMomentsBLabel);
    const auto transparentSoftHalfResult = ImportExternalTexture(m_graph, inputs.targets.transparentSoftHalf, Name(s_TransparentSoftHalfName), s_TransparentSoftHalfLabel);
    const auto transparentHistoryAResult = ImportExternalTexture(m_graph, inputs.targets.transparentHistA, Name(s_TransparentHistoryAName), s_TransparentHistoryALabel);
    const auto transparentHistoryBResult = ImportExternalTexture(m_graph, inputs.targets.transparentHistB, Name(s_TransparentHistoryBName), s_TransparentHistoryBLabel);
    const auto transparentMomentsAResult = ImportExternalTexture(m_graph, inputs.targets.transparentMomentsA, Name(s_TransparentMomentsAName), s_TransparentMomentsALabel);
    const auto transparentMomentsBResult = ImportExternalTexture(m_graph, inputs.targets.transparentMomentsB, Name(s_TransparentMomentsBName), s_TransparentMomentsBLabel);
    const auto importSoftwareBuffer = [&](const Core::BufferHandle& buffer, const Name identity, const AStringView label) -> Expected<Core::GpuGraphResourceId>{
        if(hardwareTransparentTrace)
            return Core::GpuGraphResourceId{};
        return ImportExternalBuffer(m_graph, buffer, identity, label);
    };
    const auto sceneBvhNodesResult = importSoftwareBuffer(inputs.rayTracingResources.sceneBvhNodeBuffer, Name(s_SceneBvhNodesName), s_SceneBvhNodesLabel);
    const auto sceneInstancesResult = importSoftwareBuffer(inputs.rayTracingResources.sceneInstanceBuffer, Name(s_SceneInstancesName), s_SceneInstancesLabel);
    const auto shadowInstanceMaterialsResult = ImportExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceMaterialBuffer, Name(s_SceneShadowInstanceMaterialName), s_SceneShadowInstanceMaterialLabel);
    const auto shadowTypedMaterialsResult = ImportExternalBuffer(m_graph, inputs.rayTracingResources.shadowMaterialTypedBuffer, Name(s_SceneShadowMaterialTypedName), s_SceneShadowMaterialTypedLabel);
    const auto shadowInstancesResult = ImportExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceBuffer, Name(s_SceneShadowInstancesName), s_SceneShadowInstancesLabel);
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
    const Core::GpuGraphResourceId sceneInstances = *sceneInstancesResult;
    const Core::GpuGraphResourceId sceneBvhNodes = *sceneBvhNodesResult;
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

    // The opaque callback has no temporal/coarse access. Keep its writes apart so a fresh target never reads as synthetic-first.
    uses.opaque.reserve(s_SoftShadowOpaqueResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResources.size()));
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
    // The compiler lowers the trace from UAV to the sampled state merge/wavelet needs. First wavelet publishes half-B.
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
        // One-wavelet resolve samples only first-wavelet half-B. Keep a conservative ping-pong declaration if passes grow.
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

    uses.transparentTrace.reserve(s_SoftShadowTransparentTraceResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResources.size()));
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
    const b8 transparentHistoryFrontIsA = inputs.rayTracingPlan.historyFrontIsA;
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
        // Selection freezes at compile. The merge samples the front pair and publishes the opposite; wavelets sample graph-owned.
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
    // One RGB wavelet samples only half-A before multiplying visibility. Keep a ping-pong declaration if passes grow.
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

