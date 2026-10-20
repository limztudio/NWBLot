// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/compact_string.h>
#include <global/hash_utils.h>
#include <global/numeric_text.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_VolumeSegmentExtension = ".vol";

[[nodiscard]] inline bool ValidVolumeName(const AStringView volumeName)noexcept{
    if(volumeName.empty())
        return false;

    for(const char ch : volumeName){
        if(IsAsciiAlphaNumeric(ch) || ch == '_' || ch == '-')
            continue;
        return false;
    }
    return true;
}

[[nodiscard]] inline u64 HashVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex)noexcept{
    char segmentIndexBuffer[TextDetail::s_DecimalTextBufferBytes];
    const AStringView segmentIndexText = FormatDecimal(segmentIndex, segmentIndexBuffer);
    NWB_ASSERT(!segmentIndexText.empty());

    u64 hash = s_Fnv64OffsetBasis;
    hash = UpdateFnv64TextExact(hash, volumeName);
    hash = UpdateFnv64TextExact(hash, AStringView("_"));
    hash = UpdateFnv64TextExact(hash, segmentIndexText);
    hash = UpdateFnv64TextExact(hash, s_VolumeSegmentExtension);
    return hash;
}

[[nodiscard]] inline ACompactString MakeVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex)noexcept{
    const u64 hash = HashVolumeSegmentFileName(volumeName, segmentIndex);

    ACompactString fileName;
    AppendHexU64(hash, fileName);
    fileName += s_VolumeSegmentExtension;
    return fileName;
}

template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> MakeVolumeSegmentPath(const ::Path<ArenaT>& directory, const AStringView volumeName, const usize segmentIndex){
    return directory / MakeVolumeSegmentFileName(volumeName, segmentIndex).view();
}

template<typename ArenaT>
[[nodiscard]] inline bool VolumeSegmentExists(const ::Path<ArenaT>& directory, const AStringView volumeName, const usize segmentIndex = 0u){
    if(directory.empty())
        return false;

    const auto isDirectory = IsDirectory(directory);
    if(!isDirectory || !*isDirectory)
        return false;

    const ::Path<ArenaT> segmentPath = MakeVolumeSegmentPath(directory, volumeName, segmentIndex);
    const auto exists = FileExists(segmentPath);
    return exists && *exists;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

