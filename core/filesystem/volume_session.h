// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "volume_build.h"
#include "volume_file_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VolumeSession final : NoCopy{
public:
    explicit VolumeSession(Alloc::GlobalArena& arena);


public:
    bool create(const Path& outputDirectory, const VolumeBuildConfig& config);
    bool load(AStringView volumeName, const Path& mountDirectory);
    void reserveFileCapacity(usize fileCount);

    bool pushData(const Name& virtualPath, const void* data, usize bytes);
    bool pushData(AStringView virtualPath, const void* data, usize bytes);
    bool pushDataDeferred(const Name& virtualPath, const void* data, usize bytes);
    bool pushDataDeferred(AStringView virtualPath, const void* data, usize bytes);

    template<typename ByteContainer>
    bool pushData(const Name& virtualPath, const ByteContainer& data){
        return pushData(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool pushData(const AStringView virtualPath, const ByteContainer& data){
        return pushData(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool pushDataDeferred(const Name& virtualPath, const ByteContainer& data){
        return pushDataDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool pushDataDeferred(const AStringView virtualPath, const ByteContainer& data){
        return pushDataDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    bool flush();
    template<typename ByteContainer>
    bool loadData(AStringView virtualPath, ByteContainer& outData)const;
    template<typename ByteContainer>
    bool loadData(const Name& virtualPath, ByteContainer& outData)const;

    [[nodiscard]] bool mounted()const{ return m_volumeFileSystem.mounted(); }
    [[nodiscard]] bool writable()const{ return m_volumeFileSystem.writable(); }
    [[nodiscard]] u64 fileCount()const{ return m_volumeFileSystem.fileCount(); }
    [[nodiscard]] usize segmentCount()const{ return m_volumeFileSystem.segmentCount(); }


private:
    VolumeFileSystem m_volumeFileSystem;
};


template<typename ByteContainer>
bool VolumeSession::loadData(const AStringView virtualPath, ByteContainer& outData)const{
    outData.clear();

    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: volume is not mounted"));
        return false;
    }
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: virtual path is empty"));
        return false;
    }

    const Name virtualPathName(virtualPath);
    if(!virtualPathName){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: virtual path is invalid"));
        return false;
    }

    if(m_volumeFileSystem.readFile(virtualPathName, outData))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed to read '{}'"), StringConvert(virtualPath));
    return false;
}

template<typename ByteContainer>
bool VolumeSession::loadData(const Name& virtualPath, ByteContainer& outData)const{
    outData.clear();

    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: volume is not mounted"));
        return false;
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed: virtual path is empty"));
        return false;
    }

    if(m_volumeFileSystem.readFile(virtualPath, outData))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::loadData failed to read '{}'"), StringConvert(virtualPath.c_str()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
