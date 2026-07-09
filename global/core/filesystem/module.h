// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/core/common/log.h>


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

using VolumeArena = Alloc::GlobalArena;
using VolumeString = AString<VolumeArena>;
using VolumeBytes = Vector<u8, VolumeArena>;
using VolumeBuildFileMap = HashMap<VolumeString, VolumeBytes, VolumeArena>;

struct VolumeMountDesc{
    ACompactString volumeName;
    Path mountDirectory;

    u64 segmentSize = 0;
    u64 metadataSize = 0;
    usize maxSegments = 0;

    bool createIfMissing = false;
    VolumeUsage::Enum usage = VolumeUsage::RuntimeReadOnly;

    explicit VolumeMountDesc(VolumeArena& arena)
        : mountDirectory(arena)
    {}
};


struct VolumeBuildConfig{
    ACompactString volumeName;
    u64 segmentSize = 0;
    u64 metadataSize = 0;
};

struct VolumeBuildInfo{
    u64 fileCount = 0;
    u64 segmentCount = 0;
};

[[nodiscard]] bool ComputeVolumeMetadataRequirement(u64 fileCount, u64& outMetadataBytes);
bool BuildVolume(
    const Path& outputDirectory,
    const VolumeBuildConfig& config,
    const VolumeBuildFileMap& files,
    VolumeBuildInfo& outBuildInfo
);

bool RemoveStagedDirectoryIfPresent(const Path& directoryPath, AStringView operationName, AStringView label);
void CleanupStagedDirectoryBestEffort(const Path& directoryPath, AStringView operationName, AStringView label);
bool EnsureEmptyStagedDirectory(const Path& directoryPath, AStringView operationName, AStringView label);

class StagedDirectoryCleanupGuard : NoCopy{
public:
    StagedDirectoryCleanupGuard(const Path& directoryPath, AStringView operationName, AStringView label = "stage directory");
    ~StagedDirectoryCleanupGuard();

    void dismiss();

private:
    Path m_directoryPath;
    ACompactString m_operationName;
    ACompactString m_label;
    bool m_active = true;
};

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

    using SegmentPathVector = Vector<Path, Alloc::GlobalArena>;

    using FileMap = HashMap<Name, FileRecord, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    explicit VolumeFileSystem(Alloc::GlobalArena& arena);
    ~VolumeFileSystem();


public:
    bool mount(const VolumeMountDesc& desc);
    void unmount();

    [[nodiscard]] bool mounted()const;
    [[nodiscard]] bool writable()const;

    [[nodiscard]] AStringView volumeName()const;
    [[nodiscard]] Path mountDirectory()const;

    [[nodiscard]] u64 segmentSize()const;
    [[nodiscard]] u64 metadataSize()const;
    [[nodiscard]] usize segmentCount()const;
    [[nodiscard]] u64 fileCount()const;
    [[nodiscard]] u64 usedBytes()const;
    [[nodiscard]] u64 wastedBytes()const;


public:
    bool writeFile(const Name& virtualPath, const void* data, usize bytes);
    bool writeFileDeferred(const Name& virtualPath, const void* data, usize bytes);

    template<typename ByteContainer>
    bool writeFile(const Name& virtualPath, const ByteContainer& data){
        return writeFile(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool writeFileDeferred(const Name& virtualPath, const ByteContainer& data){
        return writeFileDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    bool flushMetadata();
    void reserveFileCapacity(usize fileCount);

    template<typename ByteContainer>
    bool readFile(const Name& virtualPath, ByteContainer& outData)const;
    bool removeFile(const Name& virtualPath);

    bool fileExists(const Name& virtualPath)const;
    bool fileSize(const Name& virtualPath, u64& outSize)const;

    Vector<Name, VolumeArena> listFiles()const;
    bool compact(bool shrinkSegments = true);


private:
    struct MetadataFlushMode{
        enum Enum : u8{
            Deferred = 0u,
            Immediate = 1u,
        };
    };

    bool writeFileLocked(const Name& virtualPath, const void* data, usize bytes, MetadataFlushMode::Enum flushMode);
    bool scanSegmentsLocked();

    bool createSegmentLocked(usize segmentIndex);
    bool ensureCapacityLocked(u64 requiredBytes);

    bool loadMetadataLocked();
    bool flushMetadataLocked();
    bool canFitMetadataForFileCountLocked(u64 fileCount)const;
    bool readFileRecordLocked(const Name& virtualPath, FileRecord& outRecord)const;
    bool computePhysicalCapacityLocked(u64& outCapacityBytes)const;

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

    ACompactString m_volumeName;
    Path m_mountDirectory;

    u64 m_segmentSize = 0;
    u64 m_metadataBytes = 0;
    u64 m_nextFreeOffset = 0;
    usize m_maxSegments = 0;

    Alloc::GlobalArena& m_arena;
    SegmentPathVector m_segmentPaths;
    FileMap m_files;
};


class VolumeSession final : NoCopy{
public:
    explicit VolumeSession(Alloc::GlobalArena& arena);


public:
    bool create(const Path& outputDirectory, const VolumeBuildConfig& config);
    bool load(AStringView volumeName, const Path& mountDirectory);
    void reserveFileCapacity(usize fileCount);

    bool pushData(const Name& virtualPath, const void* data, usize bytes);
    bool pushData(AStringView virtualPath, const void* data, usize bytes);
    bool pushDataDeferred(const Name& virtualPath, const void* data, usize bytes);
    bool pushDataDeferred(AStringView virtualPath, const void* data, usize bytes);

    template<typename ByteContainer>
    bool pushData(const Name& virtualPath, const ByteContainer& data){
        return pushData(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool pushData(const AStringView virtualPath, const ByteContainer& data){
        return pushData(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool pushDataDeferred(const Name& virtualPath, const ByteContainer& data){
        return pushDataDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool pushDataDeferred(const AStringView virtualPath, const ByteContainer& data){
        return pushDataDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    bool flush();
    template<typename ByteContainer>
    bool loadData(AStringView virtualPath, ByteContainer& outData)const;
    template<typename ByteContainer>
    bool loadData(const Name& virtualPath, ByteContainer& outData)const;

    [[nodiscard]] bool mounted()const{ return m_volumeFileSystem.mounted(); }
    [[nodiscard]] bool writable()const{ return m_volumeFileSystem.writable(); }
    [[nodiscard]] u64 fileCount()const{ return m_volumeFileSystem.fileCount(); }
    [[nodiscard]] usize segmentCount()const{ return m_volumeFileSystem.segmentCount(); }


private:
    VolumeFileSystem m_volumeFileSystem;
};


template<typename ByteContainer>
bool VolumeFileSystem::readFile(const Name& virtualPath, ByteContainer& outData)const{
    ScopedLock lock(m_mutex);
    outData.clear();

    FileRecord record;
    if(!readFileRecordLocked(virtualPath, record))
        return false;

    if(record.size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): readFile failed: file size {} exceeds runtime buffer limit {}")
            , StringConvert(m_volumeName.view())
            , record.size
            , static_cast<u64>(Limit<usize>::s_Max)
        );
        return false;
    }

    outData.resize(static_cast<usize>(record.size));
    if(record.size == 0)
        return true;

    if(readBytesLocked(record.offset, outData.data(), record.size))
        return true;

    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): readFile failed: payload read failed"), StringConvert(m_volumeName.view()));
    return false;
}

template<typename ByteContainer>
bool VolumeSession::loadData(const AStringView virtualPath, ByteContainer& outData)const{
    outData.clear();

    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: volume is not mounted"));
        return false;
    }
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: virtual path is empty"));
        return false;
    }

    const Name virtualPathName(virtualPath);
    if(!virtualPathName){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: virtual path is invalid"));
        return false;
    }

    if(m_volumeFileSystem.readFile(virtualPathName, outData))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed to read '{}'"), StringConvert(virtualPath));
    return false;
}

template<typename ByteContainer>
bool VolumeSession::loadData(const Name& virtualPath, ByteContainer& outData)const{
    outData.clear();

    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: volume is not mounted"));
        return false;
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: virtual path is empty"));
        return false;
    }

    if(m_volumeFileSystem.readFile(virtualPath, outData))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed to read '{}'"), StringConvert(virtualPath.c_str()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

