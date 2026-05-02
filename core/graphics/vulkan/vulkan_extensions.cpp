// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Local Helpers


namespace __hidden_vulkan_extensions{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ClearTextureTilingOutputs(u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum){
    if(numTiles)
        *numTiles = 0;
    if(desc)
        *desc = {};
    if(tileShape)
        *tileShape = {};
    if(subresourceTilingsNum)
        *subresourceTilingsNum = 0;
}

[[nodiscard]] bool DivideUpU32(const u32 value, const u32 divisor, u32& outValue){
    if(divisor == 0)
        return false;

    outValue = DivideUp(value, divisor);
    return true;
}

[[nodiscard]] bool AddSparseTileCount(
    const u32 startTileIndex,
    const u32 widthInTiles,
    const u32 heightInTiles,
    const u32 depthInTiles,
    u32& outNextTileIndex
){
    u64 tileCount = widthInTiles;
    if(heightInTiles != 0 && tileCount > Limit<u64>::s_Max / heightInTiles)
        return false;
    tileCount *= heightInTiles;
    if(depthInTiles != 0 && tileCount > Limit<u64>::s_Max / depthInTiles)
        return false;
    tileCount *= depthInTiles;
    if(tileCount > static_cast<u64>(Limit<u32>::s_Max - startTileIndex))
        return false;

    outNextTileIndex = startTileIndex + static_cast<u32>(tileCount);
    return true;
}

[[nodiscard]] bool ComputeSparseTileCount(const VkDeviceSize byteSize, const u64 tileByteSize, u32& outTileCount){
    if(tileByteSize == 0){
        outTileCount = 0;
        return true;
    }

    const u64 tileCount = byteSize / tileByteSize;
    if(tileCount > Limit<u32>::s_Max)
        return false;

    outTileCount = static_cast<u32>(tileCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push Constants


void CommandList::setPushConstants(const void* data, usize byteSize){
    if(byteSize == 0)
        return;
    if(!data){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: data is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: data is null"));
        return;
    }
    if(byteSize > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds uint32 range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds uint32 range"));
        return;
    }

    const u32 pushConstantByteSize = static_cast<u32>(byteSize);
    if(!VulkanDetail::ValidatePushConstantByteSize(m_context, pushConstantByteSize, NWB_TEXT("set push constants")))
        return;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    u32 pipelinePushConstantByteSize = 0;

    if(m_currentGraphicsState.pipeline){
        auto* gp = checked_cast<GraphicsPipeline*>(m_currentGraphicsState.pipeline);
        layout = gp->m_pipelineLayout;
        pipelinePushConstantByteSize = gp->m_pushConstantByteSize;
    }
    else if(m_currentComputeState.pipeline){
        auto* cp = checked_cast<ComputePipeline*>(m_currentComputeState.pipeline);
        layout = cp->m_pipelineLayout;
        pipelinePushConstantByteSize = cp->m_pushConstantByteSize;
    }
    else if(m_currentMeshletState.pipeline){
        auto* mp = checked_cast<MeshletPipeline*>(m_currentMeshletState.pipeline);
        layout = mp->m_pipelineLayout;
        pipelinePushConstantByteSize = mp->m_pushConstantByteSize;
    }
    else if(m_currentRayTracingState.shaderTable){
        auto* rtp = m_currentRayTracingState.shaderTable->getPipeline();
        if(rtp){
            auto* rtpImpl = checked_cast<RayTracingPipeline*>(rtp);
            layout = rtpImpl->m_pipelineLayout;
            pipelinePushConstantByteSize = rtpImpl->m_pushConstantByteSize;
        }
    }

    if(layout == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: no active pipeline layout"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: no active pipeline layout"));
        return;
    }
    if(pipelinePushConstantByteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: active pipeline layout has no push constant range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: active pipeline layout has no push constant range"));
        return;
    }
    if(pushConstantByteSize > pipelinePushConstantByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size {} exceeds active pipeline push constant range {}")
            , pushConstantByteSize
            , pipelinePushConstantByteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds active pipeline push constant range"));
        return;
    }

    vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, pushConstantByteSize, data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cluster Acceleration Structure


void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& opDesc){
    if(!m_context.extensions.NV_cluster_acceleration_structure)
        return;

    VkClusterAccelerationStructureInputInfoNV inputInfo{};
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput{};
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{};
    if(!VulkanDetail::BuildClusterOperationInputInfo(opDesc.params, inputInfo, moveInput, clusterInput, blasInput, NWB_TEXT("execute cluster operation")))
        return;

    auto* indirectArgCountBuffer = opDesc.inIndirectArgCountBuffer ? checked_cast<Buffer*>(opDesc.inIndirectArgCountBuffer) : nullptr;
    auto* indirectArgsBuffer = opDesc.inIndirectArgsBuffer ? checked_cast<Buffer*>(opDesc.inIndirectArgsBuffer) : nullptr;
    auto* inOutAddressesBuffer = opDesc.inOutAddressesBuffer ? checked_cast<Buffer*>(opDesc.inOutAddressesBuffer) : nullptr;
    auto* outSizesBuffer = opDesc.outSizesBuffer ? checked_cast<Buffer*>(opDesc.outSizesBuffer) : nullptr;
    auto* outAccelerationStructuresBuffer = opDesc.outAccelerationStructuresBuffer ? checked_cast<Buffer*>(opDesc.outAccelerationStructuresBuffer) : nullptr;

    if(indirectArgCountBuffer && opDesc.inIndirectArgCountOffsetInBytes >= indirectArgCountBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation indirect-arg-count offset is out of range."));
        return;
    }
    if(indirectArgsBuffer && opDesc.inIndirectArgsOffsetInBytes >= indirectArgsBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation indirect-args offset is out of range."));
        return;
    }
    if(inOutAddressesBuffer && opDesc.inOutAddressesOffsetInBytes >= inOutAddressesBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation in/out-addresses offset is out of range."));
        return;
    }
    if(outSizesBuffer && opDesc.outSizesOffsetInBytes >= outSizesBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation out-sizes offset is out of range."));
        return;
    }
    if(outAccelerationStructuresBuffer && opDesc.outAccelerationStructuresOffsetInBytes >= outAccelerationStructuresBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation output AS offset is out of range."));
        return;
    }

    if(m_enableAutomaticBarriers){
        if(indirectArgsBuffer)
            setBufferState(opDesc.inIndirectArgsBuffer, ResourceStates::ShaderResource);
        if(indirectArgCountBuffer)
            setBufferState(opDesc.inIndirectArgCountBuffer, ResourceStates::ShaderResource);
        if(inOutAddressesBuffer)
            setBufferState(opDesc.inOutAddressesBuffer, ResourceStates::UnorderedAccess);
        if(outSizesBuffer)
            setBufferState(opDesc.outSizesBuffer, ResourceStates::UnorderedAccess);
        if(outAccelerationStructuresBuffer)
            setBufferState(opDesc.outAccelerationStructuresBuffer, ResourceStates::AccelStructWrite);
    }

    if(indirectArgCountBuffer)
        retainResource(opDesc.inIndirectArgCountBuffer);
    if(indirectArgsBuffer)
        retainResource(opDesc.inIndirectArgsBuffer);
    if(inOutAddressesBuffer)
        retainResource(opDesc.inOutAddressesBuffer);
    if(outSizesBuffer)
        retainResource(opDesc.outSizesBuffer);
    if(outAccelerationStructuresBuffer)
        retainResource(opDesc.outAccelerationStructuresBuffer);

    commitBarriers();

    BufferHandle scratchBufferHandle;
    Buffer* scratchBuffer = nullptr;
    if(opDesc.scratchSizeInBytes > 0){
        BufferDesc scratchDesc;
        scratchDesc.byteSize = opDesc.scratchSizeInBytes;
        scratchDesc.structStride = 1;
        scratchDesc.debugName = "ClusterOp_Scratch";
        scratchDesc.canHaveUAVs = true;

        scratchBufferHandle = m_device.createBuffer(scratchDesc);
        if(!scratchBufferHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate cluster operation scratch buffer"));
            return;
        }

        scratchBuffer = checked_cast<Buffer*>(scratchBufferHandle.get());
    }

    auto commandsInfo = VulkanDetail::MakeVkStruct<VkClusterAccelerationStructureCommandsInfoNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV);
    commandsInfo.input = inputInfo;
    commandsInfo.scratchData = scratchBuffer ? scratchBuffer->m_deviceAddress : 0;
    commandsInfo.dstImplicitData = outAccelerationStructuresBuffer ? outAccelerationStructuresBuffer->m_deviceAddress + opDesc.outAccelerationStructuresOffsetInBytes : 0;

    if(inOutAddressesBuffer){
        commandsInfo.dstAddressesArray.deviceAddress = inOutAddressesBuffer->m_deviceAddress + opDesc.inOutAddressesOffsetInBytes;
        commandsInfo.dstAddressesArray.stride = inOutAddressesBuffer->getDescription().structStride;
        commandsInfo.dstAddressesArray.size = inOutAddressesBuffer->getDescription().byteSize - opDesc.inOutAddressesOffsetInBytes;
    }

    if(outSizesBuffer){
        commandsInfo.dstSizesArray.deviceAddress = outSizesBuffer->m_deviceAddress + opDesc.outSizesOffsetInBytes;
        commandsInfo.dstSizesArray.stride = outSizesBuffer->getDescription().structStride;
        commandsInfo.dstSizesArray.size = outSizesBuffer->getDescription().byteSize - opDesc.outSizesOffsetInBytes;
    }

    if(indirectArgsBuffer){
        commandsInfo.srcInfosArray.deviceAddress = indirectArgsBuffer->m_deviceAddress + opDesc.inIndirectArgsOffsetInBytes;
        commandsInfo.srcInfosArray.stride = indirectArgsBuffer->getDescription().structStride;
        commandsInfo.srcInfosArray.size = indirectArgsBuffer->getDescription().byteSize - opDesc.inIndirectArgsOffsetInBytes;
    }

    commandsInfo.srcInfosCount = indirectArgCountBuffer ? indirectArgCountBuffer->m_deviceAddress + opDesc.inIndirectArgCountOffsetInBytes : 0;
    commandsInfo.addressResolutionFlags = static_cast<VkClusterAccelerationStructureAddressResolutionFlagsNV>(0);

    vkCmdBuildClusterAccelerationStructureIndirectNV(m_currentCmdBuf->m_cmdBuf, &commandsInfo);

    if(scratchBufferHandle)
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBufferHandle));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cooperative Vector


void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    if(!m_context.extensions.NV_cooperative_vector || !m_context.coopVecFeatures.cooperativeVector)
        return;

    if(numDescs == 0)
        return;
    if(!convertDescs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptors are null"));
        return;
    }
    if(numDescs > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptor count exceeds Vulkan limit"));
        return;
    }

    Alloc::ScratchArena<> scratchArena;

    Vector<CooperativeVectorConvertMatrixLayoutDesc const*, Alloc::ScratchAllocator<CooperativeVectorConvertMatrixLayoutDesc const*>> validDescs{ Alloc::ScratchAllocator<CooperativeVectorConvertMatrixLayoutDesc const*>(scratchArena) };
    validDescs.reserve(numDescs);

    for(usize i = 0; i < numDescs; ++i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = convertDescs[i];
        if(!convertDesc.src.buffer || !convertDesc.dst.buffer)
            continue;

        auto* srcBuffer = checked_cast<Buffer*>(convertDesc.src.buffer);
        auto* dstBuffer = checked_cast<Buffer*>(convertDesc.dst.buffer);
        if(!srcBuffer || !dstBuffer){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: buffer is invalid"));
            continue;
        }
        if(!VulkanDetail::IsBufferRangeInBounds(srcBuffer->m_desc, convertDesc.src.offset, convertDesc.src.size)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: source range is outside the buffer"));
            continue;
        }
        if(!VulkanDetail::IsBufferRangeInBounds(dstBuffer->m_desc, convertDesc.dst.offset, convertDesc.dst.size)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: destination range is outside the buffer"));
            continue;
        }

        if(m_enableAutomaticBarriers){
            setBufferState(convertDesc.src.buffer, ResourceStates::ConvertCoopVecMatrixInput);
            setBufferState(convertDesc.dst.buffer, ResourceStates::ConvertCoopVecMatrixOutput);
        }

        validDescs.push_back(&convertDesc);
    }

    Vector<VkConvertCooperativeVectorMatrixInfoNV, Alloc::ScratchAllocator<VkConvertCooperativeVectorMatrixInfoNV>> vkConvertDescs(validDescs.size(), Alloc::ScratchAllocator<VkConvertCooperativeVectorMatrixInfoNV>(scratchArena));
    Vector<usize, Alloc::ScratchAllocator<usize>> dstSizes(validDescs.size(), Alloc::ScratchAllocator<usize>(scratchArena));

    auto buildConvertDesc = [&](usize i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = *validDescs[i];
        dstSizes[i] = convertDesc.dst.size;

        auto vkDesc = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
        vkDesc.srcSize = convertDesc.src.size;
        vkDesc.srcData.deviceAddress = checked_cast<Buffer*>(convertDesc.src.buffer)->m_deviceAddress + convertDesc.src.offset;
        vkDesc.pDstSize = &dstSizes[i];
        vkDesc.dstData.deviceAddress = checked_cast<Buffer*>(convertDesc.dst.buffer)->m_deviceAddress + convertDesc.dst.offset;
        vkDesc.srcComponentType = VulkanDetail::ConvertCoopVecDataType(convertDesc.src.type);
        vkDesc.dstComponentType = VulkanDetail::ConvertCoopVecDataType(convertDesc.dst.type);
        vkDesc.numRows = convertDesc.numRows;
        vkDesc.numColumns = convertDesc.numColumns;

        vkDesc.srcLayout = VulkanDetail::ConvertCoopVecMatrixLayout(convertDesc.src.layout);
        vkDesc.srcStride =
            convertDesc.src.stride != 0
            ? convertDesc.src.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.src.type, convertDesc.src.layout, convertDesc.numRows, convertDesc.numColumns)
        ;

        vkDesc.dstLayout = VulkanDetail::ConvertCoopVecMatrixLayout(convertDesc.dst.layout);
        vkDesc.dstStride =
            convertDesc.dst.stride != 0
            ? convertDesc.dst.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.dst.type, convertDesc.dst.layout, convertDesc.numRows, convertDesc.numColumns)
        ;

        vkConvertDescs[i] = vkDesc;
    };

    if(taskPool().isParallelEnabled() && validDescs.size() >= s_ParallelConvertThreshold)
        scheduleParallelFor(static_cast<usize>(0), validDescs.size(), s_ConvertGrainSize, buildConvertDesc);
    else{
        for(usize i = 0; i < validDescs.size(); ++i)
            buildConvertDesc(i);
    }

    commitBarriers();

    if(vkConvertDescs.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptor count exceeds Vulkan limit"));
        return;
    }

    if(!vkConvertDescs.empty())
        vkCmdConvertCooperativeVectorMatrixNV(m_currentCmdBuf->m_cmdBuf, static_cast<u32>(vkConvertDescs.size()), vkConvertDescs.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture Tiling


void Device::getTextureTiling(ITexture* textureResource, u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum, SubresourceTiling* subresourceTilings){
    if(!textureResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: texture is null"));
        __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
        return;
    }
    if(!queryFeatureSupport(Feature::VirtualResources)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: virtual/tiled resources are not supported by this backend"));
        __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
        return;
    }

    auto* texture = checked_cast<Texture*>(textureResource);
    if(!texture->m_desc.isTiled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: texture is not tiled"));
        __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
        return;
    }

    u32 numStandardMips = 0;
    u32 tileWidth = 1;
    u32 tileHeight = 1;
    u32 tileDepth = 1;

    Alloc::ScratchArena<> scratchArena;

    SparseImageMemoryRequirementsVector sparseReqs{ Alloc::ScratchAllocator<VkSparseImageMemoryRequirements>(scratchArena) };
    VulkanDetail::GetImageSparseMemoryRequirements(m_context.device, texture->m_image, sparseReqs);

    if(!sparseReqs.empty()){
        numStandardMips = sparseReqs[0].imageMipTailFirstLod;
        if(numStandardMips > texture->m_desc.mipLevels){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image mip tail exceeds texture mip levels"));
            __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
            return;
        }

        if(desc){
            desc->numStandardMips = numStandardMips;
            desc->numPackedMips = texture->m_desc.mipLevels - numStandardMips;
            if(
                !__hidden_vulkan_extensions::ComputeSparseTileCount(sparseReqs[0].imageMipTailOffset, texture->m_tileByteSize, desc->startTileIndexInOverallResource)
                || !__hidden_vulkan_extensions::ComputeSparseTileCount(sparseReqs[0].imageMipTailSize, texture->m_tileByteSize, desc->numTilesForPackedMips)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: packed mip tile range exceeds u32 limits"));
                __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
                return;
            }
        }
    }

    uint32_t formatPropCount = 0;
    vkGetPhysicalDeviceSparseImageFormatProperties(
        m_context.physicalDevice,
        texture->m_imageInfo.format,
        texture->m_imageInfo.imageType,
        texture->m_imageInfo.samples,
        texture->m_imageInfo.usage,
        texture->m_imageInfo.tiling,
        &formatPropCount, nullptr
    );

    Vector<VkSparseImageFormatProperties, Alloc::ScratchAllocator<VkSparseImageFormatProperties>> formatProps(formatPropCount, Alloc::ScratchAllocator<VkSparseImageFormatProperties>(scratchArena));
    if(formatPropCount > 0){
        vkGetPhysicalDeviceSparseImageFormatProperties(
            m_context.physicalDevice,
            texture->m_imageInfo.format,
            texture->m_imageInfo.imageType,
            texture->m_imageInfo.samples,
            texture->m_imageInfo.usage,
            texture->m_imageInfo.tiling,
            &formatPropCount, formatProps.data()
        );
    }

    if(!formatProps.empty()){
        tileWidth = formatProps[0].imageGranularity.width;
        tileHeight = formatProps[0].imageGranularity.height;
        tileDepth = formatProps[0].imageGranularity.depth;
    }
    if(tileWidth == 0 || tileHeight == 0 || tileDepth == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image tile shape is invalid"));
        __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
        return;
    }

    if(tileShape){
        tileShape->widthInTexels = tileWidth;
        tileShape->heightInTexels = tileHeight;
        tileShape->depthInTexels = tileDepth;
    }

    if(subresourceTilingsNum && subresourceTilings){
        *subresourceTilingsNum = Min(*subresourceTilingsNum, texture->m_desc.mipLevels);
        u32 startTileIndex = 0;

        u32 width = texture->m_desc.width;
        u32 height = texture->m_desc.height;
        u32 depth = texture->m_desc.depth;

        for(u32 i = 0; i < *subresourceTilingsNum; ++i){
            if(i < numStandardMips){
                if(
                    !__hidden_vulkan_extensions::DivideUpU32(width, tileWidth, subresourceTilings[i].widthInTiles)
                    || !__hidden_vulkan_extensions::DivideUpU32(height, tileHeight, subresourceTilings[i].heightInTiles)
                    || !__hidden_vulkan_extensions::DivideUpU32(depth, tileDepth, subresourceTilings[i].depthInTiles)
                ){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image tile shape is invalid"));
                    __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
                    return;
                }
                subresourceTilings[i].startTileIndexInOverallResource = startTileIndex;
            }
            else{
                subresourceTilings[i].widthInTiles = 0;
                subresourceTilings[i].heightInTiles = 0;
                subresourceTilings[i].depthInTiles = 0;
                subresourceTilings[i].startTileIndexInOverallResource = UINT32_MAX;
            }

            width = Max(width / 2, tileWidth);
            height = Max(height / 2, tileHeight);
            depth = Max(depth / 2, tileDepth);

            if(
                !__hidden_vulkan_extensions::AddSparseTileCount(
                    startTileIndex,
                    subresourceTilings[i].widthInTiles,
                    subresourceTilings[i].heightInTiles,
                    subresourceTilings[i].depthInTiles,
                    startTileIndex
                )
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image tile count exceeds u32 limits"));
                __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
                return;
            }
        }
    }

    if(numTiles){
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(m_context.device, texture->m_image, &memReqs);
        if(!__hidden_vulkan_extensions::ComputeSparseTileCount(memReqs.size, texture->m_tileByteSize, *numTiles)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: texture tile count exceeds u32 limits"));
            __hidden_vulkan_extensions::ClearTextureTilingOutputs(numTiles, desc, tileShape, subresourceTilingsNum);
            return;
        }
    }
}

void Device::updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings, CommandQueue::Enum executionQueue){
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: texture is null"));
        return;
    }
    if(!queryFeatureSupport(Feature::VirtualResources)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: virtual/tiled resources are not supported by this backend"));
        return;
    }
    if(!checked_cast<Texture*>(texture)->m_desc.isTiled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: texture is not tiled"));
        return;
    }
    if(!tileMappings && numTileMappings > 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: mappings are null"));
        return;
    }
    if(numTileMappings == 0)
        return;

    Queue* queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: requested queue is not available"));
        return;
    }

    queue->updateTextureTileMappings(texture, tileMappings, numTileMappings);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

