// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_mip_preparer.h"

#include <core/common/log.h>

#include <global/simdmath.h>

#include <basisu_comp.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using EncodedMip = TextureFormat::MipLevelLayout;

Expected<::Vector<EncodedMip, Core::Alloc::ScratchArena>> TextureMipPreparer::BuildLevels(const TextureEncodeInput& entry, Core::Alloc::ScratchArena& scratchArena){
    auto mipLevels = TextureFormat::BuildMipChainLayout(
        entry.format,
        entry.dimension,
        entry.width,
        entry.height,
        entry.depth,
        ::Vector<EncodedMip, Core::Alloc::ScratchArena>(scratchArena),
        [](const TextureFormat::MipLevelLayout& level)noexcept -> EncodedMip{
            return level;
        }
    );
    if(!mipLevels){
        switch(mipLevels.error().failure){
        case TextureFormat::MipChainLayoutFailure::InvalidMipChain:
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("tex_conv: dimensions cannot form a complete mip chain."));
            break;
        case TextureFormat::MipChainLayoutFailure::BlockLayoutOverflow:
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("tex_conv: mip {} block grid exceeds runtime limits."), mipLevels.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::SliceCountInvalid:
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("tex_conv: mip {} has an invalid slice count."), mipLevels.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::ByteSizeOverflow:
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("tex_conv: mip {} byte size overflows."), mipLevels.error().mipIndex);
            break;
        case TextureFormat::MipChainLayoutFailure::OffsetOverflow:
        default:
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("tex_conv: mip payload offsets overflow."));
            break;
        }
        return MakeUnexpected(Failure{});
    }
    return Move(*mipLevels);
}

Expected<FloatPlanes> TextureMipPreparer::GenerateNext(const FloatPlanes& source, const TextureDimension::Enum dimension){
    if(source.empty())
        return MakeUnexpected(Failure{});
    const u32 sourceWidth = source.front().get_width();
    const u32 sourceHeight = source.front().get_height();
    const u32 targetWidth = TextureFormat::NextMipExtent(sourceWidth);
    const u32 targetHeight = TextureFormat::NextMipExtent(sourceHeight);
    FloatPlanes filtered;
    filtered.resize(source.size());
    for(usize planeIndex = 0u; planeIndex < source.size(); ++planeIndex){
        basisu::imagef& target = filtered[planeIndex];
        target.resize(targetWidth, targetHeight);
        if(!basisu::image_resample(source[planeIndex], target, s_MipFilter.data(), 1.0f, false, 0u, s_ColorChannelCount)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("tex_conv: failed to filter linear mip planes."));
            return MakeUnexpected(Failure{});
        }
    }
    if(dimension != TextureDimension::Texture3D || source.size() == 1u)
        return filtered;

    const u32 sourceDepth = static_cast<u32>(source.size());
    const SIMDVectorSingle inverseDepth = VectorDivide(s_SIMDOne, VectorReplicate(static_cast<f32>(sourceDepth)));
    const u32 targetDepth = sourceDepth >> 1u;
    FloatPlanes target;
    target.resize(targetDepth);
    for(u32 z = 0u; z < targetDepth; ++z){
        // Integer interval coordinates preserve fractional source-slab coverage when the depth is odd.
        const VolumeMipZSlabRange slabRange = ComputeVolumeMipZSlabRange(z, sourceDepth, targetDepth);
        basisu::imagef& plane = target[z];
        plane.resize(targetWidth, targetHeight);
        for(u32 y = 0u; y < targetHeight; ++y){
            for(u32 x = 0u; x < targetWidth; ++x){
                SIMDVectorSingle sum = VectorZero();
                for(u32 sourceZ = slabRange.first; sourceZ < slabRange.end; ++sourceZ){
                    const basisu::vec4F& color = filtered[sourceZ](x, y);
                    sum = AccumulateVolumeMipZSlabTexel(slabRange, sourceZ, targetDepth, VectorSet(color[0u], color[1u], color[2u], color[3u]), sum);
                }
                const SIMDVectorSingle mean = VectorMultiply(sum, inverseDepth);
                plane(x, y).set(VectorGetX(mean), VectorGetY(mean), VectorGetZ(mean), VectorGetW(mean));
            }
        }
    }
    return target;
}

Expected<TexConvAlphaTransport> TextureMipPreparer::DeriveAlpha(const FloatMipChain& mips, const TexturePayloadFormat::Enum format){
    b8 found = false;
    b8 constant = true;
    b8 opaque = true;
    u8 firstAlpha = TextureFormat::s_OpaqueAlphaUnorm8;
    for(const FloatPlanes& planes : mips){
        for(const basisu::imagef& plane : planes){
            for(u32 y = 0u; y < plane.get_height(); ++y){
                for(u32 x = 0u; x < plane.get_width(); ++x){
                    const basisu::vec4F& color = plane(x, y);
                    const auto alphaResult = TryQuantizeTexConvMipAlpha(VectorSet(color[0u], color[1u], color[2u], color[3u]));
                    if(!alphaResult)
                        return MakeUnexpected(Failure{});
                    const u8 alpha = *alphaResult;
                    if(!found){
                        firstAlpha = alpha;
                        found = true;
                    }
                    else if(alpha != firstAlpha)
                        constant = false;
                    opaque = opaque && alpha == TextureFormat::s_OpaqueAlphaUnorm8;
                }
            }
        }
    }
    return TryResolveTexConvAlphaTransport(
        found,
        opaque,
        format == TexturePayloadFormat::UastcLdr4x4,
        constant,
        firstAlpha,
        TextureFormat::s_OpaqueAlphaUnorm8,
        TextureAlphaMode::Opaque,
        TextureAlphaMode::EmbeddedLdr,
        TextureAlphaMode::ConstantUnorm8,
        TextureAlphaMode::SeparateUastcLdr4x4
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
