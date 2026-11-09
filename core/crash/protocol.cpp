// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "protocol.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CrashProtocol::IsCurrentCrashRequest(const CrashRequest& request)noexcept{
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;
    if(request.metadataCount > s_MaxMetadata || request.breadcrumbCount > s_MaxBreadcrumbs || request.callstackFrameCount > s_MaxCallstackFrames)
        return false;
    const auto terminated = [](const auto& buffer)noexcept{ return FixedBufferView(buffer).size() < buffer.size(); };
    if(
        !terminated(request.crashId) || !terminated(request.applicationName) || !terminated(request.versionText)
        || !terminated(request.buildId) || !terminated(request.abi) || !terminated(request.spoolDirectory)
        || !terminated(request.logServerUrl) || !terminated(request.crashUploadToken) || !terminated(request.event)
        || !terminated(request.triggerCategory) || !terminated(request.triggerExpression) || !terminated(request.triggerMessage)
        || !terminated(request.triggerFile)
    )
        return false;
    const AStringView crashId = FixedBufferView(request.crashId);
    if(crashId.empty() || crashId == "." || crashId == "..")
        return false;
    for(const char ch : crashId){
        if(!IsAsciiIdentifierChar(ch) && ch != '-' && ch != '.')
            return false;
    }
    for(u32 i = 0u; i < request.metadataCount; ++i){
        if(!terminated(request.metadata[i].key) || !terminated(request.metadata[i].value))
            return false;
    }
    for(u32 i = 0u; i < request.breadcrumbCount; ++i){
        if(!terminated(request.breadcrumbs[i].category) || !terminated(request.breadcrumbs[i].message))
            return false;
    }
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    if(request.manualContextSize == 0u)
        return request.manualExceptionRecordSize == 0u && request.manualContextArchitecture == 0u;
    if(request.platform != PlatformKind::Windows || request.exceptionPointers != 0u)
        return false;
    if(request.manualContextSize != sizeof(CONTEXT) || request.manualExceptionRecordSize != sizeof(EXCEPTION_RECORD) || request.manualContextArchitecture != s_WindowsContextArchitecture)
        return false;
    if((request.manualContext.ContextFlags & s_WindowsContextArchitectureMask) != s_WindowsContextArchitecture || (request.manualContext.ContextFlags & CONTEXT_CONTROL) != CONTEXT_CONTROL)
        return false;
    if((request.manualContext.ContextFlags & s_WindowsExtendedContextFlag) != 0u)
        return false;
    return request.manualExceptionRecord.ExceptionCode == s_ManualDumpExceptionCode && request.manualExceptionRecord.ExceptionRecord == nullptr && request.manualExceptionRecord.NumberParameters == 0u;
#else
    return true;
#endif
}

b8 CrashProtocol::CrashAckMatchesRequest(const CrashAck& ack, const CrashRequest& request)noexcept{
    const AStringView ackId = FixedBufferView(ack.crashId);
    const AStringView requestId = FixedBufferView(request.crashId);
    return ack.magic == s_AckMagic && ack.version == s_RequestVersion && ack.packageWritten <= 1u
        && !ackId.empty() && ackId.size() < ack.crashId.size() && requestId.size() < request.crashId.size() && ackId == requestId;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

