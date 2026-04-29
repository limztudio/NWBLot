// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.h"

#include <logger/client/logger.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* GraphicsAllocator::allocatePersistentSystemMemory(void* userData, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope){
    static_cast<void>(scope);
    if(!userData)
        return nullptr;

    auto* arena = static_cast<Alloc::MemoryArena*>(userData);
    return arena->allocate(alignment, size);
}

void* GraphicsAllocator::reallocatePersistentSystemMemory(void* userData, void* original, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope){
    static_cast<void>(scope);
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
    if(static_cast<usize>(format) >= static_cast<usize>(Format::kCount))
        return s_FormatInfo[static_cast<u32>(Format::UNKNOWN)];

    return s_FormatInfo[static_cast<u32>(format)];
}

u32 GetFormatBlockWidth(const FormatInfo& formatInfo)noexcept{
    return formatInfo.blockSize;
}

u32 GetFormatBlockHeight(const FormatInfo& formatInfo)noexcept{
    switch(formatInfo.format){
    case Format::ASTC_5x4_UNORM:
    case Format::ASTC_5x4_UNORM_SRGB:
    case Format::ASTC_5x4_FLOAT:
        return 4u;

    case Format::ASTC_6x5_UNORM:
    case Format::ASTC_6x5_UNORM_SRGB:
    case Format::ASTC_6x5_FLOAT:
    case Format::ASTC_8x5_UNORM:
    case Format::ASTC_8x5_UNORM_SRGB:
    case Format::ASTC_8x5_FLOAT:
    case Format::ASTC_10x5_UNORM:
    case Format::ASTC_10x5_UNORM_SRGB:
    case Format::ASTC_10x5_FLOAT:
        return 5u;

    case Format::ASTC_8x6_UNORM:
    case Format::ASTC_8x6_UNORM_SRGB:
    case Format::ASTC_8x6_FLOAT:
    case Format::ASTC_10x6_UNORM:
    case Format::ASTC_10x6_UNORM_SRGB:
    case Format::ASTC_10x6_FLOAT:
        return 6u;

    case Format::ASTC_10x8_UNORM:
    case Format::ASTC_10x8_UNORM_SRGB:
    case Format::ASTC_10x8_FLOAT:
        return 8u;

    case Format::ASTC_12x10_UNORM:
    case Format::ASTC_12x10_UNORM_SRGB:
    case Format::ASTC_12x10_FLOAT:
        return 10u;

    default:
        return formatInfo.blockSize;
    }
}


TextureSlice TextureSlice::resolve(const TextureDesc& desc)const{
    TextureSlice ret(*this);
    NWB_ASSERT(mipLevel < desc.mipLevels);
    const MipLevel resolvedMipLevel = (desc.mipLevels > 0 && mipLevel < desc.mipLevels) ? mipLevel : 0;

    const u32 mipWidth = Max(desc.width >> resolvedMipLevel, static_cast<u32>(1));
    const u32 mipHeight = Max(desc.height >> resolvedMipLevel, static_cast<u32>(1));
    const u32 mipDepth = desc.dimension == TextureDimension::Texture3D
        ? Max(desc.depth >> resolvedMipLevel, static_cast<u32>(1))
        : static_cast<u32>(1)
    ;

    if(width == static_cast<u32>(-1))
        ret.width = x < mipWidth ? mipWidth - x : 0;
    if(height == static_cast<u32>(-1))
        ret.height = y < mipHeight ? mipHeight - y : 0;

    if(depth == static_cast<u32>(-1))
        ret.depth = z < mipDepth ? mipDepth - z : 0;

    return ret;
}


TextureSubresourceSet TextureSubresourceSet::resolve(const TextureDesc& desc, bool singleMipLevel)const{
    TextureSubresourceSet ret;
    ret.baseMipLevel = baseMipLevel;

    const u32 availableMipLevels = baseMipLevel < desc.mipLevels ? desc.mipLevels - baseMipLevel : 0;
    if(singleMipLevel)
        ret.numMipLevels = availableMipLevels > 0 ? 1 : 0;
    else if(numMipLevels == AllMipLevels)
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
        if(numArraySlices == AllArraySlices)
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
bool TextureSubresourceSet::isEntireTexture(const TextureDesc& desc)const{
    const TextureSubresourceSet resolved = resolve(desc, false);

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
    const usize colorAttachmentCount = desc.colorAttachments.size();
    colorFormats.resize(colorAttachmentCount);
    for(usize i = 0; i < colorAttachmentCount; ++i){
        const FramebufferAttachment& attachment = desc.colorAttachments[i];
        colorFormats[i] = attachment.format == Format::UNKNOWN && attachment.texture ? attachment.texture->getDescription().format : attachment.format;
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

static bool ResolveFramebufferAttachmentExtent(const FramebufferAttachment& attachment, u32& outWidth, u32& outHeight, u32& outArraySize){
    const TextureDesc& textureDesc = attachment.texture->getDescription();
    TextureSubresourceSet const subresources = attachment.subresources.resolve(textureDesc, true);
    if(subresources.numMipLevels == 0 || subresources.numArraySlices == 0){
        outWidth = 0;
        outHeight = 0;
        outArraySize = 0;
        return false;
    }

    outWidth = Max(textureDesc.width >> subresources.baseMipLevel, static_cast<u32>(1));
    outHeight = Max(textureDesc.height >> subresources.baseMipLevel, static_cast<u32>(1));
    outArraySize = subresources.numArraySlices;
    return true;
}

FramebufferInfoEx::FramebufferInfoEx(const FramebufferDesc& desc) : FramebufferInfo(desc){
    if(desc.depthAttachment.valid()){
        if(!ResolveFramebufferAttachmentExtent(desc.depthAttachment, width, height, arraySize))
            return;
    }
    else if(!desc.colorAttachments.empty() && desc.colorAttachments[0].valid()){
        if(!ResolveFramebufferAttachmentExtent(desc.colorAttachments[0], width, height, arraySize))
            return;
    }
}


usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type){
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
    NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Unknown CooperativeVectorDataType::Enum value"));
    return 0;
}

usize GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, u32 rows, u32 columns){
    const usize dataTypeSize = GetCooperativeVectorDataTypeSize(type);

    switch(layout){
    case CooperativeVectorMatrixLayout::RowMajor:
        return dataTypeSize * columns;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return dataTypeSize * rows;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        return 0;
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
    usize hash = Hasher<decltype(eventString)>{}(eventString);

    if(m_eventStrings.try_emplace(hash, Move(eventString)).second){
        const usize oldHash = m_eventHashes[m_oldestHashIndex];
        if(oldHash != hash)
            m_eventStrings.erase(oldHash);
        m_eventHashes[m_oldestHashIndex] = hash;
        m_oldestHashIndex = (m_oldestHashIndex + 1) % s_MaxAftermathEventStrings;
    }

    return hash;
}

Pair<bool, AString> AftermathMarkerTracker::getEventString(usize hash){
    auto found = m_eventStrings.find(hash);
    if(found != m_eventStrings.end())
        return MakePair(true, found.value());

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
    m_shaderBinaryLookupCallbacks.insert_or_assign(client, lookupCallback);
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


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

