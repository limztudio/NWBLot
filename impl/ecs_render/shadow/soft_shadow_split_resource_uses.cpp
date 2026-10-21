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

    // The opaque callback has no temporal or coarse scratch access. Keep its direct serial writes separate from the unsplit adaptive declaration so a fresh retained target never becomes a synthetic first read.
    constexpr usize s_OpaqueResourceUseCapacity = 18u;
    uses.opaque.reserve(s_OpaqueResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResourceCount));
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
                for(usize resourceIndex = 0u; resourceIndex < inputs.traceGeometryResourceCount; ++resourceIndex)
                    uses.opaque.push_back(ReadUse(inputs.traceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
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
            for(usize resourceIndex = 0u; resourceIndex < inputs.traceGeometryResourceCount; ++resourceIndex)
                uses.opaque.push_back(ReadUse(inputs.traceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
        }
    }

    constexpr usize s_OpaqueFirstWaveletResourceUseCapacity = 12u;
    uses.opaqueFirstWavelet.reserve(s_OpaqueFirstWaveletResourceUseCapacity);
    // The compiler lowers the opaque trace from UAV to the exact sampled state required by temporal merge or the first wavelet. The first wavelet then publishes half-B for the resolve tail.
    uses.opaqueFirstWavelet.push_back(ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource));
    if(!route.combinedSoftWavelet)
        uses.opaqueFirstWavelet.push_back(WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
    uses.opaqueFirstWavelet.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
    if(route.graphOwnsOpaqueTemporalMergeEntryStates){
        // The frozen selector permits exact selected input/output declarations for the opaque temporal merge.
        const Core::GpuGraphResourceId opaqueHistoryIn = opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB;
        const Core::GpuGraphResourceId opaqueMomentsIn = opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB;
        const Core::GpuGraphResourceId opaqueHistoryOut = opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA;
        const Core::GpuGraphResourceId opaqueMomentsOut = opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA;
        uses.opaqueFirstWavelet.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
        if(softShadowHistoryReadable){
            uses.opaqueFirstWavelet.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
            uses.opaqueFirstWavelet.push_back(ReadUse(opaqueHistoryIn, Core::ResourceStates::ShaderResource));
            uses.opaqueFirstWavelet.push_back(ReadUse(opaqueMomentsIn, Core::ResourceStates::ShaderResource));
        }
        uses.opaqueFirstWavelet.push_back(WriteUse(opaqueHistoryOut, Core::ResourceStates::UnorderedAccess));
        uses.opaqueFirstWavelet.push_back(WriteUse(opaqueMomentsOut, Core::ResourceStates::UnorderedAccess));
    }

    if(!route.combinedSoftUpsample){
        constexpr usize s_OpaqueResolveResourceUseCapacity = 8u;
        uses.opaqueResolve.reserve(s_OpaqueResolveResourceUseCapacity);
        // With the current one-wavelet opaque resolve, the tail only samples the first-wavelet half-B result for upsample. Keep a conservative native ping-pong declaration if that compile-time pass count grows.
        if(NWB_SHADOW_RESOLVE_PASS_COUNT == 1u)
            uses.opaqueResolve.push_back(ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource));
        else{
            uses.opaqueResolve.push_back(ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            uses.opaqueResolve.push_back(ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        }
        uses.opaqueResolve.push_back(WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess));
        uses.opaqueResolve.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        uses.opaqueResolve.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
        uses.opaqueResolve.push_back(ReadUse(inputs.normal, Core::ResourceStates::ShaderResource));
        uses.opaqueResolve.push_back(ReadUse(inputs.depth, Core::ResourceStates::ShaderResource));
        uses.opaqueResolve.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    }

    constexpr usize s_TransparentTraceResourceUseCapacity = 16u;
    uses.transparentTrace.reserve(s_TransparentTraceResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResourceCount));
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
        for(usize resourceIndex = 0u; resourceIndex < inputs.traceGeometryResourceCount; ++resourceIndex)
            uses.transparentTrace.push_back(ReadUse(inputs.traceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
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
    if(uses.graphOwnsTransparentTemporalMergeEntryStates){
        // Selection is frozen with the compiled frame. The merge samples the current front pair and publishes the opposite pair, which the following wavelet receives as graph-owned sampled inputs.
        constexpr usize s_TransparentTemporalMergeResourceUseCapacity = 8u;
        constexpr usize s_CombinedSoftTemporalExtraUseCapacity = 5u;
        uses.transparentTemporalMerge.reserve(s_TransparentTemporalMergeResourceUseCapacity + (route.combinedSoftTemporal ? s_CombinedSoftTemporalExtraUseCapacity : 0u));
        uses.transparentTemporalMerge.push_back(ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource));
        uses.transparentTemporalMerge.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        uses.transparentTemporalMerge.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
        if(softShadowHistoryReadable){
            uses.transparentTemporalMerge.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
            uses.transparentTemporalMerge.push_back(ReadUse(transparentHistoryIn, Core::ResourceStates::ShaderResource));
            uses.transparentTemporalMerge.push_back(ReadUse(transparentMomentsIn, Core::ResourceStates::ShaderResource));
        }
        uses.transparentTemporalMerge.push_back(WriteUse(transparentHistoryOut, Core::ResourceStates::UnorderedAccess));
        uses.transparentTemporalMerge.push_back(WriteUse(transparentMomentsOut, Core::ResourceStates::UnorderedAccess));
        if(route.combinedSoftTemporal){
            uses.transparentTemporalMerge.push_back(ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource));
            if(softShadowHistoryReadable){
                uses.transparentTemporalMerge.push_back(ReadUse(opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB, Core::ResourceStates::ShaderResource));
                uses.transparentTemporalMerge.push_back(ReadUse(opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB, Core::ResourceStates::ShaderResource));
            }
            uses.transparentTemporalMerge.push_back(WriteUse(opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA, Core::ResourceStates::UnorderedAccess));
            uses.transparentTemporalMerge.push_back(WriteUse(opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA, Core::ResourceStates::UnorderedAccess));
        }

        constexpr usize s_CombinedFirstWaveletResourceUseCapacity = 7u;
        constexpr usize s_FirstWaveletResourceUseCapacity = 4u;
        uses.transparentFirstWavelet.reserve(route.combinedSoftWavelet ? s_CombinedFirstWaveletResourceUseCapacity : s_FirstWaveletResourceUseCapacity);
        uses.transparentFirstWavelet.push_back(ReadUse(transparentHistoryOut, Core::ResourceStates::ShaderResource));
        uses.transparentFirstWavelet.push_back(ReadUse(transparentMomentsOut, Core::ResourceStates::ShaderResource));
        uses.transparentFirstWavelet.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
        uses.transparentFirstWavelet.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
    }else{
        // Inactive temporal frames keep their existing trace-to-wavelet route and do not acquire stale-history declarations or a no-op merge callback.
        constexpr usize s_InactiveFirstWaveletResourceUseCapacity = 3u;
        uses.transparentFirstWavelet.reserve(s_InactiveFirstWaveletResourceUseCapacity);
        uses.transparentFirstWavelet.push_back(ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource));
        uses.transparentFirstWavelet.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
        uses.transparentFirstWavelet.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
    }
    if(route.combinedSoftWavelet){
        // Both temporal merges publish their selected output pair before the fused wavelet reads either pair.
        uses.transparentFirstWavelet.push_back(ReadUse(opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA, Core::ResourceStates::ShaderResource));
        uses.transparentFirstWavelet.push_back(ReadUse(opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA, Core::ResourceStates::ShaderResource));
        uses.transparentFirstWavelet.push_back(WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
    }
    constexpr usize s_TransparentFoldResourceUseCapacity = 9u;
    uses.transparentFold.reserve(s_TransparentFoldResourceUseCapacity);
    if(route.combinedSoftUpsample)
        uses.transparentFold.push_back(ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource));
    // With one RGB wavelet the terminal task only samples half-A before multiplying visibility. Preserve a native ping-pong declaration if the compile-time pass count grows.
    if(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u)
        uses.transparentFold.push_back(ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource));
    else{
        uses.transparentFold.push_back(ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
        uses.transparentFold.push_back(ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
    }
    uses.transparentFold.push_back(route.combinedSoftUpsample ? WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess) : ReadWriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess));
    uses.transparentFold.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
    uses.transparentFold.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
    uses.transparentFold.push_back(ReadUse(inputs.normal, Core::ResourceStates::ShaderResource));
    uses.transparentFold.push_back(ReadUse(inputs.depth, Core::ResourceStates::ShaderResource));
    uses.transparentFold.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

