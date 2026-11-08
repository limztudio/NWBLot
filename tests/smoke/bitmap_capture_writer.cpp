// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bitmap_capture_writer.h"

#include <core/common/log.h>

#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_bitmap_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_BitmapFileHeaderSize = 14u;
static constexpr usize s_BitmapInfoHeaderSize = 40u;
static constexpr usize s_BitmapPixelOffset = s_BitmapFileHeaderSize + s_BitmapInfoHeaderSize;
static constexpr usize s_BitmapBytesPerPixel = 3u;
static constexpr usize s_SourceBytesPerPixel = 4u;
static constexpr u32 s_ByteMask = 0xffu;
static constexpr u32 s_Byte1Shift = 8u;
static constexpr u32 s_Byte2Shift = 16u;
static constexpr u32 s_Byte3Shift = 24u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void WriteU16LE(const u16 value, const NotNull<u8*> destination)noexcept{
    destination.get()[0] = static_cast<u8>(value & s_ByteMask);
    destination.get()[1] = static_cast<u8>((value >> s_Byte1Shift) & s_ByteMask);
}

static void WriteU32LE(const u32 value, const NotNull<u8*> destination)noexcept{
    destination.get()[0] = static_cast<u8>(value & s_ByteMask);
    destination.get()[1] = static_cast<u8>((value >> s_Byte1Shift) & s_ByteMask);
    destination.get()[2] = static_cast<u8>((value >> s_Byte2Shift) & s_ByteMask);
    destination.get()[3] = static_cast<u8>((value >> s_Byte3Shift) & s_ByteMask);
}

static b8 IsBgraFormat(const Core::Format::Enum format)noexcept{
    return format == Core::Format::BGRA8_UNORM || format == Core::Format::BGRA8_UNORM_SRGB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BitmapCaptureWriter::write(const NotNull<const u8*> sourceBytes, const usize sourceRowPitch, Core::Alloc::ScratchArena& scratchArena){
    const u32 width = m_captureDescription.width;
    const u32 height = m_captureDescription.height;
    if(width > static_cast<u32>(Limit<i32>::s_Max) || height > static_cast<u32>(Limit<i32>::s_Max))
        return false;

    const auto sourceRowBytes = TryMultiply<usize>(static_cast<usize>(width), __hidden_bitmap_capture::s_SourceBytesPerPixel);
    if(!sourceRowBytes)
        return false;
    const auto bitmapRowBytes = TryMultiply<usize>(static_cast<usize>(width), __hidden_bitmap_capture::s_BitmapBytesPerPixel);
    if(!bitmapRowBytes || sourceRowPitch < *sourceRowBytes || AddOverflows<usize>(*bitmapRowBytes, 3u))
        return false;

    const usize bitmapRowPitch = (*bitmapRowBytes + 3u) & ~usize(3u);
    const auto bitmapPixelBytes = TryMultiply<usize>(bitmapRowPitch, static_cast<usize>(height));
    if(!bitmapPixelBytes)
        return false;
    if(AddOverflows<usize>(__hidden_bitmap_capture::s_BitmapPixelOffset, *bitmapPixelBytes))
        return false;

    const usize bitmapFileBytes = __hidden_bitmap_capture::s_BitmapPixelOffset + *bitmapPixelBytes;
    if(*bitmapPixelBytes > static_cast<usize>(Limit<u32>::s_Max) || bitmapFileBytes > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    Vector<u8, Core::Alloc::ScratchArena> bitmapBytes(bitmapFileBytes, 0u, scratchArena);
    bitmapBytes[0] = static_cast<u8>('B');
    bitmapBytes[1] = static_cast<u8>('M');
    __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(bitmapFileBytes), MakeNotNull(bitmapBytes.data() + 2u));
    __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(__hidden_bitmap_capture::s_BitmapPixelOffset), MakeNotNull(bitmapBytes.data() + 10u));
    __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(__hidden_bitmap_capture::s_BitmapInfoHeaderSize), MakeNotNull(bitmapBytes.data() + __hidden_bitmap_capture::s_BitmapFileHeaderSize));
    __hidden_bitmap_capture::WriteU32LE(width, MakeNotNull(bitmapBytes.data() + 18u));
    __hidden_bitmap_capture::WriteU32LE(height, MakeNotNull(bitmapBytes.data() + 22u));
    __hidden_bitmap_capture::WriteU16LE(1u, MakeNotNull(bitmapBytes.data() + 26u));
    __hidden_bitmap_capture::WriteU16LE(24u, MakeNotNull(bitmapBytes.data() + 28u));
    __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(*bitmapPixelBytes), MakeNotNull(bitmapBytes.data() + 34u));

    const b8 sourceIsBgra = __hidden_bitmap_capture::IsBgraFormat(m_captureDescription.format);
    for(u32 bitmapY = 0u; bitmapY < height; ++bitmapY){
        const usize sourceY = static_cast<usize>(height - 1u - bitmapY);
        const auto sourceRowOffset = TryMultiply<usize>(sourceY, sourceRowPitch);
        if(!sourceRowOffset)
            return false;
        const auto sourceRow = MakeNotNull(sourceBytes.get() + *sourceRowOffset);
        const auto bitmapRow = MakeNotNull(bitmapBytes.data() + __hidden_bitmap_capture::s_BitmapPixelOffset + static_cast<usize>(bitmapY) * bitmapRowPitch);
        for(u32 x = 0u; x < width; ++x){
            const auto sourcePixel = MakeNotNull(sourceRow.get() + static_cast<usize>(x) * __hidden_bitmap_capture::s_SourceBytesPerPixel);
            const auto bitmapPixel = MakeNotNull(bitmapRow.get() + static_cast<usize>(x) * __hidden_bitmap_capture::s_BitmapBytesPerPixel);
            bitmapPixel.get()[0] = sourceIsBgra ? sourcePixel.get()[0] : sourcePixel.get()[2];
            bitmapPixel.get()[1] = sourcePixel.get()[1];
            bitmapPixel.get()[2] = sourceIsBgra ? sourcePixel.get()[2] : sourcePixel.get()[0];
        }
    }

    const ::Path<Core::Alloc::ScratchArena> outputPath(scratchArena, m_outputPath);
    {
        const auto outputDirectory = outputPath.parentPath();
        if(!outputDirectory.empty() && !EnsureDirectories(outputDirectory))
            return false;
    }

    ::Path<Core::Alloc::ScratchArena> partialPath(scratchArena, outputPath);
    partialPath += NWB_TEXT(".partial");
    if(!WriteBinaryFile(partialPath, bitmapBytes)){
        const auto removed = RemoveFile(partialPath);
        if(!removed)
            NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an incomplete partial capture"));
        return false;
    }
    if(!RenamePath(partialPath, outputPath)){
        const auto removed = RemoveFile(partialPath);
        if(!removed)
            NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an uncommitted partial capture"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

