// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../compact_string.h"
#include "../hash_utils.h"
#include "../text_utils.h"
#include "operations.h"
#include "path.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ValidVolumeName(const AStringView volumeName){
    if(volumeName.empty())
        return false;

    for(const char ch : volumeName){
        if(IsAsciiAlphaNumeric(ch) || ch == '_' || ch == '-')
            continue;
        return false;
    }
    return true;
}

[[nodiscard]] inline u64 HashVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex){
    char segmentIndexBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
    const AStringView segmentIndexText = FormatDecimal(segmentIndex, segmentIndexBuffer);
    NWB_ASSERT(!segmentIndexText.empty());

    u64 hash = FNV64_OFFSET_BASIS;
    hash = UpdateFnv64TextExact(hash, volumeName);
    hash = UpdateFnv64TextExact(hash, AStringView("_"));
    hash = UpdateFnv64TextExact(hash, segmentIndexText);
    hash = UpdateFnv64TextExact(hash, AStringView(".vol"));
    return hash;
}

[[nodiscard]] inline ACompactString MakeVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex){
    const u64 hash = HashVolumeSegmentFileName(volumeName, segmentIndex);

    ACompactString fileName;
    AppendHexU64(hash, fileName);
    fileName += ".vol";
    return fileName;
}

template<typename ArenaT>
[[nodiscard]] inline Path<ArenaT> MakeVolumeSegmentPath(
    const Path<ArenaT>& directory,
    const AStringView volumeName,
    const usize segmentIndex
){
    return directory / MakeVolumeSegmentFileName(volumeName, segmentIndex).c_str();
}

template<typename ArenaT>
[[nodiscard]] inline bool VolumeSegmentExists(
    const Path<ArenaT>& directory,
    const AStringView volumeName,
    const usize segmentIndex = 0u
){
    if(directory.empty())
        return false;

    ErrorCode errorCode;
    if(!IsDirectory(directory, errorCode) || errorCode)
        return false;

    const Path<ArenaT> segmentPath = MakeVolumeSegmentPath(directory, volumeName, segmentIndex);
    errorCode.clear();
    return FileExists(segmentPath, errorCode) && !errorCode;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

