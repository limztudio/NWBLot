// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr char s_VolumeMagic[8] = { 'N', 'W', 'B', 'V', 'O', 'L', '1', '\0' };
inline constexpr u32 s_VolumeFormatVersion = 2u;


struct VolumeHeaderDisk{
    char magic[8];
    u32 version;
    u32 reserved;
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


static_assert(sizeof(VolumeHeaderDisk) == 56, "VolumeHeaderDisk size mismatch");
static_assert(sizeof(VolumeIndexEntryDisk) == 80, "VolumeIndexEntryDisk size mismatch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VolumeUsage{
enum Enum : u8{
    RuntimeReadOnly = 0,
    RuntimeReadWrite,
    CookWrite
};
};

struct VolumeMountDesc{
    AString volumeName;
    Path mountDirectory;

    u64 segmentSize = 0;
    u64 metadataSize = 0;
    usize maxSegments = 0;

    bool createIfMissing = false;
    VolumeUsage::Enum usage = VolumeUsage::RuntimeReadOnly;
};


struct VolumeBuildConfig{
    AString volumeName;
    u64 segmentSize = 0;
    u64 metadataSize = 0;
};

struct VolumeBuildInfo{
    u64 fileCount = 0;
    u64 segmentCount = 0;
};

bool BuildVolume(
    const Path& outputDirectory,
    const VolumeBuildConfig& config,
    const HashMap<AString, Vector<u8>>& files,
    VolumeBuildInfo& outBuildInfo
);

bool PublishStagedVolume(
    const StagedDirectoryPaths& stagedPaths,
    const Path& outputDirectory,
    AStringView volumeName,
    usize segmentCount
);
bool RemoveVolumeSegments(const Path& outputDirectory, AStringView volumeName);


class VolumeFileSystem : NoCopy{
private:
    struct FileRecord{
        u64 offset = 0;
        u64 size = 0;
    };

    using SegmentPathAllocator = Alloc::CustomAllocator<Path>;
    using SegmentPathVector = Vector<Path, SegmentPathAllocator>;

    using FileMapAllocator = Alloc::CustomAllocator<Pair<const Name, FileRecord>>;
    using FileMap = HashMap<Name, FileRecord, Hasher<Name>, EqualTo<Name>, FileMapAllocator>;


public:
    explicit VolumeFileSystem(Alloc::CustomArena& arena);
    ~VolumeFileSystem();


public:
    bool mount(const VolumeMountDesc& desc);
    void unmount();

    [[nodiscard]] bool mounted()const;
    [[nodiscard]] bool writable()const;

    [[nodiscard]] AString volumeName()const;
    [[nodiscard]] Path mountDirectory()const;

    [[nodiscard]] u64 segmentSize()const;
    [[nodiscard]] u64 metadataSize()const;
    [[nodiscard]] usize segmentCount()const;
    [[nodiscard]] u64 fileCount()const;
    [[nodiscard]] u64 usedBytes()const;
    [[nodiscard]] u64 wastedBytes()const;


public:
    bool writeFile(const Name& virtualPath, const void* data, usize bytes);
    bool writeFile(const Name& virtualPath, const Vector<u8>& data);
    bool writeFileDeferred(const Name& virtualPath, const void* data, usize bytes);
    bool writeFileDeferred(const Name& virtualPath, const Vector<u8>& data);
    bool flushMetadata();

    bool readFile(const Name& virtualPath, Vector<u8>& outData)const;
    bool removeFile(const Name& virtualPath);

    bool fileExists(const Name& virtualPath)const;
    bool fileSize(const Name& virtualPath, u64& outSize)const;

    Vector<Name> listFiles()const;
    bool compact(bool shrinkSegments = true);


private:
    bool writeFileLocked(const Name& virtualPath, const void* data, usize bytes, bool flushMetadataAfterWrite);
    bool scanSegmentsLocked();

    bool createSegmentLocked(usize segmentIndex);
    bool ensureCapacityLocked(u64 requiredBytes);

    bool loadMetadataLocked();
    bool flushMetadataLocked();
    bool canFitMetadataForFileCountLocked(u64 fileCount)const;

    bool readBytesLocked(u64 offset, void* data, u64 byteCount)const;
    bool writeBytesLocked(u64 offset, const void* data, u64 byteCount);
    bool moveBytesLocked(u64 destinationOffset, u64 sourceOffset, u64 byteCount);
    bool trimSegmentsForNextFreeOffsetLocked();

    void unmountLocked();
    Path segmentPath(usize segmentIndex)const;


private:
    mutable Futex m_mutex;

    VolumeUsage::Enum m_usage = VolumeUsage::RuntimeReadOnly;
    bool m_mounted = false;
    bool m_writable = false;

    AString m_volumeName;
    Path m_mountDirectory;

    u64 m_segmentSize = 0;
    u64 m_metadataBytes = 0;
    u64 m_nextFreeOffset = 0;
    usize m_maxSegments = 0;

    Alloc::CustomArena& m_arena;
    SegmentPathVector m_segmentPaths;
    FileMap m_files;
};


class VolumeSession final : NoCopy{
public:
    explicit VolumeSession(Alloc::CustomArena& arena);


public:
    bool create(const Path& outputDirectory, const VolumeBuildConfig& config);
    bool load(AStringView volumeName, const Path& mountDirectory);

    bool pushData(const Name& virtualPath, const Vector<u8>& data);
    bool pushData(AStringView virtualPath, const Vector<u8>& data);
    bool pushDataDeferred(const Name& virtualPath, const Vector<u8>& data);
    bool pushDataDeferred(AStringView virtualPath, const Vector<u8>& data);
    bool flush();
    bool loadData(AStringView virtualPath, Vector<u8>& outData)const;
    bool loadData(const Name& virtualPath, Vector<u8>& outData)const;

    [[nodiscard]] bool mounted()const{ return m_volumeFileSystem.mounted(); }
    [[nodiscard]] bool writable()const{ return m_volumeFileSystem.writable(); }
    [[nodiscard]] u64 fileCount()const{ return m_volumeFileSystem.fileCount(); }
    [[nodiscard]] usize segmentCount()const{ return m_volumeFileSystem.segmentCount(); }


private:
    VolumeFileSystem m_volumeFileSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

