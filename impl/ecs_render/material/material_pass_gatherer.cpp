// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_pass_gatherer.h"
#include "material_typed_stager.h"

#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_csg/shape_registry.h>
#include <impl/ecs_scene/module.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass_gatherer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static MaterialPassMeshResourceSnapshot CaptureMeshResourceSnapshot(const MeshResources& mesh)noexcept{
    MaterialPassMeshResourceSnapshot snapshot;
    snapshot.sourceBuffers = mesh;
    snapshot.geometryHeapHandles = mesh.geometryHeapHandles;
    snapshot.emulationVertexBuffer = mesh.emulationVertexBuffer;
    snapshot.objectGeometryCache = RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh);
    snapshot.emulationVertexHeapHandle = mesh.emulationVertexHeapHandle;
    snapshot.emulationIndexByteOffset = mesh.emulationIndexByteOffset;
    snapshot.meshletCount = mesh.meshletCount;
    snapshot.meshletPrimitiveIndexCount = mesh.meshletPrimitiveIndexCount;
    snapshot.runtimeMesh = mesh.runtimeMesh;
    snapshot.dynamicMeshletBoundsFresh = mesh.dynamicMeshletBoundsFresh;
    snapshot.dynamicMeshletConesFresh = mesh.dynamicMeshletConesFresh;
    return snapshot;
}

[[nodiscard]] static MaterialPassPipelineResourceSnapshot CapturePipelineResourceSnapshot(const MaterialPipelineResources& pipelineResources)noexcept{
    return {
        .indexedPipeline = pipelineResources.indexedPipeline,
        .emulationPipeline = pipelineResources.emulationPipeline,
        .meshletPipeline = pipelineResources.meshletPipeline,
        .computePipeline = pipelineResources.computePipeline,
        .objectGeometryDecodePipeline = pipelineResources.objectGeometryDecodePipeline,
        .sharedGeometryComputeProgram = pipelineResources.sharedGeometryComputeProgram,
        .indexedGeometryOutput = pipelineResources.indexedGeometryOutput,
    };
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialPassGatherer::gather(
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const b8 transparent,
    const CsgFrameState& csgFrameState,
    const RendererResourceLookupMode::Enum lookupMode,
    const MeshViewGpuData* const csgWorkRegionMeshViewState,
    MaterialPassDrawItemPartitions& drawItems,
    InstanceGpuDataVector& instanceData,
    CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
    MaterialTypedByteDataVector& materialTypedBytes
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto* ecsMeshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!ecsMeshSystemPtr){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: MeshSystem is not registered; material pass cannot resolve meshes"));
        return;
    }
    NWB::Impl::MeshSystem& ecsMeshSystem = *ecsMeshSystemPtr;
    const usize rendererCapacity = rendererView.candidateCount();
    drawItems.reserve(rendererCapacity);
    instanceData.reserve(rendererCapacity);
#if defined(NWB_DEBUG)
    materialTypedRanges.reserve(rendererCapacity);
#endif
    const usize materialTypedByteReserve = rendererCapacity <= Limit<usize>::s_Max / sizeof(u32) ? rendererCapacity * sizeof(u32) : rendererCapacity;
    materialTypedBytes.reserve(materialTypedByteReserve);

    MaterialTypedStager typedStager(m_system, m_world, lookupMode, materialTypedBytes, rendererCapacity);

    const Core::FramebufferInfoEx& framebufferInfo = framebuffer->getFramebufferInfo();
    const CsgReceiverPass::Enum csgReceiverPass = transparent ? CsgReceiverPass::Transparent : CsgReceiverPass::Opaque;
    const b8 csgPassActive = MaterialPipelinePassUsesRendererCsgClip(pass, transparent) && CsgFrameHasReceiverPassWork(csgFrameState, csgReceiverPass);
    Optional<CsgFrameReceiverLookup> csgReceiverLookup;
    const CsgFrameReceiverLookup* csgReceiverLookupPtr = nullptr;
    if(csgPassActive){
        csgReceiverLookup.emplace(m_world, materialTypedBytes.get_allocator().arena());
        if(!csgReceiverLookup->empty()){
            csgReceiverLookupPtr = &*csgReceiverLookup;
            csgFrameData.reserve(rendererCapacity, csgReceiverLookupPtr->cutterCount());
        }
    }

    auto appendDrawForMesh = [&](const Core::ECS::EntityID entity, const NotNull<MaterialSurfaceInfo*> materialInfo, MeshResources& mesh, const CsgReceiverDrawState& csgReceiverState) -> b8{
        NWB_ASSERT(mesh.valid());
        // Mesh creation establishes source-stream descriptors before draw gathering.
        NWB_ASSERT(RendererMeshSystem::GeometryHeapHandlesReady(mesh.geometryHeapHandles));
        NWB_ASSERT(materialInfo->transparent == transparent);

        const NWB::Impl::Scene::TransformComponent* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;
        pipelineKey.twoSided = materialInfo->twoSided;
        const b8 csgClipRequested = csgReceiverLookupPtr && csgReceiverState.active && MaterialPipelinePassUsesRendererCsgClip(pass, transparent);
        LogCsgCapSurfaceDispatchUnavailableIfNeeded(
            csgClipRequested,
            materialInfo->csgCapSurfaceDispatchAvailable,
            materialInfo->materialName,
            materialInfo->csgCapSurfaceDispatchUnavailableLogged
        );
        const b8 csgClipCandidate = csgClipRequested && materialInfo->csgCapSurfaceDispatchAvailable;
        const auto csgClipInfo = csgClipCandidate ? m_csgSystem.resolveCsgReceiverClipDrawInfo(*csgReceiverLookupPtr, csgReceiverState, mesh.csgLocalBounds, transform) : Expected<CsgReceiverClipDrawInfo>(MakeUnexpected(Failure{}));
        const b8 csgClipActive = csgClipInfo && csgClipInfo->cutterCount > 0u;
        if(csgClipActive){
            pipelineKey.csgMode = MaterialPipelineCsgMode::ClipOnly;
            pipelineKey.csgEvaluatorVariant = csgClipInfo->evaluatorVariant;
            pipelineKey.csgShapeRegistryRevision = m_csgShapeRegistry.revision();
            pipelineKey.twoSided = true;
        }

        auto appendInstance = [&]() -> Expected<MaterialInstanceAppend>{
            NWB_ASSERT(instanceData.size() < static_cast<usize>(Limit<u32>::s_Max));

            const auto constantRange = typedStager.constant(*materialInfo);
            if(!constantRange)
                return MakeUnexpected(Failure{});
            const auto mutableRange = typedStager.mutableInstance(entity, *materialInfo);
            if(!mutableRange)
                return MakeUnexpected(Failure{});
            const MaterialTypedInstanceRanges typedRanges{ *constantRange, *mutableRange };

            NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(typedRanges.constantRange));
            NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(typedRanges.mutableRange));
            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            const NWB::Impl::Scene::TransformComponent defaultTransform;
            const NWB::Impl::Scene::TransformComponent& resolvedTransform = transform ? *transform : defaultTransform;
            InstanceGpuData instance = ::NWB::Impl::BuildInstanceGpuData(
                entity,
                transform != nullptr,
                LoadFloat(resolvedTransform.position),
                LoadFloat(resolvedTransform.rotation),
                LoadFloat(resolvedTransform.scale),
                typedRanges.mutableRange.byteOffset
            );
            m_meshSystem.populateMeshGeometryHeapSlots(mesh, instance);
            instanceData.push_back(Move(instance));
            if(csgReceiverLookupPtr)
                csgFrameData.receiverRanges.push_back(CsgReceiverRangeGpuData{});
#if defined(NWB_DEBUG)
            materialTypedRanges.push_back(typedRanges);
#endif
            return MaterialInstanceAppend(instanceIndex, typedRanges);
        };

        if(pass == MaterialPipelinePass::CsgReceiverSurface && !csgClipActive){
            if(!csgReceiverLookupPtr)
                return false;

            return appendInstance().has_value();
        }

        const auto pipelineResult = lookupMode == RendererResourceLookupMode::CreateMissing ? m_system.createRendererPipeline(*materialInfo, pipelineKey, *framebuffer) : m_system.findRendererPipeline(pipelineKey);
        if(!pipelineResult)
            return false;
        const NotNull<MaterialPipelineResources*> pipelineResources = *pipelineResult;
        const auto prepareGeometryResources = [&](const MaterialPipelineResources& resources){
            switch(resources.renderPath){
            case RenderPath::MeshShader:
                return true;
            case RenderPath::VertexIndexed:{
                if(lookupMode == RendererResourceLookupMode::CreateMissing && !m_meshSystem.prepareObjectGeometryCache(mesh, resources.objectGeometryDecodePipeline))
                    return false;
                const auto cache = RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh);
                return cache.valid() && cache.decoderPipeline == resources.objectGeometryDecodePipeline;
            }
            case RenderPath::ComputeEmulation:
                if(lookupMode == RendererResourceLookupMode::CreateMissing && !m_meshSystem.prepareComputeEmulationResources(mesh))
                    return false;
                return
                    mesh.emulationVertexBuffer
                    && mesh.emulationVertexHeapHandle.valid()
                    && mesh.emulationVertexHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
                    && (!resources.indexedGeometryOutput || mesh.emulationIndexByteOffset != 0u)
                ;
            default:
                return false;
            }
        };
        if(!prepareGeometryResources(*pipelineResources))
            return false;
        const RenderPath::Enum renderPath = pipelineResources->renderPath;
        // Freeze primary handles first; sibling cache creation may invalidate this pointer.
        const MaterialPassPipelineResourceSnapshot pipelineResourceSnapshot = __hidden_material_pass_gatherer::CapturePipelineResourceSnapshot(*pipelineResources);

        const b8 passDrawItemActive = pass != MaterialPipelinePass::CsgReceiverSurface;
        const b8 csgReceiverSurfaceActive = csgClipActive && (pass == MaterialPipelinePass::Opaque || pass == MaterialPipelinePass::CsgReceiverSurface);
        MaterialPipelineKey csgReceiverSurfacePipelineKey = pipelineKey;
        MaterialPipelineResources* csgReceiverSurfacePipelineResources = nullptr;
        RenderPath::Enum csgReceiverSurfaceRenderPath = RenderPath::MeshShader;
        if(csgReceiverSurfaceActive){
            csgReceiverSurfacePipelineKey.pass = MaterialPipelinePass::CsgReceiverSurface;
            if(pass == MaterialPipelinePass::CsgReceiverSurface)
                csgReceiverSurfacePipelineResources = pipelineResources.get();
            else{
                const auto csgReceiverSurfacePipeline = lookupMode == RendererResourceLookupMode::CreateMissing ? m_system.createRendererPipeline(*materialInfo, csgReceiverSurfacePipelineKey, *framebuffer) : m_system.findRendererPipeline(csgReceiverSurfacePipelineKey);
                if(!csgReceiverSurfacePipeline)
                    return false;
                csgReceiverSurfacePipelineResources = csgReceiverSurfacePipeline->get();
            }
            if(!prepareGeometryResources(*csgReceiverSurfacePipelineResources))
                return false;
            csgReceiverSurfaceRenderPath = csgReceiverSurfacePipelineResources->renderPath;
        }

        auto appendDrawItemForRenderPath = [](const RenderPath::Enum renderPath, MaterialPassDrawItem drawItem, MaterialPassDrawItems& targetDrawItems){
            switch(renderPath){
            case RenderPath::MeshShader:{
                targetDrawItems.meshDrawItems.push_back(Move(drawItem));
                break;
            }
            case RenderPath::VertexIndexed:{
                targetDrawItems.indexedDrawItems.push_back(Move(drawItem));
                break;
            }
            case RenderPath::ComputeEmulation:{
                targetDrawItems.computeDrawItems.push_back(Move(drawItem));
                break;
            }
            default:
                NWB_ASSERT(false);
                break;
            }
        };

        const auto appendedInstance = appendInstance();
        if(!appendedInstance)
            return false;
        const u32 instanceIndex = appendedInstance->instanceIndex;
        const auto& typedRanges = appendedInstance->typedRanges;

        if(csgClipActive){
            auto csgRange = m_csgSystem.appendCsgReceiverClipData(*csgReceiverLookupPtr, csgReceiverState, mesh.csgLocalBounds, transform, framebufferInfo.width, framebufferInfo.height, csgWorkRegionMeshViewState, csgFrameData);
            if(!csgRange)
                return false;
            // Cap shader shares the receiver surface hook; rangeInfo.w carries the BXDF id.
            csgRange->shadingModelId = materialInfo->shadingModelId;
            csgRange->surfaceDispatchId = materialInfo->surfaceDispatchId;
            csgRange->materialConstantByteOffset = typedRanges.constantRange.byteOffset;
            csgRange->meshInstanceIndex = instanceIndex;
            NWB_ASSERT(instanceIndex < csgFrameData.receiverRanges.size());
            csgFrameData.receiverRanges[instanceIndex] = *csgRange;
        }

        MaterialPassDrawItem drawItem;
        drawItem.meshKey = mesh.meshName;
        drawItem.pipelineKey = pipelineKey;
        drawItem.meshResources = __hidden_material_pass_gatherer::CaptureMeshResourceSnapshot(mesh);
        drawItem.pipelineResources = pipelineResourceSnapshot;
        drawItem.instanceIndex = instanceIndex;
        drawItem.materialConstantByteOffset = typedRanges.constantRange.byteOffset;
        drawItem.shadingModelId = materialInfo->shadingModelId;
        drawItem.meshletConeCullScaleSafe = transform ? MeshletConeCullScaleSafe(LoadFloat(transform->scale)) : true;

        if(passDrawItemActive){
            MaterialPassDrawItems& targetDrawItems = csgClipActive ? drawItems.csg : drawItems.regular;
            if(csgReceiverSurfaceActive)
                appendDrawItemForRenderPath(renderPath, drawItem, targetDrawItems);
            else
                appendDrawItemForRenderPath(renderPath, Move(drawItem), targetDrawItems);
        }

        if(csgReceiverSurfaceActive){
            drawItem.pipelineKey = csgReceiverSurfacePipelineKey;
            drawItem.pipelineResources = __hidden_material_pass_gatherer::CapturePipelineResourceSnapshot(*csgReceiverSurfacePipelineResources);
            appendDrawItemForRenderPath(csgReceiverSurfaceRenderPath, Move(drawItem), drawItems.csgReceiverSurface);
        }

        return true;
    };

    for(auto&& [entity, renderer] : rendererView){
        // Every pass consumes the same frozen selection before assigning indices.
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        MaterialSurfaceInfo* preparedMaterialInfo = nullptr;
        if(lookupMode == RendererResourceLookupMode::PreparedOnly){
            const auto materialInfoResult = m_system.findMaterialSurfaceInfo(renderer.material);
            if(!materialInfoResult || (*materialInfoResult)->transparent != transparent)
                continue;
            preparedMaterialInfo = materialInfoResult->get();
        }

        const auto resolvedMeshResult = ecsMeshSystem.resolveRenderableMeshStatus(entity);
        if(!resolvedMeshResult)
            continue;
        const auto& resolvedMesh = *resolvedMeshResult;

        const auto meshResult = resolvedMesh.runtime
            ? (lookupMode == RendererResourceLookupMode::CreateMissing ? m_meshSystem.createRuntimeMeshResources(resolvedMesh.runtimeMesh) : m_meshSystem.findRuntimeMeshResources(resolvedMesh.runtimeMesh))
            : (lookupMode == RendererResourceLookupMode::CreateMissing ? m_meshSystem.createMeshResources(resolvedMesh.mesh, materialTypedBytes.get_allocator().arena()) : m_meshSystem.findMeshResources(resolvedMesh.mesh.name()))
        ;
        if(!meshResult)
            continue;
        const NotNull<MeshResources*> mesh = *meshResult;

        MaterialSurfaceInfo* materialInfo = preparedMaterialInfo;
        if(!materialInfo){
            const auto materialInfoResult = m_system.createMaterialSurfaceInfo(renderer.material);
            if(!materialInfoResult || (*materialInfoResult)->transparent != transparent)
                continue;
            materialInfo = materialInfoResult->get();
        }

        CsgReceiverDrawState csgReceiverState;
        if(csgReceiverLookupPtr){
            if(const auto resolvedState = csgReceiverLookupPtr->resolveReceiverDrawState(entity, csgReceiverPass))
                csgReceiverState = *resolvedState;
        }

        if(!appendDrawForMesh(entity, MakeNotNull(materialInfo), *mesh, csgReceiverState))
            continue;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

