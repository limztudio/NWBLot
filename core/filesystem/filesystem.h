// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "volume_types.h"

#include <global/compile.h>
#include <global/limit.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IFilesystem;

namespace FileSeekOrigin{
    enum Enum : u8{
        Begin = 0,
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
    // Successful reads may stop at EOF. Failure resets outBytesRead to zero.
    virtual bool readFile(const Name& virtualPath, u64 offset, void* data, usize bytes, usize& outBytesRead)const = 0;
    virtual bool seekFile(FileCursor& cursor, i64 offset, FileSeekOrigin::Enum origin)const = 0;
    // Writes replace the complete file and copy the supplied bytes before returning.
    virtual bool writeFile(const Name& virtualPath, const void* data, usize bytes) = 0;
    virtual bool writeFileDeferred(const Name& virtualPath, const void* data, usize bytes) = 0;
    virtual bool flush() = 0;
    virtual bool removeFile(const Name& virtualPath) = 0;
    [[nodiscard]] virtual bool fileExists(const Name& virtualPath)const = 0;
    virtual bool fileSize(const Name& virtualPath, u64& outSize)const = 0;
    [[nodiscard]] virtual u64 fileCount()const = 0;
    virtual void reserveFileCapacity(usize fileCount) = 0;
    virtual Vector<Name, VolumeArena> listFiles()const = 0;


public:
    bool openFile(const Name& virtualPath, FileCursor& outCursor)const;
    bool readFile(FileCursor& cursor, void* data, usize bytes, usize& outBytesRead)const;
    void closeFile(FileCursor& cursor)const;

    template<typename ByteContainer>
    bool readFile(const Name& virtualPath, ByteContainer& outData)const;

    template<typename ByteContainer>
    bool writeFile(const Name& virtualPath, const ByteContainer& data){
        static_assert(sizeof(typename ByteContainer::value_type) == 1, "Filesystem buffers must contain bytes");
        return writeFile(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool writeFileDeferred(const Name& virtualPath, const ByteContainer& data){
        static_assert(sizeof(typename ByteContainer::value_type) == 1, "Filesystem buffers must contain bytes");
        return writeFileDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ByteContainer>
bool IFilesystem::readFile(const Name& virtualPath, ByteContainer& outData)const{
    static_assert(sizeof(typename ByteContainer::value_type) == 1, "Filesystem buffers must contain bytes");
    outData.clear();

    u64 size = 0;
    if(!fileSize(virtualPath, size))
        return false;
    if(size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem: file exceeds the runtime buffer limit"));
        return false;
    }

    outData.resize(static_cast<usize>(size));
    usize bytesRead = 0;
    if(!readFile(virtualPath, 0, outData.empty() ? nullptr : outData.data(), outData.size(), bytesRead) || bytesRead != outData.size()){
        outData.clear();
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

