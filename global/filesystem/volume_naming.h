#pragma once


#include "../compact_string.h"
#include "../hash_utils.h"
#include "../text_utils.h"


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

