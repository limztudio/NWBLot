// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "renderer_capacity_private.h"
#include "renderer_csg_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] static bool CsgCutterIntersectsReceiver(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform
){
    SIMDVector receiverMinBounds;
    SIMDVector receiverMaxBounds;
    if(!ECSRenderCsgDetail::BuildCsgReceiverWorldBounds(receiverBounds, transform, receiverMinBounds, receiverMaxBounds))
        return true;

    SIMDVector cutterMinBounds;
    SIMDVector cutterMaxBounds;
    bool finiteBounds = false;
    if(!ECSRenderCsgDetail::BuildCsgCutterComponentWorldBounds(shapeRegistry, cutter, cutterMinBounds, cutterMaxBounds, finiteBounds))
        return false;
    if(!finiteBounds)
        return true;

    return AabbTests::Intersects(receiverMinBounds, receiverMaxBounds, cutterMinBounds, cutterMaxBounds);
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
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_CSG_BINDING_PARAMETER_BYTES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        csgState().m_clipBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!csgState().m_clipBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding layout"));
            return false;
        }
    }

    if(csgState().m_clipBindingSet)
        return true;
    if(!csgState().m_receiverRangeBuffer || !csgState().m_cutterBuffer || !csgState().m_parameterByteBuffer)
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(
        NWB_CSG_BINDING_RECEIVER_RANGES,
        csgState().m_receiverRangeBuffer.get()
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CUTTERS, csgState().m_cutterBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_CSG_BINDING_PARAMETER_BYTES, csgState().m_parameterByteBuffer.get()));

    csgState().m_clipBindingSet = device->createBindingSet(bindingSetDesc, csgState().m_clipBindingLayout);
    if(!csgState().m_clipBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG clip binding set"));
        return false;
    }

    return true;
}

bool RendererCsgSystem::createCsgOpeningMaskWriteResources(Core::Texture* openingMask){
    auto* device = graphics().getDevice();
    if(!csgState().m_openingMaskWriteBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CSG_BINDING_OPENING_MASK_WRITE, 1));

        csgState().m_openingMaskWriteBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!csgState().m_openingMaskWriteBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG opening mask write binding layout"));
            return false;
        }
    }

    if(csgState().m_openingMaskWriteBindingSet)
        return true;

    if(!openingMask){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG opening mask write resources require a valid opening mask target"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CSG_BINDING_OPENING_MASK_WRITE,
        openingMask,
        openingMask->getDescription().format,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    csgState().m_openingMaskWriteBindingSet = device->createBindingSet(bindingSetDesc, csgState().m_openingMaskWriteBindingLayout);
    if(!csgState().m_openingMaskWriteBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG opening mask write binding set"));
        return false;
    }

    return true;
}

void RendererCsgSystem::destroyCsgClipBindingSet(){
    csgState().m_clipBindingSet.reset();
}

void RendererCsgSystem::destroyCsgOpeningMaskWriteBindingSet(){
    csgState().m_openingMaskWriteBindingSet.reset();
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

bool RendererCsgSystem::reserveCsgParameterByteBufferCapacity(const usize byteCount){
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
#if defined(NWB_DEBUG)
    if(!AlignUpChecked(requiredByteCount, sizeof(u32), requiredByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG parameter byte buffer request overflows alignment"));
        return false;
    }
#else
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
#endif
    if(csgState().m_parameterByteBuffer && csgState().m_parameterByteBufferCapacity >= requiredByteCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(csgState().m_parameterByteBufferCapacity, requiredByteCount);
    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveRawViews(true)
        .setDebugName(ECSRenderDetail::s_CsgParameterByteBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = graphics().createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG parameter byte buffer"));
        return false;
    }

    csgState().m_parameterByteBuffer = Move(createdBuffer);
    csgState().m_parameterByteBufferCapacity = capacity;
    destroyCsgClipBindingSet();
    return true;
}

bool RendererCsgSystem::uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasWork())
        return true;
    if(
        !reserveCsgReceiverRangeBufferCapacity(csgFrameData.receiverRanges.size())
        || !reserveCsgCutterBufferCapacity(csgFrameData.cutters.size())
        || !reserveCsgParameterByteBufferCapacity(csgFrameData.parameterBytes.size())
    )
        return false;
    if(!csgState().m_clipBindingSet && !createCsgClipResources())
        return false;

    commandList.setBufferState(csgState().m_receiverRangeBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(csgState().m_cutterBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(csgState().m_parameterByteBuffer.get(), Core::ResourceStates::CopyDest);
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
    const u32 emptyParameterBytes = 0u;
    const void* parameterByteData = &emptyParameterBytes;
    if(!csgFrameData.parameterBytes.empty())
        parameterByteData = csgFrameData.parameterBytes.data();
    commandList.writeBuffer(
        csgState().m_parameterByteBuffer.get(),
        parameterByteData,
        Max<usize>(csgFrameData.parameterBytes.size(), sizeof(u32))
    );
    setCsgClipBufferStates(commandList);
    commandList.commitBarriers();
    return true;
}

void RendererCsgSystem::setCsgClipBufferStates(Core::CommandList& commandList){
    commandList.setBufferState(csgState().m_receiverRangeBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(csgState().m_cutterBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(csgState().m_parameterByteBuffer.get(), Core::ResourceStates::ShaderResource);
}

void RendererCsgSystem::setCsgOpeningMaskWriteTextureState(Core::CommandList& commandList){
    Core::Texture* openingMask = deferredState().m_targets.csgOpeningMask.get();
    if(openingMask)
        commandList.setTextureState(openingMask, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
}

bool RendererCsgSystem::resolveCsgReceiverEvaluatorVariant(
    const CsgFrameReceiverLookup& receiverLookup,
    const Core::ECS::EntityID entity,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    Name& outEvaluatorVariant
)const{
    outEvaluatorVariant = s_CsgBuiltInShapeShaderModuleName;

    bool resolved = true;
    receiverLookup.forEachReceiverCutter(
        entity,
        [&](const Core::ECS::EntityID cutterEntity, const CsgCutterComponent& cutter){
            static_cast<void>(cutterEntity);
            if(!resolved || cutter.operation != CsgOperation::Subtract)
                return;
            if(!__hidden_csg_resources::CsgCutterIntersectsReceiver(csgShapeRegistry(), cutter, receiverBounds, transform))
                return;

            CsgShapeTypeInfo shapeType;
            if(!csgShapeRegistry().findShapeType(cutter.shapeType, shapeType)){
                resolved = false;
                return;
            }
            if(shapeType.desc.shaderModule == s_CsgBuiltInShapeShaderModuleName)
                return;

            if(outEvaluatorVariant == s_CsgBuiltInShapeShaderModuleName){
                outEvaluatorVariant = shapeType.desc.shaderModule;
                return;
            }
            if(outEvaluatorVariant != shapeType.desc.shaderModule)
                resolved = false;
        }
    );
    return resolved;
}

u32 RendererCsgSystem::countCsgReceiverClipCutters(
    const CsgFrameReceiverLookup& receiverLookup,
    const Core::ECS::EntityID entity,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform
)const{
    u32 cutterCount = 0u;
    receiverLookup.forEachReceiverCutter(
        entity,
        [&](const Core::ECS::EntityID cutterEntity, const CsgCutterComponent& cutter){
            static_cast<void>(cutterEntity);
            if(!__hidden_csg_resources::CsgCutterIntersectsReceiver(csgShapeRegistry(), cutter, receiverBounds, transform))
                return;

            CsgCutterGpuData unusedCutter;
            if(!ECSRenderCsgDetail::BuildCsgCutterGpuData(csgShapeRegistry(), cutter, nullptr, unusedCutter))
                return;
            if(cutterCount < Limit<u32>::s_Max)
                ++cutterCount;
        }
    );
    return cutterCount;
}

bool RendererCsgSystem::appendCsgReceiverClipData(
    const CsgFrameReceiverLookup& receiverLookup,
    const Core::ECS::EntityID entity,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    CsgFrameGpuData& csgFrameData,
    CsgReceiverRangeGpuData& outRange
)const{
    outRange = CsgReceiverRangeGpuData{};
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    if(!receiverBounds.valid() || !receiverBounds.finite())
        return false;

    SIMDMatrix worldToReceiver;
    if(!ECSRenderCsgDetail::BuildCsgReceiverWorldToLocal(transform, worldToReceiver))
        return false;

    StoreFloat(worldToReceiver, &outRange.worldToReceiver);
    outRange.localBounds = receiverBounds;
    outRange.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    bool appendFailed = false;
    receiverLookup.forEachReceiverCutter(
        entity,
        [&](const Core::ECS::EntityID cutterEntity, const CsgCutterComponent& cutter){
            static_cast<void>(cutterEntity);
            if(appendFailed)
                return;
            if(!__hidden_csg_resources::CsgCutterIntersectsReceiver(csgShapeRegistry(), cutter, receiverBounds, transform))
                return;

            CsgCutterGpuData cutterGpuData;
            if(!ECSRenderCsgDetail::BuildCsgCutterGpuData(csgShapeRegistry(), cutter, &csgFrameData.parameterBytes, cutterGpuData))
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

