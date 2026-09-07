// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "filesystem.h"
#include "volume_file_system.h"


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


UniquePtr<IFilesystem> CreateFilesystem(Alloc::GlobalArena& arena, const VolumeMountDesc& desc, const FilesystemFactory& factory){
    UniquePtr<IFilesystem> filesystem = factory ? factory(arena, desc) : MakeUnique<VolumeFileSystem>(arena);
    if(!filesystem){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem: project factory did not create a filesystem"));
        return nullptr;
    }
    if(!filesystem->mountVolume(desc))
        return nullptr;
    return filesystem;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

