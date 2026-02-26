// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.h"

#include <logger/client/logger.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* GraphicsAllocator::allocatePersistentSystemMemory(void* userData, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope){
    (void)scope;
    if(!userData)
        return nullptr;

    auto* arena = static_cast<Alloc::MemoryArena*>(userData);
    return arena->allocate(alignment, size);
}

void* GraphicsAllocator::reallocatePersistentSystemMemory(void* userData, void* original, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope){
    (void)scope;
    if(!userData)
        return nullptr;

    auto* arena = static_cast<Alloc::MemoryArena*>(userData);
    return arena->reallocate(original, alignment, size);
}

void GraphicsAllocator::freePersistentSystemMemory(void* userData, void* memory){
    if(!userData || !memory)
        return;

    auto* arena = static_cast<Alloc::MemoryArena*>(userData);
    arena->deallocate(memory, 1, 0);
}

GraphicsAllocator::GraphicsAllocator(Alloc::MemoryArena& persistentArena, Alloc::CustomArena& objectArena)
    : m_persistentArena(persistentArena)
    , m_systemMemoryAllocator{
        &m_persistentArena
        , &GraphicsAllocator::allocatePersistentSystemMemory
        , &GraphicsAllocator::reallocatePersistentSystemMemory
        , &GraphicsAllocator::freePersistentSystemMemory
    }
    , m_objectArena(objectArena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr FormatInfo s_FormatInfo[Format::kCount] = {
    { Format::UNKNOWN              , "UNKNOWN"              ,  0,  0, FormatKind::Integer     , false, false, false, false, false, false, false, false },

    { Format::R8_UINT              , "R8_UINT"              ,  1,  1, FormatKind::Integer     , true , false, false, false, false, false, false, false },
    { Format::R8_SINT              , "R8_SINT"              ,  1,  1, FormatKind::Integer     , true , false, false, false, false, false, true , false },
    { Format::R8_UNORM             , "R8_UNORM"             ,  1,  1, FormatKind::Normalized  , true , false, false, false, false, false, false, false },
    { Format::R8_SNORM             , "R8_SNORM"             ,  1,  1, FormatKind::Normalized  , true , false, false, false, false, false, true , false },
    { Format::RG8_UINT             , "RG8_UINT"             ,  2,  1, FormatKind::Integer     , true , true , false, false, false, false, false, false },
    { Format::RG8_SINT             , "RG8_SINT"             ,  2,  1, FormatKind::Integer     , true , true , false, false, false, false, true , false },
    { Format::RG8_UNORM            , "RG8_UNORM"            ,  2,  1, FormatKind::Normalized  , true , true , false, false, false, false, false, false },
    { Format::RG8_SNORM            , "RG8_SNORM"            ,  2,  1, FormatKind::Normalized  , true , true , false, false, false, false, true , false },
    { Format::R16_UINT             , "R16_UINT"             ,  2,  1, FormatKind::Integer     , true , false, false, false, false, false, false, false },
    { Format::R16_SINT             , "R16_SINT"             ,  2,  1, FormatKind::Integer     , true , false, false, false, false, false, true , false },
    { Format::R16_UNORM            , "R16_UNORM"            ,  2,  1, FormatKind::Normalized  , true , false, false, false, false, false, false, false },
    { Format::R16_SNORM            , "R16_SNORM"            ,  2,  1, FormatKind::Normalized  , true , false, false, false, false, false, true , false },
    { Format::R16_FLOAT            , "R16_FLOAT"            ,  2,  1, FormatKind::Float       , true , false, false, false, false, false, true , false },
    { Format::BGRA4_UNORM          , "BGRA4_UNORM"          ,  2,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::B5G6R5_UNORM         , "B5G6R5_UNORM"         ,  2,  1, FormatKind::Normalized  , true , true , true , false, false, false, false, false },
    { Format::B5G5R5A1_UNORM       , "B5G5R5A1_UNORM"       ,  2,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::RGBA8_UINT           , "RGBA8_UINT"           ,  4,  1, FormatKind::Integer     , true , true , true , true , false, false, false, false },
    { Format::RGBA8_SINT           , "RGBA8_SINT"           ,  4,  1, FormatKind::Integer     , true , true , true , true , false, false, true , false },
    { Format::RGBA8_UNORM          , "RGBA8_UNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::RGBA8_SNORM          , "RGBA8_SNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, true , false },
    { Format::RGBA8_UNORM_SRGB     , "RGBA8_UNORM_SRGB"     ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BGRA8_UNORM          , "BGRA8_UNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BGRA8_UNORM_SRGB     , "BGRA8_UNORM_SRGB"     ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BGRX8_UNORM          , "BGRX8_UNORM"          ,  4,  1, FormatKind::Normalized  , true , true , true , false, false, false, false, false },
    { Format::SRGBA8_UNORM         , "SRGBA8_UNORM"         ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::SBGRA8_UNORM         , "SBGRA8_UNORM"         ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::SBGRX8_UNORM         , "SBGRX8_UNORM"         ,  4,  1, FormatKind::Normalized  , true , true , true , false, false, false, false, true  },
    { Format::R10G10B10A2_UNORM    , "R10G10B10A2_UNORM"    ,  4,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::R11G11B10_FLOAT      , "R11G11B10_FLOAT"      ,  4,  1, FormatKind::Float       , true , true , true , false, false, false, false, false },
    { Format::RG16_UINT            , "RG16_UINT"            ,  4,  1, FormatKind::Integer     , true , true , false, false, false, false, false, false },
    { Format::RG16_SINT            , "RG16_SINT"            ,  4,  1, FormatKind::Integer     , true , true , false, false, false, false, true , false },
    { Format::RG16_UNORM           , "RG16_UNORM"           ,  4,  1, FormatKind::Normalized  , true , true , false, false, false, false, false, false },
    { Format::RG16_SNORM           , "RG16_SNORM"           ,  4,  1, FormatKind::Normalized  , true , true , false, false, false, false, true , false },
    { Format::RG16_FLOAT           , "RG16_FLOAT"           ,  4,  1, FormatKind::Float       , true , true , false, false, false, false, true , false },
    { Format::R32_UINT             , "R32_UINT"             ,  4,  1, FormatKind::Integer     , true , false, false, false, false, false, false, false },
    { Format::R32_SINT             , "R32_SINT"             ,  4,  1, FormatKind::Integer     , true , false, false, false, false, false, true , false },
    { Format::R32_FLOAT            , "R32_FLOAT"            ,  4,  1, FormatKind::Float       , true , false, false, false, false, false, true , false },
    { Format::RGBA16_UINT          , "RGBA16_UINT"          ,  8,  1, FormatKind::Integer     , true , true , true , true , false, false, false, false },
    { Format::RGBA16_SINT          , "RGBA16_SINT"          ,  8,  1, FormatKind::Integer     , true , true , true , true , false, false, true , false },
    { Format::RGBA16_FLOAT         , "RGBA16_FLOAT"         ,  8,  1, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::RGBA16_UNORM         , "RGBA16_UNORM"         ,  8,  1, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::RGBA16_SNORM         , "RGBA16_SNORM"         ,  8,  1, FormatKind::Normalized  , true , true , true , true , false, false, true , false },
    { Format::RG32_UINT            , "RG32_UINT"            ,  8,  1, FormatKind::Integer     , true , true , false, false, false, false, false, false },
    { Format::RG32_SINT            , "RG32_SINT"            ,  8,  1, FormatKind::Integer     , true , true , false, false, false, false, true , false },
    { Format::RG32_FLOAT           , "RG32_FLOAT"           ,  8,  1, FormatKind::Float       , true , true , false, false, false, false, true , false },
    { Format::RGB32_UINT           , "RGB32_UINT"           , 12,  1, FormatKind::Integer     , true , true , true , false, false, false, false, false },
    { Format::RGB32_SINT           , "RGB32_SINT"           , 12,  1, FormatKind::Integer     , true , true , true , false, false, false, true , false },
    { Format::RGB32_FLOAT          , "RGB32_FLOAT"          , 12,  1, FormatKind::Float       , true , true , true , false, false, false, true , false },
    { Format::RGBA32_UINT          , "RGBA32_UINT"          , 16,  1, FormatKind::Integer     , true , true , true , true , false, false, false, false },
    { Format::RGBA32_SINT          , "RGBA32_SINT"          , 16,  1, FormatKind::Integer     , true , true , true , true , false, false, true , false },
    { Format::RGBA32_FLOAT         , "RGBA32_FLOAT"         , 16,  1, FormatKind::Float       , true , true , true , true , false, false, true , false },

    { Format::D16                  , "D16"                  ,  2,  1, FormatKind::DepthStencil, false, false, false, false, true , false, false, false },
    { Format::D24S8                , "D24S8"                ,  4,  1, FormatKind::DepthStencil, false, false, false, false, true , true , false, false },
    { Format::X24G8_UINT           , "X24G8_UINT"           ,  4,  1, FormatKind::Integer     , false, false, false, false, false, true , false, false },
    { Format::D32                  , "D32"                  ,  4,  1, FormatKind::DepthStencil, false, false, false, false, true , false, false, false },
    { Format::D32S8                , "D32S8"                ,  8,  1, FormatKind::DepthStencil, false, false, false, false, true , true , false, false },
    { Format::X32G8_UINT           , "X32G8_UINT"           ,  8,  1, FormatKind::Integer     , false, false, false, false, false, true , false, false },

    { Format::BC1_UNORM            , "BC1_UNORM"            ,  8,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC1_UNORM_SRGB       , "BC1_UNORM_SRGB"       ,  8,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BC2_UNORM            , "BC2_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC2_UNORM_SRGB       , "BC2_UNORM_SRGB"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BC3_UNORM            , "BC3_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC3_UNORM_SRGB       , "BC3_UNORM_SRGB"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::BC4_UNORM            , "BC4_UNORM"            ,  8,  4, FormatKind::Normalized  , true , false, false, false, false, false, false, false },
    { Format::BC4_SNORM            , "BC4_SNORM"            ,  8,  4, FormatKind::Normalized  , true , false, false, false, false, false, true , false },
    { Format::BC5_UNORM            , "BC5_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , false, false, false, false, false, false },
    { Format::BC5_SNORM            , "BC5_SNORM"            , 16,  4, FormatKind::Normalized  , true , true , false, false, false, false, true , false },
    { Format::BC6H_UFLOAT          , "BC6H_UFLOAT"          , 16,  4, FormatKind::Float       , true , true , true , false, false, false, false, false },
    { Format::BC6H_SFLOAT          , "BC6H_SFLOAT"          , 16,  4, FormatKind::Float       , true , true , true , false, false, false, true , false },
    { Format::BC7_UNORM            , "BC7_UNORM"            , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::BC7_UNORM_SRGB       , "BC7_UNORM_SRGB"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },

    { Format::ASTC_4x4_UNORM       , "ASTC_4x4_UNORM"       , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_4x4_UNORM_SRGB  , "ASTC_4x4_UNORM_SRGB"  , 16,  4, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_4x4_FLOAT       , "ASTC_4x4_FLOAT"       , 16,  4, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_5x4_UNORM       , "ASTC_5x4_UNORM"       , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_5x4_UNORM_SRGB  , "ASTC_5x4_UNORM_SRGB"  , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_5x4_FLOAT       , "ASTC_5x4_FLOAT"       , 16,  5, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_5x5_UNORM       , "ASTC_5x5_UNORM"       , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_5x5_UNORM_SRGB  , "ASTC_5x5_UNORM_SRGB"  , 16,  5, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_5x5_FLOAT       , "ASTC_5x5_FLOAT"       , 16,  5, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_6x5_UNORM       , "ASTC_6x5_UNORM"       , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_6x5_UNORM_SRGB  , "ASTC_6x5_UNORM_SRGB"  , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_6x5_FLOAT       , "ASTC_6x5_FLOAT"       , 16,  6, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_6x6_UNORM       , "ASTC_6x6_UNORM"       , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_6x6_UNORM_SRGB  , "ASTC_6x6_UNORM_SRGB"  , 16,  6, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_6x6_FLOAT       , "ASTC_6x6_FLOAT"       , 16,  6, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_8x5_UNORM       , "ASTC_8x5_UNORM"       , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_8x5_UNORM_SRGB  , "ASTC_8x5_UNORM_SRGB"  , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_8x5_FLOAT       , "ASTC_8x5_FLOAT"       , 16,  8, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_8x6_UNORM       , "ASTC_8x6_UNORM"       , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_8x6_UNORM_SRGB  , "ASTC_8x6_UNORM_SRGB"  , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_8x6_FLOAT       , "ASTC_8x6_FLOAT"       , 16,  8, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x5_UNORM      , "ASTC_10x5_UNORM"      , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x5_UNORM_SRGB , "ASTC_10x5_UNORM_SRGB" , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x5_FLOAT      , "ASTC_10x5_FLOAT"      , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x6_UNORM      , "ASTC_10x6_UNORM"      , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x6_UNORM_SRGB , "ASTC_10x6_UNORM_SRGB" , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x6_FLOAT      , "ASTC_10x6_FLOAT"      , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_8x8_UNORM       , "ASTC_8x8_UNORM"       , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_8x8_UNORM_SRGB  , "ASTC_8x8_UNORM_SRGB"  , 16,  8, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_8x8_FLOAT       , "ASTC_8x8_FLOAT"       , 16,  8, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x8_UNORM      , "ASTC_10x8_UNORM"      , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x8_UNORM_SRGB , "ASTC_10x8_UNORM_SRGB" , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x8_FLOAT      , "ASTC_10x8_FLOAT"      , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_10x10_UNORM     , "ASTC_10x10_UNORM"     , 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_10x10_UNORM_SRGB, "ASTC_10x10_UNORM_SRGB", 16, 10, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_10x10_FLOAT     , "ASTC_10x10_FLOAT"     , 16, 10, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_12x10_UNORM     , "ASTC_12x10_UNORM"     , 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_12x10_UNORM_SRGB, "ASTC_12x10_UNORM_SRGB", 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_12x10_FLOAT     , "ASTC_12x10_FLOAT"     , 16, 12, FormatKind::Float       , true , true , true , true , false, false, true , false },
    { Format::ASTC_12x12_UNORM     , "ASTC_12x12_UNORM"     , 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, false },
    { Format::ASTC_12x12_UNORM_SRGB, "ASTC_12x12_UNORM_SRGB", 16, 12, FormatKind::Normalized  , true , true , true , true , false, false, false, true  },
    { Format::ASTC_12x12_FLOAT     , "ASTC_12x12_FLOAT"     , 16, 12, FormatKind::Float       , true , true , true , true , false, false, true , false },
};

const FormatInfo& GetFormatInfo(Format::Enum format)noexcept{
    NWB_ASSERT_MSG(static_cast<usize>(format) < static_cast<usize>(Format::kCount), NWB_TEXT("Format::Enum out of range"));
    return s_FormatInfo[static_cast<u32>(format)];
}


TextureSlice TextureSlice::resolve(const TextureDesc& desc)const{
    TextureSlice ret(*this);
    NWB_ASSERT(mipLevel < desc.mipLevels);
    
    if(width == static_cast<u32>(-1))
        ret.width = Max(desc.width >> mipLevel, static_cast<u32>(1));
    if(height == static_cast<u32>(-1))
        ret.height = Max(desc.height >> mipLevel, static_cast<u32>(1));
    
    if(depth == static_cast<u32>(-1)){
        if(desc.dimension == TextureDimension::Texture3D)
            ret.depth = Max(desc.depth >> mipLevel, static_cast<u32>(1));
        else{
            ret.depth = 1;
        }
    }

    return ret;
}


TextureSubresourceSet TextureSubresourceSet::resolve(const TextureDesc& desc, bool singleMipLevel)const{
    TextureSubresourceSet ret;
    ret.baseMipLevel = baseMipLevel;

    if(singleMipLevel)
        ret.numMipLevels = 1;
    else{
        auto lastMipLevelPlusOne = Min(baseMipLevel + numMipLevels, desc.mipLevels);
        ret.numMipLevels = static_cast<MipLevel>(Max(static_cast<u32>(0), lastMipLevelPlusOne - baseMipLevel));
    }

    switch(desc.dimension){
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMSArray:
    {
        ret.baseArraySlice = baseArraySlice;
        auto lastArraySlicePlusOne = static_cast<i32>(Min(baseArraySlice + numArraySlices, desc.arraySize));
        ret.numArraySlices = static_cast<ArraySlice>(Max(static_cast<i32>(0), lastArraySlicePlusOne - static_cast<i32>(baseArraySlice)));
        break;
    }
    default: 
        ret.baseArraySlice = 0;
        ret.numArraySlices = 1;
        break;
    }

    return ret;
}
bool TextureSubresourceSet::isEntireTexture(const TextureDesc& desc)const{
    if(baseMipLevel > 0u || baseMipLevel + numMipLevels < desc.mipLevels)
        return false;

    switch(desc.dimension){
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMSArray: 
        if(baseArraySlice > 0u || baseArraySlice + numArraySlices < desc.arraySize)
            return false;
    }
    
    return true;
}


BufferRange BufferRange::resolve(const BufferDesc& desc)const{
    BufferRange ret;
    ret.byteOffset = Min(byteOffset, desc.byteSize);
    
    if(!byteSize)
        ret.byteSize = desc.byteSize - ret.byteOffset;
    else
        ret.byteSize = Min(byteSize, desc.byteSize - ret.byteOffset);

    return ret;
}


bool BlendState::RenderTarget::usesConstantColor()const{
    return 
        srcBlend == BlendFactor::ConstantColor || srcBlend == BlendFactor::OneMinusConstantColor
        || destBlend == BlendFactor::ConstantColor || destBlend == BlendFactor::OneMinusConstantColor
        || srcBlendAlpha == BlendFactor::ConstantColor || srcBlendAlpha == BlendFactor::OneMinusConstantColor
        || destBlendAlpha == BlendFactor::ConstantColor || destBlendAlpha == BlendFactor::OneMinusConstantColor
        ;
}

bool BlendState::usesConstantColor(u32 numTargets)const{
    NWB_ASSERT(numTargets <= s_MaxRenderTargets);
    for(u32 rt = 0; rt < numTargets && rt < s_MaxRenderTargets; ++rt){
        if(targets[rt].usesConstantColor())
            return true;
    }

    return false;
}


FramebufferInfo::FramebufferInfo(const FramebufferDesc& desc){
    for(usize i = 0; i < desc.colorAttachments.size(); ++i){
        const FramebufferAttachment& attachment = desc.colorAttachments[i];
        colorFormats.push_back(attachment.format == Format::UNKNOWN && attachment.texture ? attachment.texture->getDescription().format : attachment.format);
    }

    if(desc.depthAttachment.valid()){
        const TextureDesc& textureDesc = desc.depthAttachment.texture->getDescription();
        depthFormat = textureDesc.format;
        sampleCount = textureDesc.sampleCount;
        sampleQuality = textureDesc.sampleQuality;
    }
    else if(!desc.colorAttachments.empty() && desc.colorAttachments[0].valid()){
        const TextureDesc& textureDesc = desc.colorAttachments[0].texture->getDescription();
        sampleCount = textureDesc.sampleCount;
        sampleQuality = textureDesc.sampleQuality;
    }
}

FramebufferInfoEx::FramebufferInfoEx(const FramebufferDesc& desc) : FramebufferInfo(desc){
    if(desc.depthAttachment.valid()){
        const TextureDesc& textureDesc = desc.depthAttachment.texture->getDescription();
        TextureSubresourceSet const subresources = desc.depthAttachment.subresources.resolve(textureDesc, true);
        width = Max(textureDesc.width >> subresources.baseMipLevel, static_cast<u32>(1));
        height = Max(textureDesc.height >> subresources.baseMipLevel, static_cast<u32>(1));
        arraySize = subresources.numArraySlices;
    }
    else if(!desc.colorAttachments.empty() && desc.colorAttachments[0].valid()){
        const TextureDesc& textureDesc = desc.colorAttachments[0].texture->getDescription();
        TextureSubresourceSet const subresources = desc.colorAttachments[0].subresources.resolve(textureDesc, true);
        width = Max(textureDesc.width >> subresources.baseMipLevel, static_cast<u32>(1));
        height = Max(textureDesc.height >> subresources.baseMipLevel, static_cast<u32>(1));
        arraySize = subresources.numArraySlices;
    }
}


constexpr usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type){
    switch(type){
    case CooperativeVectorDataType::UInt8:
    case CooperativeVectorDataType::SInt8:
        return 1;
    case CooperativeVectorDataType::UInt8Packed:
    case CooperativeVectorDataType::SInt8Packed:
        // Not sure if this is correct or even relevant because packed types
        // cannot be used in matrices accessible from the host side.
        return 1;
    case CooperativeVectorDataType::UInt16:
    case CooperativeVectorDataType::SInt16:
        return 2;
    case CooperativeVectorDataType::UInt32:
    case CooperativeVectorDataType::SInt32:
        return 4;
    case CooperativeVectorDataType::UInt64:
    case CooperativeVectorDataType::SInt64:
        return 8;
    case CooperativeVectorDataType::FloatE4M3:
    case CooperativeVectorDataType::FloatE5M2:
        return 1;
    case CooperativeVectorDataType::Float16:
    case CooperativeVectorDataType::BFloat16:
        return 2;
    case CooperativeVectorDataType::Float32:
        return 4;
    case CooperativeVectorDataType::Float64:
        return 8;
    }
    NWB_ASSERT_MSG(false, NWB_TEXT("Unknown CooperativeVectorDataType::Enum value"));
    return 0;
}

constexpr usize GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, u32 rows, u32 columns){
    const usize dataTypeSize = GetCooperativeVectorDataTypeSize(type);
        
    switch(layout){
    case CooperativeVectorMatrixLayout::RowMajor:
        return dataTypeSize * columns;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return dataTypeSize * rows;
    }
    return 0;
}


void ICommandList::setResourceStatesForFramebuffer(IFramebuffer& framebuffer){
    const FramebufferDesc& desc = framebuffer.getDescription();

    for(const auto& attachment : desc.colorAttachments)
        setTextureState(attachment.texture, attachment.subresources, ResourceStates::RenderTarget);

    if(desc.depthAttachment.valid())
        setTextureState(desc.depthAttachment.texture, desc.depthAttachment.subresources, desc.depthAttachment.isReadOnly ? ResourceStates::DepthRead : ResourceStates::DepthWrite);
        
    if(desc.shadingRateAttachment.valid())
        setTextureState(desc.shadingRateAttachment.texture, desc.shadingRateAttachment.subresources, ResourceStates::ShadingRateSurface);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const AString s_NotFoundMarkerString = "ERROR: could not resolve marker";
static constexpr usize s_NumDestroyedMarkerTrackers = 2;


AftermathMarkerTracker::AftermathMarkerTracker()
    : m_eventStack()
    , m_eventHashes()
    , m_oldestHashIndex(0)
    , m_eventStrings()
{}

usize AftermathMarkerTracker::pushEvent(const char* name){
    m_eventStack.append(name);
    auto eventString = m_eventStack.generic_string<char>();
    usize hash = Hash<decltype(eventString)>{}(eventString);

    if(m_eventStrings.find(hash) == m_eventStrings.end()){
        m_eventStrings.erase(m_eventHashes[m_oldestHashIndex]);
        m_eventStrings[hash] = eventString;
        m_eventHashes[m_oldestHashIndex] = hash;
        m_oldestHashIndex = (m_oldestHashIndex + 1) % s_MaxAftermathEventStrings;
    }

    return hash;
}

void AftermathMarkerTracker::popEvent(){
    m_eventStack = m_eventStack.parent_path();
}

Pair<bool, AString> AftermathMarkerTracker::getEventString(usize hash){
    auto found = m_eventStrings.find(hash);
    if(found != m_eventStrings.end())
        return MakePair(true, found->second);

    return MakePair(false, s_NotFoundMarkerString);
}


AftermathCrashDumpHelper::AftermathCrashDumpHelper()
    : m_markerTrackers()
    , m_destroyedMarkerTrackers()
    , m_shaderBinaryLookupCallbacks()
{}

void AftermathCrashDumpHelper::registerAftermathMarkerTracker(AftermathMarkerTracker& tracker){
    m_markerTrackers.insert(&tracker);
}

void AftermathCrashDumpHelper::unRegisterAftermathMarkerTracker(AftermathMarkerTracker& tracker){
    if(m_destroyedMarkerTrackers.size() >= s_NumDestroyedMarkerTrackers)
        m_destroyedMarkerTrackers.pop_front();
    
    m_destroyedMarkerTrackers.push_back(tracker);
    m_markerTrackers.erase(&tracker);
}

void AftermathCrashDumpHelper::registerShaderBinaryLookupCallback(void* client, ShaderBinaryLookupCallback lookupCallback){
    m_shaderBinaryLookupCallbacks[client] = lookupCallback;
}

void AftermathCrashDumpHelper::unRegisterShaderBinaryLookupCallback(void* client){
    m_shaderBinaryLookupCallbacks.erase(client);
}

Pair<bool, AString> AftermathCrashDumpHelper::resolveMarker(usize markerHash){
    // Search in active marker trackers
    for(auto* markerTracker : m_markerTrackers){
        auto result = markerTracker->getEventString(markerHash);
        if(result.first()){
            return result;
        }
    }

    // Search in recently destroyed marker trackers
    for(auto& markerTracker : m_destroyedMarkerTrackers){
        auto result = markerTracker.getEventString(markerHash);
        if(result.first()){
            return result;
        }
    }

    return MakePair(false, s_NotFoundMarkerString);
}

Pair<const void*, usize> AftermathCrashDumpHelper::findShaderBinary(u64 shaderHash, ShaderHashGeneratorFunction hashGenerator){
    for(auto& clientCallback : m_shaderBinaryLookupCallbacks){
        auto result = clientCallback.second(shaderHash, hashGenerator);
        if(result.second() > 0){
            return result;
        }
    }

    return MakePair(static_cast<const void*>(nullptr), static_cast<usize>(0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IDeviceManager::IDeviceManager(const DeviceCreationParameters& params)
    : m_deviceParams(params)
{}

bool IDeviceManager::createHeadlessDevice(){
    m_deviceParams.headlessDevice = true;

    if(!m_instanceCreated){
        if(!createInstanceInternal())
            return false;
        m_instanceCreated = true;
    }

    if(!createDeviceInternal())
        return false;

    NWB_LOGGER_INFO(NWB_TEXT("DeviceManager: Headless device created."));
    return true;
}

bool IDeviceManager::createWindowDeviceAndSwapChain(const Common::FrameData& frameData){
    m_deviceParams.headlessDevice = false;

    m_deviceParams.backBufferWidth = frameData.width();
    m_deviceParams.backBufferHeight = frameData.height();

    extractPlatformHandles(frameData);

    if(!m_instanceCreated){
        if(!createInstanceInternal())
            return false;
        m_instanceCreated = true;
    }

    if(!createDeviceInternal())
        return false;

    if(!createSwapChainInternal())
        return false;

    m_deviceParams.backBufferWidth = 0;
    m_deviceParams.backBufferHeight = 0;
    updateWindowState(frameData.width(), frameData.height(), true, true);
    m_previousFrameTimestamp = TimerNow();

    return true;
}

bool IDeviceManager::createInstance(const InstanceParameters& params){
    m_deviceParams.enableDebugRuntime = params.enableDebugRuntime;
    m_deviceParams.enableWarningsAsErrors = params.enableWarningsAsErrors;
    m_deviceParams.headlessDevice = params.headlessDevice;
    m_deviceParams.enableAftermath = params.enableAftermath;
    m_deviceParams.logBufferLifetime = params.logBufferLifetime;
    m_deviceParams.enablePerMonitorDPI = params.enablePerMonitorDPI;
    m_deviceParams.vulkanLibraryName = params.vulkanLibraryName;
    m_deviceParams.requiredVulkanInstanceExtensions = params.requiredVulkanInstanceExtensions;
    m_deviceParams.requiredVulkanLayers = params.requiredVulkanLayers;
    m_deviceParams.optionalVulkanInstanceExtensions = params.optionalVulkanInstanceExtensions;
    m_deviceParams.optionalVulkanLayers = params.optionalVulkanLayers;

    if(!createInstanceInternal())
        return false;

    m_instanceCreated = true;
    return true;
}

void IDeviceManager::addRenderPassToFront(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_front(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_deviceParams.backBufferWidth, m_deviceParams.backBufferHeight, m_deviceParams.swapChainSampleCount);
}

void IDeviceManager::addRenderPassToBack(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_back(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_deviceParams.backBufferWidth, m_deviceParams.backBufferHeight, m_deviceParams.swapChainSampleCount);
}

void IDeviceManager::removeRenderPass(IRenderPass& pass){
    m_renderPasses.remove(&pass);
}

void IDeviceManager::backBufferResizing(){
    m_swapChainFramebuffers.clear();

    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResizing();
}

void IDeviceManager::backBufferResized(){
    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResized(m_deviceParams.backBufferWidth, m_deviceParams.backBufferHeight, m_deviceParams.swapChainSampleCount);

    u32 backBufferCount = getBackBufferCount();
    m_swapChainFramebuffers.resize(backBufferCount);
    for(u32 index = 0; index < backBufferCount; ++index)
        m_swapChainFramebuffers[index] = getDevice()->createFramebuffer(FramebufferDesc().addColorAttachment(getBackBuffer(index)));

    NWB_LOGGER_INFO(NWB_TEXT("DeviceManager: Back buffer resized to {}x{}"), m_deviceParams.backBufferWidth, m_deviceParams.backBufferHeight);
}

void IDeviceManager::displayScaleChanged(){
    for(auto* renderPass : m_renderPasses)
        renderPass->displayScaleChanged(m_dpiScaleFactorX, m_dpiScaleFactorY);
}

void IDeviceManager::animate(f64 elapsedTime){
    for(auto* renderPass : m_renderPasses){
        renderPass->animate(static_cast<f32>(elapsedTime));
        renderPass->setLatewarpOptions();
    }
}

void IDeviceManager::render(){
    IFramebuffer* framebuffer = m_swapChainFramebuffers[getCurrentBackBufferIndex()].get();

    for(auto* renderPass : m_renderPasses)
        renderPass->render(framebuffer);
}

void IDeviceManager::updateAverageFrameTime(f64 elapsedTime){
    m_frameTimeSum += elapsedTime;
    m_numberOfAccumulatedFrames += 1;

    if(m_frameTimeSum > m_averageTimeUpdateInterval && m_numberOfAccumulatedFrames > 0){
        m_averageFrameTime = m_frameTimeSum / static_cast<f64>(m_numberOfAccumulatedFrames);
        m_numberOfAccumulatedFrames = 0;
        m_frameTimeSum = 0.0;
    }
}

bool IDeviceManager::shouldRenderUnfocused()const{
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
}

bool IDeviceManager::animateRenderPresent(){
    Timer now = TimerNow();
    f64 elapsedTime = DurationInSeconds<f64>(now, m_previousFrameTimestamp);

    if(m_windowVisible && (m_windowIsInFocus || shouldRenderUnfocused())){
        if(m_prevDPIScaleFactorX != m_dpiScaleFactorX || m_prevDPIScaleFactorY != m_dpiScaleFactorY){
            displayScaleChanged();
            m_prevDPIScaleFactorX = m_dpiScaleFactorX;
            m_prevDPIScaleFactorY = m_dpiScaleFactorY;
        }

        if(m_callbacks.beforeAnimate)
            m_callbacks.beforeAnimate(*this, m_frameIndex);

        animate(elapsedTime);

        if(m_callbacks.afterAnimate)
            m_callbacks.afterAnimate(*this, m_frameIndex);

        if(m_frameIndex > 0 || !m_skipRenderOnFirstFrame){
            if(beginFrame()){
                u32 frameIndex = m_frameIndex;

                if(m_skipRenderOnFirstFrame)
                    --frameIndex;

                if(m_callbacks.beforeRender)
                    m_callbacks.beforeRender(*this, frameIndex);
                render();
                if(m_callbacks.afterRender)
                    m_callbacks.afterRender(*this, frameIndex);

                if(m_callbacks.beforePresent)
                    m_callbacks.beforePresent(*this, frameIndex);
                bool presentSuccess = present();
                if(m_callbacks.afterPresent)
                    m_callbacks.afterPresent(*this, frameIndex);

                if(!presentSuccess)
                    return false;
            }
        }
    }

    yield();

    getDevice()->runGarbageCollection();

    updateAverageFrameTime(elapsedTime);
    m_previousFrameTimestamp = now;

    ++m_frameIndex;
    return true;
}

bool IDeviceManager::runFrame(){
    if(m_callbacks.beforeFrame)
        m_callbacks.beforeFrame(*this, m_frameIndex);

    return animateRenderPresent();
}

void IDeviceManager::updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    m_windowVisible = windowVisible;
    m_windowIsInFocus = windowIsInFocus;

    if(!m_windowVisible)
        return;

    if(width == 0 || height == 0){
        m_windowVisible = false;
        return;
    }

    if(
        static_cast<i32>(m_deviceParams.backBufferWidth) != static_cast<i32>(width)
        || static_cast<i32>(m_deviceParams.backBufferHeight) != static_cast<i32>(height)
        || (m_deviceParams.vsyncEnabled != m_requestedVSync && getGraphicsAPI() == GraphicsAPI::VULKAN)
    )
    {
        backBufferResizing();

        m_deviceParams.backBufferWidth = width;
        m_deviceParams.backBufferHeight = height;
        m_deviceParams.vsyncEnabled = m_requestedVSync;

        resizeSwapChain();
        backBufferResized();
    }

    m_deviceParams.vsyncEnabled = m_requestedVSync;
}

void IDeviceManager::getWindowDimensions(i32& width, i32& height){
    width = m_deviceParams.backBufferWidth;
    height = m_deviceParams.backBufferHeight;
}

IFramebuffer* IDeviceManager::getCurrentFramebuffer(){
    return getFramebuffer(getCurrentBackBufferIndex());
}

IFramebuffer* IDeviceManager::getFramebuffer(u32 index){
    if(index < m_swapChainFramebuffers.size())
        return m_swapChainFramebuffers[index].get();
    return nullptr;
}

const tchar* IDeviceManager::getWindowTitle(){
    return m_windowTitle.c_str();
}

void IDeviceManager::setWindowTitle(NotNull<const tchar*> title){
    if(m_windowTitle == title.get())
        return;

    m_windowTitle = title.get();
}

void IDeviceManager::shutdown(){
    m_swapChainFramebuffers.clear();
    destroyDeviceAndSwapChain();
    m_instanceCreated = false;
}

void IDeviceManager::keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods){
    if(key == -1)
        return;

    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->keyboardUpdate(key, scancode, action, mods))
            break;
    }
}

void IDeviceManager::keyboardCharInput(u32 unicode, i32 mods){
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->keyboardCharInput(unicode, mods))
            break;
    }
}

void IDeviceManager::mousePosUpdate(f64 xpos, f64 ypos){
    if(!m_deviceParams.supportExplicitDisplayScaling){
        xpos /= m_dpiScaleFactorX;
        ypos /= m_dpiScaleFactorY;
    }

    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->mousePosUpdate(xpos, ypos))
            break;
    }
}

void IDeviceManager::mouseButtonUpdate(i32 button, i32 action, i32 mods){
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->mouseButtonUpdate(button, action, mods))
            break;
    }
}

void IDeviceManager::mouseScrollUpdate(f64 xoffset, f64 yoffset){
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->mouseScrollUpdate(xoffset, yoffset))
            break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

