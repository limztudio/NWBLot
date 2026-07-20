// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_VolumeDefaultMetadataBytes = 512ull * 1024ull;
inline constexpr u64 s_VolumeMinMetadataBytes = 4ull * 1024ull;
inline constexpr u64 s_VolumeMoveChunkBytes = 1024ull * 1024ull;

using ::AddNoOverflow;
using ::CanRepresentU64;

inline constexpr char s_VolumeMagic[8] = { 'N', 'W', 'B', 'V', 'O', 'L', '1', '\0' };

struct VolumeHeaderDisk{
    char magic[8];
    u64 segmentSize;
    u64 metadataBytes;
    u64 fileCount;
    u64 indexBytes;
    u64 nextFreeOffset;
};

struct VolumeIndexEntryDisk{
    NameHash hash;
    u64 offset;
    u64 size;
};

static_assert(sizeof(VolumeHeaderDisk) == 48, "VolumeHeaderDisk size mismatch");
static_assert(sizeof(VolumeIndexEntryDisk) == 80, "VolumeIndexEntryDisk size mismatch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u64 DefaultMetadataBytes(const u64 segmentSize){
    u64 output = s_VolumeDefaultMetadataBytes;
    if(output >= segmentSize)
        output = segmentSize / 8;
    if(output < s_VolumeMinMetadataBytes)
        output = s_VolumeMinMetadataBytes;
    return output;
}

ACompactString LastErrnoMessage();
void LogFailure(AStringView volumeName, AStringView operation, AStringView detail);
void LogFailureWithPath(AStringView volumeName, AStringView operation, const Path& path, AStringView detail);
void LogFailureWithFsError(AStringView volumeName, AStringView operation, const Path& path, const ErrorCode& errorCode);
bool ReadVolumeHeaderFromSegment(AStringView volumeName, const Path& segmentPath, VolumeHeaderDisk& outHeader);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
