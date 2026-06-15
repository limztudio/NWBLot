// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DumpArenaPayloadSize = 512u * 1024u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Alloc::PersistentArena& DumpArena(){
    static Alloc::PersistentArena s_Arena(
        Alloc::PersistentArena::StructureAlignedSize(s_DumpArenaPayloadSize),
        "NWB::Core::Crash::DumpArena"
    );
    return s_Arena;
}

template<typename ArenaT>
bool EnsureCrashSpoolDirectories(const ::Path<ArenaT>& spoolDirectory){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(PendingDirectory(spoolDirectory), error));
    if(error)
        return false;

    error.clear();
    static_cast<void>(EnsureDirectories(UploadedDirectory(spoolDirectory), error));
    if(error)
        return false;

    error.clear();
    static_cast<void>(EnsureDirectories(UploadingDirectory(spoolDirectory), error));
    if(error)
        return false;

    error.clear();
    static_cast<void>(EnsureDirectories(FailedDirectory(spoolDirectory), error));
    return !error;
}

template bool EnsureCrashSpoolDirectories(const ::Path<Alloc::PersistentArena>& spoolDirectory);

CrashDumpResult CrashPackageResult(const CrashRequest& request){
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return CrashDumpResult{ CrashDumpStatus::RequestQueued };
    if(request.spoolDirectory[0] == 0 || request.crashId[0] == 0)
        return CrashDumpResult{ CrashDumpStatus::RequestQueued };

    Alloc::PersistentArena& arena = DumpArena();
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_UploadedDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::Uploaded };
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_FailedDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::UploadFailed };
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_UploadingDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::PackageWritten };
    if(PathIsDirectory(RequestBucketDirectory(arena, request, PackageNames::s_PendingDirectoryName)))
        return CrashDumpResult{ CrashDumpStatus::PackageWritten };

    return CrashDumpResult{ CrashDumpStatus::RequestQueued };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

