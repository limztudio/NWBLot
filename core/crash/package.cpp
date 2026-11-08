// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_internal.h"
#include "package_report.h"

#include <core/crash/package_values.adhd.h>

#include <global/diagnostics.h>
#include <global/error_code.h>
#include <global/fixed_buffer.h>
#include <global/json_text.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_LinuxProcPathTextCapacity = ::NWB::Core::Crash::PackageValues::s_LinuxProcPathTextCapacity;
inline constexpr usize s_LinuxCopyBufferBytes = ::NWB::Core::Crash::PackageValues::s_LinuxCopyBufferBytes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
template<typename ArenaT>
static b8 WriteWindowsMinidump(ArenaT& arena, const CrashRequest& request){
    constexpr AStringView s_CloseDumpProcessOperation = ::NWB::Core::Crash::PackageValues::s_CloseDumpProcessOperation;

    const ::Path<ArenaT> dumpPath = RequestPendingDirectory(arena, request) / PackageNames::s_ProcessDumpFileName;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, request.processId);
    if(!process){
        CaptureCrashPackageFilesystemFailure("open_dump_process", LastSystemError());
        return false;
    }
    HANDLE dumpFile = CreateFile(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(dumpFile == INVALID_HANDLE_VALUE){
        const ErrorCode createError = LastSystemError();
        const BOOL processClosed = CloseHandle(process);
        const ErrorCode closeError = processClosed ? ErrorCode{} : LastSystemError();
        CaptureCrashPackageFilesystemFailure("create_minidump_file", createError);
        if(!processClosed)
            CaptureCrashPackageFilesystemFailure(s_CloseDumpProcessOperation, closeError);
        return false;
    }
    const MINIDUMP_TYPE dumpType = request.dumpDetailMode == DumpDetailMode::Full
        ? static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules)
        : static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules)
    ;
    ManualDumpContextStorage manualContext(request);
    MINIDUMP_EXCEPTION_INFORMATION exceptionInformation = {};
    MINIDUMP_EXCEPTION_INFORMATION* exceptionInformationPointer = nullptr;
    if(request.manualContextSize != 0u){
        exceptionInformation.ThreadId = static_cast<DWORD>(request.threadId);
        exceptionInformation.ExceptionPointers = &manualContext.exceptionPointers;
        exceptionInformation.ClientPointers = FALSE;
        exceptionInformationPointer = &exceptionInformation;
    }
    else if(request.exceptionPointers != 0u){
        exceptionInformation.ThreadId = static_cast<DWORD>(request.threadId);
        exceptionInformation.ExceptionPointers = reinterpret_cast<EXCEPTION_POINTERS*>(static_cast<usize>(request.exceptionPointers));
        exceptionInformation.ClientPointers = TRUE;
        exceptionInformationPointer = &exceptionInformation;
    }
    const BOOL written = MiniDumpWriteDump(process, request.processId, dumpFile, dumpType, exceptionInformationPointer, nullptr, nullptr);
    const ErrorCode writeError = written ? ErrorCode{} : LastSystemError();
    const BOOL dumpClosed = CloseHandle(dumpFile);
    const ErrorCode dumpCloseError = dumpClosed ? ErrorCode{} : LastSystemError();
    const BOOL processClosed = CloseHandle(process);
    const ErrorCode processCloseError = processClosed ? ErrorCode{} : LastSystemError();
    if(!written)
        CaptureCrashPackageFilesystemFailure("write_minidump", writeError);
    if(!dumpClosed)
        CaptureCrashPackageFilesystemFailure("close_minidump_file", dumpCloseError);
    if(!processClosed)
        CaptureCrashPackageFilesystemFailure(s_CloseDumpProcessOperation, processCloseError);
    return written == TRUE && dumpClosed == TRUE && processClosed == TRUE;
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
template<typename ArenaT>
static void CopyFileToPackage(ArenaT& arena, const CrashRequest& request, const AStringView sourcePath, const AStringView outputName){
    constexpr StringView s_OptionalArtifactCategory = ::NWB::Core::Crash::PackageValues::s_OptionalArtifactCategory;

    InputFileStream input(sourcePath.data(), s_FileOpenBinary);
    if(!input.is_open())
        return;

    const ::Path<ArenaT> outputPath = RequestPendingDirectory(arena, request) / outputName;
    OutputFileStream output(outputPath.c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(!output.is_open()){
        CaptureDiagnosticEvent(s_OptionalArtifactCategory, "Failed to open optional crash artifact output", __FILE__, __LINE__);
        return;
    }

    b8 copied = true;
    Array<char, s_LinuxCopyBufferBytes> buffer;
    for(;;){
        input.read(buffer.data(), static_cast<StreamSize>(sizeof(buffer)));
        const StreamSize bytesRead = input.gcount();
        if(bytesRead > 0)
            output.write(buffer.data(), bytesRead);
        if(!output.good()){
            copied = false;
            break;
        }
        if(input.eof() && !input.bad())
            break;
        if(!input.good()){
            copied = false;
            break;
        }
    }
    output.close();
    if(copied && output.good())
        return;

    CaptureDiagnosticEvent(s_OptionalArtifactCategory, "Failed to copy or close optional crash artifact output", __FILE__, __LINE__);
    const auto removed = RemoveFile(outputPath);
    if(!removed)
        CaptureCrashPackageFilesystemFailure("remove_incomplete_optional_artifact", removed.error());
}

template<typename ArenaT>
static void CopyProcFile(ArenaT& arena, const CrashRequest& request, const AStringView procName, const AStringView outputName){
    Array<char, s_LinuxProcPathTextCapacity> procPath;
    CopyFixedBuffer(procPath, PackageNames::s_LinuxProcRootPath);
    AppendUnsignedToFixedBuffer(procPath, request.processId);
    AppendFixedBuffer(procPath, "/");
    AppendFixedBuffer(procPath, procName);

    CopyFileToPackage(arena, request, FixedBufferView(procPath), outputName);
}
#endif

void CaptureCrashPackageFilesystemFailure(const AStringView operation, const ErrorCode& error)noexcept{
    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash package I/O failure: {} {}", error.category().name(), error.value());
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_PackageErrorCategory, .expression = operation, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
}

b8 WriteCrashPackage(const CrashRequest& request){
    auto& arena = DumpArena();
    if(!CrashProtocol::IsCurrentCrashRequest(request))
        return false;
    if(!CrashPackageReport::Write(arena, request))
        return false;

#if defined(NWB_PLATFORM_WINDOWS)
    if(request.platform == PlatformKind::Windows){
        const b8 dumpWritten = WriteWindowsMinidump(arena, request);
        if(!WriteTextFile(
            RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName,
            dumpWritten ? "minidump captured; server-side PDB symbolication pending\n" : "minidump capture failed; metadata package only\n"
        ))
            return false;
    }
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    if(request.platform == PlatformKind::Linux){
        CopyProcFile(arena, request, PackageNames::s_ProcAuxvName, PackageNames::s_ProcAuxvFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcCmdlineName, PackageNames::s_ProcCmdlineFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcCoredumpFilterName, PackageNames::s_ProcCoredumpFilterFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcEnvironName, PackageNames::s_ProcEnvironFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcLimitsName, PackageNames::s_ProcLimitsFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcMapsName, PackageNames::s_ProcMapsFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcStatName, PackageNames::s_ProcStatFileName);
        CopyProcFile(arena, request, PackageNames::s_ProcStatusName, PackageNames::s_ProcStatusFileName);
        CopyFileToPackage(arena, request, PackageNames::s_LinuxCorePatternPath, PackageNames::s_LinuxCorePatternFileName);
        CopyFileToPackage(arena, request, PackageNames::s_LinuxCoreUsesPidPath, PackageNames::s_LinuxCoreUsesPidFileName);
        if(!WriteTextFile(
            RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName,
            "linux crash package captured; OS core policy remains authoritative; server-side DWARF symbolication uses reachable module symbols\n"
        ))
            return false;
    }
#else
    if(!WriteTextFile(RequestPendingDirectory(arena, request) / PackageNames::s_SymbolicationFileName, "native platform crash artifact expected; server-side symbolication pending\n"))
        return false;
#endif

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

