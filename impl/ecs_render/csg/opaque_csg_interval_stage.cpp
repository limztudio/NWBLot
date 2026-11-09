// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_csg/module.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_scene/components.adhd.h>
#include <impl/ecs_render/csg/csg_snapshot_identity_builder.adhd.h>
#include <impl/ecs_render/material/material_surface_lookup.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/components.adhd.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_scene/module.h>
#include <core/ecs/world.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/assets_graphics/csg_subsystem.adhd.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/assets_graphics/components.adhd.h>
#include <core/graphics/shader_archive.h>
#include "format_private.h"
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/csg/graph_types.adhd.h>
#include <core/graphics/backend_selection.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include "shadow_cutter_snapshot.h"
#include <global/hash_utils.h>
#include "opaque_csg_interval_stage.h"
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/csg/types.adhd.h>
#include "opaque_csg_interval_clear_builder.h"
#include <core/graphics/backend_selection/backend.h>
#include "task_graph_opaque_compute_tasks.h"
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/material/material_system.h>
#include <core/graphics/gpu_timing.h>
#include "task_graph_opaque_interval_tasks.h"
#include <impl/ecs_render/csg/csg_system.h>
#include "task_graph_transparent_interval_tasks.h"
#include "transparent_csg_interval_builder.h"
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <global/limit.h>
#include "frame_graph_transparent_csg_tasks.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ReserveCsgStructuredBuffer(const usize requiredCount, const usize elementByteSize, const Name& debugName, Core::GraphicsRuntime& graphics, Core::BufferHandle& buffer, usize& inOutCapacity){
    if(requiredCount == 0u)
        return true;
    if(buffer && inOutCapacity >= requiredCount)
        return true;
    usize capacity = 0u;
    u64 byteSize = 0u;
    if(!ResolveCsgStructuredBufferCapacity(inOutCapacity, requiredCount, elementByteSize, capacity, byteSize))
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(byteSize)
        .setStructStride(static_cast<u32>(elementByteSize))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(debugName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    auto createdBufferResult = graphics.createBuffer(bufferDesc);
    if(!createdBufferResult)
        return false;
    Core::BufferHandle createdBuffer = Move(*createdBufferResult);

    buffer = Move(createdBuffer);
    inOutCapacity = capacity;
    return true;
}

[[nodiscard]] static b8 ReplaceCsgStorageBufferHeapHandle(Core::Device& device, Core::Buffer& buffer, Core::GpuDescriptorHandle& inOutHandle){
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !RayTracingDetail::ReplaceHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, buffer, inOutHandle)){
        // The buffer was replaced; drop the stale handle for a later retry.
        RendererDescriptorRegisterDetail::RetireDescriptor(heap, inOutHandle);
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgGraphResourceSnapshot::bindingValid()const noexcept{
    return
        receiverRanges
        && cutters
        && clipContextSlots
        && intervalSampleState
        && RendererDescriptorRegisterDetail::IsDescriptorClass(receiverRangeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RendererDescriptorRegisterDetail::IsDescriptorClass(cutterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RendererDescriptorRegisterDetail::IsDescriptorClass(clipContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        && RendererDescriptorRegisterDetail::IsDescriptorClass(intervalSampleStateHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
    ;
}

b8 CsgGraphResourceSnapshot::frameReady(const CsgFrameGpuDataView& csgFrameData)const noexcept{
    if(!csgFrameData.hasWork())
        return true;

    return bindingValid() && receiverRangeCapacity >= csgFrameData.receiverRanges.size() && cutterCapacity >= csgFrameData.cutters.size();
}

Expected<u32> CsgGraphResourceSnapshot::findClipContextHeapSlot()const noexcept{
    if(!clipContextSlotsHeapHandle.valid())
        return MakeUnexpected(Failure{});

    return clipContextSlotsHeapHandle.slot();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgState::invalidateResources()noexcept{
    m_clipBindingLayout.reset();
    m_intervalPeelBindingLayout.reset();
    m_receiverSpanBuildBindingLayout.reset();
    m_intervalCombineBindingLayout.reset();
    m_intervalPeelComputeShader.reset();
    m_receiverSpanBuildComputeShader.reset();
    m_intervalCombineComputeShader.reset();
    m_intervalCapFillPixelShader.reset();
    m_refractionCapFillPixelShader.reset();
    m_intervalPeelPipeline.reset();
    m_receiverSpanBuildPipeline.reset();
    m_intervalCombinePipeline.reset();
    m_intervalCapFillPipeline.reset();
    m_refractionCapFillPipeline.reset();
    m_receiverRangeBuffer.reset();
    m_cutterBuffer.reset();
    m_clipContextSlotsBuffer.reset();
    m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_intervalSampleStateBuffer.reset();
    m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_frameStateCacheSignature = CsgFrameStateCacheSignature{};
    m_frameStateCache = CsgFrameState{};
    m_receiverRangeBufferCapacity = 0u;
    m_cutterBufferCapacity = 0u;
    m_frameStateCacheValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::invalidateResources(){
    releaseCsgClipContextHeapHandles();
    m_csgState.invalidateResources();
}

b8 RendererCsgSystem::createCsgClipResources(){
    auto& device = m_graphics.getDevice();
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(ShaderDrivenPushConstants), Core::ShaderType::Mesh | Core::ShaderType::Compute | Core::ShaderType::Pixel, m_csgState.m_clipBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding layout"));
        return false;
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;
    if(!m_csgState.m_clipContextSlotsBuffer){
        Core::BufferDesc bufferDesc;
        bufferDesc.setByteSize(sizeof(CsgClipContextSlots)).setIsConstantBuffer(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName("engine/csg/clip_context_slots").enableAutomaticStateTracking(Core::ResourceStates::Common);
        auto clipContextSlotsBufferResult = m_graphics.createBuffer(bufferDesc);
        if(clipContextSlotsBufferResult)
            m_csgState.m_clipContextSlotsBuffer = Move(*clipContextSlotsBufferResult);
        else
            m_csgState.m_clipContextSlotsBuffer.reset();
        if(!clipContextSlotsBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip-context slot buffer"));
            return false;
        }
    }
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::UniformBuffer, false, heap, *m_csgState.m_clipContextSlotsBuffer.get(), m_csgState.m_clipContextSlotsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::UniformBuffer, false, heap, *m_csgState.m_intervalSampleStateBuffer.get(), m_csgState.m_intervalSampleStateHeapHandle)
    )
        return false;

    return true;
}

b8 RendererCsgSystem::reserveCsgReceiverRangeBufferCapacity(const usize rangeCount){
    const usize oldCapacity = m_csgState.m_receiverRangeBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(rangeCount, sizeof(CsgReceiverRangeGpuData), s_CsgReceiverRangeBufferName, m_graphics, m_csgState.m_receiverRangeBuffer, m_csgState.m_receiverRangeBufferCapacity)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver range buffer"));
        return false;
    }

    if(m_csgState.m_receiverRangeBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(m_graphics.getDevice(), *m_csgState.m_receiverRangeBuffer.get(), m_csgState.m_receiverRangeBufferHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG receiver range buffer in the descriptor heap"));
        return false;
    }
    return true;
}

b8 RendererCsgSystem::reserveCsgCutterBufferCapacity(const usize cutterCount){
    const usize oldCapacity = m_csgState.m_cutterBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(cutterCount, sizeof(CsgCutterGpuData), s_CsgCutterBufferName, m_graphics, m_csgState.m_cutterBuffer, m_csgState.m_cutterBufferCapacity)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cutter buffer"));
        return false;
    }

    if(m_csgState.m_cutterBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(m_graphics.getDevice(), *m_csgState.m_cutterBuffer.get(), m_csgState.m_cutterBufferHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG cutter buffer in the descriptor heap"));
        return false;
    }
    return true;
}

b8 RendererCsgSystem::prepareCsgFrameResources(const usize receiverRangeCount, const usize cutterCount){
    if(receiverRangeCount == 0u || cutterCount == 0u)
        return true;
    NWB_ASSERT(receiverRangeCount <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(cutterCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(!reserveCsgReceiverRangeBufferCapacity(receiverRangeCount) || !reserveCsgCutterBufferCapacity(cutterCount))
        return false;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, *m_csgState.m_receiverRangeBuffer.get(), m_csgState.m_receiverRangeBufferHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, *m_csgState.m_cutterBuffer.get(), m_csgState.m_cutterBufferHeapHandle)
        || !m_csgState.m_clipContextSlotsBuffer || !m_csgState.m_clipContextSlotsHeapHandle.valid()
        || !m_csgState.m_intervalSampleStateBuffer || !m_csgState.m_intervalSampleStateHeapHandle.valid()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG clip context heap registration is incomplete"));
        return false;
    }
    if(!m_csgState.m_clipBindingLayout)
        return false;

    // Draw paths consume these handles only via a graph snapshot.
    NWB_ASSERT(m_csgState.m_receiverRangeBufferCapacity >= receiverRangeCount);
    NWB_ASSERT(m_csgState.m_cutterBufferCapacity >= cutterCount);
    NWB_ASSERT(m_csgState.m_receiverRangeBufferHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_cutterBufferHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_clipContextSlotsHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_intervalSampleStateHeapHandle.valid());
    return true;
}

ECSRenderDetail::CsgGraphResourceSnapshot RendererCsgSystem::csgGraphResourceSnapshot(const b8 retainRefractionCapPipeline)const noexcept{
    return {
        .receiverRanges = m_csgState.m_receiverRangeBuffer,
        .cutters = m_csgState.m_cutterBuffer,
        .clipContextSlots = m_csgState.m_clipContextSlotsBuffer,
        .intervalSampleState = m_csgState.m_intervalSampleStateBuffer,
        .refractionCapFillPipeline = retainRefractionCapPipeline ? m_csgState.m_refractionCapFillPipeline : nullptr,
        .receiverRangeCapacity = m_csgState.m_receiverRangeBufferCapacity,
        .cutterCapacity = m_csgState.m_cutterBufferCapacity,
        .receiverRangeHeapHandle = m_csgState.m_receiverRangeBufferHeapHandle,
        .cutterHeapHandle = m_csgState.m_cutterBufferHeapHandle,
        .clipContextSlotsHeapHandle = m_csgState.m_clipContextSlotsHeapHandle,
        .intervalSampleStateHeapHandle = m_csgState.m_intervalSampleStateHeapHandle,
    };
}

Expected<CsgClipContextSlots> RendererCsgSystem::prepareCsgClipContextSlotData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
)const noexcept{
    CsgClipContextSlots contextSlots{};
    if(!csgFrameData.hasWork())
        return contextSlots;
    if(!csgResources.frameReady(csgFrameData) || !frameBindings.bindingValid() || !targets.bindless.slotsBufferDescriptor.valid())
        return MakeUnexpected(Failure{});

    // Freeze indirections now; later records must not see another generation.
    return ResolveCsgClipContextSlots(
        csgResources.receiverRangeHeapHandle.slot(),
        csgResources.cutterHeapHandle.slot(),
        frameBindings.materialTypedHeapHandle.slot(),
        frameBindings.instanceHeapHandle.slot(),
        targets.bindless.slotsBufferDescriptor.slot(),
        csgResources.intervalSampleStateHeapHandle.slot()
    );
}

void RendererCsgSystem::releaseCsgClipContextHeapHandles(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, m_csgState.m_receiverRangeBufferHeapHandle);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, m_csgState.m_cutterBufferHeapHandle);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, m_csgState.m_clipContextSlotsHeapHandle);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, m_csgState.m_intervalSampleStateHeapHandle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_clip_resolve{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgResolvedClipCutter{
    Float34 worldToShape = {};
    CsgClipWorkBounds workBounds;
    CsgShapeTypeInfo shapeType;
    Span<const u8> parameterBytes;
};

[[nodiscard]] static CsgClipWorkBounds LoadCsgReceiverWorldBounds(const CsgReceiverCpuBounds& receiverBounds, const Scene::TransformComponent* const transform)noexcept{
    return LoadCsgReceiverWorldBoundsFromTransform(
        receiverBounds,
        transform != nullptr,
        transform ? LoadFloat(transform->position) : VectorZero(),
        transform ? LoadFloat(transform->rotation) : VectorZero(),
        transform ? LoadFloat(transform->scale) : VectorZero()
    );
}

[[nodiscard]] static Expected<CsgResolvedClipCutter> ResolveReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    const CsgClipWorkBounds& receiverWorldBounds
){
    auto shapeType = shapeRegistry.findShapeType(cutter.shapeType);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    const auto parameterBytes = TryResolveCsgCutterParameterBytes(shapeType->desc.parameterByteSize, shapeType->desc.defaultParameterBytes, cutter.parameterBytes);
    if(!parameterBytes)
        return MakeUnexpected(Failure{});
    CsgResolvedClipCutter resolvedCutter{
        .worldToShape = cutter.worldToShape,
        .workBounds = {},
        .shapeType = Move(*shapeType),
        .parameterBytes = *parameterBytes,
    };

    if(!ResolveCsgClipWorkBounds(shapeRegistry, resolvedCutter.shapeType, LoadFloat(cutter.shapeToWorld), resolvedCutter.parameterBytes, receiverWorldBounds, resolvedCutter.workBounds))
        return MakeUnexpected(Failure{});

    return resolvedCutter;
}

template<typename CutterHandler>
[[nodiscard]] static b8 ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgClipWorkBounds& receiverWorldBounds,
    CutterHandler&& handler
){
    b8 resolved = true;
    receiverLookup.forEachReceiverCutter(receiverDrawState, [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
        if(!resolved)
            return;

        const auto resolvedCutter = ResolveReceiverClipCutter(
            shapeRegistry,
            cutter,
            receiverWorldBounds
        );
        if(!resolvedCutter)
            return;

        if(!handler(*resolvedCutter))
            resolved = false;
    });
    return resolved;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ResolveCsgClipWorkBounds(const CsgShapeRegistry& shapeRegistry, const CsgShapeTypeInfo& shapeType, const SIMDMatrix& shapeToWorld, const Span<const u8> parameterBytes, const CsgClipWorkBounds& receiverBounds, CsgClipWorkBounds& outWorkBounds){
    outWorkBounds = CsgClipWorkBounds{};
    if(!CsgClipCutterRequiresShapeBounds(receiverBounds.valid, static_cast<b8>(shapeType.desc.shaderModule), shapeType.desc.name))
        return true;

    const auto cutterBounds = shapeRegistry.buildShapeBounds(shapeType.id, shapeToWorld, parameterBytes);
    if(!cutterBounds){
        const Expected<CsgClipWorkBounds> resolved = ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, false, false, VectorZero(), VectorZero());
        if(!resolved)
            return false;
        outWorkBounds = *resolved;
        return true;
    }

    const Expected<CsgClipWorkBounds> resolved = ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, true, cutterBounds->finiteBounds, cutterBounds->minBounds, cutterBounds->maxBounds);
    if(!resolved)
        return false;
    outWorkBounds = *resolved;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CsgFrameGpuData::reserve(const usize receiverCapacity, const usize cutterCapacity){
    receiverRanges.reserve(receiverCapacity);
    cutters.reserve(cutterCapacity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CsgReceiverClipDrawInfo> RendererCsgSystem::resolveCsgReceiverClipDrawInfo(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform
)const{
    CsgReceiverClipDrawInfo info;
    const CsgClipWorkBounds receiverWorldBounds = __hidden_csg_clip_resolve::LoadCsgReceiverWorldBounds(receiverBounds, transform);

    const b8 resolved = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverWorldBounds,
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter)noexcept{
            return TryAccumulateCsgReceiverClipDrawInfo(resolvedCutter.shapeType.desc.shaderModule, info);
        });
    if(!resolved)
        return MakeUnexpected(Failure{});
    return info;
}

Expected<CsgReceiverRangeGpuData> RendererCsgSystem::appendCsgReceiverClipData(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    const u32 frameWidth,
    const u32 frameHeight,
    const MeshViewGpuData* const csgWorkRegionMeshViewState,
    CsgFrameGpuData& csgFrameData
)const{
    CsgReceiverRangeGpuData range;
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    if(!receiverBounds.valid())
        return MakeUnexpected(Failure{});
    const CsgClipWorkBounds receiverWorldBounds = __hidden_csg_clip_resolve::LoadCsgReceiverWorldBounds(receiverBounds, transform);

    const auto worldToReceiver = TryBuildCsgReceiverWorldToLocal(
        transform != nullptr,
        transform ? LoadFloat(transform->position) : VectorZero(),
        transform ? LoadFloat(transform->rotation) : VectorZero(),
        transform ? LoadFloat(transform->scale) : VectorZero()
    );
    if(!worldToReceiver)
        return MakeUnexpected(Failure{});

    b8 meshViewReady = false;
    SIMDMatrix worldToClipSimd{};
    if(csgWorkRegionMeshViewState){
        worldToClipSimd = LoadFloat(csgWorkRegionMeshViewState->worldToClip);
        meshViewReady = !MatrixIsNaN(worldToClipSimd) && !MatrixIsInfinite(worldToClipSimd);
    }
    else{
        if(const auto acceptedWorldToClip = m_meshSystem.snapshotAcceptedMeshViewWorldToClip()){
            worldToClipSimd = LoadFloat(*acceptedWorldToClip);
            meshViewReady = !MatrixIsNaN(worldToClipSimd) && !MatrixIsInfinite(worldToClipSimd);
        }
    }

    StoreFloat(*worldToReceiver, range.worldToReceiver);
    range.localBounds = receiverBounds;
    range.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    CsgFrameWorkRegion receiverWorkRegion;
    const b8 appended = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverWorldBounds,
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter){
            if(csgFrameData.cutters.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            NWB_ASSERT(resolvedCutter.parameterBytes.size() == static_cast<usize>(resolvedCutter.shapeType.desc.parameterByteSize));
            NWB_ASSERT(resolvedCutter.parameterBytes.empty() || resolvedCutter.parameterBytes.data());
            const CsgCutterGpuData cutterGpuData = BuildCsgReceiverCutterGpuDataAndExpandWorkRegion(
                resolvedCutter.shapeType.id,
                LoadFloat(resolvedCutter.worldToShape),
                resolvedCutter.parameterBytes,
                resolvedCutter.workBounds,
                meshViewReady,
                worldToClipSimd,
                frameWidth,
                frameHeight,
                receiverWorkRegion
            );
            csgFrameData.cutters.push_back(cutterGpuData);
            ++range.cutterCount;
            return true;
        });

    FinalizeCsgReceiverRangeWorkRegion(receiverWorkRegion, frameWidth, frameHeight, range, csgFrameData.workRegion);
    if(!appended || range.cutterCount == 0u)
        return MakeUnexpected(Failure{});
    return range;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CsgFrameCacheDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<CsgFrameStateCacheSignature> BuildSignature(Core::ECS::World& world, const CsgShapeRegistry& shapeRegistry, const MeshSystem& meshSystem){
    CsgFrameStateCacheSignature signature;
    signature.shapeRegistryRevision = shapeRegistry.revision();

    if(world.view<SkinnedCsgMeshComponent>().candidateCount() > 0u)
        return MakeUnexpected(Failure{});

    u64 contentHash = s_Fnv64OffsetBasis;
    auto cutterView = world.view<CsgCutterComponent>();
    cutterView.each([&](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
        CsgSnapshotIdentityBuilder().updateCsgFrameStateCutterSignature(entity, cutter, contentHash);
    });

    auto receiverView = world.view<StaticCsgMeshComponent>();
    b8 cacheable = true;
    receiverView.each([&](const Core::ECS::EntityID entity, StaticCsgMeshComponent& typedReceiver){
        const CsgReceiverComponent receiver = typedReceiver.asReceiver();
        const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
        const MeshComponent* mesh = world.tryGetComponent<MeshComponent>(entity);
        const Scene::TransformComponent* transform = world.tryGetComponent<Scene::TransformComponent>(entity);
        const b8 hasRuntimeMeshBinding = receiver.enabled && renderer && renderer->visible && meshSystem.hasRuntimeMeshBinding(entity);
        CsgSnapshotIdentityBuilder().updateCsgFrameStateReceiverSignature(
            entity,
            receiver,
            renderer != nullptr,
            renderer ? renderer->visible : false,
            renderer ? renderer->material.name() : s_NameNone,
            mesh != nullptr,
            mesh ? mesh->mesh.name() : s_NameNone,
            hasRuntimeMeshBinding,
            transform != nullptr,
            transform ? LoadFloat(transform->position) : VectorZero(),
            transform ? LoadFloat(transform->rotation) : VectorZero(),
            transform ? LoadFloat(transform->scale) : VectorZero(),
            contentHash,
            cacheable
        );
    });
    if(!cacheable)
        return MakeUnexpected(Failure{});

    signature.contentHash = contentHash;
    return signature;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState RendererCsgSystem::buildFrameState(Core::Alloc::ScratchArena& scratchArena, IMaterialSurfaceLookup& materialSurfaceLookup){
    auto* ecsMeshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    Expected<CsgFrameStateCacheSignature> signature = MakeUnexpected(Failure{});
    if(ecsMeshSystemPtr)
        signature = CsgFrameCacheDetail::BuildSignature(m_world, m_csgShapeRegistry, *ecsMeshSystemPtr);
    const b8 cacheable = signature.has_value();
    if(cacheable && m_csgState.m_frameStateCacheValid && m_csgState.m_frameStateCacheSignature == *signature)
        return m_csgState.m_frameStateCache;

    b8 frameStateCacheable = cacheable;
    CsgFrameState state;
    auto finishFrameState = [&](const CsgFrameState& frameState)noexcept -> CsgFrameState{
        if(frameStateCacheable){
            m_csgState.m_frameStateCacheSignature = *signature;
            m_csgState.m_frameStateCache = frameState;
            m_csgState.m_frameStateCacheValid = true;
        }
        else
            m_csgState.m_frameStateCacheValid = false;
        return frameState;
    };

    CsgFrameReceiverLookup receiverLookup(m_world, scratchArena);
    if(receiverLookup.empty())
        return finishFrameState(state);

    if(!ecsMeshSystemPtr){
        frameStateCacheable = false;
        return finishFrameState(state);
    }
    NWB::Impl::MeshSystem& ecsMeshSystem = *ecsMeshSystemPtr;

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const auto resolvedReceiver = ResolveCsgReceiverComponent(m_world, entity);
        if(!resolvedReceiver || !resolvedReceiver->receiver.enabled)
            continue;
        const CsgReceiverComponent& receiver = resolvedReceiver->receiver;
        const CsgReceiverKind::Enum receiverKind = resolvedReceiver->receiverKind;

        const auto resolvedMeshResult = ecsMeshSystem.resolveRenderableMeshStatus(entity);
        if(!resolvedMeshResult){
            frameStateCacheable = false;
            continue;
        }
        const auto& resolvedMesh = *resolvedMeshResult;

        if(resolvedMesh.runtime)
            frameStateCacheable = false;
        const auto meshResult = resolvedMesh.runtime ? m_meshSystem.createRuntimeMeshResources(resolvedMesh.runtimeMesh) : m_meshSystem.createMeshResources(resolvedMesh.mesh, scratchArena);
        if(!meshResult){
            frameStateCacheable = false;
            continue;
        }
        const NotNull<MeshResources*> mesh = *meshResult;

        const auto materialInfoResult = materialSurfaceLookup.findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult){
            frameStateCacheable = false;
            continue;
        }
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;

        const Scene::TransformComponent* transform = m_world.tryGetComponent<Scene::TransformComponent>(entity);

        auto countPassCutters = [&](const CsgReceiverPass::Enum pass) -> u32{
            const auto drawState = receiverLookup.resolveReceiverDrawState(entity, pass);
            if(!drawState || drawState->receiverKind != receiverKind)
                return 0u;

            const auto clipInfo = resolveCsgReceiverClipDrawInfo(receiverLookup, *drawState, mesh->csgLocalBounds, transform);
            if(!clipInfo)
                return 0u;
            if(clipInfo->cutterCount == 0u)
                return 0u;

            return clipInfo->cutterCount;
        };

        const u32 opaqueCutterCount = !materialInfo->transparent && receiver.affectOpaquePass ? countPassCutters(CsgReceiverPass::Opaque) : 0u;
        const u32 transparentCutterCount = materialInfo->transparent && receiver.affectTransparentPass ? countPassCutters(CsgReceiverPass::Transparent) : 0u;
        const b8 opaqueWork = opaqueCutterCount > 0u;
        const b8 transparentWork = transparentCutterCount > 0u;
        if(!opaqueWork && !transparentWork)
            continue;

        AddCsgFrameReceiverWork(receiverKind, opaqueWork, transparentWork, Max(opaqueCutterCount, transparentCutterCount), state);
    }

    FinalizeCsgFrameState(state);
    return finishFrameState(state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_peel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void DispatchCsgIntervalCompute(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    Core::ComputePipeline& pipeline,
    const u32 csgContextHeapSlot,
    const u32 meshViewHeapSlot = 0u
){
    Core::ComputeState computeState;
    computeState.setPipeline(&pipeline);
    // Push-only layout; the rest is heap-selected.
    commandList.setComputeState(computeState);
    // Bind after setComputeState so the StorageImage table resolves.
    heap.bindCompute(commandList, pipeline);

    const CsgIntervalDispatchPushConstants pushConstants = ResolveCsgIntervalDispatchPushConstants(
        csgFrameData.workRegion,
        targets.width,
        targets.height,
        targets.csgPeelLayerCount,
        static_cast<u32>(csgFrameData.receiverRanges.size()),
        meshViewHeapSlot,
        csgContextHeapSlot
    );
    if(pushConstants.workExtentX == 0u || pushConstants.workExtentY == 0u)
        return;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(pushConstants.workExtentX, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_X)), DivideUp(pushConstants.workExtentY, static_cast<u32>(NWB_CSG_INTERVAL_PEEL_GROUP_SIZE_Y)), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgSystem::dispatchCsgIntervalPeels(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_intervalPeelPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));
    NWB_ASSERT(frameBindings.bindingValid());

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgIntervalPeel, m_graphics.getDevice(), commandList);

    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(
        commandList,
        m_graphics.getDevice().getDescriptorHeap(),
        targets,
        csgFrameData,
        *m_csgState.m_intervalPeelPipeline,
        csgResources.clipContextSlotsHeapHandle.slot(),
        frameBindings.meshView.heapHandle.slot()
    );
}

void RendererCsgSystem::dispatchCsgReceiverSpanBuild(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_receiverSpanBuildPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgReceiverSpanBuild, m_graphics.getDevice(), commandList);

    commandList.endRenderPass();
    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(commandList, m_graphics.getDevice().getDescriptorHeap(), targets, csgFrameData, *m_csgState.m_receiverSpanBuildPipeline, csgResources.clipContextSlotsHeapHandle.slot());
}

void RendererCsgSystem::dispatchCsgIntervalCombine(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources){
    if(!csgFrameData.hasWork())
        return;
    NWB_ASSERT(m_csgState.m_intervalCombinePipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgIntervalCombine, m_graphics.getDevice(), commandList);

    commandList.endRenderPass();
    __hidden_csg_interval_peel::DispatchCsgIntervalCompute(commandList, m_graphics.getDevice().getDescriptorHeap(), targets, csgFrameData, *m_csgState.m_intervalCombinePipeline, csgResources.clipContextSlotsHeapHandle.slot());
}

void RendererCsgSystem::renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources){
    NWB_ASSERT(m_csgState.m_intervalCapFillPipeline);
    NWB_ASSERT(csgResources.frameReady(csgFrameData));
    NWB_ASSERT(targets.framebuffer);

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CsgCapFill, m_graphics.getDevice(), commandList);

    Core::ViewportState viewportState;
    viewportState.addViewport(targets.framebuffer->getFramebufferInfo().getViewport()).addScissorRect(ToCoreRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height)));

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_csgState.m_intervalCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.framebuffer.get());
    graphicsState.setViewport(viewportState);
    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *m_csgState.m_intervalCapFillPipeline);

    MeshFrameHeapSlots frameHeapSlots;
    frameHeapSlots.generatedVertex = csgResources.clipContextSlotsHeapHandle.slot();
    ECSRenderDetail::SetShaderDrivenPushConstants(commandList, 0u, 0u, 0u, viewportState, frameHeapSlots, 0u);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
}

void RendererCsgSystem::renderRefractionIntervalCaps(Core::CommandList& commandList, const DeferredFrameTargets& targets, const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources, const Core::Rect& workRect, const Span<const u32> receiverIndices){
    NWB_ASSERT(csgResources.refractionCapFillPipeline);
    NWB_ASSERT(csgResources.bindingValid());
    NWB_ASSERT(targets.avboit.refractionFramebuffer);
    NWB_ASSERT(targets.bindless.slotsBufferDescriptor.valid());
    NWB_ASSERT(!receiverIndices.empty());

    Core::ViewportState viewportState;
    viewportState.addViewport(targets.avboit.refractionFramebuffer->getFramebufferInfo().getViewport()).addScissorRect(workRect);
    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(csgResources.refractionCapFillPipeline.get());
    graphicsState.setFramebuffer(targets.avboit.refractionFramebuffer.get());
    graphicsState.setViewport(viewportState);
    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *csgResources.refractionCapFillPipeline);

    MeshFrameHeapSlots frameHeapSlots;
    frameHeapSlots.view = targets.bindless.slotsBufferDescriptor.slot();
    frameHeapSlots.generatedVertex = csgResources.clipContextSlotsHeapHandle.slot();
    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(s_FullscreenTriangleVertexCount);
    for(const u32 receiverIndex : receiverIndices){
        ECSRenderDetail::SetShaderDrivenPushConstants(commandList, 0u, receiverIndex, 0u, viewportState, frameHeapSlots, 0u);
        commandList.draw(drawArgs);
    }
}

Expected<CsgIntervalSampleStateGpuData> RendererCsgSystem::prepareCsgIntervalSampleStateData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
)const noexcept{
    CsgIntervalSampleStateGpuData state{};
    if(!csgFrameData.hasWork())
        return state;
    if(!csgResources.frameReady(csgFrameData) || !frameBindings.bindingValid())
        return MakeUnexpected(Failure{});

    // Freeze rect and slot before recording; later state must not leak in.
    state = ResolveCsgIntervalSampleState(csgFrameData.workRegion, targets.width, targets.height, frameBindings.meshView.heapHandle.slot());
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 CreateIntervalCapFillPipeline(
    Core::Device& device,
    const Core::ShaderHandle& vertexShader,
    const Core::ShaderHandle& pixelShader,
    const Core::BindingLayoutHandle& clipBindingLayout,
    const Core::FramebufferInfo& framebufferInfo,
    Core::GraphicsPipelineHandle& pipeline
){
    if(pipeline && pipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.setVertexShader(vertexShader).setPixelShader(pixelShader).setRenderState(ECSRenderDetail::BuildCompositeRenderState()).addBindingLayout(clipBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());

    auto pipelineResult = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(pipelineResult)
        pipeline = Move(*pipelineResult);
    else
        pipeline.reset();
    return static_cast<b8>(pipelineResult);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererCsgSystem::createCsgIntervalPeelResources(DeferredFrameTargets& targets, const b8 capFillRequired){
    auto& device = m_graphics.getDevice();
    if(!createCsgClipResources())
        return false;
    if(!m_csgState.m_clipBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a CSG clip binding layout"));
        return false;
    }
    if(!m_meshSystem.meshViewBufferSnapshot().valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a mesh view buffer"));
        return false;
    }
    if(
        !targets.csgCapBackNormal
        || !targets.csgIntervalDepth
        || !targets.csgIntervalId
        || !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || targets.csgPeelLayerCount == 0u
        || targets.csgReceiverEventLayerCount == 0u
        || targets.csgReceiverSpanLayerCount == 0u
        || targets.csgRemovedIntervalLayerCount == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires valid peel targets"));
        return false;
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;

    // Push range selects the CSG context and mesh view; the rest is heap-owned.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CsgIntervalDispatchPushConstants), Core::ShaderType::Compute, m_csgState.m_intervalPeelBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding layout"));
        return false;
    }

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CsgIntervalDispatchPushConstants), Core::ShaderType::Compute, m_csgState.m_receiverSpanBuildBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build binding layout"));
        return false;
    }

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CsgIntervalDispatchPushConstants), Core::ShaderType::Compute, m_csgState.m_intervalCombineBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine binding layout"));
        return false;
    }

    if(!m_csgState.m_intervalPeelComputeShader){
        if(!m_shaderSystem.loadShader<ComputeShader>(&CsgSubsystem::intervalPeelCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgIntervalPeelCS", m_csgState.m_intervalPeelComputeShader))
            return false;
    }

    if(!m_csgState.m_receiverSpanBuildComputeShader){
        if(!m_shaderSystem.loadShader<ComputeShader>(&CsgSubsystem::receiverSpanBuildCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgReceiverSpanBuildCS", m_csgState.m_receiverSpanBuildComputeShader))
            return false;
    }

    if(!m_csgState.m_intervalCombineComputeShader){
        if(!m_shaderSystem.loadShader<ComputeShader>(&CsgSubsystem::intervalCombineCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgIntervalCombineCS", m_csgState.m_intervalCombineComputeShader))
            return false;
    }

    if(capFillRequired){
        if(!m_shaderSystem.loadDeferredCompositeVertexShader())
            return false;

        if(!m_csgState.m_intervalCapFillPixelShader){
            if(!m_shaderSystem.loadShader<PixelShader>(&CsgSubsystem::intervalCapFillPs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgIntervalCapFillPS", m_csgState.m_intervalCapFillPixelShader))
                return false;
        }
    }

    if(!m_csgState.m_intervalPeelPipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_csgState.m_intervalPeelComputeShader, m_csgState.m_intervalPeelBindingLayout, false, m_csgState.m_intervalPeelPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel pipeline"));
        return false;
    }

    if(!m_csgState.m_receiverSpanBuildPipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_csgState.m_receiverSpanBuildComputeShader, m_csgState.m_receiverSpanBuildBindingLayout, false, m_csgState.m_receiverSpanBuildPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build pipeline"));
        return false;
    }

    if(!m_csgState.m_intervalCombinePipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_csgState.m_intervalCombineComputeShader, m_csgState.m_intervalCombineBindingLayout, false, m_csgState.m_intervalCombinePipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine pipeline"));
        return false;
    }

    if(capFillRequired && !__hidden_csg_interval_resources::CreateIntervalCapFillPipeline(
        device,
        m_shaderSystem.deferredCompositeVertexShader(),
        m_csgState.m_intervalCapFillPixelShader,
        m_csgState.m_clipBindingLayout,
        targets.framebuffer->getFramebufferInfo(),
        m_csgState.m_intervalCapFillPipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval cap fill pipeline"));
        return false;
    }

    if(!createCsgIntervalSampleResources(targets))
        return false;

    return true;
}

b8 RendererCsgSystem::createCsgIntervalSampleResources(DeferredFrameTargets& targets){
    if(!m_meshSystem.meshViewBufferSnapshot().valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires a mesh view buffer"));
        return false;
    }
    if(
        !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || targets.csgReceiverEventLayerCount == 0u
        || targets.csgRemovedIntervalLayerCount == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires valid peel targets"));
        return false;
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;

    return true;
}

b8 RendererCsgSystem::prepareRefractionIntervalCapResources(const DeferredFrameTargets& targets){
    if(!targets.avboit.refractionFramebuffer || !m_csgState.m_clipBindingLayout)
        return false;
    if(!m_shaderSystem.loadDeferredCompositeVertexShader())
        return false;
    if(!m_csgState.m_refractionCapFillPixelShader){
        if(!m_shaderSystem.loadShader<PixelShader>(&CsgSubsystem::refractionCapFillPs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgRefractionCapFillPS", m_csgState.m_refractionCapFillPixelShader))
            return false;
    }
    if(!__hidden_csg_interval_resources::CreateIntervalCapFillPipeline(
        m_graphics.getDevice(),
        m_shaderSystem.deferredCompositeVertexShader(),
        m_csgState.m_refractionCapFillPixelShader,
        m_csgState.m_clipBindingLayout,
        targets.avboit.refractionFramebuffer->getFramebufferInfo(),
        m_csgState.m_refractionCapFillPipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG refraction cap fill pipeline"));
        return false;
    }
    return true;
}

b8 RendererCsgSystem::createCsgIntervalSampleStateBuffer(){
    if(m_csgState.m_intervalSampleStateBuffer)
        return true;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(sizeof(CsgIntervalSampleStateGpuData))
        .setIsConstantBuffer(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName("engine/csg/interval_sample_state")
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    auto intervalSampleStateBufferResult = m_graphics.createBuffer(bufferDesc);
    if(intervalSampleStateBufferResult)
        m_csgState.m_intervalSampleStateBuffer = Move(*intervalSampleStateBufferResult);
    else
        m_csgState.m_intervalSampleStateBuffer.reset();
    if(!intervalSampleStateBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample state buffer"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum SelectCsgFormat(const Core::Format::Enum candidate, const Core::FormatSupport::Mask requiredSupport, Core::Device& device)noexcept{
    const Array<Core::Format::Enum, 1> candidates = { candidate };
    return SelectSupportedFormat(device, candidates, requiredSupport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererCsgSystem::createCsgPeelTargets(DeferredFrameTargets& targets){
    if(targets.csgIntervalTargetsValid())
        return true;


    ResetCsgIntervalTargets(targets);

    NWB_ASSERT(targets.width > 0u && targets.height > 0u);
    NWB_ASSERT(targets.csgCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgIntervalIdFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverEventCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgReceiverSpanCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalDepthFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalCapNormalFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalDataFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgRemovedIntervalCountFormat != Core::Format::UNKNOWN);
    NWB_ASSERT(targets.csgPeelLayerCount == s_CsgPeelLayerCount);
    NWB_ASSERT(targets.csgReceiverEventLayerCount == s_CsgReceiverEventLayerCount);
    NWB_ASSERT(targets.csgReceiverSpanLayerCount == s_CsgReceiverSpanLayerCount);
    NWB_ASSERT(targets.csgRemovedIntervalLayerCount == s_CsgRemovedIntervalLayerCount);

    const auto createCsgTexture = [&](
        const Core::Format::Enum format,
        const Name& name,
        const u32 layerCount,
        const b8 renderTarget,
        const tchar* const errorMessage,
        Core::TextureHandle& outTexture
    ){
        Core::TextureDesc desc;
        desc.setWidth(targets.width).setHeight(targets.height).setArraySize(layerCount).setFormat(format).setDimension(Core::TextureDimension::Texture2DArray).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        if(renderTarget)
            desc.setIsRenderTarget(true);
        desc.setIsUAV(true).setName(name);
        auto result = m_graphics.createTexture(desc);
        if(!result){
            outTexture.reset();
            NWB_LOGGER_ERROR(errorMessage);
            return false;
        }
        outTexture = Move(*result);
        return true;
    };

    return
        createCsgTexture(targets.csgCapNormalFormat, Name(s_RenderNameEngineDeferredCsgCapBackNormal), targets.csgPeelLayerCount, true, NWB_TEXT("RendererSystem: failed to create deferred CSG cap back-normal peel target"), targets.csgCapBackNormal)
        && createCsgTexture(targets.csgIntervalDepthFormat, Name(s_RenderNameEngineDeferredCsgIntervalDepth), targets.csgPeelLayerCount, true, NWB_TEXT("RendererSystem: failed to create deferred CSG interval depth peel target"), targets.csgIntervalDepth)
        && createCsgTexture(targets.csgIntervalIdFormat, Name(s_RenderNameEngineDeferredCsgIntervalId), targets.csgPeelLayerCount, true, NWB_TEXT("RendererSystem: failed to create deferred CSG interval id peel target"), targets.csgIntervalId)
        && createCsgTexture(
            targets.csgReceiverEventDataFormat, Name(s_RenderNameEngineDeferredCsgReceiverEventData), targets.csgReceiverEventLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event data target"), targets.csgReceiverEventData
        )
        && createCsgTexture(targets.csgReceiverEventCountFormat, Name(s_RenderNameEngineDeferredCsgReceiverEventCount), 1u, false, NWB_TEXT("RendererSystem: failed to create deferred CSG receiver event count target"), targets.csgReceiverEventCount)
        && createCsgTexture(
            targets.csgReceiverSpanDataFormat, Name(s_RenderNameEngineDeferredCsgReceiverSpanData), targets.csgReceiverSpanLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span data target"), targets.csgReceiverSpanData
        )
        && createCsgTexture(targets.csgReceiverSpanCountFormat, Name(s_RenderNameEngineDeferredCsgReceiverSpanCount), 1u, false, NWB_TEXT("RendererSystem: failed to create deferred CSG receiver span count target"), targets.csgReceiverSpanCount)
        && createCsgTexture(
            targets.csgRemovedIntervalDepthFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalDepth), targets.csgRemovedIntervalLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval depth target"), targets.csgRemovedIntervalDepth
        )
        && createCsgTexture(
            targets.csgRemovedIntervalCapNormalFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalCapNormal), targets.csgRemovedIntervalLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval cap-normal target"), targets.csgRemovedIntervalCapNormal
        )
        && createCsgTexture(
            targets.csgRemovedIntervalDataFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalData), targets.csgRemovedIntervalLayerCount, false,
            NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval data target"), targets.csgRemovedIntervalData
        )
        && createCsgTexture(targets.csgRemovedIntervalCountFormat, Name(s_RenderNameEngineDeferredCsgRemovedIntervalCount), 1u, false, NWB_TEXT("RendererSystem: failed to create deferred CSG removed interval count target"), targets.csgRemovedIntervalCount)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Optional<Core::GpuUploadBlobId>> RendererRayTracingSystem::retainPreparedCsgTraceContextUpload(Core::GpuTaskGraph& graph)const{
    if(!m_shadowVisibilityTraceResourcesPreflighted || !m_lightSpaceShadow.m_csg.snapshot.hasCsg)
        return Optional<Core::GpuUploadBlobId>{};
    const auto& bytes = m_lightSpaceShadow.m_csg.bytes;
    if(bytes.empty() || !m_rayTracingState.m_csgTraceContextBuffer || !m_rayTracingState.m_csgTraceContextHeapHandle.valid())
        return MakeUnexpected(Failure{});

    const auto blobResult = graph.copyUploadData(bytes.data(), bytes.size(), alignof(u32));
    if(!blobResult)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blobResult};
}

b8 RendererRayTracingSystem::prepareCausticOpticalReadTracking(const ECSRenderDetail::MeshViewBufferSnapshot& meshView){
    const b8 admitted = m_shadowVisibilityHardwareSupported ? hasHwCausticWork(meshView) : hasCausticWork(meshView);
    if(!ensureRayTraceContextReadTracking(admitted))
        return false;
    m_opticalCausticReaderAdmitted = admitted;
    return true;
}

void RendererRayTracingSystem::confirmRayTraceContextReadSubmission(const Core::QueueSubmissionToken& token)noexcept{
    if(!token.valid() || (!m_lightSpaceShadow.m_csg.snapshot.hasCsg && !m_opticalCausticReaderAdmitted))
        return;
    // Every physical reader protects the next upload, including accepted prefixes of partially rejected graphs.
    auto& readTokens = m_rayTracingState.m_rayTraceContextReadSubmissionTokens;
    NWB_ASSERT(token.hasPhysicalQueueIdentity() && token.physicalQueueIndex < readTokens.size());
    auto& previous = readTokens[token.physicalQueueIndex];
    if(!previous.valid() || !previous.matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration) || token.value > previous.value)
        previous = token;
}

b8 RendererRayTracingSystem::ensureRayTraceContextReadTracking(const b8 opticalCausticReader){
    auto& readTokens = m_rayTracingState.m_rayTraceContextReadSubmissionTokens;
    const b8 readerAdmitted = m_lightSpaceShadow.m_csg.snapshot.hasCsg || opticalCausticReader;
    if(!readerAdmitted && readTokens.empty())
        return true;

    auto& device = m_graphics.getDevice();
    // Keep pending readers when a later frame stops using their optical or CSG inputs.
    b8 readersPending = false;
    for(auto& token : readTokens){
        if(!token.valid())
            continue;
        const Core::GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
        if(!device.matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration)){
            token = {};
            continue;
        }
        const Expected<u64> completedValue = device.queueGetCompletedInstance(queue);
        if(completedValue && *completedValue >= token.value)
            token = {};
        else
            readersPending = true;
    }
    if(!readerAdmitted){
        if(!readersPending)
            readTokens.clear();
        return true;
    }

    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u)
        return false;
    // Acceptance only updates these preallocated physical queue slots.
    readTokens.resize(topology.queueCount);
    return true;
}

b8 RendererRayTracingSystem::ensureCsgTraceContextResources(){
    if(!ensureRayTraceContextReadTracking(false))
        return false;
    if(!m_lightSpaceShadow.m_csg.snapshot.hasCsg)
        return true;

    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const usize byteCount = m_lightSpaceShadow.m_csg.bytes.size();
    auto& buffer = m_rayTracingState.m_csgTraceContextBuffer;
    auto& handle = m_rayTracingState.m_csgTraceContextHeapHandle;
    u64 capacity = 0u;
    if(!ResolveCsgTraceContextBufferCapacity(
        heap.isInitialized(),
        byteCount,
        device.getMaxStorageBufferRange(),
        static_cast<b8>(buffer),
        buffer ? buffer->getCreationDescription().byteSize : 0u,
        capacity
    ))
        return false;
    if(buffer && buffer->getCreationDescription().byteSize >= byteCount)
        return RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, *buffer, handle);
    Core::BufferDesc desc;
    desc.setByteSize(capacity).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(Name(s_CsgContextDebug)).enableAutomaticStateTracking(Core::ResourceStates::Common);
    auto replacementResult = m_graphics.createBuffer(desc);
    if(!replacementResult)
        return false;
    Core::BufferHandle replacement = Move(*replacementResult);
    const auto replacementHandle = RayTracingDetail::RegisterHeapBuffer(heap, *replacement, Core::GpuDescriptorClass::StorageBuffer, false);
    if(!replacementHandle)
        return false;
    Core::GpuDescriptorRetirementGuard retireReplacement(heap, *replacementHandle);

    RayTracingDetail::RetireHeapHandle(heap, handle);
    buffer = Move(replacement);
    handle = *replacementHandle;
    // Prior readers retain the old buffer, but also read the persistent selector; its next upload must drain them.
    retireReplacement.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_cutter_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendReceiverCutters(const CsgShadowReceiverInput& receiver, const CsgReceiverDrawState& drawState, const CsgFrameReceiverLookup& receiverLookup, const CsgShapeRegistry& shapeRegistry, CsgShadowSnapshot& snapshot, CsgShadowReceiverRangeGpu& range){
    const SIMDVectorSingle receiverMin = LoadFloat(receiver.worldMin);
    const SIMDVectorSingle receiverMax = LoadFloat(receiver.worldMax);
    const b8 boundsCanCull = receiver.boundsValid && AabbTests::Valid(receiverMin, receiverMax);
    range.firstCutter = static_cast<u32>(snapshot.cutters.size());
    receiverLookup.forEachReceiverCutter(drawState, [&](const Core::ECS::EntityID entity, const CsgCutterComponent& cutter){
        const auto shapeType = shapeRegistry.findShapeType(cutter.shapeType);
        if(!shapeType)
            return;
        const auto parameters = TryResolveCsgCutterParameterBytes(shapeType->desc.parameterByteSize, shapeType->desc.defaultParameterBytes, cutter.parameterBytes);
        if(!parameters)
            return;

        const SIMDMatrix shapeToWorld = LoadFloat(cutter.shapeToWorld);
        const auto bounds = shapeRegistry.buildShapeBounds(shapeType->id, shapeToWorld, *parameters);
        if(!bounds)
            return;
        if(boundsCanCull && bounds->finiteBounds && !AabbTests::Intersects(receiverMin, receiverMax, bounds->minBounds, bounds->maxBounds))
            return;

        const SIMDMatrix worldToShape = LoadFloat(cutter.worldToShape);
        CsgSnapshotIdentityBuilder().updateShadowCutterSnapshotHashes(
            entity,
            cutter.shapeType,
            worldToShape,
            shapeToWorld,
            *parameters,
            snapshot.contentIdentity,
            snapshot.identity
        );
        AppendShadowCutterRecord(
            static_cast<b8>(shapeType->desc.shaderModule),
            shapeType->desc.name,
            worldToShape,
            shapeToWorld,
            *parameters,
            range,
            snapshot.cutters
        );
    });
    FinalizeShadowReceiverCuttersRange(range, snapshot.cutters);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildCsgShadowSnapshot(const CsgShapeRegistry& shapeRegistry, const Span<const CsgShadowReceiverInput> receivers, Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena, CsgShadowSnapshot& outSnapshot){
    outSnapshot.receiverRanges.clear();
    outSnapshot.cutters.clear();
    outSnapshot.identity = 0u;
    outSnapshot.contentIdentity = 0u;
    outSnapshot.hasCsg = false;
    const usize receiverCount = receivers.size();
    if(receiverCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(receiverCount == 0u || !HasCsgFrameCandidates(world))
        return true;

    const CsgFrameReceiverLookup receiverLookup(world, scratchArena);
    if(receiverLookup.empty())
        return true;
    Vector<CsgReceiverDrawState, Core::Alloc::ScratchArena> drawStates(scratchArena);
    drawStates.resize(receiverCount);
    b8 hasReceiver = false;
    usize cutterCapacity = 0u;
    for(usize index = 0u; index < receiverCount; ++index){
        const auto drawState = receiverLookup.resolveReceiverDrawState(receivers[index].entity, receivers[index].receiverPass);
        if(!drawState)
            continue;
        drawStates[index] = *drawState;
        hasReceiver = true;
        cutterCapacity = AddSaturating<usize>(cutterCapacity, Min<usize>(drawStates[index].cutterCount, NWB_CSG_RAY_MAX_CUTTERS));
    }
    if(!hasReceiver)
        return true;
    if(cutterCapacity > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outSnapshot.receiverRanges.resize(receiverCount);
    outSnapshot.cutters.reserve(cutterCapacity);
    CsgSnapshotIdentityBuilder().initializeShadowCutterSnapshotHashes(shapeRegistry.revision(), receiverCount, outSnapshot.contentIdentity, outSnapshot.identity);
    for(usize index = 0u; index < receiverCount; ++index){
        const CsgShadowReceiverInput& receiver = receivers[index];
        CsgSnapshotIdentityBuilder().updateShadowReceiverSnapshotHeaderHashes(receiver, drawStates[index].active, outSnapshot.contentIdentity, outSnapshot.identity);
        CsgShadowReceiverRangeGpu& range = outSnapshot.receiverRanges[index];
        range = CsgShadowReceiverRangeGpu{};
        if(drawStates[index].active){
            __hidden_shadow_cutter_snapshot::AppendReceiverCutters(receiver, drawStates[index], receiverLookup, shapeRegistry, outSnapshot, range);
            outSnapshot.hasCsg = outSnapshot.hasCsg || (range.flags & NWB_CSG_RAY_RECEIVER_ACTIVE) != 0u;
        }
        CsgSnapshotIdentityBuilder().updateShadowReceiverSnapshotRangeHashes(range, outSnapshot.contentIdentity, outSnapshot.identity);
    }
    if(!outSnapshot.hasCsg){
        outSnapshot.receiverRanges.clear();
        outSnapshot.identity = 0u;
        outSnapshot.contentIdentity = 0u;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueCsgIntervalStage::capturePayloads(
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const MaterialPassDrawItemPartitionsView& opaqueDrawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize instanceCount,
    const usize materialTypedByteCount,
    const b8 materialDrawBuffersUploaded,
    const b8 csgFrameBuffersUploaded,
    const b8 hasOpaqueDrawItems,
    const b8 hasOpaqueCsgFrameWork
){
    m_receiverSpanPayload.frameBindings = frameBindings;
    m_intervalCombinePayload.frameBindings = frameBindings;
    m_intervalSamplePayload.frameBindings = frameBindings;
    m_intervalSamplePayload.csgResources = csgResources;
    m_intervalSampleComputeEmulationPayload.frameBindings = frameBindings;
    if(!hasOpaqueCsgFrameWork)
        return;

    // The graph owns the producer fence chain. Normal prefix merges with G-buffer; FrontierSafe keeps each ticket.
    m_receiverSpanPayload.materialDrawBuffersUploaded = materialDrawBuffersUploaded;
    m_receiverSpanPayload.csgFrameBuffersUploaded = csgFrameBuffersUploaded;
    m_receiverSpanPayload.csgResources = csgResources;
    m_receiverSpanPayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceCount, materialTypedByteCount);
    m_intervalCombinePayload.materialDrawBuffersUploaded = materialDrawBuffersUploaded;
    m_intervalCombinePayload.csgFrameBuffersUploaded = csgFrameBuffersUploaded;
    m_intervalCombinePayload.csgResources = csgResources;
    m_intervalCombinePayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceCount, materialTypedByteCount);
    m_intervalSamplePayload.materialDrawBuffersUploaded = materialDrawBuffersUploaded;
    m_intervalSamplePayload.csgFrameBuffersUploaded = csgFrameBuffersUploaded;
    m_intervalSamplePayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
    m_intervalSamplePayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceCount, materialTypedByteCount);
}

Expected<OpaqueCsgIntervalStageTasks> OpaqueCsgIntervalStage::declare(const OpaqueCsgIntervalStageInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ObjectGeometryCacheGraph& objectGeometry){
    using namespace RendererTaskGraphDetail;

    const Core::TextureSubresourceSet csgPeelSubresources(0u, 1u, 0u, inputs.targets.csgPeelLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources(0u, 1u, 0u, inputs.targets.csgReceiverEventLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources(0u, 1u, 0u, inputs.targets.csgReceiverSpanLayerCount);
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources(0u, 1u, 0u, 1u);
    Array<Core::GpuTaskResourceUse, s_CsgReceiverSpanResourceUseCapacity> csgReceiverSpanResourceUses = {};
    usize csgReceiverSpanResourceUseCount = 0u;
    Array<Core::GpuTaskResourceUse, s_CsgIntervalCombineResourceUseCapacity> csgIntervalCombineResourceUses = {};
    usize csgIntervalCombineResourceUseCount = 0u;
    csgReceiverSpanResourceUses[csgReceiverSpanResourceUseCount++] = ReadTextureUse(inputs.resources.csgReceiverEventData, csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess);
    csgReceiverSpanResourceUses[csgReceiverSpanResourceUseCount++] = ReadTextureUse(inputs.resources.csgReceiverEventCount, csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess);
    if(inputs.hasCsgFrameGpuWork){
        csgReceiverSpanResourceUses[csgReceiverSpanResourceUseCount++] = ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer);
        csgReceiverSpanResourceUses[csgReceiverSpanResourceUseCount++] = ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer);
    }
    csgReceiverSpanResourceUses[csgReceiverSpanResourceUseCount++] = WriteTextureUse(inputs.resources.csgReceiverSpanData, csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess);
    csgReceiverSpanResourceUses[csgReceiverSpanResourceUseCount++] = WriteTextureUse(inputs.resources.csgReceiverSpanCount, csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = ReadTextureUse(inputs.resources.csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = ReadTextureUse(inputs.resources.csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = ReadTextureUse(inputs.resources.csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = ReadTextureUse(inputs.resources.csgReceiverSpanData, csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = ReadTextureUse(inputs.resources.csgReceiverSpanCount, csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess);
    if(inputs.hasCsgFrameGpuWork){
        csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer);
        csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer);
    }
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = WriteTextureUse(inputs.resources.csgRemovedIntervalDepth, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = WriteTextureUse(inputs.resources.csgRemovedIntervalCapNormal, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = WriteTextureUse(inputs.resources.csgRemovedIntervalData, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess);
    csgIntervalCombineResourceUses[csgIntervalCombineResourceUseCount++] = WriteTextureUse(inputs.resources.csgRemovedIntervalCount, inputs.removedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess);

    OpaqueCsgIntervalStageTasks tasks;
    Core::GpuGraphResourceSetId opaqueCsgIntervalSampleComputeEmulationOutputSet;
    Core::GpuTaskSchedulingHint csgReceiverSpanScheduling;
    csgReceiverSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
    csgReceiverSpanScheduling.forceSubmissionBoundary = false;
    csgReceiverSpanScheduling.allowPacketMerge = true;
    csgReceiverSpanScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc csgReceiverSpanDesc;
    csgReceiverSpanDesc
        .setIdentity(Name(s_RenderNameRenderGraphicsPrefixCsgReceiverSpan))
        .setMarkerLabel("Opaque CSG Receiver Span")
        .setScheduling(csgReceiverSpanScheduling)
        .setDependencies({ &inputs.dependencyTask, 1u })
        .setResourceUses({ csgReceiverSpanResourceUses.data(), csgReceiverSpanResourceUseCount })
    ;
    const auto receiverSpanTaskResult = m_graph.addTask< ECSRenderDetail::CsgReceiverSpanBuildGraphTask >(csgReceiverSpanDesc, Move(m_receiverSpanPayload));
    if(!receiverSpanTaskResult){
        tasks.receiverSpanTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver-span task"));
        return MakeUnexpected(Failure{});
    }
    tasks.receiverSpanTask = *receiverSpanTaskResult;

    Core::GpuTaskSchedulingHint csgIntervalCombineScheduling;
    csgIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
    csgIntervalCombineScheduling.forceSubmissionBoundary = false;
    csgIntervalCombineScheduling.allowPacketMerge = true;
    csgIntervalCombineScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc csgIntervalCombineDesc;
    csgIntervalCombineDesc
        .setIdentity(Name(s_RenderNameRenderGraphicsPrefixCsgIntervalCombine))
        .setMarkerLabel("Opaque CSG Interval Combine")
        .setScheduling(csgIntervalCombineScheduling)
        .setDependencies({ &tasks.receiverSpanTask, 1u })
        .setResourceUses({ csgIntervalCombineResourceUses.data(), csgIntervalCombineResourceUseCount })
    ;
    const auto intervalCombineTaskResult = m_graph.addTask< ECSRenderDetail::CsgIntervalCombineGraphTask >(csgIntervalCombineDesc, Move(m_intervalCombinePayload));
    if(!intervalCombineTaskResult){
        tasks.intervalCombineTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-combine task"));
        return MakeUnexpected(Failure{});
    }
    tasks.intervalCombineTask = *intervalCombineTaskResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalSampleResourceUses{ scratchArena };
    Core::GpuGraphResourceSetId csgIntervalSampleMaterialGeometrySet;
    Core::GpuGraphResourceSetId csgIntervalSampleMaterialSampledTextureSet;
    const Array<MaterialPassDrawItemsView, 1u> csgIntervalSampleMaterialGeometryDrawSets = { inputs.opaqueDrawItems.csg };
    const b8 csgIntervalSampleUsesMaterialGeometry = !inputs.opaqueDrawItems.csg.empty();
    m_intervalSamplePayload.materialGeometryStatesGraphOwned = false;
    if(csgIntervalSampleUsesMaterialGeometry){
        const auto csgIntervalSampleMaterialGeometrySetResult = GatherPreparedMaterialGeometryResourceSet(
            csgIntervalSampleMaterialGeometryDrawSets,
            Name(s_RenderNameRenderGraphicsPrefixCsgIntervalSampleMaterialGeometry),
            "Opaque CSG Material Geometry",
            m_graph,
            scratchArena
        );
        m_intervalSamplePayload.materialGeometryStatesGraphOwned = csgIntervalSampleMaterialGeometrySetResult.has_value();
        if(csgIntervalSampleMaterialGeometrySetResult)
            csgIntervalSampleMaterialGeometrySet = *csgIntervalSampleMaterialGeometrySetResult;
    }
    if(csgIntervalSampleUsesMaterialGeometry && !m_intervalSamplePayload.materialGeometryStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material geometry states"));
        return MakeUnexpected(Failure{});
    }
    b8 csgIntervalSampleMaterialSampledTexturesCollected = false;
    if(csgIntervalSampleUsesMaterialGeometry){
        const auto csgIntervalSampleMaterialSampledTextureSetResult = GatherPreparedMaterialSampledTextureResourceSet(
            m_materialSystem,
            csgIntervalSampleMaterialGeometryDrawSets,
            Name(s_RenderNameRenderGraphicsPrefixCsgIntervalSampleMaterialSampledTextures),
            "Opaque CSG Material Sampled Textures",
            m_graph,
            scratchArena
        );
        csgIntervalSampleMaterialSampledTexturesCollected = csgIntervalSampleMaterialSampledTextureSetResult.has_value();
        if(csgIntervalSampleMaterialSampledTextureSetResult)
            csgIntervalSampleMaterialSampledTextureSet = *csgIntervalSampleMaterialSampledTextureSetResult;
    }
    if(csgIntervalSampleUsesMaterialGeometry && !csgIntervalSampleMaterialSampledTexturesCollected){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material sampled textures"));
        return MakeUnexpected(Failure{});
    }

    // Interval-sample material follows Combine immediately, unlike opaque/receiver work. Sample rasterizes right after.
    // That makes pairwise-distinct outputs graph-ownable without excluding earlier regular/receiver aliases.
    const b8 opaqueCsgIntervalSampleComputeEmulationPlanCaptured = inputs.hasCsgFrameGpuWork
        && m_intervalSamplePayload.materialFrameStatesGraphOwned
        && m_intervalSamplePayload.materialGeometryStatesGraphOwned
        && csgIntervalSampleMaterialSampledTexturesCollected
        && m_intervalSampleComputeEmulationPayload.plan.capture(inputs.opaqueDrawItems.csg, inputs.csgFrameData, scratchArena)
    ;
    b8 opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned = false;
    if(opaqueCsgIntervalSampleComputeEmulationPlanCaptured){
        const auto opaqueCsgIntervalSampleComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            m_graph,
            m_intervalSampleComputeEmulationPayload.plan,
            scratchArena,
            Name(s_RenderNameRenderGraphicsPrefixOpaqueCsgIntervalSampleComputeEmulationOutputs),
            "Opaque CSG Interval-Sample Compute Emulation Outputs"
        );
        opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned = opaqueCsgIntervalSampleComputeEmulationOutputSetResult.has_value();
        if(opaqueCsgIntervalSampleComputeEmulationOutputSetResult)
            opaqueCsgIntervalSampleComputeEmulationOutputSet = *opaqueCsgIntervalSampleComputeEmulationOutputSetResult;
    }
    if(opaqueCsgIntervalSampleComputeEmulationPlanCaptured && !opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval-sample compute-emulation outputs"));
    m_intervalSamplePayload.csgComputeEmulationOutputStatesGraphOwned = opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
    m_intervalSamplePayload.opaqueCsgComputeEmulationTiming = opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned ? &m_intervalSampleComputeEmulationTiming : nullptr;
    // Material instance and typed-material reads.
    csgIntervalSampleResourceUses.reserve(
        1u + (inputs.hasOpaqueDrawItems ? s_OpaqueCsgMaterialBufferResourceUseCount : 0u)
        + (inputs.hasCsgFrameGpuWork ? s_OpaqueCsgFrameResourceUseCount : 0u)
        + s_OpaqueCsgRemovedIntervalResourceUseCount + s_OpaqueCsgGbufferColorResourceUseCount
    );
    csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    if(inputs.hasOpaqueDrawItems){
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange));
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange));
    }
    if(inputs.hasCsgFrameGpuWork){
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange));
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange));
        csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    }
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalDepth, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalCapNormal, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalData, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalCount, inputs.removedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.albedo, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.normal, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.worldPosition, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.specularRoughness, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.depth, Core::ResourceStates::DepthWrite));
    const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialGeometrySetUse{ .resourceSet = csgIntervalSampleMaterialGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialSampledTextureSetUse{
        .resourceSet = csgIntervalSampleMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
        .range = {},
        .requiredState = ECSRenderDetail::s_ObjectGeometryRasterState,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Array<Core::GpuTaskResourceSetUse, s_OpaqueCsgIntervalSampleResourceSetUseCapacity> csgIntervalSampleMaterialResourceSetUses = {};
    usize csgIntervalSampleMaterialResourceSetUseCount = 0u;
    if(m_intervalSamplePayload.materialGeometryStatesGraphOwned)
        csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] = csgIntervalSampleMaterialGeometrySetUse;
    if(csgIntervalSampleMaterialSampledTextureSet.valid())
        csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] = csgIntervalSampleMaterialSampledTextureSetUse;
    if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned)
        csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] = opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse;

    Core::GpuTaskId csgIntervalSampleDependency = tasks.intervalCombineTask;
    if(m_intervalSamplePayload.materialDrawBuffersUploaded && !objectGeometry.prepare(
        inputs.opaqueDrawItems.csg.indexedDrawItems,
        inputs.frameBindings, scratchArena, csgIntervalSampleDependency, csgIntervalSampleResourceUses,
        nullptr, inputs.intervalSampleTimingTicket
    ))
        return MakeUnexpected(Failure{});
    if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
        m_intervalSampleComputeEmulationPayload.csgResources = inputs.csgResources;
        // Producer and sample share one CSG interval-sample ticket, spanning compute through the raster half.
        m_intervalSampleComputeEmulationPayload.instanceCount = inputs.instanceCount;
        m_intervalSampleComputeEmulationPayload.materialTypedByteCount = inputs.materialTypedByteCount;
        m_intervalSampleComputeEmulationPayload.materialDrawBuffersUploaded = m_intervalSamplePayload.materialDrawBuffersUploaded;
        m_intervalSampleComputeEmulationPayload.csgFrameBuffersUploaded = m_intervalSamplePayload.csgFrameBuffersUploaded;
        m_intervalSampleComputeEmulationPayload.materialFrameStatesGraphOwned = m_intervalSamplePayload.materialFrameStatesGraphOwned;
        m_intervalSampleComputeEmulationPayload.materialGeometryStatesGraphOwned = m_intervalSamplePayload.materialGeometryStatesGraphOwned;

        const Array<Core::GpuTaskResourceUse, 12u> opaqueCsgIntervalSampleComputeEmulationResourceUses = {
            ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer),
            ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange),
            ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange),
            ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange),
            ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange),
            ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer),
            ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            ReadTextureUse(inputs.resources.csgRemovedIntervalDepth, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.resources.csgRemovedIntervalCapNormal, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.resources.csgRemovedIntervalData, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.resources.csgRemovedIntervalCount, inputs.removedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess),
        };
        Array<Core::GpuTaskResourceSetUse, s_OpaqueCsgIntervalSampleResourceSetUseCapacity> opaqueCsgIntervalSampleComputeEmulationResourceSetUses = {};
        usize opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount = 0u;
        opaqueCsgIntervalSampleComputeEmulationResourceSetUses[opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++] = csgIntervalSampleMaterialGeometrySetUse;
        if(csgIntervalSampleMaterialSampledTextureSet.valid())
            opaqueCsgIntervalSampleComputeEmulationResourceSetUses[opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++] = csgIntervalSampleMaterialSampledTextureSetUse;
        opaqueCsgIntervalSampleComputeEmulationResourceSetUses[opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++] = opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse;
        Core::GpuTaskSchedulingHint opaqueCsgIntervalSampleComputeEmulationScheduling;
        opaqueCsgIntervalSampleComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueCsgIntervalSampleComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueCsgIntervalSampleComputeEmulationScheduling.allowPacketMerge = true;
        opaqueCsgIntervalSampleComputeEmulationScheduling.mergeWithPrevious = true;
        // Combine is the explicit immediate predecessor.
        // The interval-image UAV handoff survives FrontierSafe splits; Sample merges so vertex handoff and timing stay together.
        opaqueCsgIntervalSampleComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc opaqueCsgIntervalSampleComputeEmulationDesc;
        opaqueCsgIntervalSampleComputeEmulationDesc
            .setIdentity(Name(s_RenderNameRenderGraphicsPrefixOpaqueCsgIntervalSampleComputeEmulation))
            .setMarkerLabel("Opaque CSG Interval-Sample Compute Emulation")
            .setScheduling(opaqueCsgIntervalSampleComputeEmulationScheduling)
            .setDependencies({ &csgIntervalSampleDependency, 1u })
            .setResourceUses(opaqueCsgIntervalSampleComputeEmulationResourceUses)
            .setResourceSetUses({ opaqueCsgIntervalSampleComputeEmulationResourceSetUses.data(), opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount })
        ;
        const auto intervalSampleComputeEmulationTaskResult = m_graph.addTask< ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask >(opaqueCsgIntervalSampleComputeEmulationDesc, Move(m_intervalSampleComputeEmulationPayload));
        if(!intervalSampleComputeEmulationTaskResult){
            tasks.intervalSampleComputeEmulationTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-sample compute-emulation producer"));
            return MakeUnexpected(Failure{});
        }
        tasks.intervalSampleComputeEmulationTask = *intervalSampleComputeEmulationTaskResult;
        csgIntervalSampleDependency = tasks.intervalSampleComputeEmulationTask;
    }

    Core::GpuTaskSchedulingHint csgIntervalSampleScheduling;
    csgIntervalSampleScheduling.cost = Core::GpuTaskCostHint::Medium;
    csgIntervalSampleScheduling.forceSubmissionBoundary = false;
    csgIntervalSampleScheduling.allowPacketMerge = true;
    csgIntervalSampleScheduling.mergeWithPrevious = true;
    csgIntervalSampleScheduling.allowMergeAcrossConsumerFrontier = opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
    Core::GpuTaskDesc csgIntervalSampleDesc;
    csgIntervalSampleDesc
        .setIdentity(Name(s_RenderNameRenderGraphicsPrefixCsgIntervalSample))
        .setMarkerLabel("Opaque CSG Interval Sample")
        .setScheduling(csgIntervalSampleScheduling)
        .setDependencies({ &csgIntervalSampleDependency, 1u })
        .setResourceUses(csgIntervalSampleResourceUses)
        .setResourceSetUses({ csgIntervalSampleMaterialResourceSetUseCount != 0u ? csgIntervalSampleMaterialResourceSetUses.data() : nullptr, csgIntervalSampleMaterialResourceSetUseCount })
    ;
    const auto intervalSampleTaskResult = m_graph.addTask< ECSRenderDetail::CsgIntervalSampleGraphTask >(csgIntervalSampleDesc, Move(m_intervalSamplePayload));
    if(!intervalSampleTaskResult){
        tasks.intervalSampleTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-sample task"));
        return MakeUnexpected(Failure{});
    }
    tasks.intervalSampleTask = *intervalSampleTaskResult;
    return tasks;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclareCsgIntervalRectClearTasks(
    const CsgIntervalRectClearDesc& desc,
    Core::GpuTaskGraph& graph,
    GraphClearTimingRecordState& clearTimingState,
    Core::GpuTaskId& outFirstTask,
    Core::GpuTaskId& outTailTask
){
    Core::GpuTaskSchedulingHint csgIntervalClearScheduling;
    csgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    csgIntervalClearScheduling.forceSubmissionBoundary = false;
    csgIntervalClearScheduling.allowPacketMerge = true;
    csgIntervalClearScheduling.mergeWithPrevious = true;

    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(desc.intervalClearIdentity).setMarkerLabel(desc.intervalClearMarkerLabel).setScheduling(csgIntervalClearScheduling).setDependencies({ &desc.dependencyTask, 1u });
    const Core::GpuClearTextureTaskRecordHooks csgIntervalClearBeginHooks{ .context = &clearTimingState, .beforeClear = &BeginGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
    const Core::GpuClearTextureTaskRecordHooks csgIntervalClearEndHooks{ .context = &clearTimingState, .afterClear = &EndGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
    const auto firstTaskResult = graph.addClearTextureRectUIntTask(
        firstDesc,
        Core::GpuClearTextureRectUIntTaskDesc{ .destination = desc.csgIntervalId, .subresources = desc.csgPeelSubresources, .rect = desc.clearRect, .uintValue = Core::UIntColor(0u), .recordHooks = csgIntervalClearBeginHooks, });
    if(!firstTaskResult){
        outFirstTask = {};
        NWB_LOGGER_WARNING(desc.intervalClearWarningMessage);
        return false;
    }
    outFirstTask = *firstTaskResult;

    Core::GpuTaskSchedulingHint csgIntervalClearTailScheduling = csgIntervalClearScheduling;
    // The endpoint stays in the first clear's Graphics packet even when another queue observes the interval id.
    csgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc tailDesc;
    tailDesc.setIdentity(desc.receiverEventClearIdentity).setMarkerLabel(desc.receiverEventClearMarkerLabel).setScheduling(csgIntervalClearTailScheduling).setDependencies({ &outFirstTask, 1u });
    const auto tailTaskResult = graph.addClearTextureRectUIntTask(
        tailDesc,
        Core::GpuClearTextureRectUIntTaskDesc{ .destination = desc.csgReceiverEventCount, .subresources = desc.csgReceiverEventCountSubresources, .rect = desc.clearRect, .uintValue = Core::UIntColor(0u), .recordHooks = csgIntervalClearEndHooks, });
    if(!tailTaskResult){
        outTailTask = {};
        NWB_LOGGER_WARNING(desc.receiverEventClearWarningMessage);
        return false;
    }
    outTailTask = *tailTaskResult;
    return true;
}

Expected<OpaqueCsgIntervalClearResult> OpaqueCsgIntervalClearBuilder::declare(const OpaqueCsgIntervalClearInputs& inputs, GraphClearTimingRecordState& clearTimingState){
    OpaqueCsgIntervalClearResult result{};
    if(!inputs.dependencyTask.valid())
        return MakeUnexpected(Failure{});
    if(!inputs.hasOpaqueCsgFrameWork)
        return result;
    if(!inputs.csgIntervalId.valid() || !inputs.csgReceiverEventCount.valid())
        return MakeUnexpected(Failure{});

    const CsgIntervalRectClearDesc clearDesc{
        .intervalClearIdentity = Name(s_RenderNameRenderGraphicsPrefixCsgIntervalClear),
        .intervalClearMarkerLabel = "CSG Interval Id Clear",
        .intervalClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval-id clear"),
        .receiverEventClearIdentity = Name(s_RenderNameRenderGraphicsPrefixCsgReceiverEventCountClear),
        .receiverEventClearMarkerLabel = "CSG Receiver Event Count Clear",
        .receiverEventClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver-event clear"),
        .csgIntervalId = inputs.csgIntervalId,
        .csgReceiverEventCount = inputs.csgReceiverEventCount,
        .csgPeelSubresources = inputs.csgPeelSubresources,
        .csgReceiverEventCountSubresources = inputs.csgReceiverEventCountSubresources,
        .clearRect = ToCoreRect(inputs.csgFrameData.workRegion.resolveRect(inputs.targets.width, inputs.targets.height)),
        .dependencyTask = inputs.dependencyTask,
    };
    if(!DeclareCsgIntervalRectClearTasks(clearDesc, m_graph, clearTimingState, result.clearFirstTask, result.clearTask))
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueCsgReceiverComputeEmulationGraphPlan::reset()noexcept{
    meshDrawItems.clear();
    indexedDrawItems.clear();
    drawItems.clear();
    regularDrawItems.clear();
    outputBuffers.clear();
    outputLayouts.clear();
    regularOutputBuffers.clear();
    regularOutputLayouts.clear();
    receiverRanges.clear();
    cutters.clear();
    workRegion = {};
    captured = false;
}

b8 OpaqueCsgReceiverComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& receiverSurfaceDrawItems, const MaterialPassDrawItemsView& sourceRegularDrawItems, const CsgFrameGpuDataView& csgFrameData, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(receiverSurfaceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
        return false;

    meshDrawItems.reserve(receiverSurfaceDrawItems.meshDrawItems.size());
    meshDrawItems.assign(receiverSurfaceDrawItems.meshDrawItems.begin(), receiverSurfaceDrawItems.meshDrawItems.end());
    indexedDrawItems.reserve(receiverSurfaceDrawItems.indexedDrawItems.size());
    indexedDrawItems.assign(receiverSurfaceDrawItems.indexedDrawItems.begin(), receiverSurfaceDrawItems.indexedDrawItems.end());
    regularDrawItems.reserve(sourceRegularDrawItems.computeDrawItems.size());
    regularDrawItems.assign(sourceRegularDrawItems.computeDrawItems.begin(), sourceRegularDrawItems.computeDrawItems.end());
    drawItems.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
    outputBuffers.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
    outputLayouts.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
    regularOutputBuffers.reserve(regularDrawItems.size());
    regularOutputLayouts.reserve(regularDrawItems.size());
    MaterialPassEmulationOutputIndex<NotNull<Core::Buffer*>> outputs(scratchArena);
    for(const MaterialPassDrawItem& regularDrawItem : regularDrawItems){
        const MaterialPassMeshResourceSnapshot& regularMesh = regularDrawItem.meshResources;
        if(!regularMesh.emulationVertexBuffer || !regularMesh.emulationVertexHeapHandle.valid()){
            reset();
            return false;
        }
        regularOutputBuffers.push_back(regularMesh.emulationVertexBuffer);
        regularOutputLayouts.push_back({ regularMesh.emulationIndexByteOffset, regularDrawItem.pipelineResources.indexedGeometryOutput });
        outputs.include(MakeNotNull(regularMesh.emulationVertexBuffer.get()));
    }
    for(const MaterialPassDrawItem& drawItem : receiverSurfaceDrawItems.computeDrawItems){
        if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
            reset();
            return false;
        }
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        if(!mesh.emulationVertexBuffer || !mesh.emulationVertexHeapHandle.valid()){
            reset();
            return false;
        }
        // Earlier ownership and receiver outputs both exclude this receiver.
        if(!outputs.insert(MakeNotNull(mesh.emulationVertexBuffer.get()))){
            reset();
            return false;
        }
        drawItems.push_back(drawItem);
        outputBuffers.push_back(mesh.emulationVertexBuffer);
        outputLayouts.push_back({ mesh.emulationIndexByteOffset, drawItem.pipelineResources.indexedGeometryOutput });
    }
    receiverRanges.reserve(csgFrameData.receiverRanges.size());
    receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
    cutters.reserve(csgFrameData.cutters.size());
    cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
    workRegion = csgFrameData.workRegion;
    captured = drawItems.size() == outputBuffers.size() && !drawItems.empty();
    return captured;
}

b8 OpaqueCsgReceiverComputeEmulationGraphPlan::matches(Core::Alloc::ScratchArena& scratchArena)const{
    if(!captured || outputBuffers.size() != drawItems.size() || outputLayouts.size() != drawItems.size() || regularOutputBuffers.size() != regularDrawItems.size() || regularOutputLayouts.size() != regularDrawItems.size())
        return false;
    MaterialPassEmulationOutputIndex<NotNull<Core::Buffer*>> receiverOutputs(scratchArena);
    for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
        const MaterialPassMeshResourceSnapshot& mesh = drawItems[drawIndex].meshResources;
        if(
            !mesh.emulationVertexBuffer
            || !mesh.emulationVertexHeapHandle.valid()
            || mesh.emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
            || !outputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, drawItems[drawIndex].pipelineResources.indexedGeometryOutput)
        )
            return false;
        receiverOutputs.include(MakeNotNull(mesh.emulationVertexBuffer.get()));
    }
    for(usize drawIndex = 0u; drawIndex < regularDrawItems.size(); ++drawIndex){
        const MaterialPassMeshResourceSnapshot& mesh = regularDrawItems[drawIndex].meshResources;
        if(
            !mesh.emulationVertexBuffer
            || !mesh.emulationVertexHeapHandle.valid()
            || mesh.emulationVertexBuffer.get() != regularOutputBuffers[drawIndex].get()
            || !regularOutputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, regularDrawItems[drawIndex].pipelineResources.indexedGeometryOutput)
        )
            return false;
        if(receiverOutputs.contains(MakeNotNull(mesh.emulationVertexBuffer.get())))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueCsgIntervalSampleComputeEmulationGraphPlan::reset()noexcept{
    OpaqueRegularComputeEmulationGraphPlan::reset();
    receiverRanges.clear();
    cutters.clear();
    workRegion = {};
}

b8 OpaqueCsgIntervalSampleComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, const CsgFrameGpuDataView& csgFrameData, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(sourceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
        return false;

    meshDrawItems.reserve(sourceDrawItems.meshDrawItems.size());
    meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
    indexedDrawItems.reserve(sourceDrawItems.indexedDrawItems.size());
    indexedDrawItems.assign(sourceDrawItems.indexedDrawItems.begin(), sourceDrawItems.indexedDrawItems.end());
    if(!CaptureAliasFreeGeneratedGeometryDraws(sourceDrawItems, [](const MaterialPipelineCsgMode::Enum mode)noexcept{ return mode != MaterialPipelineCsgMode::None; }, *this, scratchArena)){
        reset();
        return false;
    }
    receiverRanges.reserve(csgFrameData.receiverRanges.size());
    receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
    cutters.reserve(csgFrameData.cutters.size());
    cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
    workRegion = csgFrameData.workRegion;
    captured = drawItems.size() == outputBuffers.size() && outputBuffers.size() == outputHeapSlots.size() && !drawItems.empty();
    return captured;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 OpaqueCsgReceiverComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.plan.captured)
        return false;

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    if(!frameSetupReady)
        return true;

    Core::Alloc::ScratchArena scratchArena(s_RenderArena);
    // The imported output set is immutable; reject a corrupted plan.
    if(!payload.plan.matches(scratchArena))
        return false;

    const MaterialPassDrawItemsView drawItems = payload.plan.drawItemsView();
    const CsgFrameGpuDataView csgFrameData = payload.plan.csgFrameDataView();
    const b8 deferredResourcesReady = payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount);
    const b8 csgResourcesReady = deferredResourcesReady && payload.csgFrameBuffersUploaded && csgFrameData.hasWork() && payload.csgResources.frameReady(csgFrameData);
    if(!csgResourcesReady || !materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems, payload.frameBindings))
        return true;

    Core::ViewportState csgViewportState;
    csgViewportState.addViewport(payload.targets.framebuffer->getFramebufferInfo().getViewport()).addScissorRect(ToCoreRect(csgFrameData.workRegion.resolveRect(payload.targets.width, payload.targets.height)));
    const MaterialPassDrawContext drawContext{
        commandList,
        payload.targets.framebuffer.get(),
        nullptr,
        csgViewportState,
        &payload.csgResources,
        payload.frameBindings,
        MaterialPipelinePass::CsgReceiverSurface,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true
    };
    materialSystem.drawRecorder().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 OpaqueCsgIntervalSampleComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.plan.captured)
        return false;

    Core::GraphicsRuntime& graphics = payload.graphics;
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    if(!frameSetupReady)
        return true;

    // Reject a corrupted retained plan.
    if(!payload.plan.matches())
        return false;

    const MaterialPassDrawItemsView drawItems = payload.plan.drawItemsView();
    const CsgFrameGpuDataView csgFrameData = payload.plan.csgFrameDataView();
    const b8 deferredResourcesReady = payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount);
    const b8 csgResourcesReady = deferredResourcesReady && payload.csgFrameBuffersUploaded && csgFrameData.hasWork() && payload.csgResources.frameReady(csgFrameData);
    if(!csgResourcesReady || !materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems, payload.frameBindings))
        return true;
    if(payload.opaqueCsgTiming.has_value())
        return false;

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(payload.targets.framebuffer->getFramebufferInfo().getViewport());
    payload.opaqueCsgTiming.emplace(graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, graphics.getDevice(), commandList);
    // Close the marker here; the sample callback owns finishTiming/discard.
    if(!Core::FinishSplitGpuTimingMarker(&payload.opaqueCsgTiming))
        return false;
    const MaterialPassDrawContext drawContext{
        commandList,
        nullptr,
        nullptr,
        deferredViewportState,
        &payload.csgResources,
        payload.frameBindings,
        MaterialPipelinePass::Opaque,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true
    };
    materialSystem.drawRecorder().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_opaque_interval_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The span-build, combine, and sample passes inspect the same opaque snapshot and gate on the same draw-buffer, CSG-resource, and receiver-surface readiness.
struct OpaqueIntervalReadiness{
    MaterialPassDrawItemPartitionsView opaqueDrawItems;
    CsgFrameGpuDataView csgFrameData;
    b8 csgResourcesReady = false;
    b8 csgReceiverSurfaceDrawResourcesReady = false;
};

[[nodiscard]] inline OpaqueIntervalReadiness ResolveOpaqueIntervalReadiness(const CsgOpaqueIntervalRecordInputs& payload, RendererMaterialSystem& materialSystem)noexcept{
    OpaqueIntervalReadiness readiness{};
    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    if(frameSetupReady){
        readiness.opaqueDrawItems = payload.opaqueDrawSnapshot.drawItemsView();
        readiness.csgFrameData = payload.opaqueDrawSnapshot.csgFrameDataView();
    }

    const b8 hasDeferredDrawItems = !readiness.opaqueDrawItems.empty();
    const b8 deferredResourcesReady = hasDeferredDrawItems && payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.opaqueDrawSnapshot.instanceCount, payload.opaqueDrawSnapshot.materialTypedByteCount);
    readiness.csgResourcesReady = deferredResourcesReady && (!readiness.csgFrameData.hasWork() || (payload.csgFrameBuffersUploaded && payload.csgResources.frameReady(readiness.csgFrameData)));
    readiness.csgReceiverSurfaceDrawResourcesReady = readiness.csgResourcesReady && (readiness.opaqueDrawItems.csgReceiverSurface.empty() || materialSystem.drawRecorder().materialPassDrawResourcesReady(readiness.opaqueDrawItems.csgReceiverSurface, payload.frameBindings));
    return readiness;
}

template<typename Dispatch>
[[nodiscard]] inline b8 RecordOpaqueInterval(const CsgOpaqueIntervalRecordInputs& payload, Core::CommandList& commandList, Dispatch dispatch){
    if(!payload.opaqueDrawSnapshot.captured)
        return false;

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const OpaqueIntervalReadiness readiness = ResolveOpaqueIntervalReadiness(payload, materialSystem);
    if(readiness.csgResourcesReady && readiness.csgFrameData.hasWork() && readiness.csgReceiverSurfaceDrawResourcesReady)
        dispatch(csgSystem, commandList, deferredTargets, readiness.csgFrameData, payload.csgResources);
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgReceiverSpanBuildGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    return __hidden_opaque_interval_tasks::RecordOpaqueInterval(
        payload,
        commandList,
        [](RendererCsgSystem& csgSystem, Core::CommandList& cmd, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const CsgGraphResourceSnapshot& csgResources){
            csgSystem.dispatchCsgReceiverSpanBuild(cmd, targets, csgFrameData, csgResources);
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgIntervalCombineGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    return __hidden_opaque_interval_tasks::RecordOpaqueInterval(
        payload,
        commandList,
        [](RendererCsgSystem& csgSystem, Core::CommandList& cmd, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const CsgGraphResourceSnapshot& csgResources){
            csgSystem.dispatchCsgIntervalCombine(cmd, targets, csgFrameData, csgResources);
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgIntervalSampleGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.opaqueDrawSnapshot.captured || (payload.csgComputeEmulationOutputStatesGraphOwned && !payload.opaqueCsgComputeEmulationTiming))
        return false;

    Core::GraphicsRuntime& graphics = payload.graphics;
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);

    const __hidden_opaque_interval_tasks::OpaqueIntervalReadiness readiness = __hidden_opaque_interval_tasks::ResolveOpaqueIntervalReadiness(payload, materialSystem);
    const MaterialPassDrawItemPartitionsView& opaqueDrawItems = readiness.opaqueDrawItems;
    const CsgFrameGpuDataView& csgFrameData = readiness.csgFrameData;
    const b8 csgDrawResourcesReady = readiness.csgResourcesReady && (opaqueDrawItems.csg.empty() || materialSystem.drawRecorder().materialPassDrawResourcesReady(opaqueDrawItems.csg, payload.frameBindings));
    const b8 csgReceiverSurfaceDrawResourcesReady = readiness.csgReceiverSurfaceDrawResourcesReady;
    // Retire the reservation on later disagreement; never leave it stale.
    if(payload.csgComputeEmulationOutputStatesGraphOwned && payload.opaqueCsgComputeEmulationTiming->has_value() && !csgDrawResourcesReady){
        payload.opaqueCsgComputeEmulationTiming->value().discardTiming();
        payload.opaqueCsgComputeEmulationTiming->reset();
        commandList.endRenderPass();
        return true;
    }
    const b8 csgComputeEmulationReady = !payload.csgComputeEmulationOutputStatesGraphOwned || payload.opaqueCsgComputeEmulationTiming->has_value();
    if(csgDrawResourcesReady && csgComputeEmulationReady){
        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());
        const MaterialPassDrawContext csgDrawContext{
            commandList,
            deferredTargets.framebuffer.get(),
            nullptr,
            deferredViewportState,
            &payload.csgResources,
            payload.frameBindings,
            MaterialPipelinePass::Opaque,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.csgComputeEmulationOutputStatesGraphOwned
        };
        if(!opaqueDrawItems.csg.empty()){
            if(payload.csgComputeEmulationOutputStatesGraphOwned){
                materialSystem.drawRecorder().renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
                payload.opaqueCsgComputeEmulationTiming->value().finishTiming(commandList);
                payload.opaqueCsgComputeEmulationTiming->reset();
            }
            else{
                Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, graphics.getDevice(), commandList);

                materialSystem.drawRecorder().renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
            }
        }
        if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
            csgSystem.renderCsgIntervalCaps(commandList, deferredTargets, csgFrameData, payload.csgResources);
    }
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_interval_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both transparent interval passes share one snapshot and gate on the same framebuffer, draw-buffer, CSG, and material readiness.
struct TransparentIntervalReadiness{
    CsgFrameGpuDataView csgFrameData;
    b8 ready = false;
};

[[nodiscard]] inline TransparentIntervalReadiness ResolveTransparentIntervalReadiness(const TransparentCsgIntervalRecordInputs& payload, RendererMaterialSystem& materialSystem)noexcept{
    TransparentIntervalReadiness readiness{};
    const MaterialPassDrawItemsView receiverSurfaceDrawItems = payload.transparentCsgSnapshot.receiverSurfaceDrawItemsView();
    readiness.csgFrameData = payload.transparentCsgSnapshot.csgFrameDataView();
    const b8 drawBuffersReady = payload.frameBindings.frameReady(payload.transparentCsgSnapshot.instanceCount, payload.transparentCsgSnapshot.materialTypedByteCount);
    const b8 csgResourcesReady = payload.csgResources.frameReady(readiness.csgFrameData);
    const b8 receiverSurfaceDrawResourcesReady = materialSystem.drawRecorder().materialPassDrawResourcesReady(receiverSurfaceDrawItems, payload.frameBindings);
    readiness.ready = payload.csgFrameBuffersUploaded && payload.targets.framebuffer && !receiverSurfaceDrawItems.empty() && readiness.csgFrameData.hasWork() && drawBuffersReady && csgResourcesReady && receiverSurfaceDrawResourcesReady;
    return readiness;
}


template<typename Dispatch>
[[nodiscard]] b8 RecordTransparentInterval(const TransparentCsgIntervalRecordInputs& payload, Core::CommandList& commandList, Dispatch dispatch){
    const TransparentIntervalReadiness readiness = ResolveTransparentIntervalReadiness(payload, payload.materialSystem);
    if(readiness.ready)
        dispatch(readiness);
    else
        Core::DiscardGpuTimingMeasure(&payload.transparentCsgIntervalsTiming);
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitCsgReceiverSpanGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming.has_value()){
        commandList.endRenderPass();
        return true;
    }
    return __hidden_transparent_interval_tasks::RecordTransparentInterval(payload, commandList, [&](const auto& readiness){
        payload.csgSystem.dispatchCsgReceiverSpanBuild(commandList, payload.targets, readiness.csgFrameData, payload.csgResources);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitCsgIntervalCombineGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming.has_value()){
        commandList.endRenderPass();
        return true;
    }
    return __hidden_transparent_interval_tasks::RecordTransparentInterval(payload, commandList, [&](const auto& readiness){
        payload.csgSystem.dispatchCsgIntervalCombine(commandList, payload.targets, readiness.csgFrameData, payload.csgResources);
        payload.transparentCsgIntervalsTiming.value().finishTiming(commandList);
        payload.transparentCsgIntervalsTiming.reset();
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TransparentCsgIntervalProducerResult> TransparentCsgIntervalBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const TransparentCsgIntervalProducerInputs& inputs,
    RendererTaskGraphDetail::AvboitPreGraphTask::Payload& avboitPrePayload,
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload,
    GraphClearTimingRecordState& intervalClearTimingState
){
    using namespace RendererTaskGraphDetail;
    TransparentCsgIntervalProducerResult result{};
    if(!inputs.prefixTask.valid())
        return MakeUnexpected(Failure{});

    // Freeze the transparent CSG interval producer before AVBOIT recording; snapshot covers receiver work only.
    result.uploadTask = inputs.prefixTask;
    Core::GpuGraphResourceSetId transparentCsgMaterialGeometrySet;
    Core::GpuGraphResourceSetId transparentCsgMaterialSampledTextureSet;
    const b8 hasTransparentCsgFrameWork = inputs.hasTransparentRenderers && (inputs.csgFrameState.hasTransparentStaticWork || inputs.csgFrameState.hasTransparentSkinnedWork);
    if(hasTransparentCsgFrameWork){
        MaterialPassDrawItemPartitions transparentCsgDrawItems{ scratchArena };
        InstanceGpuDataVector transparentCsgInstanceData{ scratchArena };
        CsgFrameGpuData transparentCsgFrameData{ scratchArena };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector transparentCsgMaterialTypedRanges{ scratchArena };
#endif
        MaterialTypedByteDataVector transparentCsgMaterialTypedBytes{ scratchArena };
        m_materialSystem.gatherMaterialPassDrawItems(
            inputs.targets.framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            true,
            (inputs.csgFrameState),
            RendererResourceLookupMode::PreparedOnly,
            &inputs.meshViewState,
            transparentCsgDrawItems,
            transparentCsgInstanceData,
            transparentCsgFrameData,
#if defined(NWB_DEBUG)
            transparentCsgMaterialTypedRanges,
#endif
            transparentCsgMaterialTypedBytes
        );

        if(!transparentCsgDrawItems.csgReceiverSurface.empty() && transparentCsgFrameData.hasWork()){
            if(
                !inputs.materialInstances.valid()
                || !inputs.materialTyped.valid()
                || !inputs.csgReceiverRanges.valid()
                || !inputs.csgCutters.valid()
                || !inputs.csgClipContextSlots.valid()
                || !inputs.csgIntervalSampleState.valid()
                || !(inputs.frameBindings).frameReady(transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size())
                || !(inputs.csgResources).frameReady(transparentCsgFrameData)
                || !m_materialSystem.drawRecorder().materialPassDrawResourcesReady(transparentCsgDrawItems.csgReceiverSurface, (inputs.frameBindings))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared transparent CSG interval resources were unavailable during graph declaration"));
                return MakeUnexpected(Failure{});
            }

            const Array<MaterialPassDrawItemsView, 1u> transparentCsgMaterialGeometryDrawSets = { transparentCsgDrawItems.csgReceiverSurface, };
            const auto geometrySetResult = GatherPreparedMaterialGeometryResourceSet(transparentCsgMaterialGeometryDrawSets, Name(s_RenderNameRenderAvboitIntervalsTransparentCsgMaterialGeometry), "Transparent CSG Material Geometry", m_graph, scratchArena);
            avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned = geometrySetResult.has_value();
            if(!geometrySetResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material geometry states"));
                return MakeUnexpected(Failure{});
            }
            transparentCsgMaterialGeometrySet = *geometrySetResult;
            const auto sampledTextureSetResult = GatherPreparedMaterialSampledTextureResourceSet(
                m_materialSystem,
                transparentCsgMaterialGeometryDrawSets,
                Name(s_RenderNameRenderAvboitIntervalsTransparentCsgMaterialSampledTextures),
                "Transparent CSG Material Sampled Textures",
                m_graph,
                scratchArena
            );
            if(!sampledTextureSetResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material sampled textures"));
                return MakeUnexpected(Failure{});
            }
            transparentCsgMaterialSampledTextureSet = *sampledTextureSetResult;

            m_materialSystem.prepareMaterialPassInstanceUploadData(inputs.csgResources, transparentCsgInstanceData);
#if defined(NWB_DEBUG)
            if(
                transparentCsgInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || transparentCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || transparentCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: transparent CSG interval upload size overflows graph blob capacity"));
                return MakeUnexpected(Failure{});
            }
            NWB_ASSERT(transparentCsgInstanceData.size() == transparentCsgMaterialTypedRanges.size());
            ValidateMaterialTypedUploadRanges(transparentCsgMaterialTypedRanges, transparentCsgMaterialTypedBytes);
#endif

            const auto clipContextSlotResult = m_csgSystem.prepareCsgClipContextSlotData(inputs.targets, transparentCsgFrameData, inputs.csgResources, inputs.frameBindings);
            if(!clipContextSlotResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot transparent CSG interval auxiliary upload data"));
                return MakeUnexpected(Failure{});
            }
            const auto intervalSampleStateResult = m_csgSystem.prepareCsgIntervalSampleStateData(inputs.targets, transparentCsgFrameData, inputs.csgResources, inputs.frameBindings);
            if(!intervalSampleStateResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot transparent CSG interval auxiliary upload data"));
                return MakeUnexpected(Failure{});
            }
            const CsgClipContextSlots& transparentCsgClipContextSlotData = *clipContextSlotResult;
            const CsgIntervalSampleStateGpuData& transparentCsgIntervalSampleStateData = *intervalSampleStateResult;

            const auto transparentCsgInstanceBlobResult = m_graph.copyUploadData(transparentCsgInstanceData.data(), transparentCsgInstanceData.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData));
            const auto transparentCsgMaterialTypedBlobResult = m_graph.copyUploadData(transparentCsgMaterialTypedBytes.data(), transparentCsgMaterialTypedBytes.size(), alignof(u32));
            const auto transparentCsgReceiverRangesBlobResult = m_graph.copyUploadData(transparentCsgFrameData.receiverRanges.data(), transparentCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData));
            const auto transparentCsgCuttersBlobResult = m_graph.copyUploadData(transparentCsgFrameData.cutters.data(), transparentCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData));
            const auto transparentCsgClipContextSlotsBlobResult = m_graph.copyUploadData(&transparentCsgClipContextSlotData, sizeof(transparentCsgClipContextSlotData), alignof(CsgClipContextSlots));
            const auto transparentCsgIntervalSampleStateBlobResult = m_graph.copyUploadData(&transparentCsgIntervalSampleStateData, sizeof(transparentCsgIntervalSampleStateData), alignof(CsgIntervalSampleStateGpuData));
            if(
                !transparentCsgInstanceBlobResult
                || !transparentCsgMaterialTypedBlobResult
                || !transparentCsgReceiverRangesBlobResult
                || !transparentCsgCuttersBlobResult
                || !transparentCsgClipContextSlotsBlobResult
                || !transparentCsgIntervalSampleStateBlobResult
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable transparent CSG interval upload data"));
                return MakeUnexpected(Failure{});
            }
            const Core::GpuUploadBlobId transparentCsgInstanceBlob = *transparentCsgInstanceBlobResult;
            const Core::GpuUploadBlobId transparentCsgMaterialTypedBlob = *transparentCsgMaterialTypedBlobResult;
            const Core::GpuUploadBlobId transparentCsgReceiverRangesBlob = *transparentCsgReceiverRangesBlobResult;
            const Core::GpuUploadBlobId transparentCsgCuttersBlob = *transparentCsgCuttersBlobResult;
            const Core::GpuUploadBlobId transparentCsgClipContextSlotsBlob = *transparentCsgClipContextSlotsBlobResult;
            const Core::GpuUploadBlobId transparentCsgIntervalSampleStateBlob = *transparentCsgIntervalSampleStateBlobResult;

            Core::GpuTaskSchedulingHint transparentCsgUploadScheduling;
            transparentCsgUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            transparentCsgUploadScheduling.forceSubmissionBoundary = false;
            transparentCsgUploadScheduling.allowPacketMerge = true;
            transparentCsgUploadScheduling.mergeWithPrevious = true;
            // Caustics and AVBOIT submit independently; start this upload chain in its own packet.
            Core::GpuTaskSchedulingHint transparentCsgFirstUploadScheduling = transparentCsgUploadScheduling;
            transparentCsgFirstUploadScheduling.mergeWithPrevious = false;

            const auto appendUploadTask = [&](
                const Name identity,
                const AStringView markerLabel,
                const Core::GpuTaskSchedulingHint& scheduling,
                const Core::GpuUploadBlobId source,
                const Core::GpuGraphResourceId destination,
                const tchar* const warningMessage
            ){
                Core::GpuTaskDesc uploadDesc;
                uploadDesc.setIdentity(identity).setMarkerLabel(markerLabel).setScheduling(scheduling).setDependencies({ &result.uploadTask, 1u });
                const auto uploadTaskResult = m_graph.addUploadBufferTask(
                    uploadDesc,
                    Core::GpuUploadBufferTaskDesc{ .source = source, .destination = destination, .finalState = Core::ResourceStates::Common, });
                if(!uploadTaskResult){
                    result.uploadTask = {};
                    NWB_LOGGER_WARNING(warningMessage);
                    return false;
                }
                result.uploadTask = *uploadTaskResult;
                return true;
            };
            if(
                !appendUploadTask(
                    Name(s_RenderNameRenderAvboitTransparentCsgMaterialInstancesUpload), "Transparent CSG Material Instances Upload", transparentCsgFirstUploadScheduling,
                    transparentCsgInstanceBlob, inputs.materialInstances, NWB_TEXT("RendererSystem: could not declare transparent CSG material instance upload")
                )
                || !appendUploadTask(
                    Name(s_RenderNameRenderAvboitTransparentCsgMaterialTypedUpload), "Transparent CSG Material Typed Upload", transparentCsgUploadScheduling,
                    transparentCsgMaterialTypedBlob, inputs.materialTyped, NWB_TEXT("RendererSystem: could not declare transparent CSG material typed upload")
                )
                || !appendUploadTask(
                    Name(s_RenderNameRenderAvboitTransparentCsgReceiverRangesUpload), "Transparent CSG Receiver Ranges Upload", transparentCsgUploadScheduling,
                    transparentCsgReceiverRangesBlob, inputs.csgReceiverRanges, NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-range upload")
                )
                || !appendUploadTask(
                    Name(s_RenderNameRenderAvboitTransparentCsgCuttersUpload), "Transparent CSG Cutters Upload", transparentCsgUploadScheduling,
                    transparentCsgCuttersBlob, inputs.csgCutters, NWB_TEXT("RendererSystem: could not declare transparent CSG cutter upload")
                )
                || !appendUploadTask(
                    Name(s_RenderNameRenderAvboitTransparentCsgClipContextSlotsUpload), "Transparent CSG Clip Context Slots Upload", transparentCsgUploadScheduling,
                    transparentCsgClipContextSlotsBlob, inputs.csgClipContextSlots, NWB_TEXT("RendererSystem: could not declare transparent CSG clip-context upload")
                )
                || !appendUploadTask(
                    Name(s_RenderNameRenderAvboitTransparentCsgIntervalSampleStateUpload), "Transparent CSG Interval State Upload", transparentCsgUploadScheduling,
                    transparentCsgIntervalSampleStateBlob, inputs.csgIntervalSampleState, NWB_TEXT("RendererSystem: could not declare transparent CSG interval-state upload")
                )
            )
                return MakeUnexpected(Failure{});

            avboitPrePayload.transparentCsgSnapshot.capture(transparentCsgDrawItems.csgReceiverSurface, transparentCsgFrameData, transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size());
            receiverSpanPayload.transparentCsgSnapshot.capture(transparentCsgDrawItems.csgReceiverSurface, transparentCsgFrameData, transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size());
            receiverSpanPayload.csgFrameBuffersUploaded = true;
            intervalCombinePayload.transparentCsgSnapshot.capture(transparentCsgDrawItems.csgReceiverSurface, transparentCsgFrameData, transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size());
            if(!avboitPrePayload.transparentCsgSnapshot.captured || !receiverSpanPayload.transparentCsgSnapshot.captured || !intervalCombinePayload.transparentCsgSnapshot.captured){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not capture transparent CSG interval graph snapshots"));
                return MakeUnexpected(Failure{});
            }
            intervalCombinePayload.csgFrameBuffersUploaded = true;
            avboitPrePayload.transparentCsgStreamsUploaded = true;
        }
    }

    // Clear frozen rect after uploads so the graph owns CopyDest -> UAV ordering.
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        const CsgIntervalRectClearDesc clearDesc{
            .intervalClearIdentity = Name(s_RenderNameRenderAvboitTransparentCsgIntervalClear),
            .intervalClearMarkerLabel = "Transparent CSG Interval Id Clear",
            .intervalClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG interval-id clear"),
            .receiverEventClearIdentity = Name(s_RenderNameRenderAvboitTransparentCsgReceiverEventCountClear),
            .receiverEventClearMarkerLabel = "Transparent CSG Receiver Event Count Clear",
            .receiverEventClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG receiver-event clear"),
            .csgIntervalId = inputs.csgIntervalId,
            .csgReceiverEventCount = inputs.csgReceiverEventCount,
            .csgPeelSubresources = inputs.csgPeelSubresources,
            .csgReceiverEventCountSubresources = inputs.csgReceiverEventCountSubresources,
            .clearRect = ToCoreRect(avboitPrePayload.transparentCsgSnapshot.csgWorkRegion.resolveRect(inputs.targets.width, inputs.targets.height)),
            .dependencyTask = result.uploadTask,
        };
        if(!DeclareCsgIntervalRectClearTasks(
            clearDesc,
            m_graph,
            intervalClearTimingState,
            m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask,
            m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask
        ))
            return MakeUnexpected(Failure{});
        result.uploadTask = m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask;
        NWB_ASSERT(avboitPrePayload.transparentCsgStreamsUploaded && avboitPrePayload.transparentCsgSnapshot.captured);
    }
    result.materialGeometrySet = transparentCsgMaterialGeometrySet;
    result.materialSampledTextureSet = transparentCsgMaterialSampledTextureSet;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphTransparentCsgTaskResult> FrameGraphTransparentCsgTasks::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const FrameGraphTransparentCsgTaskInputs& inputs,
    ObjectGeometryCacheGraph& objectGeometry,
    RendererTaskGraphDetail::AvboitPreGraphTask::Payload& prePayload,
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload
){
    FrameGraphTransparentCsgTaskResult result{};
    using namespace RendererTaskGraphDetail;
    Core::GpuTaskId intervalDependency = inputs.transparentCsgUploadTask;

    // Declare interval-producer states here, before native recording, not on the occupancy task.
    const Core::BufferRange transparentCsgInstanceRange(0u, prePayload.transparentCsgSnapshot.instanceCount * sizeof(InstanceGpuData));
    const Core::BufferRange transparentCsgMaterialTypedRange(0u, prePayload.transparentCsgSnapshot.materialTypedByteCount);
    const Core::BufferRange transparentCsgReceiverRange(0u, prePayload.transparentCsgSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData));
    const Core::BufferRange transparentCsgCutterRange(0u, prePayload.transparentCsgSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalResourceUses{ scratchArena };
    avboitIntervalResourceUses.reserve(s_TransparentCsgIntervalResourceUseCapacity);
    if(prePayload.transparentCsgStreamsUploaded){
        // The receiver raster binds the framebuffer with LOAD, writes off; declare every attachment to preserve contents.
        avboitIntervalResourceUses.push_back(ReadUse(inputs.albedo, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.normal, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.specularRoughness, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.depth, Core::ResourceStates::DepthWrite));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.materialInstances, transparentCsgInstanceRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.materialTyped, transparentCsgMaterialTypedRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.csgReceiverRanges, transparentCsgReceiverRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.csgCutters, transparentCsgCutterRange));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // Sparse payloads are write-only from Unknown; ID/count stay ReadWrite from their clears.
        avboitIntervalResourceUses.push_back(WriteTextureUse(inputs.csgCapBackNormal, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(WriteTextureUse(inputs.csgIntervalDepth, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(inputs.csgIntervalId, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(WriteTextureUse(inputs.csgReceiverEventData, inputs.csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(inputs.csgReceiverEventCount, inputs.csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess));
    }
    const Core::GpuTaskResourceSetUse intervalMaterialGeometrySetUse{ .resourceSet = inputs.transparentCsgMaterialGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse intervalMaterialSampledTextureSetUse{ .resourceSet = inputs.transparentCsgMaterialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    Array<Core::GpuTaskResourceSetUse, s_TransparentCsgMaterialResourceSetUseCapacity> transparentCsgMaterialResourceSetUses = {};
    usize transparentCsgMaterialResourceSetUseCount = 0u;
    if(prePayload.transparentCsgMaterialGeometryStatesGraphOwned)
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] = intervalMaterialGeometrySetUse;
    if(inputs.transparentCsgMaterialSampledTextureSet.valid())
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] = intervalMaterialSampledTextureSetUse;
    avboitIntervalResourceUses.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    avboitIntervalResourceUses.push_back(ReadUse(inputs.avboitMaterialDomain));
    avboitIntervalResourceUses.push_back(ReadWriteUse(inputs.avboitCsgDomain, Core::ResourceStates::ShaderResource));

    if(prePayload.transparentCsgStreamsUploaded && !objectGeometry.prepare(
        prePayload.transparentCsgSnapshot.receiverSurfaceIndexedDrawItems,
        prePayload.frameBindings, scratchArena, intervalDependency, avboitIntervalResourceUses,
        inputs.timingTicket
    ))
        return MakeUnexpected(Failure{});

    Core::GpuTaskSchedulingHint avboitIntervalScheduling;
    avboitIntervalScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitIntervalScheduling.forceSubmissionBoundary = false;
    avboitIntervalScheduling.allowPacketMerge = true;
    avboitIntervalScheduling.mergeWithPrevious = prePayload.transparentCsgStreamsUploaded;
    Core::GpuTaskDesc avboitIntervalDesc;
    avboitIntervalDesc
        .setIdentity(Name(s_RenderNameRenderAvboitIntervals))
        .setMarkerLabel(RendererGpuPassText::s_TransparentCsgIntervalsLabel)
        .setScheduling(avboitIntervalScheduling)
        .setDependencies({ &intervalDependency, 1u })
        .setResourceUses(avboitIntervalResourceUses)
        .setResourceSetUses({ transparentCsgMaterialResourceSetUseCount != 0u ? transparentCsgMaterialResourceSetUses.data() : nullptr, transparentCsgMaterialResourceSetUseCount })
    ;
    const b8 avboitCsgReceiverSpanGraphOwned =
        prePayload.transparentCsgStreamsUploaded
        && prePayload.transparentCsgSnapshot.captured
        && receiverSpanPayload.transparentCsgSnapshot.captured
        && receiverSpanPayload.csgFrameBuffersUploaded
    ;
    const b8 avboitCsgIntervalCombineGraphOwned = avboitCsgReceiverSpanGraphOwned && intervalCombinePayload.transparentCsgSnapshot.captured && intervalCombinePayload.csgFrameBuffersUploaded;
    const auto preTaskResult = m_graph.addTask<AvboitPreGraphTask>(avboitIntervalDesc, Move(prePayload));
    if(!preTaskResult){
        m_avboitSystem.taskGraphStage().m_preTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph task"));
        return MakeUnexpected(Failure{});
    }
    m_avboitSystem.taskGraphStage().m_preTask = *preTaskResult;

    Core::GpuTaskId avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_preTask;
    b8 avboitIntervalOutputsGraphOwned = false;
    if(avboitCsgReceiverSpanGraphOwned){
        const Array<Core::GpuTaskResourceUse, 6u> avboitIntervalSpanResourceUses = {
            ReadTextureUse(inputs.csgReceiverEventData, inputs.csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgReceiverEventCount, inputs.csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess),
            ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            WriteTextureUse(inputs.csgReceiverSpanData, inputs.csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgReceiverSpanCount, inputs.csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskSchedulingHint avboitIntervalSpanScheduling;
        avboitIntervalSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalSpanScheduling.forceSubmissionBoundary = false;
        avboitIntervalSpanScheduling.allowPacketMerge = true;
        avboitIntervalSpanScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalSpanDesc;
        avboitIntervalSpanDesc
            .setIdentity(Name(s_RenderNameRenderAvboitTransparentCsgReceiverSpan))
            .setMarkerLabel("Transparent CSG Receiver Span")
            .setScheduling(avboitIntervalSpanScheduling)
            .setDependencies({ &m_avboitSystem.taskGraphStage().m_preTask, 1u })
            .setResourceUses(avboitIntervalSpanResourceUses)
        ;
        const auto csgReceiverSpanTaskResult = m_graph.addTask< ECSRenderDetail::AvboitCsgReceiverSpanGraphTask >(avboitIntervalSpanDesc, Move(receiverSpanPayload));
        if(!csgReceiverSpanTaskResult){
            m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-span graph task"));
            return MakeUnexpected(Failure{});
        }
        m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask = *csgReceiverSpanTaskResult;
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        const Array<Core::GpuTaskResourceUse, 11u> avboitIntervalCombineResourceUses = {
            ReadTextureUse(inputs.csgCapBackNormal, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgIntervalDepth, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgIntervalId, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgReceiverSpanData, inputs.csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgReceiverSpanCount, inputs.csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess),
            ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            WriteTextureUse(inputs.csgRemovedIntervalDepth, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgRemovedIntervalCapNormal, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgRemovedIntervalData, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgRemovedIntervalCount, inputs.csgRemovedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskSchedulingHint avboitIntervalCombineScheduling;
        avboitIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalCombineScheduling.forceSubmissionBoundary = false;
        avboitIntervalCombineScheduling.allowPacketMerge = true;
        avboitIntervalCombineScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalCombineDesc;
        avboitIntervalCombineDesc
            .setIdentity(Name(s_RenderNameRenderAvboitTransparentCsgIntervalCombine))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setScheduling(avboitIntervalCombineScheduling)
            .setDependencies({ &avboitIntervalCompletionTask, 1u })
            .setResourceUses(avboitIntervalCombineResourceUses)
        ;
        const auto csgIntervalCombineTaskResult = m_graph.addTask< ECSRenderDetail::AvboitCsgIntervalCombineGraphTask >(avboitIntervalCombineDesc, Move(intervalCombinePayload));
        if(!csgIntervalCombineTaskResult){
            m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-combine graph task"));
            return MakeUnexpected(Failure{});
        }
        m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask = *csgIntervalCombineTaskResult;
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask;
        avboitIntervalOutputsGraphOwned = true;
    }
    result.intervalCompletionTask = avboitIntervalCompletionTask;
    result.intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
