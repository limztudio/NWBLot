// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>

#include <global/diagnostics.h>
#include <global/limit.h>
#include <global/not_null.h>
#include <global/scope_exit.h>

#include <dbghelp.h>
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashBytes = Vector<u8, Core::Alloc::ScratchArena>;
namespace CrashNames = ::NWB::Core::Crash::PackageNames;
inline constexpr u32 s_MaxWindowsStackFrames = 128u;
inline constexpr char s_HexDigitFirst = '0';
inline constexpr char s_HexDigitLast = '9';
inline constexpr char s_HexLowerFirst = 'a';
inline constexpr char s_HexLowerLast = 'f';
inline constexpr char s_HexUpperFirst = 'A';
inline constexpr char s_HexUpperLast = 'F';
inline constexpr u64 s_HexLetterOffset = 10u;
inline constexpr u64 s_HexRadixBits = 4u;

struct DumpMemoryRange{
    u64 begin = 0u;
    u64 size = 0u;
    NotNull<const u8*> bytes;
};

struct DumpImage{
    const u8* bytes = nullptr;
    usize byteCount = 0u;


public:
    [[nodiscard]] const u8* rva(const u64 offset, const u64 size)const noexcept{
        if(!bytes || offset > static_cast<u64>(byteCount) || size > static_cast<u64>(byteCount) - offset)
            return nullptr;
        return bytes + static_cast<usize>(offset);
    }

    template<typename T>
    [[nodiscard]] Expected<T> read(const u64 offset)const noexcept{
        static_assert(IsTriviallyCopyable_V<T> && IsNothrowConstructible_V<T> && IsNothrowMoveConstructible_V<T>);
        const u8* const source = rva(offset, sizeof(T));
        if(!source)
            return MakeUnexpected(Failure{});
        T value;
        NWB_MEMCPY(&value, sizeof(value), source, sizeof(value));
        return value;
    }
};

template<typename T>
struct MinidumpRecords{
    NotNull<const u8*> bytes;
    usize count = 0u;


public:
    [[nodiscard]] T record(const usize index)const noexcept{
        static_assert(IsTriviallyCopyable_V<T> && IsNothrowConstructible_V<T> && IsNothrowMoveConstructible_V<T>);
        NWB_ASSERT(index < count);
        T value;
        NWB_MEMCPY(&value, sizeof(value), bytes.get() + index * sizeof(T), sizeof(value));
        return value;
    }
};

struct DumpMemoryReader{
    Vector<DumpMemoryRange, Core::Alloc::ScratchArena> ranges;


public:
    explicit DumpMemoryReader(Core::Alloc::ScratchArena& scratchArena)noexcept
        : ranges(scratchArena)
    {}
};

static DumpMemoryReader* s_CurrentDumpMemoryReader = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<WString<Core::Alloc::ScratchArena>> ReadDumpString(Core::Alloc::ScratchArena& scratchArena, const DumpImage& dumpImage, const RVA rvaValue){
    const auto byteCount = dumpImage.read<ULONG32>(rvaValue);
    if(!byteCount || (*byteCount % sizeof(wchar)) != 0u)
        return MakeUnexpected(Failure{});
    const u64 textOffset = static_cast<u64>(rvaValue) + offsetof(MINIDUMP_STRING, Buffer);
    const u8* const bytes = dumpImage.rva(textOffset, static_cast<u64>(*byteCount) + sizeof(wchar));
    if(!bytes)
        return MakeUnexpected(Failure{});
    const auto terminator = dumpImage.read<wchar>(textOffset + *byteCount);
    if(!terminator || *terminator != 0)
        return MakeUnexpected(Failure{});
    WString<Core::Alloc::ScratchArena> text(scratchArena);
    text.resize(static_cast<usize>(*byteCount / sizeof(wchar)));
    if(*byteCount != 0u)
        NWB_MEMCPY(text.data(), *byteCount, bytes, *byteCount);
    return text;
}

[[nodiscard]] static Expected<Optional<DumpImage>> ReadMinidumpStream(const DumpImage& dumpImage, const MINIDUMP_STREAM_TYPE streamType)noexcept{
    const auto header = dumpImage.read<MINIDUMP_HEADER>(0u);
    if(!header || header->Signature != MINIDUMP_SIGNATURE || (header->Version & Limit<u16>::s_Max) != MINIDUMP_VERSION)
        return MakeUnexpected(Failure{});
    const u64 directoryBytes = static_cast<u64>(header->NumberOfStreams) * sizeof(MINIDUMP_DIRECTORY);
    if(!dumpImage.rva(header->StreamDirectoryRva, directoryBytes))
        return MakeUnexpected(Failure{});

    Optional<DumpImage> selected;
    for(u32 index = 0u; index < header->NumberOfStreams; ++index){
        const u64 directoryOffset = static_cast<u64>(header->StreamDirectoryRva) + static_cast<u64>(index) * sizeof(MINIDUMP_DIRECTORY);
        const auto directory = dumpImage.read<MINIDUMP_DIRECTORY>(directoryOffset);
        if(!directory)
            return MakeUnexpected(Failure{});
        const u8* const bytes = dumpImage.rva(directory->Location.Rva, directory->Location.DataSize);
        if(!bytes)
            return MakeUnexpected(Failure{});
        if(directory->StreamType == static_cast<ULONG32>(streamType) && !selected)
            selected = DumpImage{ bytes, static_cast<usize>(directory->Location.DataSize) };
    }
    return selected;
}

template<typename T>
[[nodiscard]] static Expected<MinidumpRecords<T>> ReadMinidumpRecords(const DumpImage& stream, const usize recordsOffset, const u64 count)noexcept{
    if(!stream.bytes || recordsOffset > stream.byteCount || count > static_cast<u64>((stream.byteCount - recordsOffset) / sizeof(T)))
        return MakeUnexpected(Failure{});
    return MinidumpRecords<T>{ MakeNotNull(stream.bytes + recordsOffset), static_cast<usize>(count) };
}

[[nodiscard]] static Expected<void> AddMemoryRange(DumpMemoryReader& reader, const u64 begin, const u64 size, const u8* const bytes){
    if(size == 0u)
        return {};
    if(!bytes || size > Limit<u64>::s_Max - begin)
        return MakeUnexpected(Failure{});
    reader.ranges.push_back(DumpMemoryRange{ begin, size, MakeNotNull(bytes) });
    return {};
}

[[nodiscard]] static Expected<void> AddLocationMemoryRange(const DumpImage& dumpImage, DumpMemoryReader& reader, const u64 begin, const MINIDUMP_LOCATION_DESCRIPTOR& location){
    return AddMemoryRange(reader, begin, location.DataSize, dumpImage.rva(location.Rva, location.DataSize));
}

[[nodiscard]] static Expected<usize> CountDumpMemoryRanges(const DumpImage& dumpImage)noexcept{
    usize total = 0u;
    for(const auto streamType : Array<MINIDUMP_STREAM_TYPE, 3u>{ ThreadListStream, MemoryListStream, Memory64ListStream }){
        const auto selected = ReadMinidumpStream(dumpImage, streamType);
        if(!selected)
            return MakeUnexpected(Failure{});
        if(!*selected)
            continue;
        const DumpImage& stream = **selected;
        u64 count = 0u;
        if(streamType == Memory64ListStream){
            const auto records = stream.read<ULONG64>(0u);
            if(!records || !ReadMinidumpRecords<MINIDUMP_MEMORY_DESCRIPTOR64>(stream, offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges), *records))
                return MakeUnexpected(Failure{});
            count = *records;
        }
        else{
            const auto records = stream.read<ULONG32>(0u);
            if(!records)
                return MakeUnexpected(Failure{});
            if(streamType == ThreadListStream){
                if(!ReadMinidumpRecords<MINIDUMP_THREAD>(stream, offsetof(MINIDUMP_THREAD_LIST, Threads), *records))
                    return MakeUnexpected(Failure{});
            }
            else if(!ReadMinidumpRecords<MINIDUMP_MEMORY_DESCRIPTOR>(stream, offsetof(MINIDUMP_MEMORY_LIST, MemoryRanges), *records))
                return MakeUnexpected(Failure{});
            count = *records;
        }
        if(count > static_cast<u64>(Limit<usize>::s_Max - total))
            return MakeUnexpected(Failure{});
        total += static_cast<usize>(count);
    }
    return total;
}

[[nodiscard]] static Expected<DumpMemoryReader> BuildDumpMemoryReader(Core::Alloc::ScratchArena& scratchArena, const DumpImage& dumpImage){
    const auto rangeCapacity = CountDumpMemoryRanges(dumpImage);
    if(!rangeCapacity)
        return MakeUnexpected(Failure{});
    DumpMemoryReader reader(scratchArena);
    reader.ranges.reserve(*rangeCapacity);
    const auto threadStream = ReadMinidumpStream(dumpImage, ThreadListStream);
    if(!threadStream)
        return MakeUnexpected(Failure{});
    if(*threadStream){
        const DumpImage& stream = **threadStream;
        const auto count = stream.read<ULONG32>(0u);
        if(!count)
            return MakeUnexpected(Failure{});
        const auto threads = ReadMinidumpRecords<MINIDUMP_THREAD>(stream, offsetof(MINIDUMP_THREAD_LIST, Threads), *count);
        if(!threads)
            return MakeUnexpected(Failure{});
        for(usize index = 0u; index < threads->count; ++index){
            const MINIDUMP_THREAD thread = threads->record(index);
            if(!AddLocationMemoryRange(dumpImage, reader, thread.Stack.StartOfMemoryRange, thread.Stack.Memory))
                return MakeUnexpected(Failure{});
        }
    }

    const auto memoryStream = ReadMinidumpStream(dumpImage, MemoryListStream);
    if(!memoryStream)
        return MakeUnexpected(Failure{});
    if(*memoryStream){
        const DumpImage& stream = **memoryStream;
        const auto count = stream.read<ULONG32>(0u);
        if(!count)
            return MakeUnexpected(Failure{});
        const auto ranges = ReadMinidumpRecords<MINIDUMP_MEMORY_DESCRIPTOR>(stream, offsetof(MINIDUMP_MEMORY_LIST, MemoryRanges), *count);
        if(!ranges)
            return MakeUnexpected(Failure{});
        for(usize index = 0u; index < ranges->count; ++index){
            const MINIDUMP_MEMORY_DESCRIPTOR descriptor = ranges->record(index);
            if(!AddLocationMemoryRange(dumpImage, reader, descriptor.StartOfMemoryRange, descriptor.Memory))
                return MakeUnexpected(Failure{});
        }
    }

    const auto memory64Stream = ReadMinidumpStream(dumpImage, Memory64ListStream);
    if(!memory64Stream)
        return MakeUnexpected(Failure{});
    if(*memory64Stream){
        const DumpImage& stream = **memory64Stream;
        const auto count = stream.read<ULONG64>(0u);
        const auto baseRva = stream.read<RVA64>(offsetof(MINIDUMP_MEMORY64_LIST, BaseRva));
        if(!count || !baseRva)
            return MakeUnexpected(Failure{});
        const auto ranges = ReadMinidumpRecords<MINIDUMP_MEMORY_DESCRIPTOR64>(stream, offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges), *count);
        if(!ranges || !dumpImage.rva(*baseRva, 0u))
            return MakeUnexpected(Failure{});
        u64 cursor = *baseRva;
        for(usize index = 0u; index < ranges->count; ++index){
            const MINIDUMP_MEMORY_DESCRIPTOR64 descriptor = ranges->record(index);
            const u8* const bytes = dumpImage.rva(cursor, descriptor.DataSize);
            if(!bytes || !AddMemoryRange(reader, descriptor.StartOfMemoryRange, descriptor.DataSize, bytes))
                return MakeUnexpected(Failure{});
            // The admitted payload ends within byteCount, so the cumulative RVA remains representable.
            cursor += descriptor.DataSize;
        }
    }
    return reader;
}

static BOOL CALLBACK ReadProcessMemoryFromDump(HANDLE process, DWORD64 baseAddress, PVOID buffer, DWORD size, LPDWORD bytesRead)noexcept{
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

        NWB_MEMCPY(buffer, static_cast<usize>(size), range.bytes.get() + static_cast<usize>(offset), static_cast<usize>(size));
        if(bytesRead)
            *bytesRead = size;
        return TRUE;
    }

    return FALSE;
}

struct CrashContext{
    CONTEXT context = {};
    DWORD threadId = 0u;
};

[[nodiscard]] static Expected<CrashContext> ReadCrashContext(const DumpImage& dumpImage, const DWORD threadId, const MINIDUMP_LOCATION_DESCRIPTOR& location)noexcept{
    if(location.DataSize < sizeof(CONTEXT) || !dumpImage.rva(location.Rva, location.DataSize))
        return MakeUnexpected(Failure{});
    const auto context = dumpImage.read<CONTEXT>(location.Rva);
    if(!context)
        return MakeUnexpected(Failure{});
    return CrashContext{ *context, threadId };
}

[[nodiscard]] static Expected<CrashContext> FindCrashContext(const DumpImage& dumpImage, const CrashPackageSummary& summary)noexcept{
    const auto exceptionStream = ReadMinidumpStream(dumpImage, ExceptionStream);
    if(!exceptionStream)
        return MakeUnexpected(Failure{});
    if(*exceptionStream){
        const auto exceptionInfo = (**exceptionStream).read<MINIDUMP_EXCEPTION_STREAM>(0u);
        if(!exceptionInfo)
            return MakeUnexpected(Failure{});
        return ReadCrashContext(dumpImage, exceptionInfo->ThreadId, exceptionInfo->ThreadContext);
    }

    if(summary.threadId > Limit<DWORD>::s_Max)
        return MakeUnexpected(Failure{});
    const DWORD threadId = static_cast<DWORD>(summary.threadId);
    const auto threadStream = ReadMinidumpStream(dumpImage, ThreadListStream);
    if(!threadStream || !*threadStream)
        return MakeUnexpected(Failure{});
    const DumpImage& stream = **threadStream;
    const auto count = stream.read<ULONG32>(0u);
    if(!count)
        return MakeUnexpected(Failure{});
    const auto threads = ReadMinidumpRecords<MINIDUMP_THREAD>(stream, offsetof(MINIDUMP_THREAD_LIST, Threads), *count);
    if(!threads)
        return MakeUnexpected(Failure{});
    for(usize index = 0u; index < threads->count; ++index){
        const MINIDUMP_THREAD thread = threads->record(index);
        if(threadId != 0u && thread.ThreadId != threadId)
            continue;
        return ReadCrashContext(dumpImage, thread.ThreadId, thread.ThreadContext);
    }
    return MakeUnexpected(Failure{});
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

[[nodiscard]] static WString<Core::Alloc::ScratchArena> BuildSymbolSearchPath(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config){
    WString<Core::Alloc::ScratchArena> path{scratchArena};
    const CrashScratchPath symbolStoreDirectory = EffectiveSymbolStoreDirectory(scratchArena, config);
    path.reserve(AddSize(AddSize(packageDirectory.native().size(), 1u), symbolStoreDirectory.native().size()));
    path += packageDirectory.native();
    path += L";";
    path += symbolStoreDirectory.native();
    return path;
}

static void LoadDumpModules(Core::Alloc::ScratchArena& scratchArena, const HANDLE symbolProcess, const DumpImage& dumpImage, CrashReportText& outReport){
    const auto moduleStream = ReadMinidumpStream(dumpImage, ModuleListStream);
    if(!moduleStream){
        outReport += "module_load=invalid_module_list\n";
        return;
    }
    if(!*moduleStream){
        outReport += "module_load=missing_module_list\n";
        return;
    }
    const DumpImage& stream = **moduleStream;
    const auto count = stream.read<ULONG32>(0u);
    if(!count){
        outReport += "module_load=invalid_module_list\n";
        return;
    }
    const auto modules = ReadMinidumpRecords<MINIDUMP_MODULE>(stream, offsetof(MINIDUMP_MODULE_LIST, Modules), *count);
    if(!modules){
        outReport += "module_load=invalid_module_list\n";
        return;
    }

    u32 loadedCount = 0u;
    for(usize index = 0u; index < modules->count; ++index){
        const MINIDUMP_MODULE module = modules->record(index);
        const auto moduleNameText = ReadDumpString(scratchArena, dumpImage, module.ModuleNameRva);
        if(!moduleNameText){
            outReport += "module_load=invalid_module_list\n";
            return;
        }
        const wchar* imageName = moduleNameText->empty() ? nullptr : moduleNameText->c_str();
        const DWORD64 loadedBase = SymLoadModuleExW(symbolProcess, nullptr, imageName, nullptr, module.BaseOfImage, module.SizeOfImage, nullptr, 0u);
        if(loadedBase != 0u)
            ++loadedCount;
    }

    outReport += "modules_loaded=";
    char countBuffer[s_DecimalTextBufferCapacity];
    outReport += FormatDecimal(static_cast<usize>(loadedCount), countBuffer);
    outReport += "\n";
}

static void AppendResolvedSymbol(Core::Alloc::ScratchArena& scratchArena, const HANDLE symbolProcess, CrashReportText& outReport, const DWORD frameIndex, const DWORD64 address){
    outReport += "#";
    char frameBuffer[s_DecimalTextBufferCapacity];
    outReport += FormatDecimal(static_cast<usize>(frameIndex), frameBuffer);
    outReport += " ";
    outReport += s_HexAddressPrefix;
    outReport += FormatHex64A(scratchArena, address);

    alignas(SYMBOL_INFOW) u8 symbolBuffer[sizeof(SYMBOL_INFOW) + (MAX_SYM_NAME * sizeof(wchar))] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 displacement = 0u;
    if(SymFromAddrW(symbolProcess, address, &displacement, symbol)){
        const AString<Core::Alloc::ScratchArena> name = BasicStringDetail::WideToUtf8(scratchArena, WStringView(symbol->Name, symbol->NameLen));
        outReport += " ";
        outReport += name;
        if(displacement != 0u){
            outReport += "+0x";
            outReport += FormatHex64A(scratchArena, displacement);
        }
    }
    else
        outReport += " <unknown>";

    IMAGEHLP_LINEW64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0u;
    if(SymGetLineFromAddrW64(symbolProcess, address, &lineDisplacement, &line)){
        const AString<Core::Alloc::ScratchArena> file = BasicStringDetail::WideToUtf8(scratchArena, WStringView(line.FileName));
        outReport += " at ";
        outReport += file;
        outReport += ":";
        char lineBuffer[s_DecimalTextBufferCapacity];
        outReport += FormatDecimal(static_cast<usize>(line.LineNumber), lineBuffer);
    }

    outReport += "\n";
}

[[nodiscard]] static u64 ParseHexAddressText(const AStringView hexText)noexcept{
    u64 value = 0u;
    for(const char c : hexText){
        u64 digit;
        if(c >= s_HexDigitFirst && c <= s_HexDigitLast)
            digit = static_cast<u64>(c - s_HexDigitFirst);
        else if(c >= s_HexLowerFirst && c <= s_HexLowerLast)
            digit = static_cast<u64>(c - s_HexLowerFirst) + s_HexLetterOffset;
        else if(c >= s_HexUpperFirst && c <= s_HexUpperLast)
            digit = static_cast<u64>(c - s_HexUpperFirst) + s_HexLetterOffset;
        else
            break;
        value = (value << s_HexRadixBits) | digit;
    }
    return value;
}

// Prefer client-captured frames; fall back to server StackWalk64 when none were shipped.
[[nodiscard]] static Expected<bool, ErrorCode> AppendClientCallstack(Core::Alloc::ScratchArena& scratchArena, const HANDLE symbolProcess, const CrashScratchPath& packageDirectory, CrashReportText& outReport){
    AString<Core::Alloc::ScratchArena> callstackText{scratchArena};
    const auto read = ReadTextFile(packageDirectory / CrashNames::s_CallstackFileName, callstackText);
    if(!read){
        if(read.error() == ErrorCondition::no_such_file_or_directory)
            return false;
        return MakeUnexpected(read.error());
    }
    if(callstackText.empty())
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
                AppendResolvedSymbol(scratchArena, symbolProcess, outReport, frameIndex, static_cast<DWORD64>(address));
                ++frameIndex;
                decodedAnyFrame = true;
            }
        }
        lineStart = lineEnd + 1u;
    }
    return decodedAnyFrame;
}

Expected<bool, CrashSymbolicationFailure> AppendWindowsMinidumpStack(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    const CrashScratchPath dumpPath = packageDirectory / CrashNames::s_ProcessDumpFileName;
    CrashBytes dumpBytes{scratchArena};
    const auto read = ReadBinaryFile(dumpPath, dumpBytes);
    if(!read || dumpBytes.empty()){
        outReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=";
        outReport += CrashNames::s_ProcessDumpFileName;
        outReport += " is missing or unreadable\n";
        if(!read)
            return MakeUnexpected(CrashSymbolicationFailure{ .operation = "read_windows_minidump", .code = read.error(), });
        return false;
    }

    const DumpImage dumpImage{ dumpBytes.data(), dumpBytes.size() };
    const auto crashContext = FindCrashContext(dumpImage, summary);
    if(!crashContext){
        outReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=minidump has no usable thread context\n";
        return false;
    }

    auto memoryReader = BuildDumpMemoryReader(scratchArena, dumpImage);
    if(!memoryReader){
        outReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=minidump contains invalid memory ranges\n";
        return false;
    }

    const HANDLE symbolProcess = GetCurrentProcess();
    const WString<Core::Alloc::ScratchArena> symbolPath = BuildSymbolSearchPath(scratchArena, packageDirectory, config);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if(!SymInitializeW(symbolProcess, symbolPath.c_str(), FALSE)){
        const ErrorCode error = GlobalFilesystemDetail::LastSystemError();
        outReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=SymInitializeW failed\n";
        return MakeUnexpected(CrashSymbolicationFailure{ .operation = "initialize_windows_symbols", .code = error, });
    }

    ScopeExit cleanupSymbols([&]()noexcept{
        if(!SymCleanup(symbolProcess)){
            const DiagnosticEventText detail = MakeDiagnosticEventText("Windows symbol session cleanup failed: native_category=system native_code={}", GetLastError());
            CaptureDiagnosticEvent("logger_windows_symbols", detail.view(), __FILE__, __LINE__);
        }
    }
    );
    s_CurrentDumpMemoryReader = &*memoryReader;
    ScopeExit clearMemoryReader([]()noexcept{ s_CurrentDumpMemoryReader = nullptr; });

    outReport += "status=decoded\nresolver=windows_pdb_minidump\nsymbol_path=";
    outReport += BasicStringDetail::WideToUtf8(scratchArena, WStringView(symbolPath));
    outReport += "\nthread_id=";
    char threadBuffer[s_DecimalTextBufferCapacity];
    outReport += FormatDecimal(static_cast<usize>(crashContext->threadId), threadBuffer);
    outReport += "\n";
    LoadDumpModules(scratchArena, symbolProcess, dumpImage, outReport);
    outReport += "\n[callstack]\n";

    // Resolve client frames against the dump's module map; unwind server-side only when no frames were supplied.
    const auto clientFrames = AppendClientCallstack(scratchArena, symbolProcess, packageDirectory, outReport);
    if(!clientFrames)
        AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = "read_client_callstack", .code = clientFrames.error(), });
    else if(*clientFrames)
        return true;

    CONTEXT walkContext = crashContext->context;
    STACKFRAME64 frame;
    InitializeStackFrameFromContext(frame, walkContext);

    const DWORD machineType = MachineTypeFromContext(walkContext);
    bool decodedAnyFrame = false;
    for(DWORD frameIndex = 0u; frameIndex < s_MaxWindowsStackFrames; ++frameIndex){
        if(frame.AddrPC.Offset == 0u)
            break;

        AppendResolvedSymbol(scratchArena, symbolProcess, outReport, frameIndex, frame.AddrPC.Offset);
        decodedAnyFrame = true;

        if(!StackWalk64(machineType, symbolProcess, nullptr, &frame, &walkContext, ReadProcessMemoryFromDump, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
    }

    if(!decodedAnyFrame)
        outReport += "<no frames decoded>\n";
    return decodedAnyFrame;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

