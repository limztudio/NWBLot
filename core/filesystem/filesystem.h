// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "volume_types.h"

#include <global/compile.h>
#include <global/expected.h>
#include <global/limit.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_ByteElementSize = 1;

interface IFilesystem;

namespace FileSeekOrigin{
    static constexpr u8 s_FileSeekOriginBeginBase = 0;
    enum Enum : u8{
        Begin = s_FileSeekOriginBeginBase,
        Current,
        End
    };
};

// A cursor owns no backend resources. It is invalid after its filesystem is unmounted or destroyed.
struct FileCursor{
    const IFilesystem* filesystem = nullptr;
    Name virtualPath;
    u64 offset = 0;
};


interface IFilesystem{
public:
    IFilesystem() = default;
    virtual ~IFilesystem() = default;


public:
    // A volume is one logical namespace; custom backends may ignore sizing hints.
    virtual bool mount(const VolumeMountDesc& desc) = 0;
    // Writable backends must persist pending writes before unmount.
    virtual bool unmount() = 0;
    [[nodiscard]] virtual bool mounted()const = 0;
    [[nodiscard]] virtual bool writable()const = 0;


public:
    // Successful reads may stop at EOF and return the number of bytes copied.
    [[nodiscard]] virtual Expected<usize> readFile(const Name& virtualPath, u64 offset, void* data, usize bytes)const = 0;
    virtual bool seekFile(FileCursor& cursor, i64 offset, FileSeekOrigin::Enum origin)const = 0;
    // Writes replace the complete file and copy the supplied bytes before returning.
    virtual bool writeFile(const Name& virtualPath, const void* data, usize bytes) = 0;
    virtual bool writeFileDeferred(const Name& virtualPath, const void* data, usize bytes) = 0;
    virtual bool flush() = 0;
    virtual bool removeFile(const Name& virtualPath) = 0;
    [[nodiscard]] virtual bool fileExists(const Name& virtualPath)const = 0;
    [[nodiscard]] virtual Expected<u64> fileSize(const Name& virtualPath)const = 0;
    [[nodiscard]] virtual u64 fileCount()const = 0;
    virtual void reserveFileCapacity(usize fileCount) = 0;
    virtual Vector<Name, VolumeArena> listFiles()const = 0;


public:
    [[nodiscard]] Expected<FileCursor> openFile(const Name& virtualPath)const;
    [[nodiscard]] Expected<usize> readFile(FileCursor& cursor, void* data, usize bytes)const;
    void closeFile(FileCursor& cursor)const noexcept;

    template<typename ByteContainer>
    bool readFile(const Name& virtualPath, ByteContainer& outData)const;

    template<typename ByteContainer>
    bool writeFile(const Name& virtualPath, const ByteContainer& data){
        static_assert(sizeof(typename ByteContainer::value_type) == s_ByteElementSize, "Filesystem buffers must contain bytes");
        return writeFile(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool writeFileDeferred(const Name& virtualPath, const ByteContainer& data){
        static_assert(sizeof(typename ByteContainer::value_type) == s_ByteElementSize, "Filesystem buffers must contain bytes");
        return writeFileDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ByteContainer>
bool IFilesystem::readFile(const Name& virtualPath, ByteContainer& outData)const{
    static_assert(sizeof(typename ByteContainer::value_type) == s_ByteElementSize, "Filesystem buffers must contain bytes");
    outData.clear();

    const auto size = fileSize(virtualPath);
    if(!size)
        return false;
    if(*size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem: file exceeds the runtime buffer limit"));
        return false;
    }

    outData.resize(static_cast<usize>(*size));
    const auto bytesRead = readFile(virtualPath, 0, outData.empty() ? nullptr : outData.data(), outData.size());
    if(!bytesRead || *bytesRead != outData.size()){
        outData.clear();
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

