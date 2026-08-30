// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>

#include <impl/assets/graphics/csg/constants.h>

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <impl/ecs_csg/module.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_MinClipWForWorkRegion = static_cast<f32>(NWB_CSG_HOMOGENEOUS_W_EPSILON);
static constexpr i32 s_WorkRegionPixelPadding = 2;

namespace CsgClipCutterResolveResult{
    enum Enum : u8{
        Skipped,
        Ready
    };
};

struct CsgResolvedClipCutter{
    SIMDMatrix worldToShape;
    SIMDVector workMinBounds;
    SIMDVector workMaxBounds;
    CsgShapeTypeInfo shapeType;
    const CsgCutterComponent* cutter = nullptr;
    const u8* parameterBytes = nullptr;
    usize parameterByteSize = 0u;
    bool workBoundsValid = false;
};

struct CsgCutterTransforms{
    SIMDMatrix shapeToWorld;
    SIMDMatrix worldToShape;
};

[[nodiscard]] static SIMDVector ComputeWorldToShapeScaleBound(const SIMDMatrix& worldToShape){
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector lengthSquared = VectorAdd(Vector3LengthSq(row0), Vector3LengthSq(row1));
    lengthSquared = VectorAdd(lengthSquared, Vector3LengthSq(row2));
    return VectorSqrt(lengthSquared);
}

[[nodiscard]] static bool ResolveCsgCutterParameterBytes(
    const CsgShapeTypeInfo& shapeType,
    const CsgCutterComponent& cutter,
    const u8*& outParameterBytes,
    usize& outParameterByteSize
){
    if(cutter.parameterBytes.empty()){
        outParameterBytes = shapeType.desc.defaultParameterBytes.empty() ? nullptr : shapeType.desc.defaultParameterBytes.data();
        outParameterByteSize = shapeType.desc.defaultParameterBytes.size();
    }else{
        outParameterBytes = cutter.parameterBytes.data();
        outParameterByteSize = cutter.parameterBytes.size();
    }

    return outParameterByteSize == static_cast<usize>(shapeType.desc.parameterByteSize)
        && (outParameterByteSize == 0u || outParameterBytes)
    ;
}

static void CopyCsgCutterInlineParameters(
    const u8* parameterBytes,
    const usize parameterByteSize,
    CsgCutterGpuData& inOutCutter
){
    inOutCutter.parameter0 = Float4(0.f, 0.f, 0.f, 0.f);
    inOutCutter.parameter1 = Float4(0.f, 0.f, 0.f, 0.f);
    if(!parameterBytes)
        return;

    const usize parameter0Bytes = Min(parameterByteSize, sizeof(Float4));
    if(parameter0Bytes > 0u)
        NWB_MEMCPY(&inOutCutter.parameter0, sizeof(Float4), parameterBytes, parameter0Bytes);

    if(parameterByteSize <= sizeof(Float4))
        return;

    const usize parameter1Bytes = Min(parameterByteSize - sizeof(Float4), sizeof(Float4));
    if(parameter1Bytes > 0u)
        NWB_MEMCPY(&inOutCutter.parameter1, sizeof(Float4), parameterBytes + sizeof(Float4), parameter1Bytes);
}

[[nodiscard]] static bool BuildCsgReceiverWorldToLocal(
    const SIMDMatrix* localToWorld,
    SIMDMatrix& outWorldToLocal
){
    if(!localToWorld){
        outWorldToLocal = MatrixIdentity();
        return true;
    }

    SIMDVector determinant;
    outWorldToLocal = MatrixInverse(&determinant, *localToWorld);
    return VectorIsFinite(determinant, VectorComponentMask::s_XYZW) && Vector4Greater(VectorAbs(determinant), VectorZero());
}

[[nodiscard]] static bool BuildCsgReceiverWorldBounds(
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    const SIMDMatrix* localToWorld,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
){
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return false;

    if(!localToWorld){
        outMinBounds = localMinBounds;
        outMaxBounds = localMaxBounds;
        return true;
    }

    return AabbTests::Transform(*localToWorld, localMinBounds, localMaxBounds, outMinBounds, outMaxBounds);
}

struct CsgReceiverLocalSpace{
    SIMDVector localMinBounds = VectorZero();
    SIMDVector localMaxBounds = VectorZero();
    SIMDMatrix localToWorld = MatrixIdentity();
    bool boundsCanCull = false;
    bool hasLocalToWorld = false;

    [[nodiscard]] const SIMDMatrix* localToWorldPtr()const{
        return hasLocalToWorld ? &localToWorld : nullptr;
    }
};

[[nodiscard]] static CsgReceiverLocalSpace BuildCsgReceiverLocalSpace(
    const bool boundsCanCull,
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    const SIMDMatrix* localToWorld
){
    CsgReceiverLocalSpace localSpace;
    localSpace.boundsCanCull = boundsCanCull;
    if(boundsCanCull){
        localSpace.localMinBounds = localMinBounds;
        localSpace.localMaxBounds = localMaxBounds;
    }

    if(localToWorld){
        localSpace.localToWorld = *localToWorld;
        localSpace.hasLocalToWorld = true;
    }
    return localSpace;
}

[[nodiscard]] static CsgReceiverLocalSpace BuildCsgReceiverLocalSpace(
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform
){
    const bool boundsCanCull = CsgReceiverBoundsCanCull(receiverBounds);
    const SIMDVector localMinBounds = boundsCanCull ? LoadFloatInt(receiverBounds.minBounds) : VectorZero();
    const SIMDVector localMaxBounds = boundsCanCull ? LoadFloatInt(receiverBounds.maxBounds) : VectorZero();
    SIMDMatrix localToWorld;
    const SIMDMatrix* localToWorldPtr = nullptr;
    if(transform){
        localToWorld = MatrixAffineTransformation(
            LoadFloat(transform->scale),
            VectorZero(),
            LoadFloat(transform->rotation),
            LoadFloat(transform->position)
        );
        localToWorldPtr = &localToWorld;
    }
    return BuildCsgReceiverLocalSpace(boundsCanCull, localMinBounds, localMaxBounds, localToWorldPtr);
}

static void ExpandCsgFrameWorkRegionForWorldBounds(
    CsgFrameGpuData& csgFrameData,
    const SIMDMatrix& worldToClip,
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    const u32 frameWidth,
    const u32 frameHeight
){
    if(frameWidth == 0u || frameHeight == 0u || !AabbTests::Valid(minBounds, maxBounds)){
        csgFrameData.workRegion.expandFull();
        return;
    }

    const SIMDVector frameExtent = VectorSet(
        static_cast<f32>(frameWidth),
        static_cast<f32>(frameHeight),
        0.0f,
        0.0f
    );
    SIMDVector minPixel = frameExtent;
    SIMDVector maxPixel = VectorZero();
    for(u32 corner = 0u; corner < 8u; ++corner){
        const SIMDVector cornerSelect = VectorSelectControl(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u, 0u);
        const SIMDVector worldPosition = VectorSetW(VectorSelect(minBounds, maxBounds, cornerSelect), 1.0f);
        const SIMDVector clipPosition = Vector4Transform(worldPosition, worldToClip);
        const SIMDVector clipW = VectorSplatW(clipPosition);
        if(
            !VectorIsFinite(clipW, VectorComponentMask::s_XYZW)
            || !Vector4Greater(clipW, VectorReplicate(s_MinClipWForWorkRegion))
        ){
            csgFrameData.workRegion.expandFull();
            return;
        }

        const SIMDVector ndcPosition = VectorDivide(clipPosition, clipW);
        if(!VectorIsFinite(ndcPosition, VectorComponentMask::s_XY)){
            csgFrameData.workRegion.expandFull();
            return;
        }

        SIMDVector normalizedPosition = VectorAdd(
            VectorMultiply(ndcPosition, s_SIMDOneHalf),
            s_SIMDOneHalf
        );
        normalizedPosition = VectorSelect(
            normalizedPosition,
            VectorSubtract(s_SIMDOne, normalizedPosition),
            s_SIMDMaskY
        );
        const SIMDVector pixelPosition = VectorAndInt(
            VectorMultiply(normalizedPosition, frameExtent),
            s_SIMDMaskXY
        );
        minPixel = VectorMin(minPixel, pixelPosition);
        maxPixel = VectorMax(maxPixel, pixelPosition);
    }

    const f32 minPixelX = VectorGetX(minPixel);
    const f32 minPixelY = VectorGetY(minPixel);
    const f32 maxPixelX = VectorGetX(maxPixel);
    const f32 maxPixelY = VectorGetY(maxPixel);

    if(maxPixelX < 0.0f || maxPixelY < 0.0f || minPixelX > static_cast<f32>(frameWidth) || minPixelY > static_cast<f32>(frameHeight))
        return;

    csgFrameData.workRegion.expandClamped(
        static_cast<i32>(Floor(minPixelX)) - s_WorkRegionPixelPadding,
        static_cast<i32>(Ceil(maxPixelX)) + s_WorkRegionPixelPadding,
        static_cast<i32>(Floor(minPixelY)) - s_WorkRegionPixelPadding,
        static_cast<i32>(Ceil(maxPixelY)) + s_WorkRegionPixelPadding,
        frameWidth,
        frameHeight
    );
}

static void BuildResolvedClipCutterGpuData(
    const CsgResolvedClipCutter& resolvedCutter,
    const f32 worldToShapeScaleBound,
    CsgCutterGpuData& outCutter
){
    NWB_ASSERT(resolvedCutter.cutter);
    NWB_ASSERT(resolvedCutter.parameterByteSize == static_cast<usize>(resolvedCutter.shapeType.desc.parameterByteSize));
    NWB_ASSERT(resolvedCutter.parameterByteSize == 0u || resolvedCutter.parameterBytes);

    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    outCutter.shapeType = resolvedCutter.shapeType.id;
    CopyCsgCutterInlineParameters(resolvedCutter.parameterBytes, resolvedCutter.parameterByteSize, outCutter);
}

[[nodiscard]] static bool ReserveCsgStructuredBuffer(
    Core::Graphics& graphics,
    Core::BufferHandle& buffer,
    usize& inOutCapacity,
    const usize requiredCount,
    const usize elementByteSize,
    const Name& debugName
){
    if(requiredCount == 0u)
        return true;
    if(buffer && inOutCapacity >= requiredCount)
        return true;
    if(elementByteSize == 0u || requiredCount > Limit<usize>::s_Max / elementByteSize)
        return false;

    const usize capacity = ::NextGrowingCapacity(inOutCapacity, requiredCount);
    if(capacity > Limit<usize>::s_Max / elementByteSize)
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity * elementByteSize))
        .setStructStride(static_cast<u32>(elementByteSize))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(debugName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = graphics.createBuffer(bufferDesc);
    if(!createdBuffer)
        return false;

    buffer = Move(createdBuffer);
    inOutCapacity = capacity;
    return true;
}


[[nodiscard]] static CsgClipCutterResolveResult::Enum ResolveReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    const SIMDMatrix& cutterShapeToWorld,
    const SIMDMatrix& cutterWorldToShape,
    const SIMDVector receiverLocalMinBounds,
    const SIMDVector receiverLocalMaxBounds,
    const bool receiverBoundsCanCull,
    const SIMDMatrix* receiverLocalToWorld,
    CsgResolvedClipCutter& outCutter
){
    outCutter = CsgResolvedClipCutter{};
    if(!shapeRegistry.findShapeType(cutter.shapeType, outCutter.shapeType))
        return CsgClipCutterResolveResult::Skipped;
    if(!ResolveCsgCutterParameterBytes(outCutter.shapeType, cutter, outCutter.parameterBytes, outCutter.parameterByteSize))
        return CsgClipCutterResolveResult::Skipped;
    outCutter.cutter = &cutter;
    outCutter.worldToShape = cutterWorldToShape;

    SIMDVector receiverMinBounds;
    SIMDVector receiverMaxBounds;
    if(!receiverBoundsCanCull)
        return CsgClipCutterResolveResult::Ready;
    if(!BuildCsgReceiverWorldBounds(
        receiverLocalMinBounds,
        receiverLocalMaxBounds,
        receiverLocalToWorld,
        receiverMinBounds,
        receiverMaxBounds
    ))
        return CsgClipCutterResolveResult::Ready;

    SIMDVector cutterMinBounds;
    SIMDVector cutterMaxBounds;
    bool finiteBounds = false;
    if(!shapeRegistry.buildShapeBounds(
        outCutter.shapeType.id,
        cutterShapeToWorld,
        outCutter.parameterBytes,
        outCutter.parameterByteSize,
        cutterMinBounds,
        cutterMaxBounds,
        finiteBounds
    ))
        return CsgClipCutterResolveResult::Skipped;
    if(!finiteBounds){
        outCutter.workMinBounds = receiverMinBounds;
        outCutter.workMaxBounds = receiverMaxBounds;
        outCutter.workBoundsValid = true;
        return CsgClipCutterResolveResult::Ready;
    }

    if(!AabbTests::Intersects(receiverMinBounds, receiverMaxBounds, cutterMinBounds, cutterMaxBounds))
        return CsgClipCutterResolveResult::Skipped;

    outCutter.workMinBounds = VectorMax(receiverMinBounds, cutterMinBounds);
    outCutter.workMaxBounds = VectorMin(receiverMaxBounds, cutterMaxBounds);
    outCutter.workBoundsValid = AabbTests::Valid(outCutter.workMinBounds, outCutter.workMaxBounds);
    return CsgClipCutterResolveResult::Ready;
}


template<typename CutterTransformLoader, typename CutterHandler>
[[nodiscard]] static bool ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const SIMDVector receiverLocalMinBounds,
    const SIMDVector receiverLocalMaxBounds,
    const bool receiverBoundsCanCull,
    const SIMDMatrix* receiverLocalToWorld,
    CutterTransformLoader&& loadCutterTransforms,
    CutterHandler&& handler
){
    bool resolved = true;
    receiverLookup.forEachReceiverCutter(
        receiverDrawState,
        [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
            if(!resolved)
                return;

            const CsgCutterTransforms cutterTransforms = loadCutterTransforms(cutter);
            CsgResolvedClipCutter resolvedCutter;
            const CsgClipCutterResolveResult::Enum resolveResult = ResolveReceiverClipCutter(
                shapeRegistry,
                cutter,
                cutterTransforms.shapeToWorld,
                cutterTransforms.worldToShape,
                receiverLocalMinBounds,
                receiverLocalMaxBounds,
                receiverBoundsCanCull,
                receiverLocalToWorld,
                resolvedCutter
            );
            if(resolveResult == CsgClipCutterResolveResult::Skipped)
                return;
            NWB_ASSERT(resolveResult == CsgClipCutterResolveResult::Ready);

            if(!handler(resolvedCutter))
                resolved = false;
        }
    );
    return resolved;
}


[[nodiscard]] static bool AcquireCsgBufferHeapHandle(
    Core::Device& device,
    Core::Buffer& buffer,
    const Core::GpuDescriptorClass::Enum descriptorClass,
    Core::GpuDescriptorHandle& outHandle
){
    outHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    NWB_ASSERT(
        descriptorClass == Core::GpuDescriptorClass::StorageBuffer
        || descriptorClass == Core::GpuDescriptorClass::UniformBuffer
    );
    if(
        descriptorClass != Core::GpuDescriptorClass::StorageBuffer
        && descriptorClass != Core::GpuDescriptorClass::UniformBuffer
    )
        return false;
    const Core::DescriptorWriteItem descriptorWrite = descriptorClass == Core::GpuDescriptorClass::UniformBuffer
        ? Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer)
        : Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, &buffer)
    ;
    const Core::GpuDescriptorHandle acquired = heap.allocate(descriptorClass);
    if(!acquired.valid() || !heap.write(acquired, descriptorWrite)){
        if(acquired.valid())
            heap.free(acquired);
        return false;
    }

    outHandle = acquired;
    return true;
}


[[nodiscard]] static bool ReplaceCsgStorageBufferHeapHandle(
    Core::Device& device,
    Core::Buffer& buffer,
    Core::GpuDescriptorHandle& inOutHandle
){
    Core::GpuDescriptorHandle acquired;
    if(!AcquireCsgBufferHeapHandle(device, buffer, Core::GpuDescriptorClass::StorageBuffer, acquired)){
        // The backing buffer was replaced for capacity growth, so the existing descriptor must not survive as a
        // seemingly valid handle to retired storage. Leave the context explicitly unregistered for a later retry.
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        if(inOutHandle.valid() && heap.isInitialized())
            heap.free(inOutHandle);
        inOutHandle = Core::GpuDescriptorHandle::invalid();
        return false;
    }

    // Never rewrite a descriptor a previous command list can still reach. A capacity replacement gets a fresh slot;
    // free() retires the old descriptor only after the heap's in-flight quarantine has elapsed.
    if(inOutHandle.valid())
        device.getDescriptorHeap().free(inOutHandle);
    inOutHandle = acquired;
    return true;
}


[[nodiscard]] static bool EnsureCsgBufferHeapHandle(
    Core::Device& device,
    Core::Buffer& buffer,
    const Core::GpuDescriptorClass::Enum descriptorClass,
    Core::GpuDescriptorHandle& inOutHandle
){
    if(inOutHandle.valid())
        return inOutHandle.descriptorClass() == descriptorClass;
    return AcquireCsgBufferHeapHandle(device, buffer, descriptorClass, inOutHandle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ECSRenderDetail::CsgGraphResourceSnapshot::bindingValid()const noexcept{
    return
        receiverRanges
        && cutters
        && clipContextSlots
        && intervalSampleState
        && receiverRangeHeapHandle.valid()
        && receiverRangeHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && cutterHeapHandle.valid()
        && cutterHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && clipContextSlotsHeapHandle.valid()
        && clipContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && intervalSampleStateHeapHandle.valid()
        && intervalSampleStateHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

bool ECSRenderDetail::CsgGraphResourceSnapshot::frameReady(const CsgFrameGpuData& csgFrameData)const noexcept{
    if(!csgFrameData.hasWork())
        return true;

    return
        bindingValid()
        && receiverRangeCapacity >= csgFrameData.receiverRanges.size()
        && cutterCapacity >= csgFrameData.cutters.size()
    ;
}

bool ECSRenderDetail::CsgGraphResourceSnapshot::findClipContextHeapSlot(u32& outHeapSlot)const noexcept{
    outHeapSlot = 0u;
    if(!clipContextSlotsHeapHandle.valid())
        return false;

    outHeapSlot = clipContextSlotsHeapHandle.slot();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgClipResources(){
    auto& device = m_graphics.getDevice();
    if(!m_csgState.m_clipBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Mesh | Core::ShaderType::Compute | Core::ShaderType::Pixel);
        // This layout contains only the shared 64-byte mesh push ABI used by the cap-fill fullscreen path.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        m_csgState.m_clipBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_csgState.m_clipBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding layout"));
            return false;
        }
    }

    return true;
}

bool RendererCsgSystem::reserveCsgReceiverRangeBufferCapacity(const usize rangeCount){
    const usize oldCapacity = m_csgState.m_receiverRangeBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(
        m_graphics,
        m_csgState.m_receiverRangeBuffer,
        m_csgState.m_receiverRangeBufferCapacity,
        rangeCount,
        sizeof(CsgReceiverRangeGpuData),
        ECSRenderDetail::s_CsgReceiverRangeBufferName
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver range buffer"));
        return false;
    }

    if(m_csgState.m_receiverRangeBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(
        m_graphics.getDevice(),
        *m_csgState.m_receiverRangeBuffer.get(),
        m_csgState.m_receiverRangeBufferHeapHandle
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG receiver range buffer in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererCsgSystem::reserveCsgCutterBufferCapacity(const usize cutterCount){
    const usize oldCapacity = m_csgState.m_cutterBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(
        m_graphics,
        m_csgState.m_cutterBuffer,
        m_csgState.m_cutterBufferCapacity,
        cutterCount,
        sizeof(CsgCutterGpuData),
        ECSRenderDetail::s_CsgCutterBufferName
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cutter buffer"));
        return false;
    }

    if(m_csgState.m_cutterBufferCapacity != oldCapacity && !__hidden_csg_resources::ReplaceCsgStorageBufferHeapHandle(
        m_graphics.getDevice(),
        *m_csgState.m_cutterBuffer.get(),
        m_csgState.m_cutterBufferHeapHandle
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register replacement CSG cutter buffer in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererCsgSystem::prepareCsgFrameResources(const usize receiverRangeCount, const usize cutterCount){
    if(receiverRangeCount == 0u || cutterCount == 0u)
        return true;
    NWB_ASSERT(receiverRangeCount <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(cutterCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(
        !reserveCsgReceiverRangeBufferCapacity(receiverRangeCount)
        || !reserveCsgCutterBufferCapacity(cutterCount)
    )
        return false;
    if(!createCsgIntervalSampleStateBuffer())
        return false;
    if(!m_csgState.m_clipContextSlotsBuffer){
        Core::BufferDesc bufferDesc;
        bufferDesc
            .setByteSize(sizeof(CsgClipContextSlots))
            .setIsConstantBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName("engine/csg/clip_context_slots")
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_csgState.m_clipContextSlotsBuffer = m_graphics.createBuffer(bufferDesc);
        if(!m_csgState.m_clipContextSlotsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip-context slot buffer"));
            return false;
        }
    }
    if(
        !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_receiverRangeBuffer.get(),
            Core::GpuDescriptorClass::StorageBuffer,
            m_csgState.m_receiverRangeBufferHeapHandle
        )
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_cutterBuffer.get(),
            Core::GpuDescriptorClass::StorageBuffer,
            m_csgState.m_cutterBufferHeapHandle
        )
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_clipContextSlotsBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            m_csgState.m_clipContextSlotsHeapHandle
        )
        || !__hidden_csg_resources::EnsureCsgBufferHeapHandle(
            m_graphics.getDevice(),
            *m_csgState.m_intervalSampleStateBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            m_csgState.m_intervalSampleStateHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG clip context heap registration is incomplete"));
        return false;
    }
    if(!createCsgClipResources())
        return false;

    // Keep the setup contract explicit: draw paths consume these handles only through an immutable graph snapshot.
    NWB_ASSERT(m_csgState.m_receiverRangeBufferCapacity >= receiverRangeCount);
    NWB_ASSERT(m_csgState.m_cutterBufferCapacity >= cutterCount);
    NWB_ASSERT(m_csgState.m_receiverRangeBufferHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_cutterBufferHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_clipContextSlotsHeapHandle.valid());
    NWB_ASSERT(m_csgState.m_intervalSampleStateHeapHandle.valid());
    return true;
}

ECSRenderDetail::CsgGraphResourceSnapshot RendererCsgSystem::csgGraphResourceSnapshot()const{
    return {
        .receiverRanges = m_csgState.m_receiverRangeBuffer,
        .cutters = m_csgState.m_cutterBuffer,
        .clipContextSlots = m_csgState.m_clipContextSlotsBuffer,
        .intervalSampleState = m_csgState.m_intervalSampleStateBuffer,
        .receiverRangeCapacity = m_csgState.m_receiverRangeBufferCapacity,
        .cutterCapacity = m_csgState.m_cutterBufferCapacity,
        .receiverRangeHeapHandle = m_csgState.m_receiverRangeBufferHeapHandle,
        .cutterHeapHandle = m_csgState.m_cutterBufferHeapHandle,
        .clipContextSlotsHeapHandle = m_csgState.m_clipContextSlotsHeapHandle,
        .intervalSampleStateHeapHandle = m_csgState.m_intervalSampleStateHeapHandle,
    };
}

bool RendererCsgSystem::prepareCsgClipContextSlotData(
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    CsgClipContextSlots& outContextSlots
)const{
    outContextSlots = CsgClipContextSlots{};
    if(!csgFrameData.hasWork())
        return true;
    if(
        !csgResources.frameReady(csgFrameData)
        || !frameBindings.bindingValid()
        || !targets.bindless.slotsBufferDescriptor.valid()
    )
        return false;

    // Buffer selection, descriptor registration, and target selection have all completed before graph declaration.
    // Capture every indirection now so a later native record never observes handles from a different generation.
    outContextSlots.receiverRanges = csgResources.receiverRangeHeapHandle.slot();
    outContextSlots.cutters = csgResources.cutterHeapHandle.slot();
    outContextSlots.materialTyped = frameBindings.materialTypedHeapHandle.slot();
    outContextSlots.meshInstances = frameBindings.instanceHeapHandle.slot();
    outContextSlots.deferredBindlessResources = targets.bindless.slotsBufferDescriptor.slot();
    outContextSlots.intervalSampleState = csgResources.intervalSampleStateHeapHandle.slot();
    return true;
}

void RendererCsgSystem::setCsgReceiverSurfaceImageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    commandList.setTextureState(targets.csgReceiverEventData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgReceiverEventCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}

void RendererCsgSystem::setCsgIntervalSampleImageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets){
    commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.csgRemovedIntervalCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
}

void RendererCsgSystem::setCsgClipBufferStates(
    Core::CommandList& commandList,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources
){
    commandList.setBufferState(csgResources.receiverRanges.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(csgResources.cutters.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(csgResources.clipContextSlots.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(csgResources.intervalSampleState.get(), Core::ResourceStates::ConstantBuffer);
}

bool RendererCsgSystem::resolveCsgReceiverClipDrawInfo(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    CsgReceiverClipDrawInfo& outInfo
)const{
    outInfo = CsgReceiverClipDrawInfo{};
    const __hidden_csg_resources::CsgReceiverLocalSpace receiverLocalSpace =
        __hidden_csg_resources::BuildCsgReceiverLocalSpace(receiverBounds, transform)
    ;

    return __hidden_csg_resources::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverLocalSpace.localMinBounds,
        receiverLocalSpace.localMaxBounds,
        receiverLocalSpace.boundsCanCull,
        receiverLocalSpace.localToWorldPtr(),
        [](const CsgCutterComponent& cutter){
            return __hidden_csg_resources::CsgCutterTransforms{
                LoadFloat(cutter.shapeToWorld),
                LoadFloat(cutter.worldToShape)
            };
        },
        [&](const __hidden_csg_resources::CsgResolvedClipCutter& resolvedCutter){
            if(resolvedCutter.shapeType.desc.shaderModule){
                if(!outInfo.evaluatorVariant)
                    outInfo.evaluatorVariant = resolvedCutter.shapeType.desc.shaderModule;
                else if(outInfo.evaluatorVariant != resolvedCutter.shapeType.desc.shaderModule){
                    return false;
                }
            }
            if(outInfo.cutterCount < Limit<u32>::s_Max)
                ++outInfo.cutterCount;
            return true;
        }
    );
}

bool RendererCsgSystem::appendCsgReceiverClipData(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    const u32 frameWidth,
    const u32 frameHeight,
    CsgFrameGpuData& csgFrameData,
    CsgReceiverRangeGpuData& outRange,
    const ECSRenderDetail::MeshViewGpuData* const csgWorkRegionMeshViewState
)const{
    outRange = CsgReceiverRangeGpuData{};
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    if(!receiverBounds.valid())
        return false;
    const __hidden_csg_resources::CsgReceiverLocalSpace receiverLocalSpace =
        __hidden_csg_resources::BuildCsgReceiverLocalSpace(receiverBounds, transform)
    ;

    SIMDMatrix worldToReceiver;
    if(!__hidden_csg_resources::BuildCsgReceiverWorldToLocal(receiverLocalSpace.localToWorldPtr(), worldToReceiver))
        return false;

    bool meshViewReady = false;
    SIMDMatrix worldToClip;
    if(csgWorkRegionMeshViewState){
        worldToClip = LoadFloat(csgWorkRegionMeshViewState->worldToClip);
        meshViewReady = !MatrixIsNaN(worldToClip) && !MatrixIsInfinite(worldToClip);
    }
    else{
        Float44 acceptedWorldToClip = {};
        if(m_meshSystem.snapshotAcceptedMeshViewWorldToClip(acceptedWorldToClip)){
            worldToClip = LoadFloat(acceptedWorldToClip);
            meshViewReady = !MatrixIsNaN(worldToClip) && !MatrixIsInfinite(worldToClip);
        }
    }

    StoreFloat(worldToReceiver, &outRange.worldToReceiver);
    outRange.localBounds = receiverBounds;
    outRange.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    const bool appended = __hidden_csg_resources::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverLocalSpace.localMinBounds,
        receiverLocalSpace.localMaxBounds,
        receiverLocalSpace.boundsCanCull,
        receiverLocalSpace.localToWorldPtr(),
        [](const CsgCutterComponent& cutter){
            return __hidden_csg_resources::CsgCutterTransforms{
                LoadFloat(cutter.shapeToWorld),
                LoadFloat(cutter.worldToShape)
            };
        },
        [&](const __hidden_csg_resources::CsgResolvedClipCutter& resolvedCutter){
            CsgCutterGpuData cutterGpuData;
            if(csgFrameData.cutters.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                return false;
            }
            const SIMDMatrix worldToShape = resolvedCutter.worldToShape;
            cutterGpuData = CsgCutterGpuData{};
            StoreFloat(worldToShape, &cutterGpuData.worldToShape);
            __hidden_csg_resources::BuildResolvedClipCutterGpuData(
                resolvedCutter,
                VectorGetX(__hidden_csg_resources::ComputeWorldToShapeScaleBound(worldToShape)),
                cutterGpuData
            );

            if(meshViewReady && resolvedCutter.workBoundsValid){
                __hidden_csg_resources::ExpandCsgFrameWorkRegionForWorldBounds(
                    csgFrameData,
                    worldToClip,
                    resolvedCutter.workMinBounds,
                    resolvedCutter.workMaxBounds,
                    frameWidth,
                    frameHeight
                );
            }
            else{
                csgFrameData.workRegion.expandFull();
            }

            csgFrameData.cutters.push_back(cutterGpuData);
            ++outRange.cutterCount;
            return true;
        }
    );

    return appended && outRange.cutterCount > 0u;
}


void RendererCsgSystem::releaseCsgClipContextHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(m_csgState.m_receiverRangeBufferHeapHandle);
        heap.free(m_csgState.m_cutterBufferHeapHandle);
        heap.free(m_csgState.m_clipContextSlotsHeapHandle);
        heap.free(m_csgState.m_intervalSampleStateHeapHandle);
    }
    m_csgState.m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_csgState.m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_csgState.m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_csgState.m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

