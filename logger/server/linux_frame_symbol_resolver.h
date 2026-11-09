// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_symbolicate.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
NWB_LOG_BEGIN


namespace LoggerCrashSymbolicateDetail{


struct LinuxSymbolFileCacheEntry{
    AString<Core::Alloc::ScratchArena> modulePath;
    AString<Core::Alloc::ScratchArena> symbolPath;
    b8 found = false;


public:
    explicit LinuxSymbolFileCacheEntry(Core::Alloc::ScratchArena& arena)noexcept(noexcept(decltype(modulePath)(arena)) && noexcept(decltype(symbolPath)(arena)))
        : modulePath(arena)
        , symbolPath(arena)
    {}
};

struct LinuxSymbolFileCache{
    Vector<LinuxSymbolFileCacheEntry, Core::Alloc::ScratchArena> entries;


public:
    explicit LinuxSymbolFileCache(Core::Alloc::ScratchArena& arena)noexcept(noexcept(decltype(entries)(arena)))
        : entries(arena)
    {}
};


class LinuxFrameSymbolResolver final : NoCopy{
public:
    LinuxFrameSymbolResolver(const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& arena, usize frameCount);
    [[nodiscard]] Expected<AString<Core::Alloc::ScratchArena>> resolve(AStringView modulePathText, u64 moduleOffset);


private:
    const CrashSymbolicationConfig& m_config;
    Core::Alloc::ScratchArena& m_arena;
    LinuxSymbolFileCache m_cache;
};


};


NWB_LOG_END
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

