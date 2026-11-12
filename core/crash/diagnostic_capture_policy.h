// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


class DiagnosticCapturePolicy{
public:
    [[nodiscard]] static b8 Reserve(const DiagnosticEventRecord& record, AStringView event, AStringView category)noexcept;
    [[nodiscard]] static b8 CanSuppressDuplicate(const CrashDumpResult& result)noexcept;

private:
    [[nodiscard]] static b8 Allows(const CrashCapturePolicy& policy, AStringView event, AStringView category)noexcept;
    [[nodiscard]] static u64 SiteHash(const DiagnosticEventRecord& record)noexcept;
};


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

