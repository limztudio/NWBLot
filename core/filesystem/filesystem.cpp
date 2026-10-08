// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "filesystem.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

