// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline AString MakeLegacyVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex){
    return StringFormat("{}_{}.vol", volumeName, segmentIndex);
}

[[nodiscard]] inline u64 HashVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex){
    const AString legacyFileName = MakeLegacyVolumeSegmentFileName(volumeName, segmentIndex);
    return ComputeFnv64Bytes(legacyFileName.data(), legacyFileName.size());
}

[[nodiscard]] inline AString MakeVolumeSegmentFileName(const AStringView volumeName, const usize segmentIndex){
    static constexpr char s_HexDigits[16] = {
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'a', 'b',
        'c', 'd', 'e', 'f'
    };

    const u64 hash = HashVolumeSegmentFileName(volumeName, segmentIndex);

    AString fileName;
    fileName.reserve(16 + 4);
    for(u32 i = 0; i < 16; ++i){
        const u32 shift = (15u - i) * 4u;
        const usize nibble = static_cast<usize>((hash >> shift) & 0xfull);
        fileName.push_back(s_HexDigits[nibble]);
    }
    fileName += ".vol";
    return fileName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

