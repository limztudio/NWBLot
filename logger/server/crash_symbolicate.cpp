// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate.h"
#include "crash_paths.h"

#include <core/crash/package_names.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashBytes = Vector<u8, LogArena>;
namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr u32 s_ProcMapPathFieldSkipCount = 5u;
inline constexpr usize s_AndroidTombstoneFrameMinimumTextLength = 4u;
inline constexpr usize s_DecimalTextBufferCapacity = 32u;
inline constexpr u32 s_MaxWindowsStackFrames = 128u;
inline constexpr usize s_CrashReportReserveBytes = 4096u;

struct DumpMemoryRange{
    u64 begin = 0u;
    u64 size = 0u;
    const u8* bytes = nullptr;
};

#if defined(NWB_PLATFORM_WINDOWS)
struct DumpImage{
    const u8* bytes = nullptr;
    usize byteCount = 0u;

    [[nodiscard]] const void* rva(const RVA rvaValue, const u32 byteCountValue)const{
        const u64 offset = static_cast<u64>(rvaValue);
        const u64 size = static_cast<u64>(byteCountValue);
        if(offset > static_cast<u64>(byteCount) || size > static_cast<u64>(byteCount) - offset)
            return nullptr;

        return bytes + static_cast<usize>(offset);
    }
};

struct DumpMemoryReader{
    Vector<DumpMemoryRange, LogArena> ranges;

    explicit DumpMemoryReader(LogArena& arena)
        : ranges(arena)
    {}
};

static DumpMemoryReader* s_CurrentDumpMemoryReader = nullptr;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool RegularFileExists(const Path& path){
    ErrorCode error;
    const bool exists = IsRegularFile(path, error);
    return exists && !error;
}

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

static void AppendHexAddress(LogArena& arena, CrashReportText& outReport, const u64 address){
    outReport += "0x";
    outReport += FormatHex64A(arena, address);
}

[[nodiscard]] static Path DefaultSymbolStoreDirectory(LogArena& arena){
    return CrashDefaultRootDirectory(arena) / s_CrashSymbolStoreDirectoryName;
}

[[nodiscard]] static Path EffectiveSymbolStoreDirectory(LogArena& arena, const CrashSymbolicationConfig& config){
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

[[nodiscard]] static bool FindKeyValueU64(const AStringView text, const AStringView key, u64& outValue){
    usize cursor = 0u;
    while(cursor < text.size()){
        const usize begin = cursor;
        while(cursor < text.size() && text[cursor] != '\n' && text[cursor] != '\r')
            ++cursor;

        const AStringView line(text.data() + begin, cursor - begin);
        while(cursor < text.size() && (text[cursor] == '\n' || text[cursor] == '\r'))
            ++cursor;

        if(line.size() <= key.size() || !StartsWith(line, key) || line[key.size()] != '=')
            continue;

        return ParseU64(AStringView(line.data() + key.size() + 1u, line.size() - key.size() - 1u), outValue);
    }

    return false;
}

[[nodiscard]] static bool ParseProcMapAddressRange(const AStringView line, u64& outBegin, u64& outEnd){
    const usize split = line.find('-');
    if(split == AStringView::npos)
        return false;

    usize rangeEnd = split + 1u;
    while(rangeEnd < line.size() && line[rangeEnd] != ' ' && line[rangeEnd] != '\t')
        ++rangeEnd;

    return ::ParseVariableHexU64(AStringView(line.data(), split), outBegin)
        && ::ParseVariableHexU64(AStringView(line.data() + split + 1u, rangeEnd - split - 1u), outEnd)
        && outBegin < outEnd
    ;
}

[[nodiscard]] static AStringView ProcMapPathField(AStringView line)noexcept{
    line = TrimLeftView(line);
    for(u32 fieldIndex = 0u; fieldIndex < s_ProcMapPathFieldSkipCount; ++fieldIndex){
        while(!line.empty() && line.front() != ' ' && line.front() != '\t')
            line.remove_prefix(1u);
        line = TrimLeftView(line);
        if(line.empty())
            return AStringView();
    }

    return line;
}

[[nodiscard]] static bool FindProcMapForAddress(
    const AStringView mapsText,
    const u64 address,
    u64& outModuleBegin,
    CrashReportText& outModulePath
){
    outModuleBegin = 0u;
    outModulePath.clear();

    usize cursor = 0u;
    while(cursor < mapsText.size()){
        const usize begin = cursor;
        while(cursor < mapsText.size() && mapsText[cursor] != '\n' && mapsText[cursor] != '\r')
            ++cursor;

        const AStringView line(mapsText.data() + begin, cursor - begin);
        while(cursor < mapsText.size() && (mapsText[cursor] == '\n' || mapsText[cursor] == '\r'))
            ++cursor;

        u64 mapBegin = 0u;
        u64 mapEnd = 0u;
        if(!ParseProcMapAddressRange(line, mapBegin, mapEnd) || address < mapBegin || address >= mapEnd)
            continue;

        outModuleBegin = mapBegin;
        const AStringView path = ProcMapPathField(line);
        outModulePath.assign(path.data(), path.size());
        if(outModulePath.empty())
            outModulePath = "<anonymous>";
        return true;
    }

    return false;
}

static void AppendLinuxArtifactSummary(LogArena& arena, const Path& packageDirectory, CrashReportText& outReport){
    outReport += "status=not_decoded\nresolver=elf_dwarf_core\n";

    const bool corePresent =
        RegularFileExists(packageDirectory / CrashNames::s_LinuxCoreFileName)
        || RegularFileExists(packageDirectory / CrashNames::s_LinuxCoreDumpFileName)
        || RegularFileExists(packageDirectory / CrashNames::s_LinuxProcessCoreFileName)
    ;
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

    u64 instructionPointer = 0u;
    if(!cpuContextPresent || !FindKeyValueU64(AStringView(cpuContext.data(), cpuContext.size()), "instruction_pointer", instructionPointer) || instructionPointer == 0u){
        outReport += "detail=ELF/DWARF stack resolver requires a core-compatible artifact; instruction pointer mapping unavailable\n";
        return;
    }

    outReport += "instruction_pointer=";
    AppendHexAddress(arena, outReport, instructionPointer);
    outReport += "\n";

    if(!procMapsPresent){
        outReport += "detail=ELF/DWARF stack resolver requires a core-compatible artifact; proc maps missing for module lookup\n";
        return;
    }

    u64 moduleBegin = 0u;
    CrashReportText modulePath{arena};
    if(!FindProcMapForAddress(AStringView(procMaps.data(), procMaps.size()), instructionPointer, moduleBegin, modulePath)){
        outReport += "detail=ELF/DWARF stack resolver requires a core-compatible artifact; instruction pointer was not found in proc maps\n";
        return;
    }

    outReport += "instruction_pointer_module=";
    outReport += modulePath;
    outReport += "\nmodule_relative_ip=";
    AppendHexAddress(arena, outReport, instructionPointer - moduleBegin);
    outReport += "\ndetail=module-relative crash address captured; full Linux callstack requires a core-compatible artifact and DWARF resolver\n";
}

static void AppendAndroidTombstoneSummary(LogArena& arena, const Path& packageDirectory, CrashReportText& outReport){
    CrashReportText tombstone{arena};
    const bool tombstonePresent = ReadTextFile(packageDirectory / CrashNames::s_AndroidTombstoneFileName, tombstone) && !tombstone.empty();
    if(!tombstonePresent){
        outReport += "status=not_decoded\nresolver=android_tombstone_native_symbols\n";
        outReport += "android_tombstone=missing\ndetail=Android resolver requires Java/ApplicationExitInfo tombstone attachment and native symbol store\n";
        return;
    }

    CrashReportText frames{arena};
    usize cursor = 0u;
    while(cursor < tombstone.size()){
        const usize begin = cursor;
        while(cursor < tombstone.size() && tombstone[cursor] != '\n' && tombstone[cursor] != '\r')
            ++cursor;

        const AStringView line(tombstone.data() + begin, cursor - begin);
        while(cursor < tombstone.size() && (tombstone[cursor] == '\n' || tombstone[cursor] == '\r'))
            ++cursor;

        const AStringView trimmed = TrimLeftView(line);
        if(trimmed.size() < s_AndroidTombstoneFrameMinimumTextLength || trimmed.front() != '#' || trimmed.find(" pc ") == AStringView::npos)
            continue;

        frames.append(trimmed.data(), trimmed.size());
        frames += '\n';
    }

    outReport += frames.empty()
        ? "status=not_decoded\n"
        : "status=tombstone_parsed\n"
    ;
    outReport += "resolver=android_tombstone_native_symbols\n";
    outReport += "android_tombstone=present\n";
    outReport += frames.empty()
        ? "detail=tombstone attached, but no native frame lines were recognized; native symbols are required for full decoding\n"
        : "detail=tombstone native frame lines copied; native symbol store is required for offline address resolution\n"
    ;

    if(!frames.empty()){
        outReport += "\n[tombstone_callstack]\n";
        outReport += frames;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] static const MINIDUMP_STRING* DumpStringAtRva(const DumpImage& dumpImage, const RVA rvaValue){
    return static_cast<const MINIDUMP_STRING*>(dumpImage.rva(rvaValue, sizeof(MINIDUMP_STRING)));
}

[[nodiscard]] static WStringView DumpStringView(const DumpImage& dumpImage, const RVA rvaValue){
    const MINIDUMP_STRING* dumpString = DumpStringAtRva(dumpImage, rvaValue);
    if(!dumpString || dumpString->Length == 0u || (dumpString->Length % sizeof(wchar)) != 0u)
        return WStringView();

    const u32 byteCount = dumpString->Length;
    const u32 charCount = byteCount / sizeof(wchar);
    const void* fullString = dumpImage.rva(rvaValue, sizeof(MINIDUMP_STRING) + byteCount);
    if(!fullString)
        return WStringView();

    return WStringView(dumpString->Buffer, static_cast<usize>(charCount));
}

[[nodiscard]] static bool ReadMinidumpStream(const DumpImage& dumpImage, const MINIDUMP_STREAM_TYPE streamType, void*& outStream, ULONG& outStreamBytes){
    outStream = nullptr;
    outStreamBytes = 0u;

    PMINIDUMP_DIRECTORY directory = nullptr;
    PVOID stream = nullptr;
    ULONG streamBytes = 0u;
    if(!MiniDumpReadDumpStream(const_cast<u8*>(dumpImage.bytes), streamType, &directory, &stream, &streamBytes))
        return false;

    static_cast<void>(directory);
    outStream = stream;
    outStreamBytes = streamBytes;
    return stream != nullptr && streamBytes > 0u;
}

static void AddMemoryRange(DumpMemoryReader& reader, const u64 begin, const u64 size, const u8* bytes){
    if(size == 0u || !bytes)
        return;

    reader.ranges.push_back(DumpMemoryRange{ begin, size, bytes });
}

static void AddLocationMemoryRange(const DumpImage& dumpImage, DumpMemoryReader& reader, const u64 begin, const MINIDUMP_LOCATION_DESCRIPTOR& location){
    const void* bytes = dumpImage.rva(location.Rva, location.DataSize);
    AddMemoryRange(reader, begin, location.DataSize, static_cast<const u8*>(bytes));
}

static void BuildDumpMemoryReader(LogArena& arena, const DumpImage& dumpImage, DumpMemoryReader& outReader){
    void* threadStream = nullptr;
    ULONG threadStreamBytes = 0u;
    if(ReadMinidumpStream(dumpImage, ThreadListStream, threadStream, threadStreamBytes)){
        const auto* threadList = static_cast<const MINIDUMP_THREAD_LIST*>(threadStream);
        for(u32 i = 0u; i < threadList->NumberOfThreads; ++i)
            AddLocationMemoryRange(dumpImage, outReader, threadList->Threads[i].Stack.StartOfMemoryRange, threadList->Threads[i].Stack.Memory);
    }

    void* memoryStream = nullptr;
    ULONG memoryStreamBytes = 0u;
    if(ReadMinidumpStream(dumpImage, MemoryListStream, memoryStream, memoryStreamBytes)){
        const auto* memoryList = static_cast<const MINIDUMP_MEMORY_LIST*>(memoryStream);
        for(u32 i = 0u; i < memoryList->NumberOfMemoryRanges; ++i)
            AddLocationMemoryRange(dumpImage, outReader, memoryList->MemoryRanges[i].StartOfMemoryRange, memoryList->MemoryRanges[i].Memory);
    }

    void* memory64Stream = nullptr;
    ULONG memory64StreamBytes = 0u;
    if(ReadMinidumpStream(dumpImage, Memory64ListStream, memory64Stream, memory64StreamBytes)){
        const auto* memoryList = static_cast<const MINIDUMP_MEMORY64_LIST*>(memory64Stream);
        u64 cursor = memoryList->BaseRva;
        for(u64 i = 0u; i < memoryList->NumberOfMemoryRanges; ++i){
            const MINIDUMP_MEMORY_DESCRIPTOR64& descriptor = memoryList->MemoryRanges[i];
            const void* bytes = dumpImage.rva(static_cast<RVA>(cursor), static_cast<u32>(descriptor.DataSize));
            AddMemoryRange(outReader, descriptor.StartOfMemoryRange, descriptor.DataSize, static_cast<const u8*>(bytes));
            cursor += descriptor.DataSize;
        }
    }

    static_cast<void>(arena);
}

static BOOL CALLBACK ReadProcessMemoryFromDump(HANDLE process, DWORD64 baseAddress, PVOID buffer, DWORD size, LPDWORD bytesRead){
    static_cast<void>(process);

    if(bytesRead)
        *bytesRead = 0u;
    if(!s_CurrentDumpMemoryReader || !buffer)
        return FALSE;

    for(const DumpMemoryRange& range : s_CurrentDumpMemoryReader->ranges){
        if(baseAddress < range.begin)
            continue;
        const u64 offset = baseAddress - range.begin;
        if(offset >= range.size)
            continue;
        if(static_cast<u64>(size) > range.size - offset)
            return FALSE;

        NWB_MEMCPY(buffer, static_cast<usize>(size), range.bytes + static_cast<usize>(offset), static_cast<usize>(size));
        if(bytesRead)
            *bytesRead = size;
        return TRUE;
    }

    return FALSE;
}

[[nodiscard]] static const CONTEXT* FindCrashContext(const DumpImage& dumpImage, const CrashPackageSummary& summary, DWORD& outThreadId){
    outThreadId = static_cast<DWORD>(summary.threadId);

    void* exceptionStream = nullptr;
    ULONG exceptionStreamBytes = 0u;
    if(ReadMinidumpStream(dumpImage, ExceptionStream, exceptionStream, exceptionStreamBytes)){
        const auto* exceptionInfo = static_cast<const MINIDUMP_EXCEPTION_STREAM*>(exceptionStream);
        outThreadId = exceptionInfo->ThreadId;
        return static_cast<const CONTEXT*>(dumpImage.rva(exceptionInfo->ThreadContext.Rva, exceptionInfo->ThreadContext.DataSize));
    }

    void* threadStream = nullptr;
    ULONG threadStreamBytes = 0u;
    if(!ReadMinidumpStream(dumpImage, ThreadListStream, threadStream, threadStreamBytes))
        return nullptr;

    const auto* threadList = static_cast<const MINIDUMP_THREAD_LIST*>(threadStream);
    for(u32 i = 0u; i < threadList->NumberOfThreads; ++i){
        const MINIDUMP_THREAD& thread = threadList->Threads[i];
        if(outThreadId != 0u && thread.ThreadId != outThreadId)
            continue;

        outThreadId = thread.ThreadId;
        return static_cast<const CONTEXT*>(dumpImage.rva(thread.ThreadContext.Rva, thread.ThreadContext.DataSize));
    }

    return nullptr;
}

[[nodiscard]] static DWORD MachineTypeFromContext(const CONTEXT& context)noexcept{
    static_cast<void>(context);
#if defined(_M_X64) || defined(__x86_64__)
    return IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86) || defined(__i386__)
    return IMAGE_FILE_MACHINE_I386;
#elif defined(_M_ARM64) || defined(__aarch64__)
    return IMAGE_FILE_MACHINE_ARM64;
#else
    return 0u;
#endif
}

static void InitializeStackFrameFromContext(STACKFRAME64& outFrame, const CONTEXT& context)noexcept{
    outFrame = STACKFRAME64{};
#if defined(_M_X64) || defined(__x86_64__)
    outFrame.AddrPC.Offset = context.Rip;
    outFrame.AddrFrame.Offset = context.Rbp;
    outFrame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86) || defined(__i386__)
    outFrame.AddrPC.Offset = context.Eip;
    outFrame.AddrFrame.Offset = context.Ebp;
    outFrame.AddrStack.Offset = context.Esp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    outFrame.AddrPC.Offset = context.Pc;
    outFrame.AddrFrame.Offset = context.Fp;
    outFrame.AddrStack.Offset = context.Sp;
#endif
    outFrame.AddrPC.Mode = AddrModeFlat;
    outFrame.AddrFrame.Mode = AddrModeFlat;
    outFrame.AddrStack.Mode = AddrModeFlat;
}

[[nodiscard]] static WString<LogArena> BuildSymbolSearchPath(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config){
    WString<LogArena> path{arena};
    path += packageDirectory.native();
    path += L";";
    path += EffectiveSymbolStoreDirectory(arena, config).native();
    return path;
}

static void LoadDumpModules(LogArena& arena, const HANDLE symbolProcess, const DumpImage& dumpImage, CrashReportText& outReport){
    void* moduleStream = nullptr;
    ULONG moduleStreamBytes = 0u;
    if(!ReadMinidumpStream(dumpImage, ModuleListStream, moduleStream, moduleStreamBytes)){
        outReport += "module_load=missing_module_list\n";
        return;
    }

    const auto* moduleList = static_cast<const MINIDUMP_MODULE_LIST*>(moduleStream);
    u32 loadedCount = 0u;
    for(u32 i = 0u; i < moduleList->NumberOfModules; ++i){
        const MINIDUMP_MODULE& module = moduleList->Modules[i];
        const WStringView moduleName = DumpStringView(dumpImage, module.ModuleNameRva);
        WString<LogArena> moduleNameText{arena};
        if(!moduleName.empty())
            moduleNameText.assign(moduleName.data(), moduleName.size());
        const wchar* imageName = moduleNameText.empty() ? nullptr : moduleNameText.c_str();
        const DWORD64 loadedBase = SymLoadModuleExW(
            symbolProcess,
            nullptr,
            imageName,
            nullptr,
            module.BaseOfImage,
            module.SizeOfImage,
            nullptr,
            0u
        );
        if(loadedBase != 0u)
            ++loadedCount;
    }

    outReport += "modules_loaded=";
    char countBuffer[s_DecimalTextBufferCapacity] = {};
    outReport += FormatDecimal(static_cast<usize>(loadedCount), countBuffer);
    outReport += "\n";
    static_cast<void>(arena);
}

static void AppendResolvedSymbol(LogArena& arena, const HANDLE symbolProcess, CrashReportText& outReport, const DWORD frameIndex, const DWORD64 address){
    outReport += "#";
    char frameBuffer[s_DecimalTextBufferCapacity] = {};
    outReport += FormatDecimal(static_cast<usize>(frameIndex), frameBuffer);
    outReport += " ";
    AppendHexAddress(arena, outReport, address);

    alignas(SYMBOL_INFOW) u8 symbolBuffer[sizeof(SYMBOL_INFOW) + (MAX_SYM_NAME * sizeof(wchar))] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 displacement = 0u;
    if(SymFromAddrW(symbolProcess, address, &displacement, symbol)){
        const AString<LogArena> name = BasicStringDetail::WideToUtf8(arena, WStringView(symbol->Name, symbol->NameLen));
        outReport += " ";
        outReport += name;
        if(displacement != 0u){
            outReport += "+0x";
            outReport += FormatHex64A(arena, displacement);
        }
    }
    else
        outReport += " <unknown>";

    IMAGEHLP_LINEW64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0u;
    if(SymGetLineFromAddrW64(symbolProcess, address, &lineDisplacement, &line)){
        const AString<LogArena> file = BasicStringDetail::WideToUtf8(arena, WStringView(line.FileName));
        outReport += " at ";
        outReport += file;
        outReport += ":";
        char lineBuffer[s_DecimalTextBufferCapacity] = {};
        outReport += FormatDecimal(static_cast<usize>(line.LineNumber), lineBuffer);
    }

    outReport += "\n";
}

[[nodiscard]] static bool AppendWindowsMinidumpStack(LogArena& arena, const Path& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    const Path dumpPath = packageDirectory / CrashNames::s_ProcessDumpFileName;
    CrashBytes dumpBytes{arena};
    ErrorCode readError;
    if(!ReadBinaryFile(dumpPath, dumpBytes, readError) || dumpBytes.empty()){
        outReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=";
        outReport += CrashNames::s_ProcessDumpFileName;
        outReport += " is missing or unreadable\n";
        return false;
    }

    const DumpImage dumpImage{ dumpBytes.data(), dumpBytes.size() };
    DWORD threadId = 0u;
    const CONTEXT* context = FindCrashContext(dumpImage, summary, threadId);
    if(!context){
        outReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=minidump has no usable thread context\n";
        return false;
    }

    const HANDLE symbolProcess = GetCurrentProcess();
    const WString<LogArena> symbolPath = BuildSymbolSearchPath(arena, packageDirectory, config);
    SymCleanup(symbolProcess);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if(!SymInitializeW(symbolProcess, symbolPath.c_str(), FALSE)){
        outReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=SymInitializeW failed\n";
        return false;
    }

    DumpMemoryReader memoryReader(arena);
    BuildDumpMemoryReader(arena, dumpImage, memoryReader);
    s_CurrentDumpMemoryReader = &memoryReader;

    outReport += "status=decoded\nresolver=windows_pdb_minidump\nsymbol_path=";
    outReport += BasicStringDetail::WideToUtf8(arena, WStringView(symbolPath));
    outReport += "\nthread_id=";
    char threadBuffer[s_DecimalTextBufferCapacity] = {};
    outReport += FormatDecimal(static_cast<usize>(threadId), threadBuffer);
    outReport += "\n";
    LoadDumpModules(arena, symbolProcess, dumpImage, outReport);
    outReport += "\n[callstack]\n";

    CONTEXT walkContext = *context;
    STACKFRAME64 frame;
    InitializeStackFrameFromContext(frame, walkContext);

    const DWORD machineType = MachineTypeFromContext(walkContext);
    bool decodedAnyFrame = false;
    for(DWORD frameIndex = 0u; frameIndex < s_MaxWindowsStackFrames; ++frameIndex){
        if(frame.AddrPC.Offset == 0u)
            break;

        AppendResolvedSymbol(arena, symbolProcess, outReport, frameIndex, frame.AddrPC.Offset);
        decodedAnyFrame = true;

        if(!StackWalk64(
            machineType,
            symbolProcess,
            nullptr,
            &frame,
            &walkContext,
            ReadProcessMemoryFromDump,
            SymFunctionTableAccess64,
            SymGetModuleBase64,
            nullptr
        ))
            break;
    }

    s_CurrentDumpMemoryReader = nullptr;
    SymCleanup(symbolProcess);

    if(!decodedAnyFrame)
        outReport += "<no frames decoded>\n";
    return decodedAnyFrame;
}
#endif


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

    if(summary.platform == "windows"){
#if defined(NWB_PLATFORM_WINDOWS)
        static_cast<void>(Symbolicate::AppendWindowsMinidumpStack(arena, packageDirectory, summary, config, report));
#else
        report += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=Windows minidump resolver is only available on Windows logserver builds\n";
#endif
    }
    else if(summary.platform == "linux"){
        Symbolicate::AppendLinuxArtifactSummary(arena, packageDirectory, report);
    }
    else if(summary.platform == "android"){
        Symbolicate::AppendAndroidTombstoneSummary(arena, packageDirectory, report);
    }
    else{
        report += "status=not_decoded\nresolver=unknown\ndetail=unknown crash platform\n";
    }

    Symbolicate::AppendOptionalTextFile(arena, report, packageDirectory, Core::Crash::PackageNames::s_TriggerFileName, "trigger");
    Symbolicate::AppendOptionalTextFile(arena, report, packageDirectory, Core::Crash::PackageNames::s_CpuContextFileName, "cpu_context");
    Symbolicate::AppendOptionalTextFile(arena, report, packageDirectory, Core::Crash::PackageNames::s_SymbolicationFileName, "client_symbolication_note");
    Symbolicate::AppendOptionalTextFile(arena, report, packageDirectory, Core::Crash::PackageNames::s_AndroidCollectionFileName, "android_collection");
    return report;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

