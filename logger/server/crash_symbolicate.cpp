// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"
#include "crash_paths.h"

#include <core/crash/package_names.h>
#include <core/crash/reason_names.h>
#include <global/diagnostics.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;


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
    if(summary.reasonKind == "manual"){
        outReport += "exception=manual diagnostic dump\n";
        return;
    }

    outReport += "exception=";
    outReport += summary.reasonKind;
    outReport += "\n";
}

[[nodiscard]] static const char* EventNameFromSummary(const CrashPackageSummary& summary, const CrashReportText& category)noexcept{
    if(!summary.event.empty())
        return summary.event.c_str();
    if(const char* const diagnosticEventName = DiagnosticEventNameFromCategory(category.c_str()))
        return diagnosticEventName;
    if(summary.reasonKind == "manual" || summary.reasonKind == "manual_dump")
        return DiagnosticEventName::s_ManualDump;
    return DiagnosticEventName::s_Crash;
}

struct TriggerSummary{
    CrashReportText category;
    CrashReportText expression;
    CrashReportText message;
    CrashReportText file;
    u64 line = 0u;
    bool present = false;

    explicit TriggerSummary(LogArena& arena)
        : category(arena)
        , expression(arena)
        , message(arena)
        , file(arena)
    {}
};

static void AppendOptionalTriggerField(CrashReportText& outReport, const char* key, const CrashReportText& value){
    if(value.empty())
        return;

    outReport += key;
    outReport += "=";
    outReport += value;
    outReport += "\n";
}

static TriggerSummary ReadTriggerSummary(LogArena& arena, const Path& packageDirectory){
    TriggerSummary trigger(arena);
    CrashReportText triggerText{arena};
    trigger.present = ReadTextFile(packageDirectory / CrashNames::s_TriggerFileName, triggerText) && !triggerText.empty();
    if(!trigger.present)
        return trigger;

    const AStringView triggerView(triggerText.data(), triggerText.size());
    static_cast<void>(FindLineKeyValue(triggerView, "category", trigger.category));
    static_cast<void>(FindLineKeyValue(triggerView, "expression", trigger.expression));
    static_cast<void>(FindLineKeyValue(triggerView, "message", trigger.message));
    static_cast<void>(FindLineKeyValue(triggerView, "file", trigger.file));
    static_cast<void>(FindLineKeyValueU64(triggerView, "line", trigger.line));
    return trigger;
}

static void AppendEventSummary(LogArena& arena, const CrashPackageSummary& summary, const TriggerSummary& trigger, CrashReportText& outReport){
    const char* const event = EventNameFromSummary(summary, trigger.category);

    outReport += "\n[event]\n";
    outReport += "event=";
    outReport += event;
    outReport += "\n";

    if(!trigger.present)
        AppendExceptionSummary(arena, outReport, summary);
}

static void AppendTriggerSummary(CrashReportText& outReport, const TriggerSummary& trigger){
    if(!trigger.present)
        return;

    outReport += "\n[trigger]\n";
    AppendOptionalTriggerField(outReport, "category", trigger.category);
    AppendOptionalTriggerField(outReport, "expression", trigger.expression);
    AppendOptionalTriggerField(outReport, "message", trigger.message);
    AppendOptionalTriggerField(outReport, "file", trigger.file);
    if(trigger.line != 0u){
        outReport += "line=";
        char buffer[s_DecimalTextBufferCapacity] = {};
        outReport += FormatDecimal(static_cast<usize>(trigger.line), buffer);
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
    const Symbolicate::TriggerSummary trigger = Symbolicate::ReadTriggerSummary(arena, packageDirectory);
    Symbolicate::AppendEventSummary(arena, summary, trigger, report);
    Symbolicate::AppendTriggerSummary(report, trigger);

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

