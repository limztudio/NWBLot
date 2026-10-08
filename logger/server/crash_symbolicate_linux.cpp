// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>
#include <global/process_execution.h>
#include <global/process_memory_map.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr AStringView s_LinuxUnknownSymbolText = "??";
inline constexpr AStringView s_LinuxUnknownLocationPrefix = "??:";
inline constexpr AStringView s_LinuxSymbolFrameSeparator = " <- ";
inline constexpr AStringView s_LinuxSymbolLocationSeparator = " at ";
inline constexpr AStringView s_LinuxSymbolLocationBarePrefix = "at ";
inline constexpr AStringView s_LlvmSymbolizerTool = "llvm-symbolizer";
inline constexpr AStringView s_Addr2LineTool = "addr2line";
inline constexpr AStringView s_LlvmSymbolizerObjectPrefix = "--obj=";
inline constexpr AStringView s_LlvmSymbolizerDemangleFlag = "--demangle";
inline constexpr AStringView s_LlvmSymbolizerFunctionsFlag = "--functions";
inline constexpr AStringView s_LlvmSymbolizerInliningFlag = "--inlining=true";
inline constexpr AStringView s_Addr2LineFunctionsFlag = "-f";
inline constexpr AStringView s_Addr2LineDemangleFlag = "-C";
inline constexpr AStringView s_Addr2LineInlineFlag = "-i";
inline constexpr AStringView s_Addr2LineExeFlag = "-e";
inline constexpr AStringView s_AnonymousModulePath = "<anonymous>";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LinuxProcessMemoryMapTable{
    Vector<LinuxProcessMemoryMapEntry, LogArena> entries;

    explicit LinuxProcessMemoryMapTable(LogArena& arena)
        : entries(arena)
    {}
};

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
struct LinuxSymbolFileCacheEntry{
    CrashReportText modulePath;
    CrashReportText symbolPath;
    bool found = false;

    explicit LinuxSymbolFileCacheEntry(LogArena& arena)
        : modulePath(arena)
        , symbolPath(arena)
    {}
};

struct LinuxSymbolFileCache{
    Vector<LinuxSymbolFileCacheEntry, LogArena> entries;

    explicit LinuxSymbolFileCache(LogArena& arena)
        : entries(arena)
    {}
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u64> ParseCallstackFrameAddress(const AStringView line)noexcept{
    const usize prefix = line.find(s_HexAddressPrefix);
    if(prefix == AStringView::npos)
        return MakeUnexpected(Failure{});

    usize end = prefix + 2u;
    while(end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r' && line[end] != '\n')
        ++end;
    if(end == prefix + 2u)
        return MakeUnexpected(Failure{});

    return ::ParseVariableHexU64(AStringView(line.data() + prefix + 2u, end - prefix - 2u));
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[nodiscard]] static bool IsUnknownSymbolLine(const AStringView line){
    const AStringView trimmed = TrimView(line);
    return trimmed.empty() || trimmed == s_LinuxUnknownSymbolText || StartsWith(trimmed, s_LinuxUnknownLocationPrefix);
}

[[nodiscard]] static Expected<CrashReportText> ExtractSymbolizerResult(
    LogArena& arena,
    const AStringView outputText
){
    CrashReportText outSymbol(arena);

    usize cursor = 0u;
    while(const auto function = NextTrimmedTextLine(outputText, cursor)){
        const auto location = NextTrimmedTextLine(outputText, cursor);

        const bool hasFunction = !IsUnknownSymbolLine(*function);
        const bool hasLocation = location && !IsUnknownSymbolLine(*location);
        if(!hasFunction && !hasLocation)
            continue;

        if(!outSymbol.empty())
            outSymbol += s_LinuxSymbolFrameSeparator;
        bool wroteFrame = false;
        if(hasFunction){
            outSymbol.append(function->data(), function->size());
            wroteFrame = true;
        }
        if(hasLocation){
            if(wroteFrame)
                outSymbol += s_LinuxSymbolLocationSeparator;
            else
                outSymbol += s_LinuxSymbolLocationBarePrefix;
            outSymbol.append(location->data(), location->size());
        }
    }

    if(outSymbol.empty())
        return MakeUnexpected(Failure{});
    return outSymbol;
}

[[nodiscard]] static Expected<CrashReportText> RunLinuxSymbolizerCommand(
    LogArena& arena,
    const Span<const AStringView> arguments
){
    CrashReportText output{arena};
    if(!CaptureProcessOutput(arena, output, arguments))
        return MakeUnexpected(Failure{});

    return ExtractSymbolizerResult(arena, AStringView(output.data(), output.size()));
}

[[nodiscard]] static Expected<CrashReportText> TryRunLinuxSymbolizer(
    LogArena& arena,
    const AStringView toolName,
    const AStringView modulePathText,
    const u64 moduleOffset
){
    CrashReportText addressArgument{arena};
    AppendHexAddress(arena, addressArgument, moduleOffset);

    if(toolName == s_LlvmSymbolizerTool){
        CrashReportText objectArgument{arena};
        objectArgument += s_LlvmSymbolizerObjectPrefix;
        objectArgument.append(modulePathText.data(), modulePathText.size());

        const AStringView arguments[] = {
            s_LlvmSymbolizerTool,
            s_LlvmSymbolizerDemangleFlag,
            s_LlvmSymbolizerFunctionsFlag,
            s_LlvmSymbolizerInliningFlag,
            AStringView(objectArgument),
            AStringView(addressArgument),
        };

        return RunLinuxSymbolizerCommand(arena, arguments);
    }

    CrashReportText modulePathArgument{arena};
    modulePathArgument.append(modulePathText.data(), modulePathText.size());

    const AStringView arguments[] = {
        s_Addr2LineTool,
        s_Addr2LineFunctionsFlag,
        s_Addr2LineDemangleFlag,
        s_Addr2LineInlineFlag,
        s_Addr2LineExeFlag,
        AStringView(modulePathArgument),
        AStringView(addressArgument),
    };

    return RunLinuxSymbolizerCommand(arena, arguments);
}

[[nodiscard]] static Expected<Path> FindLinuxSymbolFile(
    LogArena& arena,
    const AStringView modulePathText,
    const CrashSymbolicationConfig& config
){
    if(modulePathText.empty() || modulePathText == s_AnonymousModulePath)
        return MakeUnexpected(Failure{});

    Path modulePath(arena, modulePathText);
    if(PathIsRegularFile(modulePath)){
        return modulePath;
    }

    const Path symbolStoreDirectory = EffectiveSymbolStoreDirectory(arena, config);
    if(symbolStoreDirectory.empty())
        return MakeUnexpected(Failure{});

    const Path moduleFileName = modulePath.filename();
    if(moduleFileName.empty())
        return MakeUnexpected(Failure{});

    Path symbolStoreCandidate = symbolStoreDirectory / moduleFileName;
    if(PathIsRegularFile(symbolStoreCandidate)){
        return symbolStoreCandidate;
    }

    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<AStringView> FindLinuxSymbolFileText(
    LogArena& arena,
    LinuxSymbolFileCache& cache,
    const AStringView modulePathText,
    const CrashSymbolicationConfig& config
){
    for(const LinuxSymbolFileCacheEntry& entry : cache.entries){
        if(AStringView(entry.modulePath.data(), entry.modulePath.size()) != modulePathText)
            continue;

        if(!entry.found)
            return MakeUnexpected(Failure{});

        return AStringView(entry.symbolPath.data(), entry.symbolPath.size());
    }

    LinuxSymbolFileCacheEntry& entry = cache.entries.emplace_back(arena);
    entry.modulePath.assign(modulePathText.data(), modulePathText.size());

    const auto symbolPath = FindLinuxSymbolFile(arena, modulePathText, config);
    entry.found = symbolPath.has_value();
    if(!entry.found)
        return MakeUnexpected(Failure{});

    const CrashReportText symbolPathText = PathToString<char>(arena, *symbolPath);
    entry.symbolPath.assign(symbolPathText.data(), symbolPathText.size());
    return AStringView(entry.symbolPath.data(), entry.symbolPath.size());
}

[[nodiscard]] static Expected<CrashReportText> ResolveLinuxFrameSymbol(
    LogArena& arena,
    LinuxSymbolFileCache& cache,
    const AStringView modulePathText,
    const u64 moduleOffset,
    const CrashSymbolicationConfig& config
){
    const auto symbolPathText = FindLinuxSymbolFileText(arena, cache, modulePathText, config);
    if(!symbolPathText)
        return MakeUnexpected(Failure{});

    auto llvmSymbol = TryRunLinuxSymbolizer(arena, s_LlvmSymbolizerTool, *symbolPathText, moduleOffset);
    if(llvmSymbol)
        return Move(*llvmSymbol);
    return TryRunLinuxSymbolizer(arena, s_Addr2LineTool, *symbolPathText, moduleOffset);
}
#endif

static void AppendLinuxClientCallstack(
    LogArena& arena,
    const AStringView callstackText,
    const LinuxProcessMemoryMapTable* const procMaps,
    const CrashSymbolicationConfig& config,
    CrashReportText& outReport
){
    static_cast<void>(config);

    outReport += "\n[callstack]\n";

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    LinuxSymbolFileCache symbolFileCache(arena);
#endif

    usize cursor = 0u;
    while(const auto line = NextTextLine(callstackText, cursor)){
        const AStringView trimmed = TrimLeftView(*line);
        if(trimmed.empty())
            continue;

        outReport.append(trimmed.data(), trimmed.size());

        const auto address = ParseCallstackFrameAddress(trimmed);
        if(procMaps && address){
            const auto mapEntry = ::FindLinuxProcessMemoryMapForAddress(procMaps->entries, *address);
            if(mapEntry){
                const u64 moduleOffset = *address - mapEntry->begin;
                const AStringView modulePath = mapEntry->path.empty() ? AStringView("<anonymous>") : mapEntry->path;
                outReport += " ";
                outReport.append(modulePath.data(), modulePath.size());
                outReport += "+";
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
                const u64 symbolOffset = moduleOffset + mapEntry->fileOffset;
                AppendHexAddress(arena, outReport, moduleOffset);
                const auto symbol = ResolveLinuxFrameSymbol(arena, symbolFileCache, modulePath, symbolOffset, config);
                if(symbol){
                    outReport += " ";
                    outReport += *symbol;
                }
#else
                AppendHexAddress(arena, outReport, moduleOffset);
#endif
            }
        }

        outReport += "\n";
    }
}

void AppendLinuxArtifactSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    CrashReportText clientCallstack{arena};
    const bool clientCallstackPresent = ReadTextFile(packageDirectory / CrashNames::s_CallstackFileName, clientCallstack) && !clientCallstack.empty();

    outReport += clientCallstackPresent
        ? "status=callstack_captured\nresolver=linux_client_callstack\n"
        : "status=not_decoded\nresolver=elf_dwarf_core\n"
    ;

    const bool corePresent = PathIsRegularFile(packageDirectory / CrashNames::s_LinuxCoreFileName);
    outReport += corePresent
        ? "core_artifact=present\n"
        : "core_artifact=missing\n"
    ;

    CrashReportText cpuContext{arena};
    CrashReportText procMaps{arena};
    const bool cpuContextPresent = ReadTextFile(packageDirectory / CrashNames::s_CpuContextFileName, cpuContext) && !cpuContext.empty();
    const bool procMapsPresent = ReadTextFile(packageDirectory / CrashNames::s_ProcMapsFileName, procMaps) && !procMaps.empty();
    outReport += procMapsPresent
        ? "proc_maps=present\n"
        : "proc_maps=missing\n"
    ;

    LinuxProcessMemoryMapTable procMapTable(arena);
    if(procMapsPresent)
        ::ParseLinuxProcessMemoryMaps(AStringView(procMaps.data(), procMaps.size()), procMapTable.entries);
    const LinuxProcessMemoryMapTable* const procMapTablePtr = procMapsPresent ? &procMapTable : nullptr;

    const auto instructionPointer = FindLineKeyValueU64(AStringView(cpuContext.data(), cpuContext.size()), "instruction_pointer");
    if(!cpuContextPresent || !instructionPointer || *instructionPointer == 0u){
        outReport += "detail=ELF/DWARF stack resolver requires a Linux core artifact; instruction pointer mapping unavailable\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(arena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
        return;
    }

    outReport += "instruction_pointer=";
    AppendHexAddress(arena, outReport, *instructionPointer);
    outReport += "\n";

    if(!procMapsPresent){
        outReport += "detail=proc maps missing for module lookup; symbolic frame resolution unavailable\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(arena, AStringView(clientCallstack.data(), clientCallstack.size()), nullptr, config, outReport);
        return;
    }

    const auto instructionMapEntry = ::FindLinuxProcessMemoryMapForAddress(procMapTable.entries, *instructionPointer);
    if(!instructionMapEntry){
        outReport += "detail=instruction pointer was not found in proc maps\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(arena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
        return;
    }

    const AStringView modulePath = instructionMapEntry->path.empty() ? AStringView("<anonymous>") : instructionMapEntry->path;
    outReport += "instruction_pointer_module=";
    outReport.append(modulePath.data(), modulePath.size());
    outReport += "\nmodule_relative_ip=";
    AppendHexAddress(arena, outReport, *instructionPointer - instructionMapEntry->begin);
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    outReport += "\nsymbolication_relative_ip=";
    AppendHexAddress(arena, outReport, *instructionPointer - instructionMapEntry->begin + instructionMapEntry->fileOffset);
#endif
    outReport += clientCallstackPresent
        ? "\ndetail=client callstack captured; module frames are symbolized with DWARF when symbols are reachable\n"
        : "\ndetail=module-relative crash address captured; full Linux callstack requires a Linux core artifact and DWARF resolver\n"
    ;
    if(clientCallstackPresent)
        AppendLinuxClientCallstack(arena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

