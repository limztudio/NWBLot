// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"

#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <global/filesystem/volume_naming.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_device_pipeline_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_PipelineCacheVolumeSegmentSize = 16ull * 1024ull * 1024ull;
static constexpr u64 s_PipelineCacheVolumeMetadataSize = 4ull * 1024ull;
static constexpr usize s_PipelineCacheDataMaxAttempts = 4;

[[nodiscard]] static u32 ReadPipelineCacheU32(const BinaryByteView cacheData, const usize offset)noexcept{
    return
        static_cast<u32>(cacheData[offset])
        | (static_cast<u32>(cacheData[offset + 1u]) << 8u)
        | (static_cast<u32>(cacheData[offset + 2u]) << 16u)
        | (static_cast<u32>(cacheData[offset + 3u]) << 24u)
    ;
}

static bool MountPipelineCacheVolume(
    const Path& directory,
    const AStringView volumeName,
    const bool createIfMissing,
    Filesystem::VolumeUsage::Enum usage,
    Filesystem::VolumeFileSystem& outVolume
){
    Filesystem::VolumeMountDesc mountDesc(directory.arena());
    if(!mountDesc.volumeName.assign(volumeName))
        return false;
    mountDesc.mountDirectory = directory;
    mountDesc.createIfMissing = createIfMissing;
    mountDesc.usage = usage;
    if(createIfMissing){
        mountDesc.segmentSize = s_PipelineCacheVolumeSegmentSize;
        mountDesc.metadataSize = s_PipelineCacheVolumeMetadataSize;
    }

    return outVolume.mount(mountDesc);
}

template<typename CacheDataVector>
static bool RetrievePipelineCacheData(
    const VolkDeviceTable& deviceDispatch,
    VkDevice device,
    VkPipelineCache pipelineCache,
    CacheDataVector& outData
){
    static_assert(IsSame_V<typename CacheDataVector::value_type, u8>, "pipeline cache data must be byte-addressable");

    outData.clear();

    for(usize attempt = 0; attempt < s_PipelineCacheDataMaxAttempts; ++attempt){
        size_t cacheSize = 0;
        VkResult res = deviceDispatch.vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query pipeline cache data size. {}"), ResultToString(res));
            return false;
        }
        if(cacheSize == 0)
            return true;
        if(cacheSize > static_cast<size_t>(Limit<usize>::s_Max)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Pipeline cache data size {} exceeds runtime buffer limit {}.")
                , static_cast<u64>(cacheSize)
                , static_cast<u64>(Limit<usize>::s_Max)
            );
            return false;
        }

        outData.resize(static_cast<usize>(cacheSize));
        size_t retrievedSize = cacheSize;
        res = deviceDispatch.vkGetPipelineCacheData(device, pipelineCache, &retrievedSize, outData.data());
        if(res == VK_SUCCESS){
            if(retrievedSize > cacheSize || retrievedSize > static_cast<size_t>(Limit<usize>::s_Max)){
                outData.clear();
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned an invalid pipeline cache data size while serializing."));
                return false;
            }

            outData.resize(static_cast<usize>(retrievedSize));
            return true;
        }
        if(res == VK_INCOMPLETE)
            continue;

        outData.clear();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to retrieve pipeline cache data. {}"), ResultToString(res));
        return false;
    }

    outData.clear();
    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Pipeline cache data kept changing while serializing."));
    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PipelineCacheDataValidation::Enum ValidatePipelineCacheData(
    const BinaryByteView cacheData,
    const VkPhysicalDeviceProperties& properties
)noexcept{
    if(cacheData.data() == nullptr || cacheData.size() < s_PipelineCacheHeaderVersionOneSize)
        return PipelineCacheDataValidation::Malformed;

    const u32 headerSize = __hidden_vulkan_device_pipeline_cache::ReadPipelineCacheU32(cacheData, 0u);
    if(headerSize != s_PipelineCacheHeaderVersionOneSize)
        return PipelineCacheDataValidation::Malformed;

    const u32 headerVersion = __hidden_vulkan_device_pipeline_cache::ReadPipelineCacheU32(cacheData, 4u);
    if(headerVersion != static_cast<u32>(VK_PIPELINE_CACHE_HEADER_VERSION_ONE))
        return PipelineCacheDataValidation::Incompatible;

    const u32 vendorId = __hidden_vulkan_device_pipeline_cache::ReadPipelineCacheU32(cacheData, 8u);
    const u32 deviceId = __hidden_vulkan_device_pipeline_cache::ReadPipelineCacheU32(cacheData, 12u);
    if(vendorId != properties.vendorID || deviceId != properties.deviceID)
        return PipelineCacheDataValidation::Incompatible;
    if(NWB_MEMCMP(cacheData.data() + 16u, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
        return PipelineCacheDataValidation::Incompatible;

    return PipelineCacheDataValidation::Usable;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Device::loadPipelineCacheData(GraphicsBytes& outData){
    outData.clear();
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty())
        return false;
    if(!::VolumeSegmentExists(m_pipelineCacheDirectory, m_pipelineCacheVolumeName))
        return false;

    Filesystem::VolumeFileSystem volume(m_context.objectArena);
    if(
        !__hidden_vulkan_device_pipeline_cache::MountPipelineCacheVolume(
            m_pipelineCacheDirectory,
            m_pipelineCacheVolumeName,
            false,
            Filesystem::VolumeUsage::RuntimeReadOnly,
            volume
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' from '{}'.")
            , StringConvert(m_pipelineCacheVolumeName)
            , PathToString<tchar>(m_pipelineCacheDirectory)
        );
        return false;
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!volume.fileExists(cachePath))
        return false;
    if(!volume.readFile(cachePath, outData)){
        outData.clear();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to read pipeline cache data from runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return false;
    }
    const VulkanDetail::PipelineCacheDataValidation::Enum validation = VulkanDetail::ValidatePipelineCacheData(
        BinaryByteView{ outData.data(), outData.size() },
        m_context.physicalDeviceProperties
    );
    if(validation != VulkanDetail::PipelineCacheDataValidation::Usable){
        outData.clear();
        if(validation == VulkanDetail::PipelineCacheDataValidation::Malformed)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring malformed pipeline cache data in runtime volume '{}'.")
                , StringConvert(m_pipelineCacheVolumeName)
            );
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring incompatible pipeline cache data in runtime volume '{}'.")
                , StringConvert(m_pipelineCacheVolumeName)
            );
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Loaded pipeline cache runtime volume '{}' ({} bytes).")
        , StringConvert(m_pipelineCacheVolumeName)
        , outData.size()
    );
    return true;
}

void Device::savePipelineCacheData(){
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty() || !m_context.pipelineCache)
        return;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_PipelineCacheSaveArena);
    Vector<u8, Alloc::ScratchArena> cacheData{scratchArena};
    if(!__hidden_vulkan_device_pipeline_cache::RetrievePipelineCacheData(m_context.deviceDispatch, m_context.device, m_context.pipelineCache, cacheData))
        return;
    if(cacheData.empty())
        return;

    const VulkanDetail::PipelineCacheDataValidation::Enum validation = VulkanDetail::ValidatePipelineCacheData(
        BinaryByteView{ cacheData.data(), cacheData.size() },
        m_context.physicalDeviceProperties
    );
    if(validation != VulkanDetail::PipelineCacheDataValidation::Usable){
        if(validation == VulkanDetail::PipelineCacheDataValidation::Malformed)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned malformed pipeline cache data; skipping cache write."));
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned incompatible pipeline cache data; skipping cache write."));
        return;
    }

    Filesystem::VolumeFileSystem volume(m_context.objectArena);
    if(
        !__hidden_vulkan_device_pipeline_cache::MountPipelineCacheVolume(
            m_pipelineCacheDirectory,
            m_pipelineCacheVolumeName,
            true,
            Filesystem::VolumeUsage::RuntimeReadWrite,
            volume
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' for write at '{}'.")
            , StringConvert(m_pipelineCacheVolumeName)
            , PathToString<tchar>(m_pipelineCacheDirectory)
        );
        if(!Filesystem::RemoveVolumeSegments(m_pipelineCacheDirectory, m_pipelineCacheVolumeName)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to remove unusable pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
            return;
        }
        if(
            !__hidden_vulkan_device_pipeline_cache::MountPipelineCacheVolume(
                m_pipelineCacheDirectory,
                m_pipelineCacheVolumeName,
                true,
                Filesystem::VolumeUsage::RuntimeReadWrite,
                volume
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to recreate pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
            return;
        }
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!volume.writeFile(cachePath, cacheData)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to write pipeline cache data to runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return;
    }
    if(!volume.compact(true))
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to compact pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Saved pipeline cache runtime volume '{}' ({} bytes).")
        , StringConvert(m_pipelineCacheVolumeName)
        , cacheData.size()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

