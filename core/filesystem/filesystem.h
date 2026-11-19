// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "volume_types.h"

#include <core/common/log.h>

#include <global/compile.h>
#include <global/expected.h>
#include <global/limit.h>


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

struct FileCursor{
    const IFilesystem* filesystem = nullptr;
    Name virtualPath;
    u64 offset = 0;
};


interface IFilesystem{
public:
    IFilesystem()noexcept = default;
    virtual ~IFilesystem()noexcept = default;


public:
    virtual b8 mount(const VolumeMountDesc& desc) = 0;
    virtual b8 unmount() = 0;
    [[nodiscard]] virtual b8 mounted()const = 0;
    [[nodiscard]] virtual b8 writable()const = 0;


public:
    [[nodiscard]] virtual Expected<usize> readFile(const Name& virtualPath, u64 offset, void* data, usize bytes)const = 0;
    virtual b8 seekFile(FileCursor& cursor, i64 offset, FileSeekOrigin::Enum origin)const = 0;
    virtual b8 writeFile(const Name& virtualPath, const void* data, usize bytes) = 0;
    virtual b8 writeFileDeferred(const Name& virtualPath, const void* data, usize bytes) = 0;
    virtual b8 flush() = 0;
    virtual b8 removeFile(const Name& virtualPath) = 0;
    [[nodiscard]] virtual b8 fileExists(const Name& virtualPath)const = 0;
    [[nodiscard]] virtual Expected<u64> fileSize(const Name& virtualPath)const = 0;
    [[nodiscard]] virtual u64 fileCount()const = 0;
    virtual void reserveFileCapacity(usize fileCount) = 0;


public:
    [[nodiscard]] Expected<FileCursor> openFile(const Name& virtualPath)const;
    [[nodiscard]] Expected<usize> readFile(FileCursor& cursor, void* data, usize bytes)const;
    void closeFile(FileCursor& cursor)const noexcept;

    template<typename ByteContainer>
    b8 readFile(const Name& virtualPath, ByteContainer& outData)const;

    template<typename ByteContainer>
    b8 writeFile(const Name& virtualPath, const ByteContainer& data){
        static_assert(sizeof(typename ByteContainer::value_type) == s_ByteElementSize, "Filesystem buffers must contain bytes");
        return writeFile(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    b8 writeFileDeferred(const Name& virtualPath, const ByteContainer& data){
        static_assert(sizeof(typename ByteContainer::value_type) == s_ByteElementSize, "Filesystem buffers must contain bytes");
        return writeFileDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ByteContainer>
b8 IFilesystem::readFile(const Name& virtualPath, ByteContainer& outData)const{
    static_assert(sizeof(typename ByteContainer::value_type) == s_ByteElementSize, "Filesystem buffers must contain bytes");
    outData.clear();

    const auto size = fileSize(virtualPath);
    if(!size)
        return false;
    if(*size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Filesystem: file exceeds the runtime buffer limit"));
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

