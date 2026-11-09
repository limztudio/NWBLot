// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"
#include "linux_frame_symbol_resolver.h"

#include <logger/server/crash_values.adhd.h>

#include <core/crash/package_names.h>

#include <global/overflow.h>
#include <global/process_memory_map.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LinuxProcessMemoryMapTable{
    Vector<LinuxProcessMemoryMapEntry, Core::Alloc::ScratchArena> entries;


public:
    explicit LinuxProcessMemoryMapTable(Core::Alloc::ScratchArena& arena)noexcept(noexcept(decltype(entries)(arena)))
        : entries(arena)
    {}
};




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




static void AppendLinuxClientCallstack(const AStringView callstackText, const LinuxProcessMemoryMapTable* const procMaps, [[maybe_unused]] const CrashSymbolicationConfig& config, [[maybe_unused]] Core::Alloc::ScratchArena& arena, CrashReportText& outReport){
    outReport += s_CallstackSectionHeader;

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    usize frameLineCount = 1u;
    for(const char ch : callstackText){
        if(ch == '\n')
            ++frameLineCount;
    }
    LinuxFrameSymbolResolver symbolResolver(config, arena, frameLineCount);
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
                const AStringView modulePath = mapEntry->path.empty() ? AStringView(s_AnonymousModulePath) : mapEntry->path;
                outReport += " ";
                outReport.append(modulePath.data(), modulePath.size());
                outReport += "+";
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
                const auto symbolOffset = AddNoOverflow(moduleOffset, mapEntry->fileOffset);
                AppendHexAddress(moduleOffset, outReport);
                if(symbolOffset){
                    const auto symbol = symbolResolver.resolve(modulePath, *symbolOffset);
                    if(symbol){
                        outReport += " ";
                        outReport += *symbol;
                    }
                }
#else
                AppendHexAddress(moduleOffset, outReport);
#endif
            }
        }

        outReport += s_ReportLineEnd;
    }
}

void AppendLinuxArtifactSummary(const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& scratchArena, CrashReportText& outReport){
    AString<Core::Alloc::ScratchArena> clientCallstack(scratchArena);
    const auto clientCallstackRead = ReadTextFile(packageDirectory / CrashNames::s_CallstackFileName, clientCallstack);
    const b8 clientCallstackPresent = clientCallstackRead && !clientCallstack.empty();

    outReport += clientCallstackPresent ? "status=callstack_captured\nresolver=linux_client_callstack\n" : "status=not_decoded\nresolver=elf_dwarf_core\n";
    if(!clientCallstackRead && clientCallstackRead.error() != ErrorCondition::no_such_file_or_directory)
        AppendNativeFailure(CrashSymbolicationFailure{ .operation = "read_linux_client_callstack", .code = clientCallstackRead.error(), }, outReport);

    const auto corePath = packageDirectory / CrashNames::s_LinuxCoreFileName;
    const auto corePresent = IsRegularFile(corePath);
    if(!corePresent){
        NWB_LOGGER_WARNING(NWB_TEXT("Log server: failed to inspect Linux core artifact '{}': {}"), PathToString<tchar>(corePath), ConvertString(corePresent.error().message()));
        outReport += "core_artifact=inspect_failed\n";
    }
    else
        outReport += *corePresent ? "core_artifact=present\n" : "core_artifact=missing\n";

    AString<Core::Alloc::ScratchArena> cpuContext(scratchArena);
    AString<Core::Alloc::ScratchArena> procMaps(scratchArena);
    const auto cpuContextRead = ReadTextFile(packageDirectory / CrashNames::s_CpuContextFileName, cpuContext);
    const auto procMapsRead = ReadTextFile(packageDirectory / CrashNames::s_ProcMapsFileName, procMaps);
    const b8 cpuContextPresent = cpuContextRead && !cpuContext.empty();
    const b8 procMapsPresent = procMapsRead && !procMaps.empty();
    outReport += procMapsPresent ? "proc_maps=present\n" : "proc_maps=missing\n";
    if(!cpuContextRead && cpuContextRead.error() != ErrorCondition::no_such_file_or_directory)
        AppendNativeFailure(CrashSymbolicationFailure{ .operation = "read_linux_cpu_context", .code = cpuContextRead.error(), }, outReport);
    if(!procMapsRead && procMapsRead.error() != ErrorCondition::no_such_file_or_directory)
        AppendNativeFailure(CrashSymbolicationFailure{ .operation = "read_linux_proc_maps", .code = procMapsRead.error(), }, outReport);

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
            AppendLinuxClientCallstack(AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, scratchArena, outReport);
        return;
    }

    outReport += "instruction_pointer=";
    AppendHexAddress(*instructionPointer, outReport);
    outReport += s_ReportLineEnd;

    if(!procMapsPresent){
        outReport += "detail=proc maps missing for module lookup; symbolic frame resolution unavailable\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(AStringView(clientCallstack.data(), clientCallstack.size()), nullptr, config, scratchArena, outReport);
        return;
    }

    const auto instructionMapEntry = ::FindLinuxProcessMemoryMapForAddress(procMapTable.entries, *instructionPointer);
    if(!instructionMapEntry){
        outReport += "detail=instruction pointer was not found in proc maps\n";
        if(clientCallstackPresent)
            AppendLinuxClientCallstack(AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, scratchArena, outReport);
        return;
    }

    const AStringView modulePath = instructionMapEntry->path.empty() ? AStringView(s_AnonymousModulePath) : instructionMapEntry->path;
    outReport += "instruction_pointer_module=";
    outReport.append(modulePath.data(), modulePath.size());
    outReport += "\nmodule_relative_ip=";
    AppendHexAddress(*instructionPointer - instructionMapEntry->begin, outReport);
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    outReport += "\nsymbolication_relative_ip=";
    const auto symbolOffset = AddNoOverflow(*instructionPointer - instructionMapEntry->begin, instructionMapEntry->fileOffset);
    if(symbolOffset)
        AppendHexAddress(*symbolOffset, outReport);
    else
        outReport += "unavailable";
#endif
    outReport += clientCallstackPresent
        ? "\ndetail=client callstack captured; module frames are symbolized with DWARF when symbols are reachable\n"
        : "\ndetail=module-relative crash address captured; full Linux callstack requires a Linux core artifact and DWARF resolver\n"
    ;
    if(clientCallstackPresent)
        AppendLinuxClientCallstack(AStringView(clientCallstack.data(), clientCallstack.size()), procMapTablePtr, config, scratchArena, outReport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

