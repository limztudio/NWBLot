
#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>
#include <global/process_execution.h>
#include <global/process_memory_map.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;


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


[[nodiscard]] static bool ParseCallstackFrameAddress(const AStringView line, u64& outAddress){
    const usize prefix = line.find("0x");
    if(prefix == AStringView::npos)
        return false;

    usize end = prefix + 2u;
    while(end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r' && line[end] != '\n')
        ++end;
    if(end == prefix + 2u)
        return false;

    return ::ParseVariableHexU64(AStringView(line.data() + prefix + 2u, end - prefix - 2u), outAddress);
}

static void ParseLinuxProcessMemoryMaps(const AStringView mapsText, LinuxProcessMemoryMapTable& outTable){
    outTable.entries.clear();

    usize cursor = 0u;
    AStringView line;
    while(NextTextLine(mapsText, cursor, line)){
        LinuxProcessMemoryMapEntry entry;
        if(ParseLinuxProcessMemoryMapLine(line, entry))
            outTable.entries.push_back(entry);
    }
}

[[nodiscard]] static bool FindLinuxProcessMemoryMapForAddress(
    const LinuxProcessMemoryMapTable& table,
    const u64 address,
    LinuxProcessMemoryMapEntry& outEntry
){
    for(const LinuxProcessMemoryMapEntry& entry : table.entries){
        if(address < entry.begin || address >= entry.end)
            continue;

        outEntry = entry;
        return true;
    }

    outEntry = LinuxProcessMemoryMapEntry{};
    return false;
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[nodiscard]] static bool IsUnknownSymbolLine(const AStringView line){
    const AStringView trimmed = TrimView(line);
    return trimmed.empty() || trimmed == "??" || StartsWith(trimmed, "??:");
}

[[nodiscard]] static bool ExtractSymbolizerResult(
    CrashReportText& outSymbol,
    const AStringView outputText
){
    outSymbol.clear();

    usize cursor = 0u;
    AStringView function;
    AStringView location;
    while(NextTrimmedTextLine(outputText, cursor, function)){
        location = AStringView();
        const bool readLocation = NextTrimmedTextLine(outputText, cursor, location);

        const bool hasFunction = !IsUnknownSymbolLine(function);
        const bool hasLocation = readLocation && !IsUnknownSymbolLine(location);
        if(!hasFunction && !hasLocation)
            continue;

        if(!outSymbol.empty())
            outSymbol += " <- ";
        bool wroteFrame = false;
        if(hasFunction){
            outSymbol.append(function.data(), function.size());
            wroteFrame = true;
        }
        if(hasLocation){
            if(wroteFrame)
                outSymbol += " at ";
            else
                outSymbol += "at ";
            outSymbol.append(location.data(), location.size());
        }
    }

    return !outSymbol.empty();
}

[[nodiscard]] static bool RunLinuxSymbolizerCommand(
    LogArena& arena,
    const char* const* argv,
    CrashReportText& outSymbol
){
    CrashReportText output{arena};
    if(!CaptureProcessOutput(output, argv))
        return false;

    return ExtractSymbolizerResult(outSymbol, AStringView(output.data(), output.size()));
}

[[nodiscard]] static bool TryRunLinuxSymbolizer(
    LogArena& arena,
    const AStringView toolName,
    const AStringView modulePathText,
    const u64 moduleOffset,
    CrashReportText& outSymbol
){
    CrashReportText addressArgument{arena};
    AppendHexAddress(arena, addressArgument, moduleOffset);

    if(toolName == "llvm-symbolizer"){
        CrashReportText objectArgument{arena};
        objectArgument += "--obj=";
        objectArgument.append(modulePathText.data(), modulePathText.size());

        const char* const argv[] = {
            "llvm-symbolizer",
            "--demangle",
            "--functions",
            "--inlining=true",
            objectArgument.c_str(),
            addressArgument.c_str(),
            nullptr
        };

        return RunLinuxSymbolizerCommand(arena, argv, outSymbol);
    }

    CrashReportText modulePathArgument{arena};
    modulePathArgument.append(modulePathText.data(), modulePathText.size());

    const char* const argv[] = {
        "addr2line",
        "-f",
        "-C",
        "-i",
        "-e",
        modulePathArgument.c_str(),
        addressArgument.c_str(),
        nullptr
    };

    return RunLinuxSymbolizerCommand(arena, argv, outSymbol);
}

[[nodiscard]] static bool FindLinuxSymbolFile(
    LogArena& arena,
    const AStringView modulePathText,
    const CrashSymbolicationConfig& config,
    Path& outPath
){
    if(modulePathText.empty() || modulePathText == "<anonymous>")
        return false;

    const Path modulePath(arena, modulePathText);
    if(PathIsRegularFile(modulePath)){
        outPath = modulePath;
        return true;
    }

    const Path symbolStoreDirectory = EffectiveSymbolStoreDirectory(arena, config);
    if(symbolStoreDirectory.empty())
        return false;

    const Path moduleFileName = modulePath.filename();
    if(moduleFileName.empty())
        return false;

    const Path symbolStoreCandidate = symbolStoreDirectory / moduleFileName;
    if(PathIsRegularFile(symbolStoreCandidate)){
        outPath = symbolStoreCandidate;
        return true;
    }

    return false;
}

[[nodiscard]] static bool FindLinuxSymbolFileText(
    LogArena& arena,
    LinuxSymbolFileCache& cache,
    const AStringView modulePathText,
    const CrashSymbolicationConfig& config,
    AStringView& outSymbolPathText
){
    outSymbolPathText = AStringView();

    for(const LinuxSymbolFileCacheEntry& entry : cache.entries){
        if(AStringView(entry.modulePath.data(), entry.modulePath.size()) != modulePathText)
            continue;

        if(!entry.found)
            return false;

        outSymbolPathText = AStringView(entry.symbolPath.data(), entry.symbolPath.size());
        return true;
    }

    LinuxSymbolFileCacheEntry& entry = cache.entries.emplace_back(arena);
    entry.modulePath.assign(modulePathText.data(), modulePathText.size());

    Path symbolPath(arena);
    entry.found = FindLinuxSymbolFile(arena, modulePathText, config, symbolPath);
    if(!entry.found)
        return false;

    const CrashReportText symbolPathText = PathToString<char>(arena, symbolPath);
    entry.symbolPath.assign(symbolPathText.data(), symbolPathText.size());
    outSymbolPathText = AStringView(entry.symbolPath.data(), entry.symbolPath.size());
    return true;
}

[[nodiscard]] static bool ResolveLinuxFrameSymbol(
    LogArena& arena,
    LinuxSymbolFileCache& cache,
    const AStringView modulePathText,
    const u64 moduleOffset,
    const CrashSymbolicationConfig& config,
    CrashReportText& outSymbol
){
    AStringView symbolPathText;
    if(!FindLinuxSymbolFileText(arena, cache, modulePathText, config, symbolPathText))
        return false;

    return TryRunLinuxSymbolizer(arena, "llvm-symbolizer", symbolPathText, moduleOffset, outSymbol)
        || TryRunLinuxSymbolizer(arena, "addr2line", symbolPathText, moduleOffset, outSymbol)
    ;
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
    while(cursor < callstackText.size()){
        const usize begin = cursor;
        while(cursor < callstackText.size() && callstackText[cursor] != '\n' && callstackText[cursor] != '\r')
            ++cursor;

        const AStringView line(callstackText.data() + begin, cursor - begin);
        while(cursor < callstackText.size() && (callstackText[cursor] == '\n' || callstackText[cursor] == '\r'))
            ++cursor;

        const AStringView trimmed = TrimLeftView(line);
        if(trimmed.empty())
            continue;

        outReport.append(trimmed.data(), trimmed.size());

        u64 address = 0u;
        if(procMaps && ParseCallstackFrameAddress(trimmed, address)){
            LinuxProcessMemoryMapEntry mapEntry;
            if(FindLinuxProcessMemoryMapForAddress(*procMaps, address, mapEntry)){
                const u64 moduleOffset = address - mapEntry.begin;
                const AStringView modulePath = mapEntry.path.empty() ? AStringView("<anonymous>") : mapEntry.path;
                outReport += " ";
                outReport.append(modulePath.data(), modulePath.size());
                outReport += "+";
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
                const u64 symbolOffset = moduleOffset + mapEntry.fileOffset;
                AppendHexAddress(arena, outReport, moduleOffset);
                CrashReportText symbol{arena};
                if(ResolveLinuxFrameSymbol(arena, symbolFileCache, modulePath, symbolOffset, config, symbol)){
                    outReport += " ";
                    outReport += symbol;
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
        ParseLinuxProcessMemoryMaps(AStringView(procMaps.data(), procMaps.size()), procMapTable);
    const LinuxProcessMemoryMapTable* const procMapTablePtr = procMapsPresent ? &procMapTable : nullptr;

    u64 instructionPointer = 0u;
    if(!cpuContextPresent || !FindLineKeyValueU64(AStringView(cpuContext.data(), cpuContext.size()), "instruction_pointer", instructionPointer) || instructionPointer == 0u){
        outReport += "detail=ELF/DWARF stack resolver requires a Linux core artifact; instruction pointer mapping unavailable\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(arena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
        return;
    }

    outReport += "instruction_pointer=";
    AppendHexAddress(arena, outReport, instructionPointer);
    outReport += "\n";

    if(!procMapsPresent){
        outReport += "detail=proc maps missing for module lookup; symbolic frame resolution unavailable\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(arena, AStringView(clientCallstack.data(), clientCallstack.size()), nullptr, config, outReport);
        return;
    }

    LinuxProcessMemoryMapEntry instructionMapEntry;
    if(!FindLinuxProcessMemoryMapForAddress(procMapTable, instructionPointer, instructionMapEntry)){
        outReport += "detail=instruction pointer was not found in proc maps\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(arena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
        return;
    }

    const AStringView modulePath = instructionMapEntry.path.empty() ? AStringView("<anonymous>") : instructionMapEntry.path;
    outReport += "instruction_pointer_module=";
    outReport.append(modulePath.data(), modulePath.size());
    outReport += "\nmodule_relative_ip=";
    AppendHexAddress(arena, outReport, instructionPointer - instructionMapEntry.begin);
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    outReport += "\nsymbolication_relative_ip=";
    AppendHexAddress(arena, outReport, instructionPointer - instructionMapEntry.begin + instructionMapEntry.fileOffset);
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

