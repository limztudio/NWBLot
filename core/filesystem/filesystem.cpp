// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "filesystem.h"


#include <logger/client/logger.h>

#include <cerrno>
#include <cstring>
#include <fstream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_VolumeDefaultMetadataBytes = 512ull * 1024ull;
static constexpr u64 s_VolumeMinMetadataBytes = 4ull * 1024ull;
static constexpr u32 s_VolumeFormatVersion = 2u;
static constexpr u64 s_VolumeMoveChunkBytes = 1024ull * 1024ull;

static constexpr char s_VolumeMagic[8] = { 'N', 'W', 'B', 'V', 'O', 'L', '1', '\0' };

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


template<typename T>
static bool CanRepresentU64(const u64 value){
    static_assert(IsArithmetic_V<T>, "CanRepresentU64 requires arithmetic target type");

    constexpr bool signedType = static_cast<T>(-1) < static_cast<T>(0);
    if constexpr(signedType){
        constexpr usize bitCount = sizeof(T) * 8;
        constexpr u64 maxValue = bitCount >= 64
            ? (Limit<u64>::s_Max >> 1)
            : ((u64(1) << (bitCount - 1)) - 1);
        return value <= maxValue;
    }
    else{
        constexpr usize bitCount = sizeof(T) * 8;
        constexpr u64 maxValue = bitCount >= 64
            ? Limit<u64>::s_Max
            : ((u64(1) << bitCount) - 1);
        return value <= maxValue;
    }
}

static bool IsAsciiAlphaNumeric(const char ch){
    return (ch >= '0' && ch <= '9')
        || (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z');
}

static bool IsAsciiDigit(const char ch){
    return ch >= '0' && ch <= '9';
}

static bool ValidVolumeName(AStringView name){
    if(name.empty())
        return false;

    for(const char ch : name){
        const bool alphaNum = IsAsciiAlphaNumeric(ch);
        if(alphaNum || ch == '_' || ch == '-')
            continue;
        return false;
    }

    return true;
}

static bool AddNoOverflow(const u64 lhs, const u64 rhs, u64& out){
    if(lhs > Limit<u64>::s_Max - rhs)
        return false;
    out = lhs + rhs;
    return true;
}

static u64 DefaultMetadataBytes(const u64 segmentSize){
    u64 output = s_VolumeDefaultMetadataBytes;
    if(output >= segmentSize)
        output = segmentSize / 8;
    if(output < s_VolumeMinMetadataBytes)
        output = s_VolumeMinMetadataBytes;
    return output;
}

static bool ToStreamOff(const u64 value, std::streamoff& out){
    if(!CanRepresentU64<std::streamoff>(value))
        return false;
    out = static_cast<std::streamoff>(value);
    return true;
}

static bool ToStreamSize(const u64 value, std::streamsize& out){
    if(!CanRepresentU64<std::streamsize>(value))
        return false;
    out = static_cast<std::streamsize>(value);
    return true;
}

static AString MakeSegmentFileName(AStringView volumeName, const usize index){
    return StringFormat("{}_{}.vol", volumeName, index);
}

template<typename T>
static void AppendPOD(Vector<u8>& output, const T& value){
    const auto* begin = reinterpret_cast<const u8*>(&value);
    output.insert(output.end(), begin, begin + sizeof(T));
}

static bool LessNameHash(const NameHash& lhs, const NameHash& rhs){
    for(u32 i = 0; i < 8; ++i){
        if(lhs.qwords[i] < rhs.qwords[i])
            return true;
        if(lhs.qwords[i] > rhs.qwords[i])
            return false;
    }
    return false;
}

static bool LessName(const Name& lhs, const Name& rhs){
    return LessNameHash(lhs.hash(), rhs.hash());
}

static AString LastErrnoMessage(){
    const int errorNumber = errno;
    if(errorNumber == 0)
        return AString("none");

    char errorText[256] = {};
    if(NWB_STRERROR(errorText, sizeof(errorText), errorNumber) != 0)
        return StringFormat("unknown ({})", errorNumber);

    return StringFormat("{} ({})", errorText, errorNumber);
}

static void LogFailure(AStringView volumeName, AStringView operation, AStringView detail){
    NWB_LOGGER_WARNING(
        NWB_TEXT("Filesystem('{}'): {} failed: {}"),
        StringConvert(volumeName),
        StringConvert(operation),
        StringConvert(detail)
    );
}

static void LogFailureWithPath(AStringView volumeName, AStringView operation, const Path& path, AStringView detail){
    NWB_LOGGER_WARNING(
        NWB_TEXT("Filesystem('{}'): {} failed on '{}': {}"),
        StringConvert(volumeName),
        StringConvert(operation),
        StringConvert(path.string()),
        StringConvert(detail)
    );
}

static void LogFailureWithFsError(AStringView volumeName, AStringView operation, const Path& path, const std::error_code& errorCode){
    NWB_LOGGER_WARNING(
        NWB_TEXT("Filesystem('{}'): {} failed on '{}': [{}] {}"),
        StringConvert(volumeName),
        StringConvert(operation),
        StringConvert(path.string()),
        errorCode.value(),
        StringConvert(errorCode.message())
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VolumeFileSystem::VolumeFileSystem()
{}

VolumeFileSystem::~VolumeFileSystem(){
    unmount();
}


bool VolumeFileSystem::mount(const VolumeMountDesc& desc){
    ScopedLock lock(m_mutex);
    unmountLocked();

    if(!__hidden_filesystem::ValidVolumeName(desc.volumeName)){
        __hidden_filesystem::LogFailure(desc.volumeName, "mount", "invalid volume name");
        return false;
    }

    m_volumeName = desc.volumeName;
    m_mountDirectory = desc.mountDirectory.empty() ? Path(".") : desc.mountDirectory;
    m_usage = desc.usage;
    m_writable = (desc.usage != VolumeUsage::RuntimeReadOnly);
    m_maxSegments = desc.maxSegments;

    std::error_code errorCode;
    if(!std::filesystem::exists(m_mountDirectory, errorCode)){
        if(errorCode){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:exists", m_mountDirectory, errorCode);
            return false;
        }

        if(!desc.createIfMissing || !m_writable){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "mount directory does not exist and creation is disabled");
            return false;
        }

        if(!std::filesystem::create_directories(m_mountDirectory, errorCode)){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:create_directories", m_mountDirectory, errorCode);
            return false;
        }
    }
    else if(!std::filesystem::is_directory(m_mountDirectory, errorCode)){
        if(errorCode)
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:is_directory", m_mountDirectory, errorCode);
        else
            __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:is_directory", m_mountDirectory, "path is not a directory");
        return false;
    }

    if(!scanSegmentsLocked()){
        __hidden_filesystem::LogFailure(m_volumeName, "mount", "segment scan failed");
        unmountLocked();
        return false;
    }

    if(m_segmentPaths.empty()){
        if(!desc.createIfMissing || !m_writable || desc.segmentSize == 0){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "volume does not exist and creation parameters are invalid");
            unmountLocked();
            return false;
        }

        m_segmentSize = desc.segmentSize;
        m_metadataBytes = desc.metadataSize == 0
            ? __hidden_filesystem::DefaultMetadataBytes(m_segmentSize)
            : desc.metadataSize;

        if(m_metadataBytes <= sizeof(__hidden_filesystem::VolumeHeaderDisk) || m_metadataBytes >= m_segmentSize){
            __hidden_filesystem::LogFailure(
                m_volumeName,
                "mount",
                "metadata size is outside valid segment bounds"
            );
            unmountLocked();
            return false;
        }

        m_nextFreeOffset = m_metadataBytes;

        if(!createSegmentLocked(0)){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "failed to create first segment");
            unmountLocked();
            return false;
        }
        if(!flushMetadataLocked()){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "failed to write initial metadata");
            unmountLocked();
            return false;
        }
    }
    else{
        std::error_code sizeError;
        const u64 discoveredSegmentSize = std::filesystem::file_size(m_segmentPaths[0], sizeError);
        if(sizeError || discoveredSegmentSize == 0){
            if(sizeError)
                __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:file_size", m_segmentPaths[0], sizeError);
            else
                __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:file_size", m_segmentPaths[0], "segment size is zero");
            unmountLocked();
            return false;
        }
        m_segmentSize = discoveredSegmentSize;

        for(const Path& segmentPath : m_segmentPaths){
            const u64 segmentFileSize = std::filesystem::file_size(segmentPath, sizeError);
            if(sizeError || segmentFileSize != m_segmentSize){
                if(sizeError){
                    __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:file_size", segmentPath, sizeError);
                }
                else{
                    NWB_LOGGER_WARNING(
                        NWB_TEXT("Filesystem('{}'): mount failed: segment '{}' has size {}, expected {}"),
                        StringConvert(m_volumeName),
                        StringConvert(segmentPath.string()),
                        segmentFileSize,
                        m_segmentSize
                    );
                }
                unmountLocked();
                return false;
            }
        }

        if(desc.segmentSize != 0 && desc.segmentSize != m_segmentSize){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Filesystem('{}'): mount failed: requested segment size {} does not match volume size {}"),
                StringConvert(m_volumeName),
                desc.segmentSize,
                m_segmentSize
            );
            unmountLocked();
            return false;
        }

        if(!loadMetadataLocked()){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "metadata load failed");
            unmountLocked();
            return false;
        }

        if(desc.metadataSize != 0 && desc.metadataSize != m_metadataBytes){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Filesystem('{}'): mount failed: requested metadata size {} does not match volume metadata size {}"),
                StringConvert(m_volumeName),
                desc.metadataSize,
                m_metadataBytes
            );
            unmountLocked();
            return false;
        }
    }

    if(m_maxSegments != 0 && m_segmentPaths.size() > m_maxSegments){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): mount failed: discovered {} segments, maxSegments is {}"),
            StringConvert(m_volumeName),
            m_segmentPaths.size(),
            m_maxSegments
        );
        unmountLocked();
        return false;
    }

    m_mounted = true;
    return true;
}

void VolumeFileSystem::unmount(){
    ScopedLock lock(m_mutex);
    unmountLocked();
}

bool VolumeFileSystem::mounted()const{
    ScopedLock lock(m_mutex);
    return m_mounted;
}

bool VolumeFileSystem::writable()const{
    ScopedLock lock(m_mutex);
    return m_mounted && m_writable;
}

AString VolumeFileSystem::volumeName()const{
    ScopedLock lock(m_mutex);
    return m_volumeName;
}

Path VolumeFileSystem::mountDirectory()const{
    ScopedLock lock(m_mutex);
    return m_mountDirectory;
}

u64 VolumeFileSystem::segmentSize()const{
    ScopedLock lock(m_mutex);
    return m_segmentSize;
}

u64 VolumeFileSystem::metadataSize()const{
    ScopedLock lock(m_mutex);
    return m_metadataBytes;
}

usize VolumeFileSystem::segmentCount()const{
    ScopedLock lock(m_mutex);
    return m_segmentPaths.size();
}


u64 VolumeFileSystem::fileCount()const{
    ScopedLock lock(m_mutex);
    return static_cast<u64>(m_files.size());
}

u64 VolumeFileSystem::usedBytes()const{
    ScopedLock lock(m_mutex);

    u64 totalUsedBytes = 0;
    for(const auto& current : m_files){
        if(!__hidden_filesystem::AddNoOverflow(totalUsedBytes, current.second.size, totalUsedBytes))
            return Limit<u64>::s_Max;
    }

    return totalUsedBytes;
}

u64 VolumeFileSystem::wastedBytes()const{
    ScopedLock lock(m_mutex);
    if(m_nextFreeOffset <= m_metadataBytes)
        return 0;

    u64 totalUsedBytes = 0;
    for(const auto& current : m_files){
        if(!__hidden_filesystem::AddNoOverflow(totalUsedBytes, current.second.size, totalUsedBytes))
            return 0;
    }

    const u64 payloadSpan = m_nextFreeOffset - m_metadataBytes;
    if(totalUsedBytes >= payloadSpan)
        return 0;

    return payloadSpan - totalUsedBytes;
}


bool VolumeFileSystem::writeFile(const Name& virtualPath, const void* data, const usize bytes){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "virtual path is invalid");
        return false;
    }
    if(bytes != 0 && data == nullptr){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "data pointer is null while byte count is non-zero");
        return false;
    }

    const auto itrFind = m_files.find(virtualPath);
    const bool existed = itrFind != m_files.end();

    u64 fileCountAfterWrite = static_cast<u64>(m_files.size());
    if(!existed){
        if(fileCountAfterWrite == Limit<u64>::s_Max){
            __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "file count overflow");
            return false;
        }
        ++fileCountAfterWrite;
    }
    if(!canFitMetadataForFileCountLocked(fileCountAfterWrite)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): writeFile failed: metadata area is full for file count {}"),
            StringConvert(m_volumeName),
            fileCountAfterWrite
        );
        return false;
    }

    const u64 byteCount = static_cast<u64>(bytes);
    u64 newFileEnd = 0;
    if(!__hidden_filesystem::AddNoOverflow(m_nextFreeOffset, byteCount, newFileEnd)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "offset overflow while reserving payload bytes");
        return false;
    }
    if(!ensureCapacityLocked(newFileEnd))
        return false;

    const u64 writeOffset = m_nextFreeOffset;
    if(byteCount > 0 && !writeBytesLocked(writeOffset, data, byteCount))
        return false;

    FileRecord previousRecord;
    if(existed)
        previousRecord = itrFind->second;
    const u64 previousNextFreeOffset = m_nextFreeOffset;

    m_files[virtualPath] = FileRecord{ writeOffset, byteCount };
    m_nextFreeOffset = newFileEnd;

    if(flushMetadataLocked())
        return true;

    if(existed)
        m_files[virtualPath] = previousRecord;
    else
        m_files.erase(virtualPath);
    m_nextFreeOffset = previousNextFreeOffset;
    __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "failed to flush metadata after payload write");
    return false;
}

bool VolumeFileSystem::writeFile(const Name& virtualPath, const Vector<u8>& data){
    return writeFile(virtualPath, data.empty() ? nullptr : data.data(), data.size());
}

bool VolumeFileSystem::readFile(const Name& virtualPath, Vector<u8>& outData)const{
    ScopedLock lock(m_mutex);
    if(!m_mounted){
        __hidden_filesystem::LogFailure(m_volumeName, "readFile", "filesystem is not mounted");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "readFile", "virtual path is invalid");
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName, "readFile", "file was not found");
        return false;
    }

    const FileRecord& record = itr->second;
    if(record.size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): readFile failed: file size {} exceeds runtime buffer limit {}"),
            StringConvert(m_volumeName),
            record.size,
            static_cast<u64>(Limit<usize>::s_Max)
        );
        return false;
    }

    outData.resize(static_cast<usize>(record.size));
    if(record.size == 0)
        return true;

    if(readBytesLocked(record.offset, outData.data(), record.size))
        return true;

    outData.clear();
    __hidden_filesystem::LogFailure(m_volumeName, "readFile", "payload read failed");
    return false;
}

bool VolumeFileSystem::removeFile(const Name& virtualPath){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "virtual path is invalid");
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "file was not found");
        return false;
    }

    const FileRecord removedRecord = itr->second;
    m_files.erase(itr);
    if(flushMetadataLocked())
        return true;

    m_files[virtualPath] = removedRecord;
    __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "failed to flush metadata after erase");
    return false;
}

bool VolumeFileSystem::fileExists(const Name& virtualPath)const{
    ScopedLock lock(m_mutex);
    if(!m_mounted)
        return false;
    if(!virtualPath)
        return false;
    return m_files.find(virtualPath) != m_files.end();
}

bool VolumeFileSystem::fileSize(const Name& virtualPath, u64& outSize)const{
    ScopedLock lock(m_mutex);
    if(!m_mounted){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "filesystem is not mounted");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "virtual path is invalid");
        return false;
    }

    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "file was not found");
        return false;
    }

    outSize = itr->second.size;
    return true;
}

Vector<Name> VolumeFileSystem::listFiles()const{
    ScopedLock lock(m_mutex);

    Vector<Name> output;
    output.reserve(m_files.size());
    for(const auto& current : m_files)
        output.push_back(current.first);

    Sort(output.begin(), output.end(), __hidden_filesystem::LessName);
    return output;
}


bool VolumeFileSystem::compact(const bool shrinkSegments){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "compact", "filesystem is not mounted in writable mode");
        return false;
    }

    struct FileLayout{
        Name path;
        u64 sourceOffset = 0;
        u64 destinationOffset = 0;
        u64 size = 0;
    };

    Vector<FileLayout> layouts;
    layouts.reserve(m_files.size());

    for(const auto& current : m_files){
        const FileRecord& record = current.second;

        u64 endOffset = 0;
        if(!__hidden_filesystem::AddNoOverflow(record.offset, record.size, endOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "offset overflow detected in file layout");
            return false;
        }
        if(record.offset < m_metadataBytes){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout overlaps metadata region");
            return false;
        }
        if(endOffset > m_nextFreeOffset){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout exceeds next-free boundary");
            return false;
        }

        layouts.push_back(FileLayout{ current.first, record.offset, 0, record.size });
    }

    Sort(
        layouts.begin(),
        layouts.end(),
        [](const FileLayout& lhs, const FileLayout& rhs){
            return lhs.sourceOffset < rhs.sourceOffset;
        }
    );

    u64 compactedWriteOffset = m_metadataBytes;
    u64 previousSourceEnd = m_metadataBytes;
    for(auto& layout : layouts){
        if(layout.sourceOffset < previousSourceEnd){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout overlap detected");
            return false;
        }

        layout.destinationOffset = compactedWriteOffset;

        if(!__hidden_filesystem::AddNoOverflow(layout.sourceOffset, layout.size, previousSourceEnd)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "source offset overflow while building compaction plan");
            return false;
        }
        if(!__hidden_filesystem::AddNoOverflow(compactedWriteOffset, layout.size, compactedWriteOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "destination offset overflow while building compaction plan");
            return false;
        }
    }

    for(const auto& layout : layouts){
        if(layout.size == 0 || layout.destinationOffset == layout.sourceOffset)
            continue;

        if(!moveBytesLocked(layout.destinationOffset, layout.sourceOffset, layout.size))
            return false;
    }

    const FileMap previousFiles = m_files;
    const u64 previousNextFreeOffset = m_nextFreeOffset;

    for(const auto& layout : layouts)
        m_files[layout.path] = FileRecord{ layout.destinationOffset, layout.size };
    m_nextFreeOffset = compactedWriteOffset;

    if(!flushMetadataLocked()){
        m_files = previousFiles;
        m_nextFreeOffset = previousNextFreeOffset;
        __hidden_filesystem::LogFailure(m_volumeName, "compact", "failed to flush metadata after move");
        return false;
    }

    if(!shrinkSegments)
        return true;

    if(trimSegmentsForNextFreeOffsetLocked())
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "compact", "failed to trim trailing segments");
    return false;
}


bool VolumeFileSystem::scanSegmentsLocked(){
    m_segmentPaths.clear();

    Vector<Pair<usize, Path>> sortedSegments;
    HashSet<usize> seenIndices;
    const AString prefix = m_volumeName + "_";
    constexpr AStringView suffix = ".vol";

    std::error_code errorCode;
    std::filesystem::directory_iterator directoryIt(m_mountDirectory, errorCode);
    std::filesystem::directory_iterator end;

    if(errorCode){
        __hidden_filesystem::LogFailureWithFsError(m_volumeName, "scanSegments:open_directory", m_mountDirectory, errorCode);
        return false;
    }

    for(; directoryIt != end; directoryIt.increment(errorCode)){
        if(errorCode){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "scanSegments:iterate", m_mountDirectory, errorCode);
            return false;
        }
        const bool regularFile = directoryIt->is_regular_file(errorCode);
        if(errorCode){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "scanSegments:is_regular_file", directoryIt->path(), errorCode);
            return false;
        }
        if(!regularFile)
            continue;

        const AString fileName = directoryIt->path().filename().string();
        if(fileName.size() <= prefix.size() + suffix.size())
            continue;
        if(fileName.compare(0, prefix.size(), prefix) != 0)
            continue;
        if(fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;

        const AString indexString = fileName.substr(prefix.size(), fileName.size() - prefix.size() - suffix.size());
        if(indexString.empty())
            continue;

        bool numeric = true;
        for(const char ch : indexString){
            if(__hidden_filesystem::IsAsciiDigit(ch))
                continue;
            numeric = false;
            break;
        }
        if(!numeric)
            continue;

        usize index = 0;
        try{
            const unsigned long long parsed = std::stoull(indexString);
            if(parsed > static_cast<unsigned long long>(Limit<usize>::s_Max))
                continue;
            index = static_cast<usize>(parsed);
        }
        catch(...){
            continue;
        }

        if(seenIndices.find(index) != seenIndices.end())
        {
            NWB_LOGGER_WARNING(
                NWB_TEXT("Filesystem('{}'): scanSegments failed: duplicate segment index {}"),
                StringConvert(m_volumeName),
                index
            );
            return false;
        }
        seenIndices.insert(index);

        sortedSegments.push_back(MakePair(index, directoryIt->path()));
    }

    if(sortedSegments.empty())
        return true;

    Sort(
        sortedSegments.begin(),
        sortedSegments.end(),
        [](const Pair<usize, Path>& lhs, const Pair<usize, Path>& rhs){
            return lhs.first() < rhs.first();
        }
    );

    if(sortedSegments.front().first() != 0){
        __hidden_filesystem::LogFailure(m_volumeName, "scanSegments", "segment index 0 is missing");
        return false;
    }

    usize expectedIndex = 0;
    for(const auto& segment : sortedSegments){
        if(segment.first() != expectedIndex){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Filesystem('{}'): scanSegments failed: expected segment index {}, found {}"),
                StringConvert(m_volumeName),
                expectedIndex,
                segment.first()
            );
            return false;
        }
        m_segmentPaths.push_back(segment.second());
        ++expectedIndex;
    }

    return true;
}

bool VolumeFileSystem::createSegmentLocked(const usize segmentIndex){
    if(m_maxSegments != 0 && segmentIndex >= m_maxSegments){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} exceeds maxSegments {}"),
            StringConvert(m_volumeName),
            segmentIndex,
            m_maxSegments
        );
        return false;
    }
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "createSegment", "segment size is zero");
        return false;
    }

    const Path path = segmentPath(segmentIndex);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if(!stream.is_open()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:open",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    std::streamoff streamOffset = 0;
    if(!__hidden_filesystem::ToStreamOff(m_segmentSize - 1, streamOffset)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): createSegment failed: segment size {} cannot be represented as stream offset"),
            StringConvert(m_volumeName),
            m_segmentSize
        );
        return false;
    }

    stream.seekp(streamOffset);
    if(!stream.good()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:seek",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    char zero = 0;
    stream.write(&zero, 1);
    if(!stream.good()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:write",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    if(segmentIndex == m_segmentPaths.size())
        m_segmentPaths.push_back(path);
    else if(segmentIndex < m_segmentPaths.size())
        m_segmentPaths[segmentIndex] = path;
    else{
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} is non-contiguous (segment count {})"),
            StringConvert(m_volumeName),
            segmentIndex,
            m_segmentPaths.size()
        );
        return false;
    }

    return true;
}

bool VolumeFileSystem::ensureCapacityLocked(const u64 requiredBytes){
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "ensureCapacity", "segment size is zero");
        return false;
    }

    while(true){
        const u64 segmentCount = static_cast<u64>(m_segmentPaths.size());
        if(segmentCount > Limit<u64>::s_Max / m_segmentSize){
            __hidden_filesystem::LogFailure(m_volumeName, "ensureCapacity", "capacity overflow while computing current volume size");
            return false;
        }

        const u64 capacity = segmentCount * m_segmentSize;
        if(requiredBytes <= capacity)
            return true;

        if(!m_writable){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Filesystem('{}'): ensureCapacity failed: required {} bytes, current capacity {} bytes, filesystem is read-only"),
                StringConvert(m_volumeName),
                requiredBytes,
                capacity
            );
            return false;
        }
        if(!createSegmentLocked(m_segmentPaths.size()))
            return false;
    }
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
    if(header.version != __hidden_filesystem::s_VolumeFormatVersion){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): loadMetadata failed: format version {} is not supported (expected {})"),
            StringConvert(m_volumeName),
            header.version,
            __hidden_filesystem::s_VolumeFormatVersion
        );
        return false;
    }
    if(header.segmentSize != m_segmentSize){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): loadMetadata failed: segment size {} does not match mounted size {}"),
            StringConvert(m_volumeName),
            header.segmentSize,
            m_segmentSize
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
    if(header.indexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index byte count exceeds runtime addressable range");
        return false;
    }
    if(header.fileCount > Limit<u64>::s_Max / static_cast<u64>(sizeof(__hidden_filesystem::VolumeIndexEntryDisk))){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "file count overflows index entry byte computation");
        return false;
    }

    const u64 expectedIndexBytes = header.fileCount * static_cast<u64>(sizeof(__hidden_filesystem::VolumeIndexEntryDisk));
    if(header.indexBytes != expectedIndexBytes){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): loadMetadata failed: index byte count {} does not match expected {}"),
            StringConvert(m_volumeName),
            header.indexBytes,
            expectedIndexBytes
        );
        return false;
    }

    Vector<u8> indexData(static_cast<usize>(header.indexBytes), 0);
    if(header.indexBytes > 0 && !readBytesLocked(static_cast<u64>(sizeof(header)), indexData.data(), header.indexBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "failed to read metadata index");
        return false;
    }

    FileMap loadedFiles;
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
        if(loadedFiles.find(pathName) != loadedFiles.end()){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "duplicate path hash in metadata index");
            return false;
        }

        loadedFiles[pathName] = FileRecord{ entry.offset, entry.size };
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
    header.version = __hidden_filesystem::s_VolumeFormatVersion;
    header.reserved = 0;
    header.segmentSize = m_segmentSize;
    header.metadataBytes = m_metadataBytes;
    header.fileCount = static_cast<u64>(m_files.size());
    header.nextFreeOffset = m_nextFreeOffset;

    Vector<Name> sortedPaths;
    sortedPaths.reserve(m_files.size());
    for(const auto& current : m_files)
        sortedPaths.push_back(current.first);
    Sort(sortedPaths.begin(), sortedPaths.end(), __hidden_filesystem::LessName);

    Vector<u8> indexBytes;
    if(header.fileCount > Limit<u64>::s_Max / static_cast<u64>(sizeof(__hidden_filesystem::VolumeIndexEntryDisk))){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "file count overflows index size");
        return false;
    }
    const u64 expectedIndexBytes = header.fileCount * static_cast<u64>(sizeof(__hidden_filesystem::VolumeIndexEntryDisk));
    if(expectedIndexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata index exceeds runtime addressable range");
        return false;
    }
    indexBytes.reserve(static_cast<usize>(expectedIndexBytes));

    for(const Name& pathName : sortedPaths){
        const auto itr = m_files.find(pathName);
        if(itr == m_files.end()){
            __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "file map changed during metadata build");
            return false;
        }

        const FileRecord& record = itr->second;
        __hidden_filesystem::VolumeIndexEntryDisk entry{};
        entry.hash = pathName.hash();
        entry.offset = record.offset;
        entry.size = record.size;

        __hidden_filesystem::AppendPOD(indexBytes, entry);
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
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): flushMetadata failed: metadata requires {} bytes, reserved {} bytes"),
            StringConvert(m_volumeName),
            totalMetaBytes,
            m_metadataBytes
        );
        return false;
    }

    Vector<u8> metadataBuffer(static_cast<usize>(m_metadataBytes), 0);
    NWB_MEMCPY(metadataBuffer.data(), metadataBuffer.size(), &header, sizeof(header));
    if(!indexBytes.empty())
        NWB_MEMCPY(metadataBuffer.data() + sizeof(header), metadataBuffer.size() - sizeof(header), indexBytes.data(), indexBytes.size());

    if(writeBytesLocked(0, metadataBuffer.data(), metadataBuffer.size()))
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata write failed");
    return false;
}

bool VolumeFileSystem::canFitMetadataForFileCountLocked(const u64 fileCount)const{
    if(fileCount > Limit<u64>::s_Max / static_cast<u64>(sizeof(__hidden_filesystem::VolumeIndexEntryDisk)))
        return false;

    const u64 indexBytes = fileCount * static_cast<u64>(sizeof(__hidden_filesystem::VolumeIndexEntryDisk));

    u64 metadataBytes = 0;
    if(!__hidden_filesystem::AddNoOverflow(static_cast<u64>(sizeof(__hidden_filesystem::VolumeHeaderDisk)), indexBytes, metadataBytes))
        return false;

    return metadataBytes <= m_metadataBytes;
}

bool VolumeFileSystem::readBytesLocked(const u64 offset, void* data, const u64 byteCount)const{
    if(byteCount == 0)
        return true;
    if(data == nullptr || m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "readBytes", "invalid arguments");
        return false;
    }

    u64 endOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(offset, byteCount, endOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "readBytes", "offset overflow");
        return false;
    }
    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "readBytes", "capacity overflow");
        return false;
    }
    const u64 capacityBytes = static_cast<u64>(m_segmentPaths.size()) * m_segmentSize;
    if(endOffset > capacityBytes){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): readBytes failed: range [{}..{}) exceeds capacity {}"),
            StringConvert(m_volumeName),
            offset,
            endOffset,
            capacityBytes
        );
        return false;
    }

    u8* outputBytes = static_cast<u8*>(data);
    u64 currentOffset = offset;
    u64 remainingBytes = byteCount;

    while(remainingBytes > 0){
        const usize segmentIndex = static_cast<usize>(currentOffset / m_segmentSize);
        const u64 segmentOffset = currentOffset % m_segmentSize;
        const u64 chunkBytes = Min(remainingBytes, m_segmentSize - segmentOffset);

        std::ifstream stream(m_segmentPaths[segmentIndex], std::ios::binary);
        if(!stream.is_open()){
            __hidden_filesystem::LogFailureWithPath(
                m_volumeName,
                "readBytes:open",
                m_segmentPaths[segmentIndex],
                __hidden_filesystem::LastErrnoMessage()
            );
            return false;
        }

        std::streamoff streamOffset = 0;
        std::streamsize streamChunkSize = 0;
        if(!__hidden_filesystem::ToStreamOff(segmentOffset, streamOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "readBytes", "segment offset cannot be represented as stream offset");
            return false;
        }
        if(!__hidden_filesystem::ToStreamSize(chunkBytes, streamChunkSize)){
            __hidden_filesystem::LogFailure(m_volumeName, "readBytes", "chunk size cannot be represented as stream size");
            return false;
        }

        stream.seekg(streamOffset);
        if(!stream.good()){
            __hidden_filesystem::LogFailureWithPath(
                m_volumeName,
                "readBytes:seek",
                m_segmentPaths[segmentIndex],
                __hidden_filesystem::LastErrnoMessage()
            );
            return false;
        }

        stream.read(reinterpret_cast<char*>(outputBytes), streamChunkSize);
        if(stream.gcount() != streamChunkSize){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, errno {}"),
                StringConvert(m_volumeName),
                StringConvert(m_segmentPaths[segmentIndex].string()),
                static_cast<i64>(streamChunkSize),
                static_cast<i64>(stream.gcount()),
                StringConvert(__hidden_filesystem::LastErrnoMessage())
            );
            return false;
        }

        outputBytes += chunkBytes;
        currentOffset += chunkBytes;
        remainingBytes -= chunkBytes;
    }

    return true;
}

bool VolumeFileSystem::writeBytesLocked(const u64 offset, const void* data, const u64 byteCount){
    if(byteCount == 0)
        return true;
    if(data == nullptr || m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "invalid arguments");
        return false;
    }

    u64 endOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(offset, byteCount, endOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "offset overflow");
        return false;
    }
    if(!ensureCapacityLocked(endOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "insufficient capacity");
        return false;
    }

    const u8* inputBytes = static_cast<const u8*>(data);
    u64 currentOffset = offset;
    u64 remainingBytes = byteCount;

    while(remainingBytes > 0){
        const usize segmentIndex = static_cast<usize>(currentOffset / m_segmentSize);
        const u64 segmentOffset = currentOffset % m_segmentSize;
        const u64 chunkBytes = Min(remainingBytes, m_segmentSize - segmentOffset);

        std::fstream stream(m_segmentPaths[segmentIndex], std::ios::binary | std::ios::in | std::ios::out);
        if(!stream.is_open()){
            __hidden_filesystem::LogFailureWithPath(
                m_volumeName,
                "writeBytes:open",
                m_segmentPaths[segmentIndex],
                __hidden_filesystem::LastErrnoMessage()
            );
            return false;
        }

        std::streamoff streamOffset = 0;
        std::streamsize streamChunkSize = 0;
        if(!__hidden_filesystem::ToStreamOff(segmentOffset, streamOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "segment offset cannot be represented as stream offset");
            return false;
        }
        if(!__hidden_filesystem::ToStreamSize(chunkBytes, streamChunkSize)){
            __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "chunk size cannot be represented as stream size");
            return false;
        }

        stream.seekp(streamOffset);
        if(!stream.good()){
            __hidden_filesystem::LogFailureWithPath(
                m_volumeName,
                "writeBytes:seek",
                m_segmentPaths[segmentIndex],
                __hidden_filesystem::LastErrnoMessage()
            );
            return false;
        }

        stream.write(reinterpret_cast<const char*>(inputBytes), streamChunkSize);
        if(!stream.good()){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Filesystem('{}'): writeBytes failed on '{}': attempted {} bytes, errno {}"),
                StringConvert(m_volumeName),
                StringConvert(m_segmentPaths[segmentIndex].string()),
                static_cast<i64>(streamChunkSize),
                StringConvert(__hidden_filesystem::LastErrnoMessage())
            );
            return false;
        }

        inputBytes += chunkBytes;
        currentOffset += chunkBytes;
        remainingBytes -= chunkBytes;
    }

    return true;
}

bool VolumeFileSystem::moveBytesLocked(const u64 destinationOffset, const u64 sourceOffset, const u64 byteCount){
    if(byteCount == 0 || destinationOffset == sourceOffset)
        return true;
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "segment size is zero");
        return false;
    }

    u64 sourceEndOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(sourceOffset, byteCount, sourceEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "source range overflow");
        return false;
    }

    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "capacity overflow");
        return false;
    }
    const u64 capacityBytes = static_cast<u64>(m_segmentPaths.size()) * m_segmentSize;
    if(sourceEndOffset > capacityBytes){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Filesystem('{}'): moveBytes failed: source range [{}..{}) exceeds capacity {}"),
            StringConvert(m_volumeName),
            sourceOffset,
            sourceEndOffset,
            capacityBytes
        );
        return false;
    }

    u64 destinationEndOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(destinationOffset, byteCount, destinationEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "destination range overflow");
        return false;
    }
    if(!ensureCapacityLocked(destinationEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "failed to ensure destination capacity");
        return false;
    }

    const u64 moveChunkBytes = Min(__hidden_filesystem::s_VolumeMoveChunkBytes, m_segmentSize);
    if(moveChunkBytes == 0 || moveChunkBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "invalid move chunk size");
        return false;
    }

    Vector<u8> moveBuffer(static_cast<usize>(moveChunkBytes), 0);

    if(destinationOffset < sourceOffset){
        u64 movedBytes = 0;
        while(movedBytes < byteCount){
            const u64 pendingBytes = byteCount - movedBytes;
            const u64 copyBytes = Min(pendingBytes, moveChunkBytes);

            const u64 readOffset = sourceOffset + movedBytes;
            const u64 writeOffset = destinationOffset + movedBytes;
            if(!readBytesLocked(readOffset, moveBuffer.data(), copyBytes))
                return false;
            if(!writeBytesLocked(writeOffset, moveBuffer.data(), copyBytes))
                return false;

            movedBytes += copyBytes;
        }
        return true;
    }

    u64 remainingBytes = byteCount;
    while(remainingBytes > 0){
        const u64 copyBytes = Min(remainingBytes, moveChunkBytes);
        const u64 chunkBegin = remainingBytes - copyBytes;

        const u64 readOffset = sourceOffset + chunkBegin;
        const u64 writeOffset = destinationOffset + chunkBegin;
        if(!readBytesLocked(readOffset, moveBuffer.data(), copyBytes))
            return false;
        if(!writeBytesLocked(writeOffset, moveBuffer.data(), copyBytes))
            return false;

        remainingBytes -= copyBytes;
    }

    return true;
}

bool VolumeFileSystem::trimSegmentsForNextFreeOffsetLocked(){
    if(!m_writable || m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "filesystem is not writable or segment size is zero");
        return false;
    }
    if(m_segmentPaths.empty()){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "no segments are mounted");
        return false;
    }

    const u64 requiredBytes = Max(m_nextFreeOffset, m_metadataBytes);
    u64 requiredSegments = requiredBytes / m_segmentSize;
    if((requiredBytes % m_segmentSize) != 0){
        if(requiredSegments == Limit<u64>::s_Max){
            __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "required segment count overflow");
            return false;
        }
        ++requiredSegments;
    }
    if(requiredSegments == 0)
        requiredSegments = 1;
    if(requiredSegments > static_cast<u64>(m_segmentPaths.size())){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds mounted segment count");
        return false;
    }
    if(requiredSegments > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds usize range");
        return false;
    }

    while(m_segmentPaths.size() > static_cast<usize>(requiredSegments)){
        const Path removePath = m_segmentPaths.back();
        std::error_code errorCode;
        const bool removed = std::filesystem::remove(removePath, errorCode);
        if(errorCode || !removed){
            if(errorCode)
                __hidden_filesystem::LogFailureWithFsError(m_volumeName, "trimSegments:remove", removePath, errorCode);
            else
                __hidden_filesystem::LogFailureWithPath(m_volumeName, "trimSegments:remove", removePath, "remove returned false");
            return false;
        }
        m_segmentPaths.pop_back();
    }

    return true;
}

void VolumeFileSystem::unmountLocked(){
    m_mounted = false;
    m_writable = false;
    m_usage = VolumeUsage::RuntimeReadOnly;

    m_volumeName.clear();
    m_mountDirectory.clear();

    m_segmentSize = 0;
    m_metadataBytes = 0;
    m_nextFreeOffset = 0;
    m_maxSegments = 0;

    m_segmentPaths.clear();
    m_files.clear();
}

Path VolumeFileSystem::segmentPath(const usize segmentIndex)const{
    return m_mountDirectory / __hidden_filesystem::MakeSegmentFileName(m_volumeName, segmentIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

