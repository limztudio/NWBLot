// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/global.h>

#include <global/binary.h>
#include <global/filesystem/operations_file_io.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TelemetryFilePublicationResult{
    b8 published = false;
    b8 stagingRetained = false;
    ErrorCode error;
};


template<typename ArenaT>
[[nodiscard]] TelemetryFilePublicationResult PublishNewTelemetryFile(const ::Path<ArenaT>& destination, const BinaryByteView bytes){
    ::Path<ArenaT> staging(destination.arena(), destination);
    staging += ".partial";

    b8 created = false;
    const auto staged = StageNewBinaryFileDurable(staging, bytes.data(), bytes.size(), created);
    if(!staged)
        return { false, created, staged.error() };

#if defined(NWB_PLATFORM_WINDOWS)
    if(!MoveFileEx(staging.c_str(), destination.c_str(), 0u))
        return { false, true, LastSystemError() };
#else
    if(::link(staging.c_str(), destination.c_str()) != 0)
        return { false, true, LastSystemError() };
    const auto removed = RemoveFile(staging);
    if(!removed)
        return { true, true, removed.error() };
    if(!*removed)
        return { true, false, MakeErrorCode(ErrorCondition::no_such_file_or_directory) };
#endif
    return { true, false, {} };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

