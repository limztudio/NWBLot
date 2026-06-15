// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"
#include "crash_paths.h"

#include <core/crash/package_names.h>
#include <core/crash/reason_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendOptionalTextFile(LogArena& arena, CrashReportText& outReport, const Path& packageDirectory, const char* fileName, const char* label){
    CrashReportText text{arena};
    if(!ReadTextFile(packageDirectory / fileName, text) || text.empty())
        return;

    outReport += "\n[";
    outReport += label;
    outReport += "]\n";
    outReport += text;
    if(outReport.back() != '\n')
        outReport += '\n';
}

void AppendHexAddress(LogArena& arena, CrashReportText& outReport, const u64 address){
    outReport += "0x";
    outReport += FormatHex64A(arena, address);
}

[[nodiscard]] static Path DefaultSymbolStoreDirectory(LogArena& arena){
    return CrashDefaultRootDirectory(arena) / s_CrashSymbolStoreDirectoryName;
}

Path EffectiveSymbolStoreDirectory(LogArena& arena, const CrashSymbolicationConfig& config){
    if(!config.symbolStoreDirectory.empty())
        return Path(arena, config.symbolStoreDirectory);

    return DefaultSymbolStoreDirectory(arena);
}

static void AppendSymbolStoreStatus(LogArena& arena, CrashReportText& outReport, const CrashSymbolicationConfig& config){
    const Path symbolStoreDirectory = EffectiveSymbolStoreDirectory(arena, config);
    outReport += "symbol_store=";
    outReport += PathToString<char>(arena, symbolStoreDirectory);
    outReport += "\nsymbol_store_status=";

    ErrorCode error;
    const bool exists = IsDirectory(symbolStoreDirectory, error);
    if(error)
        outReport += "error";
    else
        outReport += exists ? "present" : "missing";
    outReport += "\n";
}

static void AppendExceptionSummary(LogArena& arena, CrashReportText& outReport, const CrashPackageSummary& summary){
    if(summary.reasonKind == "signal"){
        outReport += "exception=";
        outReport += Core::Crash::PosixSignalName(summary.reasonCode);
        outReport += " (";
        char buffer[s_DecimalTextBufferCapacity] = {};
        outReport += FormatDecimal(static_cast<usize>(summary.reasonCode), buffer);
        outReport += ")\n";
        return;
    }
    if(summary.reasonKind == "windows_exception"){
        outReport += "exception=";
        outReport += Core::Crash::WindowsExceptionName(summary.reasonCode);
        outReport += " ";
        AppendHexAddress(arena, outReport, summary.reasonCode);
        outReport += "\n";
        return;
    }
    if(summary.reasonKind == "terminate"){
        outReport += "exception=std::terminate\n";
        return;
    }
    if(summary.reasonKind == "manual_dump"){
        outReport += "exception=manual diagnostic dump\n";
        return;
    }

    outReport += "exception=";
    outReport += summary.reasonKind;
    outReport += "\n";
}

static bool HasTriggerSummary(const CrashPackageSummary& summary){
    return
        !summary.triggerCategory.empty()
        || !summary.triggerExpression.empty()
        || !summary.triggerMessage.empty()
        || !summary.triggerFile.empty()
        || summary.triggerLine != 0u
    ;
}

static void AppendOptionalTriggerField(CrashReportText& outReport, const char* key, const CrashReportText& value){
    if(value.empty())
        return;

    outReport += key;
    outReport += "=";
    outReport += value;
    outReport += "\n";
}

static void AppendEventSummary(LogArena& arena, const CrashPackageSummary& summary, CrashReportText& outReport){
    outReport += "\n[event]\n";
    outReport += "event=";
    outReport += summary.event;
    outReport += "\n";

    if(!HasTriggerSummary(summary))
        AppendExceptionSummary(arena, outReport, summary);
}

static void AppendTriggerSummary(CrashReportText& outReport, const CrashPackageSummary& summary){
    if(!HasTriggerSummary(summary))
        return;

    outReport += "\n[trigger]\n";
    AppendOptionalTriggerField(outReport, "category", summary.triggerCategory);
    AppendOptionalTriggerField(outReport, "expression", summary.triggerExpression);
    AppendOptionalTriggerField(outReport, "message", summary.triggerMessage);
    AppendOptionalTriggerField(outReport, "file", summary.triggerFile);
    if(summary.triggerLine != 0u){
        outReport += "line=";
        char buffer[s_DecimalTextBufferCapacity] = {};
        outReport += FormatDecimal(static_cast<usize>(summary.triggerLine), buffer);
        outReport += "\n";
    }
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashReportText BuildCrashSymbolicationReport(LogArena& arena, const Path& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config){
    namespace Symbolicate = __hidden_logger_crash_symbolicate;

    CrashReportText report{arena};
    report.reserve(Symbolicate::s_CrashReportReserveBytes);

    report += "crash_id=";
    report += summary.crashId;
    report += "\nplatform=";
    report += summary.platform;
    report += "\nreason=";
    report += summary.reasonKind;
    report += "\nartifact_strategy=";
    report += summary.artifactStrategy;
    report += "\n";
    Symbolicate::AppendSymbolStoreStatus(arena, report, config);
    Symbolicate::AppendEventSummary(arena, summary, report);
    Symbolicate::AppendTriggerSummary(report, summary);

    if(summary.platform == "windows"){
#if defined(NWB_PLATFORM_WINDOWS)
        static_cast<void>(Symbolicate::AppendWindowsMinidumpStack(arena, packageDirectory, summary, config, report));
#else
        report += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=Windows minidump resolver is only available on Windows logserver builds\n";
#endif
    }
    else if(summary.platform == "linux"){
        Symbolicate::AppendLinuxArtifactSummary(arena, packageDirectory, config, report);
    }
    else if(summary.platform == "android"){
        Symbolicate::AppendAndroidTombstoneSummary(arena, packageDirectory, report);
    }
    else{
        report += "status=not_decoded\nresolver=unknown\ndetail=unknown crash platform\n";
    }

    Symbolicate::AppendOptionalTextFile(arena, report, packageDirectory, Core::Crash::PackageNames::s_CpuContextFileName, "cpu_context");
    Symbolicate::AppendOptionalTextFile(arena, report, packageDirectory, Core::Crash::PackageNames::s_SymbolicationFileName, "client_symbolication_note");
    Symbolicate::AppendOptionalTextFile(arena, report, packageDirectory, Core::Crash::PackageNames::s_AndroidCollectionFileName, "android_collection");
    return report;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

