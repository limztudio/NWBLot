// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "minidump_image.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<WString<Core::Alloc::ScratchArena>> DumpImage::readString(const RVA rvaValue, Core::Alloc::ScratchArena& scratchArena)const{
    const auto textByteCount = read<ULONG32>(rvaValue);
    if(!textByteCount || (*textByteCount % sizeof(wchar)) != 0u)
        return MakeUnexpected(Failure{});
    const u64 textOffset = static_cast<u64>(rvaValue) + offsetof(MINIDUMP_STRING, Buffer);
    const u8* const textBytes = rva(textOffset, static_cast<u64>(*textByteCount) + sizeof(wchar));
    if(!textBytes)
        return MakeUnexpected(Failure{});
    const auto terminator = read<wchar>(textOffset + *textByteCount);
    if(!terminator || *terminator != 0)
        return MakeUnexpected(Failure{});
    WString<Core::Alloc::ScratchArena> text(scratchArena);
    text.resize(static_cast<usize>(*textByteCount / sizeof(wchar)));
    if(*textByteCount != 0u)
        FOUNDATION_MEMCPY(text.data(), *textByteCount, textBytes, *textByteCount);
    return text;
}

Expected<Optional<DumpImage>> DumpImage::stream(const MINIDUMP_STREAM_TYPE streamType)const noexcept{
    const auto header = read<MINIDUMP_HEADER>(0u);
    if(!header || header->Signature != MINIDUMP_SIGNATURE || (header->Version & Limit<u16>::s_Max) != MINIDUMP_VERSION)
        return MakeUnexpected(Failure{});
    const u64 directoryBytes = static_cast<u64>(header->NumberOfStreams) * sizeof(MINIDUMP_DIRECTORY);
    if(!rva(header->StreamDirectoryRva, directoryBytes))
        return MakeUnexpected(Failure{});

    Optional<DumpImage> selected;
    for(u32 index = 0u; index < header->NumberOfStreams; ++index){
        const u64 directoryOffset = static_cast<u64>(header->StreamDirectoryRva) + static_cast<u64>(index) * sizeof(MINIDUMP_DIRECTORY);
        const auto directory = read<MINIDUMP_DIRECTORY>(directoryOffset);
        if(!directory)
            return MakeUnexpected(Failure{});
        const u8* const streamBytes = rva(directory->Location.Rva, directory->Location.DataSize);
        if(!streamBytes)
            return MakeUnexpected(Failure{});
        if(directory->StreamType == static_cast<ULONG32>(streamType) && !selected)
            selected = DumpImage{ streamBytes, static_cast<usize>(directory->Location.DataSize) };
    }
    return selected;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

