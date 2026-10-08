// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_builtin_internal.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphBuiltinDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool UploadTextureTaskCanMaterializeRetainedState(
    const TextureDesc& resourceDesc,
    const ResourceStates::Mask graphInitialState,
    const ResourceStates::Mask externalFinalState,
    const ResourceStates::Mask uploadFinalState
)noexcept{
    if(!resourceDesc.keepInitialState)
        return true;
    if(
        resourceDesc.initialState == ResourceStates::Unknown
        || uploadFinalState != resourceDesc.initialState
        || (
            externalFinalState != ResourceStates::Unknown
            && externalFinalState != resourceDesc.initialState
        )
    )
        return false;
    // A texture upload is a first write. Its recorder materializes CopyDest and then publishes finalState,
    // an explicit Unknown graph import is safe for a fresh image while all other built-ins retain the stricter source requirement above.
    return graphInitialState == ResourceStates::Unknown || graphInitialState == resourceDesc.initialState;
}

[[nodiscard]] Expected<usize> ComputeTextureUploadByteSize(
    const TextureDesc& textureDesc,
    const u32 arraySlice,
    const u32 mipLevel,
    const usize rowPitch,
    const usize depthPitch,
    const TextureUploadAspect::Enum aspect
)noexcept{
    if(
        textureDesc.width == 0u
        || textureDesc.height == 0u
        || textureDesc.depth == 0u
        || textureDesc.mipLevels == 0u
        || textureDesc.arraySize == 0u
        || textureDesc.sampleCount != 1u
        || mipLevel >= textureDesc.mipLevels
        || arraySlice >= textureDesc.arraySize
        || static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::kCount)
    )
        return MakeUnexpected(Failure{});

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const auto aspectLayout = GetTextureUploadAspectLayout(formatInfo, aspect);
    if(!aspectLayout)
        return MakeUnexpected(Failure{});

    const u32 width = Max<u32>(1u, textureDesc.width >> mipLevel);
    const u32 height = Max<u32>(1u, textureDesc.height >> mipLevel);
    const u32 depth = textureDesc.dimension == TextureDimension::Texture3D
        ? Max<u32>(1u, textureDesc.depth >> mipLevel)
        : 1u
    ;
    const u64 blockCountX = DivideUp(static_cast<u64>(width), static_cast<u64>(aspectLayout->blockWidth));
    const u64 blockCountY = DivideUp(static_cast<u64>(height), static_cast<u64>(aspectLayout->blockHeight));
    if(blockCountX > Limit<u64>::s_Max / aspectLayout->bytesPerBlock)
        return MakeUnexpected(Failure{});

    const u64 naturalRowPitch = blockCountX * aspectLayout->bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0u ? static_cast<u64>(rowPitch) : naturalRowPitch;
    if(
        effectiveRowPitch == 0u
        || effectiveRowPitch < naturalRowPitch
        || (effectiveRowPitch % aspectLayout->bytesPerBlock) != 0u
        || blockCountY > Limit<u64>::s_Max / effectiveRowPitch
    )
        return MakeUnexpected(Failure{});

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0u ? static_cast<u64>(depthPitch) : packedSlicePitch;
    if(
        effectiveDepthPitch == 0u
        || effectiveDepthPitch < packedSlicePitch
        || (effectiveDepthPitch % effectiveRowPitch) != 0u
    )
        return MakeUnexpected(Failure{});

    // The native texture-copy contract uses 32-bit texel pitch fields in CommandList::writeTexture. Validate them at
    // declaration time so an accepted graph upload cannot lower to a native no-op after the command list rejects it.
    const u64 bufferRowBlocks = effectiveRowPitch / aspectLayout->bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(
        bufferRowBlocks > Limit<u64>::s_Max / aspectLayout->blockWidth
        || bufferImageBlocks > Limit<u64>::s_Max / aspectLayout->blockHeight
        || bufferRowBlocks * aspectLayout->blockWidth > Limit<u32>::s_Max
        || bufferImageBlocks * aspectLayout->blockHeight > Limit<u32>::s_Max
    )
        return MakeUnexpected(Failure{});

    if(depth > 1u && static_cast<u64>(depth - 1u) > (Limit<u64>::s_Max - packedSlicePitch) / effectiveDepthPitch)
        return MakeUnexpected(Failure{});

    const u64 requiredBytes = depth > 1u
        ? effectiveDepthPitch * static_cast<u64>(depth - 1u) + packedSlicePitch
        : packedSlicePitch
    ;
    if(requiredBytes > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<usize>(requiredBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

