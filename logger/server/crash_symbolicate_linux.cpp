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
    Vector<LinuxProcessMemoryMapEntry, Core::Alloc::ScratchArena> entries;


public:
    explicit LinuxProcessMemoryMapTable(Core::Alloc::ScratchArena& arena)noexcept(noexcept(decltype(entries)(arena)))
        : entries(arena)
    {}
};

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
struct LinuxSymbolFileCacheEntry{
    AString<Core::Alloc::ScratchArena> modulePath;
    AString<Core::Alloc::ScratchArena> symbolPath;
    bool found = false;


public:
    explicit LinuxSymbolFileCacheEntry(Core::Alloc::ScratchArena& arena)noexcept(noexcept(decltype(modulePath)(arena)) && noexcept(decltype(symbolPath)(arena)))
        : modulePath(arena)
        , symbolPath(arena)
    {}
};

struct LinuxSymbolFileCache{
    Vector<LinuxSymbolFileCacheEntry, Core::Alloc::ScratchArena> entries;


public:
    explicit LinuxSymbolFileCache(Core::Alloc::ScratchArena& arena)noexcept(noexcept(decltype(entries)(arena)))
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
[[nodiscard]] static bool IsUnknownSymbolLine(const AStringView line)noexcept{
    const AStringView trimmed = TrimView(line);
    return trimmed.empty() || trimmed == s_LinuxUnknownSymbolText || StartsWith(trimmed, s_LinuxUnknownLocationPrefix);
}

[[nodiscard]] static Expected<AString<Core::Alloc::ScratchArena>> ExtractSymbolizerResult(Core::Alloc::ScratchArena& arena, const AStringView outputText){
    AString<Core::Alloc::ScratchArena> outSymbol(arena);
    outSymbol.reserve(AddSize(outputText.size(), SizeOf<3u>(outputText.size())));

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

[[nodiscard]] static Expected<AString<Core::Alloc::ScratchArena>> RunLinuxSymbolizerCommand(Core::Alloc::ScratchArena& arena, const Span<const AStringView> arguments){
    AString<Core::Alloc::ScratchArena> output{arena};
    output.reserve(ProcessExecutionDetail::s_DefaultCaptureOutputMaxBytes);
    const auto captured = CaptureProcessOutput(arena, output, arguments);
    if(!captured){
        const ProcessCaptureFailure& failure = captured.error();
        NWB_LOGGER_INFO(NWB_TEXT("Linux symbolizer capture failed: phase {}, native {}, completion {}, exit {}, signal {}, truncated {}")
            , static_cast<u32>(failure.kind)
            , static_cast<i32>(failure.nativeError)
            , static_cast<i32>(failure.completionError)
            , failure.exitCode
            , failure.terminationSignal
            , failure.outputTruncated
        );
        return MakeUnexpected(Failure{});
    }

    return ExtractSymbolizerResult(arena, AStringView(output.data(), output.size()));
}

[[nodiscard]] static Expected<AString<Core::Alloc::ScratchArena>> TryRunLinuxSymbolizer(Core::Alloc::ScratchArena& arena, const AStringView toolName, const AStringView modulePathText, const u64 moduleOffset){
    AString<Core::Alloc::ScratchArena> addressArgument{arena};
    addressArgument.reserve(s_HexAddressPrefix.size() + 16u);
    addressArgument += s_HexAddressPrefix;
    AppendHexU64(moduleOffset, addressArgument);

    if(toolName == s_LlvmSymbolizerTool){
        AString<Core::Alloc::ScratchArena> objectArgument{arena};
        objectArgument.reserve(AddSize(s_LlvmSymbolizerObjectPrefix.size(), modulePathText.size()));
        objectArgument += s_LlvmSymbolizerObjectPrefix;
        objectArgument.append(modulePathText.data(), modulePathText.size());

        const AStringView arguments[] = { s_LlvmSymbolizerTool, s_LlvmSymbolizerDemangleFlag, s_LlvmSymbolizerFunctionsFlag, s_LlvmSymbolizerInliningFlag, AStringView(objectArgument), AStringView(addressArgument), };

        return RunLinuxSymbolizerCommand(arena, arguments);
    }

    AString<Core::Alloc::ScratchArena> modulePathArgument{arena};
    modulePathArgument.reserve(modulePathText.size());
    modulePathArgument.append(modulePathText.data(), modulePathText.size());

    const AStringView arguments[] = { s_Addr2LineTool, s_Addr2LineFunctionsFlag, s_Addr2LineDemangleFlag, s_Addr2LineInlineFlag, s_Addr2LineExeFlag, AStringView(modulePathArgument), AStringView(addressArgument), };

    return RunLinuxSymbolizerCommand(arena, arguments);
}

[[nodiscard]] static Expected<CrashScratchPath> FindLinuxSymbolFile(Core::Alloc::ScratchArena& arena, const AStringView modulePathText, const CrashSymbolicationConfig& config){
    if(modulePathText.empty() || modulePathText == s_AnonymousModulePath)
        return MakeUnexpected(Failure{});

    CrashScratchPath modulePath(arena, modulePathText);
    const auto regularModule = IsRegularFile(modulePath);
    if(!regularModule){
        NWB_LOGGER_WARNING(NWB_TEXT("Log server: failed to inspect Linux symbol module '{}': {}"), PathToString<tchar>(modulePath), StringConvert(regularModule.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(*regularModule)
        return modulePath;

    const CrashScratchPath symbolStoreDirectory = EffectiveSymbolStoreDirectory(arena, config);
    if(symbolStoreDirectory.empty())
        return MakeUnexpected(Failure{});

    const CrashScratchPath moduleFileName = modulePath.filename();
    if(moduleFileName.empty())
        return MakeUnexpected(Failure{});

    CrashScratchPath symbolStoreCandidate = symbolStoreDirectory / moduleFileName;
    const auto regularCandidate = IsRegularFile(symbolStoreCandidate);
    if(!regularCandidate){
        NWB_LOGGER_WARNING(NWB_TEXT("Log server: failed to inspect Linux symbol-store candidate '{}': {}"), PathToString<tchar>(symbolStoreCandidate), StringConvert(regularCandidate.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(*regularCandidate)
        return symbolStoreCandidate;

    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<AStringView> FindLinuxSymbolFileText(Core::Alloc::ScratchArena& arena, LinuxSymbolFileCache& cache, const AStringView modulePathText, const CrashSymbolicationConfig& config){
    for(const LinuxSymbolFileCacheEntry& entry : cache.entries){
        if(AStringView(entry.modulePath.data(), entry.modulePath.size()) != modulePathText)
            continue;

        if(!entry.found)
            return MakeUnexpected(Failure{});

        return AStringView(entry.symbolPath.data(), entry.symbolPath.size());
    }

    LinuxSymbolFileCacheEntry& entry = cache.entries.emplace_back(cache.entries.get_allocator().arena());
    entry.modulePath.assign(modulePathText.data(), modulePathText.size());

    const auto symbolPath = FindLinuxSymbolFile(arena, modulePathText, config);
    entry.found = symbolPath.has_value();
    if(!entry.found)
        return MakeUnexpected(Failure{});

    const auto symbolPathText = PathToString<char>(cache.entries.get_allocator().arena(), *symbolPath);
    entry.symbolPath.assign(symbolPathText.data(), symbolPathText.size());
    return AStringView(entry.symbolPath.data(), entry.symbolPath.size());
}

[[nodiscard]] static Expected<AString<Core::Alloc::ScratchArena>> ResolveLinuxFrameSymbol(Core::Alloc::ScratchArena& arena, LinuxSymbolFileCache& cache, const AStringView modulePathText, const u64 moduleOffset, const CrashSymbolicationConfig& config){
    const auto symbolPathText = FindLinuxSymbolFileText(arena, cache, modulePathText, config);
    if(!symbolPathText)
        return MakeUnexpected(Failure{});

    auto llvmSymbol = TryRunLinuxSymbolizer(arena, s_LlvmSymbolizerTool, *symbolPathText, moduleOffset);
    if(llvmSymbol)
        return Move(*llvmSymbol);
    return TryRunLinuxSymbolizer(arena, s_Addr2LineTool, *symbolPathText, moduleOffset);
}
#endif

static void AppendLinuxClientCallstack(Core::Alloc::ScratchArena& arena, const AStringView callstackText, const LinuxProcessMemoryMapTable* const procMaps, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    static_cast<void>(config);
    static_cast<void>(arena);

    outReport += "\n[callstack]\n";

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    LinuxSymbolFileCache symbolFileCache(arena);
    usize frameLineCount = 1u;
    for(const char ch : callstackText){
        if(ch == '\n')
            ++frameLineCount;
    }
    symbolFileCache.entries.reserve(frameLineCount);
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
                AppendHexAddress(outReport, moduleOffset);
                const auto symbol = ResolveLinuxFrameSymbol(arena, symbolFileCache, modulePath, symbolOffset, config);
                if(symbol){
                    outReport += " ";
                    outReport += *symbol;
                }
#else
                AppendHexAddress(outReport, moduleOffset);
#endif
            }
        }

        outReport += "\n";
    }
}

void AppendLinuxArtifactSummary(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    const CrashScratchPath& scratchDirectory = packageDirectory;
    AString<Core::Alloc::ScratchArena> clientCallstack(scratchArena);
    const auto clientCallstackRead = ReadTextFile(scratchDirectory / CrashNames::s_CallstackFileName, clientCallstack);
    const bool clientCallstackPresent = clientCallstackRead && !clientCallstack.empty();

    outReport += clientCallstackPresent ? "status=callstack_captured\nresolver=linux_client_callstack\n" : "status=not_decoded\nresolver=elf_dwarf_core\n";
    if(!clientCallstackRead && clientCallstackRead.error() != ErrorCondition::no_such_file_or_directory)
        AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = "read_linux_client_callstack", .code = clientCallstackRead.error(), });

    const auto corePath = scratchDirectory / CrashNames::s_LinuxCoreFileName;
    const auto corePresent = IsRegularFile(corePath);
    if(!corePresent){
        NWB_LOGGER_WARNING(NWB_TEXT("Log server: failed to inspect Linux core artifact '{}': {}"), PathToString<tchar>(corePath), StringConvert(corePresent.error().message()));
        outReport += "core_artifact=inspect_failed\n";
    }
    else
        outReport += *corePresent ? "core_artifact=present\n" : "core_artifact=missing\n";

    AString<Core::Alloc::ScratchArena> cpuContext(scratchArena);
    AString<Core::Alloc::ScratchArena> procMaps(scratchArena);
    const auto cpuContextRead = ReadTextFile(scratchDirectory / CrashNames::s_CpuContextFileName, cpuContext);
    const auto procMapsRead = ReadTextFile(scratchDirectory / CrashNames::s_ProcMapsFileName, procMaps);
    const bool cpuContextPresent = cpuContextRead && !cpuContext.empty();
    const bool procMapsPresent = procMapsRead && !procMaps.empty();
    outReport += procMapsPresent ? "proc_maps=present\n" : "proc_maps=missing\n";
    if(!cpuContextRead && cpuContextRead.error() != ErrorCondition::no_such_file_or_directory)
        AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = "read_linux_cpu_context", .code = cpuContextRead.error(), });
    if(!procMapsRead && procMapsRead.error() != ErrorCondition::no_such_file_or_directory)
        AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = "read_linux_proc_maps", .code = procMapsRead.error(), });

    LinuxProcessMemoryMapTable procMapTable(scratchArena);
    if(procMapsPresent){
        usize mapLineCount = 1u;
        for(const char ch : procMaps){
            if(ch == '\n')
                ++mapLineCount;
        }
        procMapTable.entries.reserve(mapLineCount);
        ::ParseLinuxProcessMemoryMaps(AStringView(procMaps.data(), procMaps.size()), procMapTable.entries);
    }
    const LinuxProcessMemoryMapTable* const procMapTablePtr = procMapsPresent ? &procMapTable : nullptr;

    const auto instructionPointer = FindLineKeyValueU64(AStringView(cpuContext.data(), cpuContext.size()), "instruction_pointer");
    if(!cpuContextPresent || !instructionPointer || *instructionPointer == 0u){
        outReport += "detail=ELF/DWARF stack resolver requires a Linux core artifact; instruction pointer mapping unavailable\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(scratchArena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
        return;
    }

    outReport += "instruction_pointer=";
    AppendHexAddress(outReport, *instructionPointer);
    outReport += "\n";

    if(!procMapsPresent){
        outReport += "detail=proc maps missing for module lookup; symbolic frame resolution unavailable\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(scratchArena, AStringView(clientCallstack.data(), clientCallstack.size()), nullptr, config, outReport);
        return;
    }

    const auto instructionMapEntry = ::FindLinuxProcessMemoryMapForAddress(procMapTable.entries, *instructionPointer);
    if(!instructionMapEntry){
        outReport += "detail=instruction pointer was not found in proc maps\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(scratchArena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
        return;
    }

    const AStringView modulePath = instructionMapEntry->path.empty() ? AStringView("<anonymous>") : instructionMapEntry->path;
    outReport += "instruction_pointer_module=";
    outReport.append(modulePath.data(), modulePath.size());
    outReport += "\nmodule_relative_ip=";
    AppendHexAddress(outReport, *instructionPointer - instructionMapEntry->begin);
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    outReport += "\nsymbolication_relative_ip=";
    AppendHexAddress(outReport, *instructionPointer - instructionMapEntry->begin + instructionMapEntry->fileOffset);
#endif
    outReport += clientCallstackPresent
        ? "\ndetail=client callstack captured; module frames are symbolized with DWARF when symbols are reachable\n"
        : "\ndetail=module-relative crash address captured; full Linux callstack requires a Linux core artifact and DWARF resolver\n"
    ;
    if(clientCallstackPresent)
        AppendLinuxClientCallstack(scratchArena, AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, outReport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

