// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FilesystemVolumeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_VolumeDefaultMetadataBytes = 512ull * 1024ull;
inline constexpr u64 s_VolumeMinMetadataBytes = 4ull * 1024ull;
inline constexpr u64 s_VolumeMoveChunkBytes = 1024ull * 1024ull;
inline constexpr u64 s_VolumeFallbackMetadataDivisor = 8u;

using ::AddNoOverflow;
using ::CanRepresentU64;

inline constexpr char s_VolumeOpLoadMetadata[] = "loadMetadata";
inline constexpr char s_VolumeOpFlushMetadata[] = "flushMetadata";
inline constexpr char s_VolumeOpMount[] = "mount";
inline constexpr char s_VolumeOpCompact[] = "compact";
inline constexpr char s_VolumeOpWriteFile[] = "writeFile";
inline constexpr char s_VolumeOpReadFile[] = "readFile";
inline constexpr char s_VolumeOpRemoveFile[] = "removeFile";
inline constexpr char s_VolumeOpMoveBytes[] = "moveBytes";
inline constexpr char s_VolumeOpWriteBytes[] = "writeBytes";
inline constexpr char s_VolumeOpReadBytes[] = "readBytes";
inline constexpr char s_VolumeOpEnsureCapacity[] = "ensureCapacity";
inline constexpr char s_VolumeOpTrimSegments[] = "trimSegments";
inline constexpr char s_VolumeOpRemove[] = "remove";
inline constexpr char s_VolumeOpFileSize[] = "fileSize";
inline constexpr char s_VolumeOpPhysicalCapacity[] = "physicalCapacity";
inline constexpr char s_VolumeOpScanSegments[] = "scanSegments";
inline constexpr char s_VolumeOpCreateSegment[] = "createSegment";
inline constexpr char s_VolumeOpMountExists[] = "mount:exists";
inline constexpr char s_VolumeOpMountCreateDirectories[] = "mount:create_directories";
inline constexpr char s_VolumeOpMountIsDirectory[] = "mount:is_directory";
inline constexpr char s_VolumeOpMountOpenHeader[] = "mount:open_header";
inline constexpr char s_VolumeOpMountReadHeader[] = "mount:read_header";
inline constexpr char s_VolumeOpMountFileSize[] = "mount:file_size";
inline constexpr char s_VolumeOpTrimSegmentsRemove[] = "trimSegments:remove";
inline constexpr char s_VolumeOpTrimSegmentsResize[] = "trimSegments:resize";
inline constexpr char s_VolumeOpTrimSegmentsFileSize[] = "trimSegments:file_size";
inline constexpr char s_VolumeOpPhysicalCapacityFileSize[] = "physicalCapacity:file_size";
inline constexpr char s_VolumeOpScanSegmentsExists[] = "scanSegments:exists";
inline constexpr char s_VolumeOpScanSegmentsIsRegularFile[] = "scanSegments:is_regular_file";
inline constexpr char s_VolumeOpCreateSegmentOpen[] = "createSegment:open";
inline constexpr char s_VolumeOpCreateSegmentSeek[] = "createSegment:seek";
inline constexpr char s_VolumeOpCreateSegmentWrite[] = "createSegment:write";
inline constexpr char s_VolumeMagic[] = "NWBVOL1";
inline constexpr usize s_VolumeMagicByteCount = sizeof(s_VolumeMagic);

struct VolumeHeaderDisk{
    char magic[s_VolumeMagicByteCount];
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

static constexpr usize s_VolumeHeaderDiskBytes = 48u;
static_assert(sizeof(VolumeHeaderDisk) == s_VolumeHeaderDiskBytes, "VolumeHeaderDisk size mismatch");
static constexpr usize s_VolumeIndexEntryDiskBytes = 80u;
static_assert(sizeof(VolumeIndexEntryDisk) == s_VolumeIndexEntryDiskBytes, "VolumeIndexEntryDisk size mismatch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u64 DefaultMetadataBytes(const u64 segmentSize){
    u64 output = s_VolumeDefaultMetadataBytes;
    if(output >= segmentSize)
        output = segmentSize / s_VolumeFallbackMetadataDivisor;
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

