// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "renderer_capacity_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CsgClipCutterResolveResult{
enum Enum : u8{
    Skipped,
    Ready
};
};

struct CsgResolvedClipCutter{
    const CsgCutterComponent* cutter = nullptr;
    CsgShapeTypeInfo shapeType;
    const u8* parameterBytes = nullptr;
    usize parameterByteSize = 0u;
    SIMDMatrix worldToShape;
    SIMDVector workMinBounds;
    SIMDVector workMaxBounds;
    bool workBoundsValid = false;
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
    return VectorIsFinite(determinant, 0xFu) && Vector4Greater(VectorAbs(determinant), VectorZero());
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

    constexpr i32 s_WorkRegionPixelPadding = 2;
    f32 minPixelX = static_cast<f32>(frameWidth);
    f32 minPixelY = static_cast<f32>(frameHeight);
    f32 maxPixelX = 0.0f;
    f32 maxPixelY = 0.0f;
    for(u32 corner = 0u; corner < 8u; ++corner){
        const SIMDVector cornerSelect = VectorSelectControl(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u, 0u);
        const SIMDVector worldPosition = VectorSetW(VectorSelect(minBounds, maxBounds, cornerSelect), 1.0f);
        const SIMDVector clipPosition = Vector4Transform(worldPosition, worldToClip);
        const f32 clipW = VectorGetW(clipPosition);
        if(!IsFinite(clipW) || clipW <= 0.000001f){
            csgFrameData.workRegion.expandFull();
            return;
        }

        const f32 ndcX = VectorGetX(clipPosition) / clipW;
        const f32 ndcY = VectorGetY(clipPosition) / clipW;
        if(!IsFinite(ndcX) || !IsFinite(ndcY)){
            csgFrameData.workRegion.expandFull();
            return;
        }

        const f32 pixelX = (ndcX * 0.5f + 0.5f) * static_cast<f32>(frameWidth);
        const f32 pixelY = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<f32>(frameHeight);
        minPixelX = Min(minPixelX, pixelX);
        minPixelY = Min(minPixelY, pixelY);
        maxPixelX = Max(maxPixelX, pixelX);
        maxPixelY = Max(maxPixelY, pixelY);
    }

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

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(inOutCapacity, requiredCount);
    if(capacity > Limit<usize>::s_Max / elementByteSize)
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity * elementByteSize))
        .setStructStride(static_cast<u32>(elementByteSize))
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
    const bool receiverBoundsValid,
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
    if(!receiverBoundsValid)
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

template<typename CutterHandler>
[[nodiscard]] static bool ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const Core::ECS::EntityID entity,
    const SIMDVector receiverLocalMinBounds,
    const SIMDVector receiverLocalMaxBounds,
    const bool receiverBoundsValid,
    const SIMDMatrix* receiverLocalToWorld,
    CutterHandler&& handler
){
    bool resolved = true;
    receiverLookup.forEachReceiverCutter(
        entity,
        [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
            if(!resolved)
                return;

            const SIMDMatrix cutterShapeToWorld = LoadFloat(cutter.shapeToWorld);
            const SIMDMatrix cutterWorldToShape = LoadFloat(cutter.worldToShape);
            CsgResolvedClipCutter resolvedCutter;
            const CsgClipCutterResolveResult::Enum resolveResult = ResolveReceiverClipCutter(
                shapeRegistry,
                cutter,
                cutterShapeToWorld,
                cutterWorldToShape,
                receiverLocalMinBounds,
                receiverLocalMaxBounds,
                receiverBoundsValid,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgClipResources(){
    auto* device = graphics().getDevice();
    if(!csgState().m_clipBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Mesh | Core::ShaderType::Compute | Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CSG_BINDING_RECEIVER_RANGES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CUTTERS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        csgState().m_clipBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!csgState().m_clipBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding layout"));
            return false;
        }
    }

    if(csgState().m_clipBindingSet)
        return true;
    if(!csgState().m_receiverRangeBuffer || !csgState().m_cutterBuffer)
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(
        NWB_CSG_BINDING_RECEIVER_RANGES,
        csgState().m_receiverRangeBuffer.get()
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CUTTERS, csgState().m_cutterBuffer.get()));

    csgState().m_clipBindingSet = device->createBindingSet(bindingSetDesc, csgState().m_clipBindingLayout);
    if(!csgState().m_clipBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding set"));
        return false;
    }

    return true;
}

void RendererCsgSystem::destroyCsgClipBindingSet(){
    csgState().m_clipBindingSet.reset();
}

bool RendererCsgSystem::reserveCsgReceiverRangeBufferCapacity(const usize rangeCount){
    const usize oldCapacity = csgState().m_receiverRangeBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(
        graphics(),
        csgState().m_receiverRangeBuffer,
        csgState().m_receiverRangeBufferCapacity,
        rangeCount,
        sizeof(CsgReceiverRangeGpuData),
        ECSRenderDetail::s_CsgReceiverRangeBufferName
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver range buffer"));
        return false;
    }

    if(csgState().m_receiverRangeBufferCapacity != oldCapacity)
        destroyCsgClipBindingSet();
    return true;
}

bool RendererCsgSystem::reserveCsgCutterBufferCapacity(const usize cutterCount){
    const usize oldCapacity = csgState().m_cutterBufferCapacity;
    if(!__hidden_csg_resources::ReserveCsgStructuredBuffer(
        graphics(),
        csgState().m_cutterBuffer,
        csgState().m_cutterBufferCapacity,
        cutterCount,
        sizeof(CsgCutterGpuData),
        ECSRenderDetail::s_CsgCutterBufferName
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cutter buffer"));
        return false;
    }

    if(csgState().m_cutterBufferCapacity != oldCapacity)
        destroyCsgClipBindingSet();
    return true;
}

bool RendererCsgSystem::prepareCsgFrameBuffers(const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasWork())
        return true;
    if(
        !reserveCsgReceiverRangeBufferCapacity(csgFrameData.receiverRanges.size())
        || !reserveCsgCutterBufferCapacity(csgFrameData.cutters.size())
    )
        return false;
    if(!csgState().m_clipBindingSet && !createCsgClipResources())
        return false;

    return true;
}

bool RendererCsgSystem::csgFrameBuffersReady(const CsgFrameGpuData& csgFrameData)const{
    if(!csgFrameData.hasWork())
        return true;

    return
        csgState().m_receiverRangeBuffer
        && csgState().m_receiverRangeBufferCapacity >= csgFrameData.receiverRanges.size()
        && csgState().m_cutterBuffer
        && csgState().m_cutterBufferCapacity >= csgFrameData.cutters.size()
        && csgState().m_clipBindingSet
    ;
}

bool RendererCsgSystem::uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasWork())
        return true;
    NWB_ASSERT(csgState().m_receiverRangeBuffer);
    NWB_ASSERT(csgState().m_cutterBuffer);
    NWB_ASSERT(csgState().m_receiverRangeBufferCapacity >= csgFrameData.receiverRanges.size());
    NWB_ASSERT(csgState().m_cutterBufferCapacity >= csgFrameData.cutters.size());
    NWB_ASSERT(csgState().m_clipBindingSet);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CsgUpload, graphics().getDevice(), commandList);

    commandList.setBufferState(csgState().m_receiverRangeBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(csgState().m_cutterBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        csgState().m_receiverRangeBuffer.get(),
        csgFrameData.receiverRanges.data(),
        csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    commandList.writeBuffer(
        csgState().m_cutterBuffer.get(),
        csgFrameData.cutters.data(),
        csgFrameData.cutters.size() * sizeof(CsgCutterGpuData)
    );
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();
    return true;
}

void RendererCsgSystem::setCsgClipBufferStates(Core::CommandList& commandList){
    commandList.setBufferState(csgState().m_receiverRangeBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(csgState().m_cutterBuffer.get(), Core::ResourceStates::ShaderResource);
}

bool RendererCsgSystem::resolveCsgReceiverClipDrawInfo(
    const CsgFrameReceiverLookup& receiverLookup,
    const Core::ECS::EntityID entity,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    CsgReceiverClipDrawInfo& outInfo
)const{
    outInfo = CsgReceiverClipDrawInfo{};
    const bool receiverBoundsValid = receiverBounds.valid();
    const SIMDVector receiverLocalMinBounds = receiverBoundsValid ? LoadFloatInt(receiverBounds.minBounds) : VectorZero();
    const SIMDVector receiverLocalMaxBounds = receiverBoundsValid ? LoadFloatInt(receiverBounds.maxBounds) : VectorZero();

    SIMDMatrix receiverLocalToWorld;
    const SIMDMatrix* receiverLocalToWorldPtr = nullptr;
    if(transform){
        receiverLocalToWorld = MatrixAffineTransformation(
            LoadFloat(transform->scale),
            VectorZero(),
            LoadFloat(transform->rotation),
            LoadFloat(transform->position)
        );
        receiverLocalToWorldPtr = &receiverLocalToWorld;
    }

    return __hidden_csg_resources::ForEachReceiverClipCutter(
        csgShapeRegistry(),
        receiverLookup,
        entity,
        receiverLocalMinBounds,
        receiverLocalMaxBounds,
        receiverBoundsValid,
        receiverLocalToWorldPtr,
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
    const Core::ECS::EntityID entity,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    const u32 frameWidth,
    const u32 frameHeight,
    CsgFrameGpuData& csgFrameData,
    CsgReceiverRangeGpuData& outRange
)const{
    outRange = CsgReceiverRangeGpuData{};
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    const bool receiverBoundsValid = receiverBounds.valid();
    if(!receiverBoundsValid || !receiverBounds.finite())
        return false;
    const SIMDVector receiverLocalMinBounds = LoadFloatInt(receiverBounds.minBounds);
    const SIMDVector receiverLocalMaxBounds = LoadFloatInt(receiverBounds.maxBounds);

    SIMDMatrix receiverLocalToWorld;
    const SIMDMatrix* receiverLocalToWorldPtr = nullptr;
    if(transform){
        receiverLocalToWorld = MatrixAffineTransformation(
            LoadFloat(transform->scale),
            VectorZero(),
            LoadFloat(transform->rotation),
            LoadFloat(transform->position)
        );
        receiverLocalToWorldPtr = &receiverLocalToWorld;
    }

    SIMDMatrix worldToReceiver;
    if(!__hidden_csg_resources::BuildCsgReceiverWorldToLocal(receiverLocalToWorldPtr, worldToReceiver))
        return false;

    bool meshViewReady = false;
    SIMDMatrix worldToClip;
    if(drawState().m_meshViewGpuDataValid){
        ECSRenderDetail::MeshViewGpuData meshViewData;
        NWB_MEMCPY(&meshViewData, sizeof(meshViewData), drawState().m_meshViewGpuData, sizeof(meshViewData));
        worldToClip = LoadFloat(meshViewData.worldToClip);
        meshViewReady = !MatrixIsNaN(worldToClip) && !MatrixIsInfinite(worldToClip);
    }

    StoreFloat(worldToReceiver, &outRange.worldToReceiver);
    outRange.localBounds = receiverBounds;
    outRange.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    const bool appended = __hidden_csg_resources::ForEachReceiverClipCutter(
        csgShapeRegistry(),
        receiverLookup,
        entity,
        receiverLocalMinBounds,
        receiverLocalMaxBounds,
        receiverBoundsValid,
        receiverLocalToWorldPtr,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

