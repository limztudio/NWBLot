// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "codec.h"

#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ArchiveStatus{
    enum Enum : u8{
        Ok,
        Disabled,
        NotConfigured,
        EncodeFailed,
        WriteFailed,
        ReadFailed,
        DecodeFailed,
    };
};

struct ArchiveResult{
    ArchiveStatus::Enum status = ArchiveStatus::Ok;
    DecodeResult decode;
    usize byteCount = 0u;

    [[nodiscard]] bool ok()const{ return status == ArchiveStatus::Ok && decode.ok(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PathArenaT>
[[nodiscard]] ArchiveResult WriteEventStreamArchive(
    TelemetryArena& arena,
    const EventView& events,
    const ::Path<PathArenaT>& path
){
    ArchiveResult result;

    TelemetryBytes encoded(arena);
    if(!EncodeEventStream(events, encoded)){
        result.status = ArchiveStatus::EncodeFailed;
        return result;
    }

    result.byteCount = encoded.size();
    if(!WriteBinaryFile(path, encoded)){
        result.status = ArchiveStatus::WriteFailed;
        return result;
    }

    return result;
}

template<typename PathArenaT>
[[nodiscard]] ArchiveResult ReadEventStreamArchive(
    TelemetryArena& arena,
    const ::Path<PathArenaT>& path,
    Recorder& outRecorder
){
    ArchiveResult result;

    TelemetryBytes encoded(arena);
    ErrorCode readError;
    if(!ReadBinaryFile(path, encoded, readError)){
        result.status = ArchiveStatus::ReadFailed;
        return result;
    }

    result.byteCount = encoded.size();
    result.decode = DecodeEventStream(arena, encoded.data(), encoded.size(), outRecorder);
    if(!result.decode.ok())
        result.status = ArchiveStatus::DecodeFailed;

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

