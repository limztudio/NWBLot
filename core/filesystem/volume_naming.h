// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u64 HashVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex){
    char segmentIndexText[32] = {};
    const auto convertResult = std::to_chars(segmentIndexText, segmentIndexText + sizeof(segmentIndexText), segmentIndex);
    NWB_ASSERT(convertResult.ec == std::errc());

    u64 hash = FNV64_OFFSET_BASIS;
    hash = UpdateFnv64TextExact(hash, volumeName);
    hash = UpdateFnv64TextExact(hash, AStringView("_"));
    hash = UpdateFnv64TextExact(hash, AStringView(segmentIndexText, static_cast<usize>(convertResult.ptr - segmentIndexText)));
    hash = UpdateFnv64TextExact(hash, AStringView(".vol"));
    return hash;
}

[[nodiscard]] inline AString MakeVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex){
    const u64 hash = HashVolumeSegmentFileName(volumeName, segmentIndex);

    AString fileName;
    fileName.reserve(16 + 4);
    AppendHexU64(hash, fileName);
    fileName += ".vol";
    return fileName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

