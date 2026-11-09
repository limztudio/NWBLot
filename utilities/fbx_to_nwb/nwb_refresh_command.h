// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

namespace CLI{ class App; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


class NwbRefreshCommand final{
public:
    [[nodiscard]] static b8 IsNwbRefreshMode(const ImportOptions& options, Core::Alloc::ScratchArena& scratchArena);
    static i32 Run(const CLI::App& app, const OptionPresence& presence, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, ImportOptions& inOutOptions, b8& inOutPrompted);

private:
    static void WriteRefreshCount(AStringView name, usize before, usize after, ScratchStringStream& inOutReport);
    static void WriteCanonicalizeReport(const SourceMeshCanonicalizeReport& canonicalizeReport, ScratchStringStream& inOutReport);
};


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

