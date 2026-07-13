// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>

#include <dbghelp.h>
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashBytes = Vector<u8, LogArena>;
namespace CrashNames = ::NWB::Core::Crash::PackageNames;
inline constexpr u32 s_MaxWindowsStackFrames = 128u;

struct DumpMemoryRange{
    u64 begin = 0u;
    u64 size = 0u;
    const u8* bytes = nullptr;
};

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

static void BuildDumpMemoryReader(const DumpImage& dumpImage, DumpMemoryReader& outReader){
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

[[nodiscard]] static u64 ParseHexAddressText(const AStringView hexText){
    u64 value = 0u;
    for(const char c : hexText){
        u64 digit;
        if(c >= '0' && c <= '9')
            digit = static_cast<u64>(c - '0');
        else if(c >= 'a' && c <= 'f')
            digit = static_cast<u64>(c - 'a') + 10u;
        else if(c >= 'A' && c <= 'F')
            digit = static_cast<u64>(c - 'A') + 10u;
        else
            break;
        value = (value << 4) | digit;
    }
    return value;
}

// The crashing process captures its own call stack at fault time (where all modules are loaded and unwinding
// is reliable) and ships the frame addresses in callstack.txt. Resolving those addresses against the dump's
// module map is far more dependable than unwinding the minidump server-side, so prefer them for the visible
// callstack. Returns false when no client frames were shipped (e.g. the exception path), so the caller can
// fall back to a server-side StackWalk64.
[[nodiscard]] static bool AppendClientCallstack(LogArena& arena, const HANDLE symbolProcess, const Path& packageDirectory, CrashReportText& outReport){
    AString<LogArena> callstackText{arena};
    if(!ReadTextFile(packageDirectory / CrashNames::s_CallstackFileName, callstackText) || callstackText.empty())
        return false;

    const AStringView text(callstackText.data(), callstackText.size());
    DWORD frameIndex = 0u;
    usize lineStart = 0u;
    bool decodedAnyFrame = false;
    while(lineStart < text.size()){
        usize lineEnd = text.find('\n', lineStart);
        if(lineEnd == AStringView::npos)
            lineEnd = text.size();

        const AStringView line = text.substr(lineStart, lineEnd - lineStart);
        const usize hexStart = line.find("0x");
        if(hexStart != AStringView::npos){
            const u64 address = ParseHexAddressText(line.substr(hexStart + 2u));
            if(address != 0u){
                AppendResolvedSymbol(arena, symbolProcess, outReport, frameIndex, static_cast<DWORD64>(address));
                ++frameIndex;
                decodedAnyFrame = true;
            }
        }
        lineStart = lineEnd + 1u;
    }
    return decodedAnyFrame;
}

bool AppendWindowsMinidumpStack(LogArena& arena, const Path& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config, CrashReportText& outReport){
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
    BuildDumpMemoryReader(dumpImage, memoryReader);
    s_CurrentDumpMemoryReader = &memoryReader;

    outReport += "status=decoded\nresolver=windows_pdb_minidump\nsymbol_path=";
    outReport += BasicStringDetail::WideToUtf8(arena, WStringView(symbolPath));
    outReport += "\nthread_id=";
    char threadBuffer[s_DecimalTextBufferCapacity] = {};
    outReport += FormatDecimal(static_cast<usize>(threadId), threadBuffer);
    outReport += "\n";
    LoadDumpModules(arena, symbolProcess, dumpImage, outReport);
    outReport += "\n[callstack]\n";

    // Prefer the client-captured backtrace (reliable in-process unwind) resolved against the dump's module
    // map; only fall back to a server-side StackWalk64 when the client shipped no frames.
    if(AppendClientCallstack(arena, symbolProcess, packageDirectory, outReport)){
        s_CurrentDumpMemoryReader = nullptr;
        SymCleanup(symbolProcess);
        return true;
    }

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

