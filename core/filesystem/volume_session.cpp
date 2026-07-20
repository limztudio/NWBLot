// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_session.h"
#include "volume_staging_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VolumeSession::VolumeSession(Alloc::GlobalArena& arena)
    : m_volumeFileSystem(arena)
{}


bool VolumeSession::create(const Path& outputDirectory, const VolumeBuildConfig& config){
    if(!__hidden_filesystem_staging::RemoveExistingVolumeSegments(outputDirectory, config.volumeName.view()))
        return false;

    VolumeMountDesc desc(outputDirectory.arena());
    desc.volumeName = config.volumeName;
    desc.mountDirectory = outputDirectory;
    desc.segmentSize = config.segmentSize;
    desc.metadataSize = config.metadataSize;
    desc.createIfMissing = true;
    desc.usage = VolumeUsage::CookWrite;

    if(m_volumeFileSystem.mount(desc))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::create failed to mount volume '{}'"), StringConvert(config.volumeName.view()));
    return false;
}


bool VolumeSession::load(const AStringView volumeName, const Path& mountDirectory){
    VolumeMountDesc desc(mountDirectory.arena());
    if(!desc.volumeName.assign(volumeName))
        return false;
    desc.mountDirectory = mountDirectory;
    desc.createIfMissing = false;
    desc.usage = VolumeUsage::RuntimeReadOnly;

    if(m_volumeFileSystem.mount(desc))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::load failed to mount volume '{}'"), StringConvert(volumeName));
    return false;
}

void VolumeSession::reserveFileCapacity(const usize fileCount){
    m_volumeFileSystem.reserveFileCapacity(fileCount);
}

bool VolumeSession::pushData(const Name& virtualPath, const void* data, const usize bytes){
    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: volume is not mounted"));
        return false;
    }
    if(!m_volumeFileSystem.writable()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: volume is read-only"));
        return false;
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: virtual path is empty"));
        return false;
    }

    if(m_volumeFileSystem.writeFile(virtualPath, data, bytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed to write '{}'"), StringConvert(virtualPath.c_str()));
    return false;
}

bool VolumeSession::pushData(const AStringView virtualPath, const void* data, const usize bytes){
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: virtual path is empty"));
        return false;
    }

    const Name virtualPathName(virtualPath);
    if(!virtualPathName){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: virtual path is invalid"));
        return false;
    }

    return pushData(virtualPathName, data, bytes);
}

bool VolumeSession::pushDataDeferred(const Name& virtualPath, const void* data, const usize bytes){
    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: volume is not mounted"));
        return false;
    }
    if(!m_volumeFileSystem.writable()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: volume is read-only"));
        return false;
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: virtual path is empty"));
        return false;
    }

    if(m_volumeFileSystem.writeFileDeferred(virtualPath, data, bytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed to write '{}'"), StringConvert(virtualPath.c_str()));
    return false;
}

bool VolumeSession::pushDataDeferred(const AStringView virtualPath, const void* data, const usize bytes){
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: virtual path is empty"));
        return false;
    }

    const Name virtualPathName(virtualPath);
    if(!virtualPathName){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: virtual path is invalid"));
        return false;
    }

    return pushDataDeferred(virtualPathName, data, bytes);
}

bool VolumeSession::flush(){
    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::flush failed: volume is not mounted"));
        return false;
    }
    if(!m_volumeFileSystem.writable()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::flush failed: volume is read-only"));
        return false;
    }

    if(m_volumeFileSystem.compact(true))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::flush failed to finalize volume"));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
