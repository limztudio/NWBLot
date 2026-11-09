// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_report.h"

#include <global/json_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_ManifestReserveBytes = 2048u;
inline constexpr StringView s_InstructionPointerFieldPrefix = "\ninstruction_pointer=";
inline constexpr StringView s_StackPointerFieldPrefix = "\nstack_pointer=";
inline constexpr StringView s_FramePointerFieldPrefix = "\nframe_pointer=";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
void CrashPackageReport::AppendManifestPropertyPrefix(CrashStringT<ArenaT>& out, const StringView key, const b8 isFirst){
    if(!isFirst)
        out += ",\n";
    out += "  \"";
    out += key;
    out += "\": ";
}

StringView CrashPackageReport::ArtifactStrategyName(const CrashRequest& request)noexcept{
    switch(request.platform){
    case PlatformKind::Windows:
        return "windows_minidump_external_handler";
    case PlatformKind::Linux:
        return "linux_os_core_policy_plus_proc_snapshot";
    case PlatformKind::Android:
        return "android_tombstone_next_launch";
    default:
        return "native_platform_artifact";
    }
}

template<typename ArenaT>
CrashStringT<ArenaT> CrashPackageReport::BuildManifest(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> manifest{arena};
    manifest.reserve(s_ManifestReserveBytes);
    manifest += "{\n";
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestFormatKey, true);
    manifest += '"';
    manifest += PackageNames::s_ManifestFormatValue;
    manifest += '"';
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestCrashIdKey, false);
    AppendJsonQuotedText(manifest, request.crashId.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestApplicationKey, false);
    AppendJsonQuotedText(manifest, request.applicationName.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestVersionKey, false);
    AppendJsonQuotedText(manifest, request.versionText.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestBuildIdKey, false);
    AppendJsonQuotedText(manifest, request.buildId.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestAbiKey, false);
    AppendJsonQuotedText(manifest, request.abi.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestPlatformKey, false);
    AppendJsonQuotedText(manifest, PlatformKindName(request.platform));
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestReasonKindKey, false);
    AppendJsonQuotedText(manifest, ReasonKindName(request.reasonKind));
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestReasonCodeKey, false);
    AppendUnsignedU64Text(manifest, request.reasonCode);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestProcessIdKey, false);
    AppendUnsignedU64Text(manifest, request.processId);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestThreadIdKey, false);
    AppendUnsignedU64Text(manifest, request.threadId);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestHasExceptionContextKey, false);
#if defined(NWB_PLATFORM_WINDOWS)
    const b8 hasExceptionContext = request.manualContextSize != 0u || request.exceptionPointers != 0u;
#else
    const b8 hasExceptionContext = request.exceptionPointers != 0u;
#endif
    manifest += hasExceptionContext ? s_TrueText : s_FalseText;
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestFaultAddressKey, false);
    AppendUnsignedU64Text(manifest, request.faultAddress);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestInstructionPointerKey, false);
    AppendUnsignedU64Text(manifest, request.instructionPointer);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestStackPointerKey, false);
    AppendUnsignedU64Text(manifest, request.stackPointer);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestFramePointerKey, false);
    AppendUnsignedU64Text(manifest, request.framePointer);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestEventKey, false);
    AppendJsonQuotedText(manifest, request.event.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerCategoryKey, false);
    AppendJsonQuotedText(manifest, request.triggerCategory.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerExpressionKey, false);
    AppendJsonQuotedText(manifest, request.triggerExpression.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerMessageKey, false);
    AppendJsonQuotedText(manifest, request.triggerMessage.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerFileKey, false);
    AppendJsonQuotedText(manifest, request.triggerFile.data());
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestTriggerLineKey, false);
    AppendUnsignedU64Text(manifest, request.triggerLine);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestDumpDetailModeKey, false);
    AppendJsonQuotedText(manifest, request.dumpDetailMode == DumpDetailMode::Full ? PackageNames::s_ManifestDumpDetailModeFullValue : PackageNames::s_ManifestDumpDetailModeSmallValue);
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestArtifactStrategyKey, false);
    AppendJsonQuotedText(manifest, ArtifactStrategyName(request));
    AppendManifestPropertyPrefix(manifest, PackageNames::s_ManifestHandlerLifetimeKey, false);
    AppendJsonQuotedText(manifest, PackageNames::s_ManifestHandlerLifetimeClientIpcValue);
    manifest += "\n}\n";
    return manifest;
}

template<typename ArenaT>
CrashStringT<ArenaT> CrashPackageReport::BuildMetadataText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    for(u32 i = 0u; i < request.metadataCount; ++i){
        text += request.metadata[i].key.data();
        text += '=';
        text += request.metadata[i].value.data();
        text += '\n';
    }
    return text;
}

template<typename ArenaT>
CrashStringT<ArenaT> CrashPackageReport::BuildBreadcrumbText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    for(u32 i = 0u; i < request.breadcrumbCount; ++i){
        AppendUnsignedU64Text(text, request.breadcrumbs[i].order);
        text += " [";
        text += request.breadcrumbs[i].category.data();
        text += "] ";
        text += request.breadcrumbs[i].message.data();
        text += '\n';
    }
    return text;
}

template<typename ArenaT>
CrashStringT<ArenaT> CrashPackageReport::BuildEmergencyText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    text += "reason=";
    text += ReasonKindName(request.reasonKind);
    text += "\ncode=";
    AppendUnsignedU64Text(text, request.reasonCode);
    text += "\npid=";
    AppendUnsignedU64Text(text, request.processId);
    text += "\ntid=";
    AppendUnsignedU64Text(text, request.threadId);
    text += "\nexception_context=";
    AppendUnsignedU64Text(text, request.exceptionPointers);
    text += "\nfault_address=";
    AppendUnsignedU64Text(text, request.faultAddress);
    text += s_InstructionPointerFieldPrefix;
    AppendUnsignedU64Text(text, request.instructionPointer);
    text += s_StackPointerFieldPrefix;
    AppendUnsignedU64Text(text, request.stackPointer);
    text += s_FramePointerFieldPrefix;
    AppendUnsignedU64Text(text, request.framePointer);
    text += "\nevent=";
    text += request.event.data();
    text += "\ntrigger_category=";
    text += request.triggerCategory.data();
    text += "\ntrigger_expression=";
    text += request.triggerExpression.data();
    text += "\ntrigger_message=";
    text += request.triggerMessage.data();
    text += "\ntrigger_file=";
    text += request.triggerFile.data();
    text += "\ntrigger_line=";
    AppendUnsignedU64Text(text, request.triggerLine);
    text += "\n";
    return text;
}

b8 CrashPackageReport::HasCpuContext(const CrashRequest& request)noexcept{
    return request.faultAddress != 0u || request.instructionPointer != 0u || request.stackPointer != 0u || request.framePointer != 0u;
}

b8 CrashPackageReport::HasCallstack(const CrashRequest& request)noexcept{
    return request.callstackFrameCount != 0u;
}

template<typename ArenaT>
CrashStringT<ArenaT> CrashPackageReport::BuildCpuContextText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    text += "fault_address=";
    AppendUnsignedU64Text(text, request.faultAddress);
    text += s_InstructionPointerFieldPrefix;
    AppendUnsignedU64Text(text, request.instructionPointer);
    text += s_StackPointerFieldPrefix;
    AppendUnsignedU64Text(text, request.stackPointer);
    text += s_FramePointerFieldPrefix;
    AppendUnsignedU64Text(text, request.framePointer);
    text += "\n";
    return text;
}

template<typename ArenaT>
CrashStringT<ArenaT> CrashPackageReport::BuildCallstackText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    const u32 frameCount = request.callstackFrameCount > s_MaxCallstackFrames ? static_cast<u32>(s_MaxCallstackFrames) : request.callstackFrameCount;
    for(u32 i = 0u; i < frameCount; ++i){
        text += '#';
        AppendUnsignedU64Text(text, i);
        text += " 0x";
        AppendHexU64(request.callstackFrames[i], text);
        text += '\n';
    }
    return text;
}

template<typename ArenaT>
CrashStringT<ArenaT> CrashPackageReport::BuildArtifactStrategyText(ArenaT& arena, const CrashRequest& request){
    CrashStringT<ArenaT> text{arena};
    text += "strategy=";
    text += ArtifactStrategyName(request);
    text += '\n';
    text += PackageNames::s_ManifestHandlerLifetimeKey;
    text += '=';
    text += PackageNames::s_ManifestHandlerLifetimeClientIpcValue;
    text += '\n';
    switch(request.platform){
    case PlatformKind::Windows:
        text += "detail=external handler writes a Windows minidump from outside the crashing process\n";
        break;
    case PlatformKind::Linux:
        text += "detail=external handler captures metadata/proc files, then fatal signals are re-raised so OS core policy can produce the core artifact\n";
        break;
    case PlatformKind::Android:
        text += "detail=next launch collects native tombstone information through Android system crash reporting\n";
        break;
    default:
        text += "detail=native platform artifact is expected outside the generic package writer\n";
        break;
    }
    return text;
}

b8 CrashPackageReport::Write(Alloc::PersistentArena& arena, const CrashRequest& request){
    const ::Path<Alloc::PersistentArena> packageDirectory = RequestPendingDirectory(arena, request);
    const auto directory = EnsureDirectories(packageDirectory);
    if(!directory){
        CaptureCrashPackageFilesystemFailure("create_package_directory", directory.error());
        return false;
    }

    if(!WriteTextFile(packageDirectory / PackageNames::s_ManifestFileName, BuildManifest(arena, request)))
        return false;
    if(!WriteTextFile(packageDirectory / PackageNames::s_MetadataFileName, BuildMetadataText(arena, request)))
        return false;
    if(!WriteTextFile(packageDirectory / PackageNames::s_BreadcrumbsFileName, BuildBreadcrumbText(arena, request)))
        return false;
    if(!WriteTextFile(packageDirectory / PackageNames::s_EmergencyFileName, BuildEmergencyText(arena, request)))
        return false;
    if(!WriteTextFile(packageDirectory / PackageNames::s_ArtifactStrategyFileName, BuildArtifactStrategyText(arena, request)))
        return false;
    if(HasCpuContext(request) && !WriteTextFile(packageDirectory / PackageNames::s_CpuContextFileName, BuildCpuContextText(arena, request)))
        return false;
    if(HasCallstack(request) && !WriteTextFile(packageDirectory / PackageNames::s_CallstackFileName, BuildCallstackText(arena, request)))
        return false;
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

