// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


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

class VolumeFileSystem : NoCopy{
private:
    struct FileRecord{
        u64 offset = 0;
        u64 size = 0;
    };

    using FileMap = HashMap<Name, FileRecord>;


public:
    VolumeFileSystem();
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

    bool readFile(const Name& virtualPath, Vector<u8>& outData)const;
    bool removeFile(const Name& virtualPath);

    bool fileExists(const Name& virtualPath)const;
    bool fileSize(const Name& virtualPath, u64& outSize)const;

    Vector<Name> listFiles()const;
    bool compact(bool shrinkSegments = true);


private:
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

    Vector<Path> m_segmentPaths;
    FileMap m_files;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

