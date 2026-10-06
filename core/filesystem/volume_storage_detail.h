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

inline constexpr AStringView s_VolumeOpLoadMetadata = "loadMetadata";
inline constexpr AStringView s_VolumeOpFlushMetadata = "flushMetadata";
inline constexpr AStringView s_VolumeOpMount = "mount";
inline constexpr AStringView s_VolumeOpCompact = "compact";
inline constexpr AStringView s_VolumeOpWriteFile = "writeFile";
inline constexpr AStringView s_VolumeOpReadFile = "readFile";
inline constexpr AStringView s_VolumeOpRemoveFile = "removeFile";
inline constexpr AStringView s_VolumeOpMoveBytes = "moveBytes";
inline constexpr AStringView s_VolumeOpWriteBytes = "writeBytes";
inline constexpr AStringView s_VolumeOpReadBytes = "readBytes";
inline constexpr AStringView s_VolumeOpEnsureCapacity = "ensureCapacity";
inline constexpr AStringView s_VolumeOpTrimSegments = "trimSegments";
inline constexpr AStringView s_VolumeOpRemove = "remove";
inline constexpr AStringView s_VolumeOpFileSize = "fileSize";
inline constexpr AStringView s_VolumeOpPhysicalCapacity = "physicalCapacity";
inline constexpr AStringView s_VolumeOpScanSegments = "scanSegments";
inline constexpr AStringView s_VolumeOpCreateSegment = "createSegment";
inline constexpr AStringView s_VolumeOpMountExists = "mount:exists";
inline constexpr AStringView s_VolumeOpMountCreateDirectories = "mount:create_directories";
inline constexpr AStringView s_VolumeOpMountIsDirectory = "mount:is_directory";
inline constexpr AStringView s_VolumeOpMountOpenHeader = "mount:open_header";
inline constexpr AStringView s_VolumeOpMountReadHeader = "mount:read_header";
inline constexpr AStringView s_VolumeOpMountFileSize = "mount:file_size";
inline constexpr AStringView s_VolumeOpTrimSegmentsRemove = "trimSegments:remove";
inline constexpr AStringView s_VolumeOpTrimSegmentsResize = "trimSegments:resize";
inline constexpr AStringView s_VolumeOpTrimSegmentsFileSize = "trimSegments:file_size";
inline constexpr AStringView s_VolumeOpPhysicalCapacityFileSize = "physicalCapacity:file_size";
inline constexpr AStringView s_VolumeOpScanSegmentsExists = "scanSegments:exists";
inline constexpr AStringView s_VolumeOpScanSegmentsIsRegularFile = "scanSegments:is_regular_file";
inline constexpr AStringView s_VolumeOpCreateSegmentOpen = "createSegment:open";
inline constexpr AStringView s_VolumeOpCreateSegmentSeek = "createSegment:seek";
inline constexpr AStringView s_VolumeOpCreateSegmentWrite = "createSegment:write";
inline constexpr AStringView s_VolumeDetailSegmentSizeZero = "segment size is zero";
inline constexpr AStringView s_VolumeDetailNoMountedSegmentsOrSizeZero = "no mounted segments or segment size is zero";
inline constexpr AStringView s_VolumeDetailNotWritableOrSizeZero = "filesystem is not writable or segment size is zero";
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


[[nodiscard]] inline u64 DefaultMetadataBytes(const u64 segmentSize)noexcept{
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

