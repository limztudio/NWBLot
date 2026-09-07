// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "filesystem.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IFilesystem::openFile(const Name& virtualPath, FileCursor& outCursor)const{
    outCursor = {};
    if(!fileExists(virtualPath))
        return false;
    outCursor.filesystem = this;
    outCursor.virtualPath = virtualPath;
    return true;
}

bool IFilesystem::readFile(FileCursor& cursor, void* data, const usize bytes, usize& outBytesRead)const{
    outBytesRead = 0;
    if(cursor.filesystem != this)
        return false;
    if(!readFile(cursor.virtualPath, cursor.offset, data, bytes, outBytesRead))
        return false;
    if(outBytesRead > bytes || static_cast<u64>(outBytesRead) > Limit<u64>::s_Max - cursor.offset){
        outBytesRead = 0;
        return false;
    }
    cursor.offset += static_cast<u64>(outBytesRead);
    return true;
}

void IFilesystem::closeFile(FileCursor& cursor)const{
    if(cursor.filesystem == this)
        cursor = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

