// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_proxy_builder.h"

#include "renderer_csg_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_proxy_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool BuildCutterBounds(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameGpuData& csgFrameData,
    const CsgCutterGpuData& cutter,
    CsgCapProxyBounds& outBounds
){
    outBounds = CsgCapProxyBounds{};

    const u8* parameterBytes = ECSRenderCsgCapProxyBuilder::CsgCutterParameterBytes(csgFrameData, cutter);
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

    outBounds = ECSRenderCsgDetail::BuildCsgBoundsGpuData(minBounds, maxBounds, finiteBounds);
    return true;
}

[[nodiscard]] static bool AppendUniqueProxyShapeType(
    const CsgShapeTypeId shapeType,
    CsgCapProxyShapeTypeVector& shapeTypes
){
    for(const CsgShapeTypeId existingShapeType : shapeTypes){
        if(existingShapeType == shapeType)
            return true;
    }

    shapeTypes.push_back(shapeType);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const u8* ECSRenderCsgCapProxyBuilder::CsgCutterParameterBytes(const CsgFrameGpuData& csgFrameData, const CsgCutterGpuData& cutter){
    if(cutter.parameterByteSize == 0u)
        return nullptr;
    const usize byteOffset = static_cast<usize>(cutter.parameterByteOffset);
    const usize byteSize = static_cast<usize>(cutter.parameterByteSize);
    if(byteOffset > csgFrameData.parameterBytes.size() || byteSize > csgFrameData.parameterBytes.size() - byteOffset)
        return nullptr;

    return csgFrameData.parameterBytes.data() + byteOffset;
}

bool ECSRenderCsgCapProxyBuilder::BuildReceiverBounds(
    const CsgReceiverCpuBounds& localBounds,
    const Scene::TransformComponent* transform,
    CsgCapProxyBounds& outBounds
){
    outBounds = CsgCapProxyBounds{};

    SIMDVector minBounds;
    SIMDVector maxBounds;
    if(!ECSRenderCsgDetail::BuildCsgReceiverWorldBounds(localBounds, transform, minBounds, maxBounds))
        return false;

    outBounds = ECSRenderCsgDetail::BuildCsgBoundsGpuData(minBounds, maxBounds, true);
    return true;
}

bool ECSRenderCsgCapProxyBuilder::AppendGpuData(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCapProxyBounds& receiverBounds,
    const CsgReceiverPass::Enum receiverPass,
    const u32 receiverIndex,
    const u32 cutterIndex,
    const CsgCutterGpuData& cutter,
    const Float4& color,
    CsgFrameGpuData& csgFrameData
){
    CsgShapeTypeInfo shapeType;
    if(!shapeRegistry.findShapeType(cutter.shapeType, shapeType))
        return true;
    if(!shapeType.desc.capProxyShader)
        return true;

    CsgCapProxyGpuData gpuItem;
    gpuItem.receiverCutterShapePass.x = receiverIndex;
    gpuItem.receiverCutterShapePass.y = cutterIndex;
    gpuItem.receiverCutterShapePass.z = cutter.shapeType;
    gpuItem.receiverCutterShapePass.w = static_cast<u32>(receiverPass);
    gpuItem.color = color;
    gpuItem.receiverBounds = receiverBounds;
    if(!__hidden_csg_cap_proxy_builder::BuildCutterBounds(shapeRegistry, csgFrameData, cutter, gpuItem.cutterBounds))
        return false;

    csgFrameData.capProxyGpuItems.push_back(gpuItem);
    return __hidden_csg_cap_proxy_builder::AppendUniqueProxyShapeType(cutter.shapeType, csgFrameData.capProxyShapeTypes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
