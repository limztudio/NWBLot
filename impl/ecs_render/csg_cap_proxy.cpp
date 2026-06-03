// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_proxy.h"

#include "renderer_csg_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_proxy{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CsgCapProxyBounds MakeCapProxyBounds(
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    const bool finiteBounds
){
    CsgCapProxyBounds bounds;
    const i32 flags = s_CsgBoundsValidFlag | (finiteBounds ? s_CsgBoundsFiniteFlag : 0);
    StoreFloatInt(VectorSetW(minBounds, 0.0f), flags, &bounds.minBounds);
    StoreFloatInt(VectorSetW(maxBounds, 0.0f), 0, &bounds.maxBounds);
    return bounds;
}

[[nodiscard]] static bool BuildCutterBounds(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameGpuData& csgFrameData,
    const CsgCutterGpuData& cutter,
    CsgCapProxyBounds& outBounds
){
    outBounds = CsgCapProxyBounds{};

    const u8* parameterBytes = ECSRenderCsgCapProxy::CsgCutterParameterBytes(csgFrameData, cutter);
    if(cutter.parameterByteSize != 0u && !parameterBytes)
        return false;

    SIMDVector minBounds;
    SIMDVector maxBounds;
    bool finiteBounds = false;
    if(!shapeRegistry.buildShapeBounds(
        cutter.shapeType,
        LoadFloat(cutter.shapeToWorld),
        parameterBytes,
        static_cast<usize>(cutter.parameterByteSize),
        minBounds,
        maxBounds,
        finiteBounds
    ))
        return false;

    outBounds = MakeCapProxyBounds(minBounds, maxBounds, finiteBounds);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const u8* ECSRenderCsgCapProxy::CsgCutterParameterBytes(const CsgFrameGpuData& csgFrameData, const CsgCutterGpuData& cutter){
    if(cutter.parameterByteSize == 0u)
        return nullptr;
    const usize byteOffset = static_cast<usize>(cutter.parameterByteOffset);
    const usize byteSize = static_cast<usize>(cutter.parameterByteSize);
    if(byteOffset > csgFrameData.parameterBytes.size() || byteSize > csgFrameData.parameterBytes.size() - byteOffset)
        return nullptr;

    return csgFrameData.parameterBytes.data() + byteOffset;
}

bool ECSRenderCsgCapProxy::BuildReceiverBounds(
    const CsgReceiverCpuBounds& localBounds,
    const Scene::TransformComponent* transform,
    CsgCapProxyBounds& outBounds
){
    outBounds = CsgCapProxyBounds{};

    SIMDVector minBounds;
    SIMDVector maxBounds;
    if(!ECSRenderCsgDetail::BuildCsgReceiverWorldBounds(localBounds, transform, minBounds, maxBounds))
        return false;

    outBounds = __hidden_csg_cap_proxy::MakeCapProxyBounds(minBounds, maxBounds, true);
    return true;
}

bool ECSRenderCsgCapProxy::AppendGpuData(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCapProxyBounds& receiverBounds,
    const CsgReceiverPass::Enum receiverPass,
    const u32 receiverIndex,
    const u32 cutterIndex,
    const CsgCutterGpuData& cutter,
    CsgFrameGpuData& csgFrameData
){
    const u32 proxyShapeMask = CsgCapProxyShapeMask(cutter.shapeType);
    if(proxyShapeMask == 0u)
        return true;

    CsgCapProxyGpuData gpuItem;
    gpuItem.receiverCutterShapePass.x = receiverIndex;
    gpuItem.receiverCutterShapePass.y = cutterIndex;
    gpuItem.receiverCutterShapePass.z = cutter.shapeType;
    gpuItem.receiverCutterShapePass.w = static_cast<u32>(receiverPass);
    gpuItem.receiverBounds = receiverBounds;
    if(!__hidden_csg_cap_proxy::BuildCutterBounds(shapeRegistry, csgFrameData, cutter, gpuItem.cutterBounds))
        return false;

    csgFrameData.capProxyGpuItems.push_back(gpuItem);
    csgFrameData.capProxyShapeMask |= proxyShapeMask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
