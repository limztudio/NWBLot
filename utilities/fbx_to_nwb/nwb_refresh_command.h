// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

namespace CLI{ class App; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


class NwbRefreshCommand final{
public:
    [[nodiscard]] static b8 isNwbRefreshMode(const ImportOptions& options, Core::Alloc::ScratchArena& scratchArena);
    static i32 run(const CLI::App& app, const OptionPresence& presence, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, ImportOptions& inOutOptions, b8& inOutPrompted);

private:
    static void writeRefreshCount(AStringView name, usize before, usize after, ScratchStringStream& inOutReport);
    static void writeCanonicalizeReport(const SourceMeshCanonicalizeReport& canonicalizeReport, ScratchStringStream& inOutReport);
};


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
