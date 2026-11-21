// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "filesystem.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<usize> IFilesystem::readWholeFile(const Name& virtualPath, void* const context, u8* (* const prepareBuffer)(void* context, usize size))const{
    if(!prepareBuffer)
        return MakeUnexpected(Failure{});

    const auto size = fileSize(virtualPath);
    if(!size)
        return MakeUnexpected(size.error());
    if(*size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Filesystem: file exceeds the runtime buffer limit"));
        return MakeUnexpected(Failure{});
    }

    const usize byteCount = static_cast<usize>(*size);
    u8* const destination = prepareBuffer(context, byteCount);
    if(byteCount != 0u && !destination)
        return MakeUnexpected(Failure{});
    return readFile(virtualPath, 0u, destination, byteCount);
}

Expected<FileCursor> IFilesystem::openFile(const Name& virtualPath)const{
    if(!fileExists(virtualPath))
        return MakeUnexpected(Failure{});
    return FileCursor{ .filesystem = this, .virtualPath = virtualPath };
}

Expected<usize> IFilesystem::readFile(FileCursor& cursor, void* data, const usize bytes)const{
    if(cursor.filesystem != this)
        return MakeUnexpected(Failure{});
    const auto bytesRead = readFile(cursor.virtualPath, cursor.offset, data, bytes);
    if(!bytesRead)
        return bytesRead;
    if(*bytesRead > bytes || static_cast<u64>(*bytesRead) > Limit<u64>::s_Max - cursor.offset)
        return MakeUnexpected(Failure{});
    cursor.offset += static_cast<u64>(*bytesRead);
    return bytesRead;
}

void IFilesystem::closeFile(FileCursor& cursor)const noexcept{
    if(cursor.filesystem == this)
        cursor = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

