// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "diagnostic_capture_policy.h"
#include "internal.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


b8 DiagnosticCapturePolicy::allows(const CrashCapturePolicy& policy, const AStringView event, const AStringView category)noexcept{
    if(category == DiagnosticEventCategory::s_Assert)
        return policy.captureAssertions;
    if(category == DiagnosticEventCategory::s_FatalAssert)
        return policy.captureFatalAssertions;
    if(event == DiagnosticEventName::s_Error)
        return policy.captureLoggerErrors;
    if(event == DiagnosticEventName::s_Fatal)
        return policy.captureLoggerFatals;

    return true;
}

u64 DiagnosticCapturePolicy::siteHash(const DiagnosticEventRecord& record)noexcept{
    u64 hash = s_Fnv64OffsetBasis;
    if(!record.event.empty())
        hash = UpdateFnv64TextExact(hash, record.event);
    if(!record.category.empty())
        hash = UpdateFnv64TextExact(hash, record.category);
    if(!record.expression.empty())
        hash = UpdateFnv64TextExact(hash, record.expression);
    if(!record.message.empty())
        hash = UpdateFnv64TextExact(hash, record.message);
    if(!record.file.empty())
        hash = UpdateFnv64TextExact(hash, record.file);
    hash = UpdateFnv64(hash, Span<const u8>(reinterpret_cast<const u8*>(&record.line), sizeof(record.line)));
    return hash;
}

b8 DiagnosticCapturePolicy::canSuppressDuplicate(const CrashDumpResult& result)noexcept{
    return result.requestAccepted() && result.status != CrashDumpStatus::PackageWriteFailed;
}

b8 DiagnosticCapturePolicy::reserve(const DiagnosticEventRecord& record, const AStringView event, const AStringView category)noexcept{
    NothrowScopedLock lock(CrashDetail::g_State.mutex);
    if(!CrashDetail::g_State.installed)
        return false;
    if(!allows(CrashDetail::g_State.capturePolicy, event, category))
        return false;

    const CrashCapturePolicy& policy = CrashDetail::g_State.capturePolicy;
    if(policy.maxDiagnosticDumpsPerProcess != 0u && CrashDetail::g_State.diagnosticCaptureCount >= policy.maxDiagnosticDumpsPerProcess)
        return false;

    CrashDetail::FixedDiagnosticSite* freeSite = nullptr;
    CrashDetail::FixedDiagnosticSite* matchingSite = nullptr;
    const u64 siteHash = DiagnosticCapturePolicy::siteHash(record);
    for(usize i = 0u; i < CrashDetail::s_MaxDiagnosticSites; ++i){
        CrashDetail::FixedDiagnosticSite& site = CrashDetail::g_State.diagnosticSites[i];
        if(site.used && site.hash == siteHash){
            matchingSite = &site;
            break;
        }

        if(!site.used && !freeSite)
            freeSite = &site;
    }

    if(policy.maxDiagnosticDumpsPerSite != 0u){
        if(matchingSite && matchingSite->captureCount >= policy.maxDiagnosticDumpsPerSite)
            return false;
        if(!matchingSite && !freeSite)
            return false;
    }

    if(!matchingSite && freeSite){
        freeSite->used = 1u;
        freeSite->hash = siteHash;
        matchingSite = freeSite;
    }

    ++CrashDetail::g_State.diagnosticCaptureCount;
    if(matchingSite)
        ++matchingSite->captureCount;
    return true;
}


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
