// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextureSlice TextureSlice::resolve(const TextureDesc& desc)const noexcept{
    NWB_ASSERT(mipLevel < desc.mipLevels);
    const MipLevel resolvedMipLevel = (desc.mipLevels > 0 && mipLevel < desc.mipLevels) ? mipLevel : 0;

    const u32 mipWidth = Max(desc.width >> resolvedMipLevel, static_cast<u32>(1));
    const u32 mipHeight = Max(desc.height >> resolvedMipLevel, static_cast<u32>(1));
    const u32 mipDepth = desc.dimension == TextureDimension::Texture3D ? Max(desc.depth >> resolvedMipLevel, static_cast<u32>(1)) : static_cast<u32>(1);

    return resolve(mipWidth, mipHeight, mipDepth);
}

TextureSlice TextureSlice::resolve(const u32 mipWidth, const u32 mipHeight, const u32 mipDepth)const noexcept{
    TextureSlice ret(*this);

    if(width == TextureSlice::s_AllDimensions)
        ret.width = x < mipWidth ? mipWidth - x : 0;
    if(height == TextureSlice::s_AllDimensions)
        ret.height = y < mipHeight ? mipHeight - y : 0;

    if(depth == TextureSlice::s_AllDimensions)
        ret.depth = z < mipDepth ? mipDepth - z : 0;

    return ret;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextureSubresourceSet TextureSubresourceSet::resolve(const TextureDesc& desc, TextureSubresourceMipResolve::Enum mipResolve)const noexcept{
    TextureSubresourceSet ret;
    ret.baseMipLevel = baseMipLevel;

    const u32 availableMipLevels = baseMipLevel < desc.mipLevels ? desc.mipLevels - baseMipLevel : 0;
    if(mipResolve == TextureSubresourceMipResolve::Single)
        ret.numMipLevels = availableMipLevels > 0 ? 1 : 0;
    else if(numMipLevels == s_AllMipLevels)
        ret.numMipLevels = static_cast<MipLevel>(availableMipLevels);
    else
        ret.numMipLevels = static_cast<MipLevel>(Min<u32>(numMipLevels, availableMipLevels));

    switch(desc.dimension){
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMSArray:
    {
        ret.baseArraySlice = baseArraySlice;
        const u32 availableArraySlices = baseArraySlice < desc.arraySize ? desc.arraySize - baseArraySlice : 0;
        if(numArraySlices == s_AllArraySlices)
            ret.numArraySlices = static_cast<ArraySlice>(availableArraySlices);
        else
            ret.numArraySlices = static_cast<ArraySlice>(Min<u32>(numArraySlices, availableArraySlices));
        break;
    }
    default:
        ret.baseArraySlice = 0;
        ret.numArraySlices = 1;
        break;
    }

    return ret;
}

bool TextureSubresourceSet::isEntireTexture(const TextureDesc& desc)const noexcept{
    const TextureSubresourceSet resolved = resolve(desc, TextureSubresourceMipResolve::Range);

    if(resolved.baseMipLevel > 0u || resolved.numMipLevels < desc.mipLevels)
        return false;

    switch(desc.dimension){
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMSArray:
        if(resolved.baseArraySlice > 0u || resolved.numArraySlices < desc.arraySize)
            return false;
        break;
    default:
        break;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BufferRange BufferRange::resolve(const BufferDesc& desc)const noexcept{
    BufferRange ret;
    ret.byteOffset = Min(byteOffset, desc.byteSize);

    if(!byteSize)
        ret.byteSize = desc.byteSize - ret.byteOffset;
    else
        ret.byteSize = Min(byteSize, desc.byteSize - ret.byteOffset);

    return ret;
}

BufferRange BufferRange::intersect(const BufferRange& other)const noexcept{
    if(!overlaps(other))
        return {};
    const u64 rangeBegin = Max(byteOffset, other.byteOffset);
    const u64 rangeEnd = Min(end(), other.end());
    return BufferRange(rangeBegin, rangeEnd == s_AllBytes ? s_AllBytes : rangeEnd - rangeBegin);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

