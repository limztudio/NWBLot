// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


class DiagnosticCapturePolicy{
public:
    [[nodiscard]] static b8 reserve(const DiagnosticEventRecord& record, AStringView event, AStringView category)noexcept;
    [[nodiscard]] static b8 canSuppressDuplicate(const CrashDumpResult& result)noexcept;

private:
    [[nodiscard]] static b8 allows(const CrashCapturePolicy& policy, AStringView event, AStringView category)noexcept;
    [[nodiscard]] static u64 siteHash(const DiagnosticEventRecord& record)noexcept;
};


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
