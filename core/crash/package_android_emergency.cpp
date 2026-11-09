// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_android_emergency.h"

#include <global/diagnostics.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_ANDROID)
static b8 WriteAndroidCollectionNote(Alloc::ScratchArena& scratchArena, const CrashRequest& request){
    constexpr AStringView s_CollectionNote = "application_exit_info=not_collected_by_native_layer\n" "detail=Java/Kotlin host should attach ApplicationExitInfo tombstone data on next launch\n";
    return WriteTextFile(RequestPendingDirectory(scratchArena, request) / PackageNames::s_AndroidCollectionFileName, s_CollectionNote);
}

void CrashAndroidEmergencyCollector::Collect(Alloc::ScratchArena& scratchArena, const CrashUploadSnapshot& snapshot){
    const ::Path<Alloc::ScratchArena> recordPath = ::Path<Alloc::ScratchArena>(scratchArena, snapshot.spoolDirectory.data()) / PackageNames::s_AndroidEmergencyRequestFileName;
    CrashBytesT<Alloc::ScratchArena> bytes(scratchArena);
    const auto read = ReadBinaryFile(recordPath, bytes);
    if(!read){
        if(read.error() != ErrorCondition::no_such_file_or_directory)
            CaptureCrashPackageFilesystemFailure("read_android_emergency_record", read.error());
        return;
    }
    if(bytes.size() < sizeof(CrashRequest))
        return;
    CrashRequest request;
    const usize offset = bytes.size() - sizeof(CrashRequest);
    FOUNDATION_MEMCPY(&request, sizeof(request), bytes.data() + offset, sizeof(request));
    if(CrashProtocol::IsCurrentCrashRequest(request)){
        if(!WriteCrashPackage(request))
            return;
        if(!WriteAndroidCollectionNote(scratchArena, request))
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = DiagnosticEventName::s_Warning,
                .category = s_PackageErrorCategory,
                .expression = "write_android_collection_note",
                .message = "Android collection note could not be written or closed",
                .file = __FILE__,
                .line = __LINE__,
            });
    }
    if(!WriteBinaryFile(recordPath, Span<const u8>{}))
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Warning,
            .category = s_PackageErrorCategory,
            .expression = "clear_android_emergency_record",
            .message = "Collected Android emergency record could not be cleared or closed",
            .file = __FILE__,
            .line = __LINE__,
        });
}
#else
void CrashAndroidEmergencyCollector::Collect([[maybe_unused]] Alloc::ScratchArena& scratchArena, [[maybe_unused]] const CrashUploadSnapshot& snapshot){}
#endif

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

