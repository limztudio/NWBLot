// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_build.h"
#include "volume_file_system.h"
#include "volume_storage_detail.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <global/binary.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ComputeVolumeIndexBytes(const u64 fileCount, u64& outIndexBytes){
    outIndexBytes = 0;
    if(fileCount > Limit<u64>::s_Max / static_cast<u64>(sizeof(VolumeIndexEntryDisk)))
        return false;

    outIndexBytes = fileCount * static_cast<u64>(sizeof(VolumeIndexEntryDisk));
    return true;
}

static bool ComputeVolumeMetadataRequirement(const u64 fileCount, u64& outMetadataBytes){
    outMetadataBytes = 0;

    u64 indexBytes = 0;
    if(!ComputeVolumeIndexBytes(fileCount, indexBytes))
        return false;

    return AddNoOverflow(static_cast<u64>(sizeof(VolumeHeaderDisk)), indexBytes, outMetadataBytes);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ComputeVolumeMetadataRequirement(const u64 fileCount, u64& outMetadataBytes){
    return __hidden_filesystem::ComputeVolumeMetadataRequirement(fileCount, outMetadataBytes);
}

bool VolumeFileSystem::loadMetadataLocked(){
    __hidden_filesystem::VolumeHeaderDisk header{};
    if(!readBytesLocked(0, &header, sizeof(header))){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "failed to read metadata header");
        return false;
    }

    if(NWB_MEMCMP(header.magic, __hidden_filesystem::s_VolumeMagic, sizeof(header.magic)) != 0){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "magic mismatch");
        return false;
    }
    if(header.segmentSize != m_segmentSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): loadMetadata failed: segment size {} does not match mounted size {}")
            , StringConvert(m_volumeName)
            , header.segmentSize
            , m_segmentSize
        );
        return false;
    }
    if(header.metadataBytes <= sizeof(header) || header.metadataBytes >= m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata byte range is invalid");
        return false;
    }

    u64 headerTotalBytes = 0;
    if(!__hidden_filesystem::AddNoOverflow(static_cast<u64>(sizeof(header)), header.indexBytes, headerTotalBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata byte overflow");
        return false;
    }
    if(headerTotalBytes > header.metadataBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata table exceeds reserved metadata area");
        return false;
    }

    if(header.nextFreeOffset < header.metadataBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "next free offset points inside metadata area");
        return false;
    }

    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "segment capacity overflow");
        return false;
    }
    const u64 maxVolumeBytes = static_cast<u64>(m_segmentPaths.size()) * m_segmentSize;
    if(header.nextFreeOffset > maxVolumeBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "next free offset exceeds volume capacity");
        return false;
    }
    u64 physicalCapacityBytes = 0;
    if(!computePhysicalCapacityLocked(physicalCapacityBytes))
        return false;
    if(header.nextFreeOffset > physicalCapacityBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "next free offset exceeds physical volume bytes");
        return false;
    }
    if(header.indexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index byte count exceeds runtime addressable range");
        return false;
    }
    u64 expectedIndexBytes = 0;
    if(!__hidden_filesystem::ComputeVolumeIndexBytes(header.fileCount, expectedIndexBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "file count overflows index entry byte computation");
        return false;
    }

    if(header.indexBytes != expectedIndexBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): loadMetadata failed: index byte count {} does not match expected {}")
            , StringConvert(m_volumeName)
            , header.indexBytes
            , expectedIndexBytes
        );
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_LoadMetadataScratch);
    Vector<u8, Core::Alloc::ScratchArena> indexData(
        static_cast<usize>(header.indexBytes),
        0,
        scratchArena
    );
    if(header.indexBytes > 0 && !readBytesLocked(static_cast<u64>(sizeof(header)), indexData.data(), header.indexBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "failed to read metadata index");
        return false;
    }

    FileMap loadedFiles(0, Hasher<Name>(), EqualTo<Name>(), m_arena);
    loadedFiles.reserve(static_cast<usize>(header.fileCount));
    u64 cursor = 0;
    for(u64 i = 0; i < header.fileCount; ++i){
        if(header.indexBytes - cursor < sizeof(__hidden_filesystem::VolumeIndexEntryDisk)){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "truncated metadata index entry");
            return false;
        }

        __hidden_filesystem::VolumeIndexEntryDisk entry{};
        NWB_MEMCPY(&entry, sizeof(entry), indexData.data() + static_cast<usize>(cursor), sizeof(entry));
        cursor += sizeof(entry);

        u64 endOffset = 0;
        if(!__hidden_filesystem::AddNoOverflow(entry.offset, entry.size, endOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index entry offset overflow");
            return false;
        }
        if(entry.offset < header.metadataBytes){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index entry points inside metadata area");
            return false;
        }
        if(endOffset > header.nextFreeOffset){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index entry end exceeds next free offset");
            return false;
        }

        const Name pathName(entry.hash);
        if(!loadedFiles.emplace(pathName, FileRecord{ entry.offset, entry.size }).second){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "duplicate path hash in metadata index");
            return false;
        }
    }

    if(cursor != header.indexBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata cursor mismatch after index parse");
        return false;
    }

    m_metadataBytes = header.metadataBytes;
    m_nextFreeOffset = header.nextFreeOffset;
    m_files = Move(loadedFiles);

    return true;
}

bool VolumeFileSystem::flushMetadataLocked(){
    if(m_segmentPaths.empty()){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "no segments are mounted");
        return false;
    }
    if(m_nextFreeOffset < m_metadataBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "next free offset points inside metadata area");
        return false;
    }
    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "segment capacity overflow");
        return false;
    }
    if(m_nextFreeOffset > static_cast<u64>(m_segmentPaths.size()) * m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "next free offset exceeds volume capacity");
        return false;
    }

    __hidden_filesystem::VolumeHeaderDisk header{};
    NWB_MEMCPY(header.magic, sizeof(header.magic), __hidden_filesystem::s_VolumeMagic, sizeof(__hidden_filesystem::s_VolumeMagic));
    header.segmentSize = m_segmentSize;
    header.metadataBytes = m_metadataBytes;
    header.fileCount = static_cast<u64>(m_files.size());
    header.nextFreeOffset = m_nextFreeOffset;

    struct MetadataIndexRecord{
        Name path;
        FileRecord file;
    };

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
    Vector<MetadataIndexRecord, Core::Alloc::ScratchArena> sortedRecords{ scratchArena };
    sortedRecords.reserve(m_files.size());
    for(const auto& [path, record] : m_files)
        sortedRecords.push_back(MetadataIndexRecord{ path, record });
    Sort(
        sortedRecords.begin(),
        sortedRecords.end(),
        [](const MetadataIndexRecord& lhs, const MetadataIndexRecord& rhs){
            return ::LessNameHash(lhs.path.hash(), rhs.path.hash());
        }
    );

    Vector<u8, Core::Alloc::ScratchArena> indexBytes{scratchArena};
    u64 expectedIndexBytes = 0;
    if(!__hidden_filesystem::ComputeVolumeIndexBytes(header.fileCount, expectedIndexBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "file count overflows index size");
        return false;
    }
    if(expectedIndexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata index exceeds runtime addressable range");
        return false;
    }
    indexBytes.reserve(static_cast<usize>(expectedIndexBytes));

    for(const MetadataIndexRecord& recordInfo : sortedRecords){
        const FileRecord& record = recordInfo.file;
        __hidden_filesystem::VolumeIndexEntryDisk entry{};
        entry.hash = recordInfo.path.hash();
        entry.offset = record.offset;
        entry.size = record.size;

        AppendPOD(indexBytes, entry);
    }

    header.indexBytes = static_cast<u64>(indexBytes.size());
    if(header.indexBytes != expectedIndexBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "serialized index size mismatch");
        return false;
    }

    u64 totalMetaBytes = 0;
    if(!__hidden_filesystem::AddNoOverflow(static_cast<u64>(sizeof(header)), header.indexBytes, totalMetaBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata byte overflow");
        return false;
    }
    if(totalMetaBytes > m_metadataBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): flushMetadata failed: metadata requires {} bytes, reserved {} bytes")
            , StringConvert(m_volumeName)
            , totalMetaBytes
            , m_metadataBytes
        );
        return false;
    }

    Vector<u8, Core::Alloc::ScratchArena> metadataBuffer{scratchArena};
    metadataBuffer.reserve(static_cast<usize>(m_metadataBytes));
    AppendPOD(metadataBuffer, header);
    ::BinaryDetail::AppendBytesNoReserveUnchecked(metadataBuffer, indexBytes.data(), indexBytes.size());
    metadataBuffer.resize(static_cast<usize>(m_metadataBytes), 0u);

    if(writeBytesLocked(0, metadataBuffer.data(), metadataBuffer.size()))
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata write failed");
    return false;
}

bool VolumeFileSystem::canFitMetadataForFileCountLocked(const u64 fileCount)const{
    u64 metadataBytes = 0;
    if(!__hidden_filesystem::ComputeVolumeMetadataRequirement(fileCount, metadataBytes))
        return false;

    return metadataBytes <= m_metadataBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

