// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "renderer_capacity_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ParameterT>
[[nodiscard]] static bool LoadCsgCutterParameters(const CsgCutterComponent& cutter, ParameterT& outParameters){
    outParameters = ParameterT{};
    if(cutter.parameterBytes.empty())
        return true;
    if(cutter.parameterBytes.size() != sizeof(ParameterT))
        return false;

    NWB_MEMCPY(&outParameters, sizeof(ParameterT), cutter.parameterBytes.data(), sizeof(ParameterT));
    return true;
}

[[nodiscard]] static bool AppendCsgParameterBytes(
    CsgParameterByteDataVector& parameterBytes,
    const void* sourceBytes,
    const usize byteSize,
    u32& outByteOffset,
    u32& outByteSize
){
    outByteOffset = 0u;
    outByteSize = 0u;
    if(!sourceBytes || byteSize == 0u)
        return true;

    usize alignedBegin = 0u;
    if(!AlignUpChecked(parameterBytes.size(), sizeof(u32), alignedBegin))
        return false;
    if(alignedBegin > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(byteSize > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(alignedBegin > Limit<usize>::s_Max - byteSize)
        return false;

    if(parameterBytes.size() < alignedBegin)
        parameterBytes.resize(alignedBegin, 0u);

    const usize byteBegin = parameterBytes.size();
    const usize byteEnd = byteBegin + byteSize;
    parameterBytes.resize(byteEnd);
    NWB_MEMCPY(parameterBytes.data() + byteBegin, byteSize, sourceBytes, byteSize);

    usize alignedEnd = 0u;
    if(!AlignUpChecked(parameterBytes.size(), sizeof(u32), alignedEnd))
        return false;
    if(parameterBytes.size() < alignedEnd)
        parameterBytes.resize(alignedEnd, 0u);

    outByteOffset = static_cast<u32>(byteBegin);
    outByteSize = static_cast<u32>(byteSize);
    return true;
}

template<typename ParameterT>
[[nodiscard]] static bool AppendCsgCutterParameters(
    CsgParameterByteDataVector* parameterBytes,
    const ParameterT& parameters,
    CsgCutterGpuData& inOutCutter
){
    if(!parameterBytes){
        inOutCutter.parameterByteSize = static_cast<u32>(sizeof(ParameterT));
        return true;
    }

    return AppendCsgParameterBytes(
        *parameterBytes,
        &parameters,
        sizeof(ParameterT),
        inOutCutter.parameterByteOffset,
        inOutCutter.parameterByteSize
    );
}

[[nodiscard]] static SIMDVector ComputeWorldToShapeScaleBound(const SIMDMatrix& worldToShape){
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector lengthSquared = VectorAdd(Vector3LengthSq(row0), Vector3LengthSq(row1));
    lengthSquared = VectorAdd(lengthSquared, Vector3LengthSq(row2));
    return VectorSqrt(lengthSquared);
}

[[nodiscard]] static bool BuildCsgCutterGpuData(
    const CsgCutterComponent& cutter,
    CsgParameterByteDataVector* parameterBytes,
    CsgCutterGpuData& outCutter
){
    if(cutter.operation != CsgOperation::Subtract)
        return false;

    outCutter = CsgCutterGpuData{};
    outCutter.operation = NWB_CSG_OPERATION_SUBTRACT;
    const SIMDMatrix worldToShape = LoadFloat(cutter.worldToShape);
    const SIMDMatrix shapeToWorld = LoadFloat(cutter.shapeToWorld);
    StoreFloat(worldToShape, &outCutter.worldToShape);
    StoreFloat(shapeToWorld, &outCutter.shapeToWorld);
    const f32 worldToShapeScaleBound = VectorGetX(ComputeWorldToShapeScaleBound(worldToShape));
    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    if(cutter.shapeType == s_CsgPlaneShapeName){
        CsgPlaneShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_PLANE;
        outCutter.parameter0 = parameters.normalDistance;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }
    if(cutter.shapeType == s_CsgBoxShapeName){
        CsgBoxShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_BOX;
        outCutter.parameter0 = parameters.halfExtents;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }
    if(cutter.shapeType == s_CsgSphereShapeName){
        CsgSphereShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_SPHERE;
        outCutter.parameter0 = parameters.radius;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }
    if(cutter.shapeType == s_CsgCapsuleShapeName){
        CsgCapsuleShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_CAPSULE;
        outCutter.parameter0 = parameters.radiusHalfHeight;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }

    return false;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createCsgClipResources(){
    auto* device = m_graphics.getDevice();
    if(!m_csgState.m_clipBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Mesh | Core::ShaderType::Compute | Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CSG_BINDING_RECEIVER_RANGES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CUTTERS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_CSG_BINDING_PARAMETER_BYTES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        m_csgState.m_clipBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_csgState.m_clipBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding layout"));
            return false;
        }
    }

    if(m_csgState.m_clipBindingSet)
        return true;
    if(!m_csgState.m_receiverRangeBuffer || !m_csgState.m_cutterBuffer || !m_csgState.m_parameterByteBuffer)
        return true;

    Core::BindingSetDesc bindingSetDesc(m_arena);
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(
        NWB_CSG_BINDING_RECEIVER_RANGES,
        m_csgState.m_receiverRangeBuffer.get()
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CUTTERS, m_csgState.m_cutterBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_CSG_BINDING_PARAMETER_BYTES, m_csgState.m_parameterByteBuffer.get()));

    m_csgState.m_clipBindingSet = device->createBindingSet(bindingSetDesc, m_csgState.m_clipBindingLayout);
    if(!m_csgState.m_clipBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding set"));
        return false;
    }

    return true;
}

void RendererSystem::destroyCsgClipBindingSet(){
    m_csgState.m_clipBindingSet.reset();
}

bool RendererSystem::reserveCsgReceiverRangeBufferCapacity(const usize rangeCount){
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

    if(m_csgState.m_receiverRangeBufferCapacity != oldCapacity)
        destroyCsgClipBindingSet();
    return true;
}

bool RendererSystem::reserveCsgCutterBufferCapacity(const usize cutterCount){
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

    if(m_csgState.m_cutterBufferCapacity != oldCapacity)
        destroyCsgClipBindingSet();
    return true;
}

bool RendererSystem::reserveCsgParameterByteBufferCapacity(const usize byteCount){
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
#if defined(NWB_DEBUG)
    if(!AlignUpChecked(requiredByteCount, sizeof(u32), requiredByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG parameter byte buffer request overflows alignment"));
        return false;
    }
#else
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
#endif
    if(m_csgState.m_parameterByteBuffer && m_csgState.m_parameterByteBufferCapacity >= requiredByteCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(m_csgState.m_parameterByteBufferCapacity, requiredByteCount);
    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveRawViews(true)
        .setDebugName(ECSRenderDetail::s_CsgParameterByteBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = m_graphics.createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG parameter byte buffer"));
        return false;
    }

    m_csgState.m_parameterByteBuffer = Move(createdBuffer);
    m_csgState.m_parameterByteBufferCapacity = capacity;
    destroyCsgClipBindingSet();
    return true;
}

bool RendererSystem::uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasWork())
        return true;
    if(
        !reserveCsgReceiverRangeBufferCapacity(csgFrameData.receiverRanges.size())
        || !reserveCsgCutterBufferCapacity(csgFrameData.cutters.size())
        || !reserveCsgParameterByteBufferCapacity(csgFrameData.parameterBytes.size())
    )
        return false;
    if(!createCsgClipResources())
        return false;

    commandList.setBufferState(m_csgState.m_receiverRangeBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(m_csgState.m_cutterBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(m_csgState.m_parameterByteBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        m_csgState.m_receiverRangeBuffer.get(),
        csgFrameData.receiverRanges.data(),
        csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    commandList.writeBuffer(
        m_csgState.m_cutterBuffer.get(),
        csgFrameData.cutters.data(),
        csgFrameData.cutters.size() * sizeof(CsgCutterGpuData)
    );
    const u32 emptyParameterBytes = 0u;
    const void* parameterByteData = &emptyParameterBytes;
    if(!csgFrameData.parameterBytes.empty())
        parameterByteData = csgFrameData.parameterBytes.data();
    commandList.writeBuffer(
        m_csgState.m_parameterByteBuffer.get(),
        parameterByteData,
        Max<usize>(csgFrameData.parameterBytes.size(), sizeof(u32))
    );
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();
    return true;
}

void RendererSystem::setCsgClipBufferStates(Core::CommandList& commandList){
    commandList.setBufferState(m_csgState.m_receiverRangeBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_csgState.m_cutterBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_csgState.m_parameterByteBuffer.get(), Core::ResourceStates::ShaderResource);
}

u32 RendererSystem::countCsgReceiverClipCutters(
    const CsgFrameReceiverLookup& receiverLookup,
    const Core::ECS::EntityID entity
)const{
    u32 cutterCount = 0u;
    receiverLookup.forEachReceiverCutter(
        entity,
        [&](const Core::ECS::EntityID cutterEntity, const CsgCutterComponent& cutter){
            static_cast<void>(cutterEntity);
            CsgCutterGpuData unusedCutter;
            if(!__hidden_csg_resources::BuildCsgCutterGpuData(cutter, nullptr, unusedCutter))
                return;
            if(cutterCount < Limit<u32>::s_Max)
                ++cutterCount;
        }
    );
    return cutterCount;
}

bool RendererSystem::appendCsgReceiverClipData(
    const CsgFrameReceiverLookup& receiverLookup,
    const Core::ECS::EntityID entity,
    CsgFrameGpuData& csgFrameData,
    CsgReceiverRangeGpuData& outRange
)const{
    outRange = CsgReceiverRangeGpuData{};
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outRange.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    bool appendFailed = false;
    receiverLookup.forEachReceiverCutter(
        entity,
        [&](const Core::ECS::EntityID cutterEntity, const CsgCutterComponent& cutter){
            static_cast<void>(cutterEntity);
            if(appendFailed)
                return;

            CsgCutterGpuData cutterGpuData;
            if(!__hidden_csg_resources::BuildCsgCutterGpuData(cutter, &csgFrameData.parameterBytes, cutterGpuData))
                return;
            if(csgFrameData.cutters.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                appendFailed = true;
                return;
            }

            csgFrameData.cutters.push_back(cutterGpuData);
            ++outRange.cutterCount;
        }
    );

    return !appendFailed && outRange.cutterCount > 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

