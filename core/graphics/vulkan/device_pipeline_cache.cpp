// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"

#include <core/filesystem/factory.h>
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
static constexpr u32 s_PipelineCacheByteShift1 = 8u;
static constexpr u32 s_PipelineCacheByteShift2 = 16u;
static constexpr u32 s_PipelineCacheByteShift3 = 24u;
static constexpr usize s_PipelineCacheVendorIdOffset = 8u;
static constexpr usize s_PipelineCacheDeviceIdOffset = 12u;
static constexpr usize s_PipelineCacheUuidOffset = 16u;

[[nodiscard]] static u32 ReadPipelineCacheU32(const BinaryByteView cacheData, const usize offset)noexcept{
    return
        static_cast<u32>(cacheData[offset])
        | (static_cast<u32>(cacheData[offset + 1u]) << s_PipelineCacheByteShift1)
        | (static_cast<u32>(cacheData[offset + 2u]) << s_PipelineCacheByteShift2)
        | (static_cast<u32>(cacheData[offset + 3u]) << s_PipelineCacheByteShift3)
    ;
}

static Expected<UniquePtr<Filesystem::IFilesystem>> MountPipelineCacheVolume(
    const Path& directory,
    const AStringView volumeName,
    const bool createIfMissing,
    Filesystem::VolumeUsage::Enum usage,
    const Filesystem::FilesystemFactory& factory
){
    Filesystem::VolumeMountDesc mountDesc(directory.arena());
    if(!mountDesc.volumeName.assign(volumeName))
        return MakeUnexpected(Failure{});
    mountDesc.mountDirectory = directory;
    mountDesc.createIfMissing = createIfMissing;
    mountDesc.usage = usage;
    if(createIfMissing){
        mountDesc.segmentSize = s_PipelineCacheVolumeSegmentSize;
        mountDesc.metadataSize = s_PipelineCacheVolumeMetadataSize;
    }

    auto volume = Filesystem::CreateFilesystem(directory.arena(), mountDesc, factory);
    if(!volume)
        return MakeUnexpected(Failure{});
    return volume;
}

static Expected<Vector<u8, Alloc::ScratchArena>> RetrievePipelineCacheData(
    const VolkDeviceTable& deviceDispatch,
    VkDevice device,
    VkPipelineCache pipelineCache,
    Alloc::ScratchArena& scratchArena
){
    Vector<u8, Alloc::ScratchArena> data(scratchArena);

    for(usize attempt = 0u; attempt < s_PipelineCacheDataMaxAttempts; ++attempt){
        size_t cacheSize = 0;
        VkResult res = deviceDispatch.vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query pipeline cache data size. {}"), ResultToString(res));
            return MakeUnexpected(Failure{});
        }
        if(cacheSize == 0)
            return data;
        if(cacheSize > static_cast<size_t>(Limit<usize>::s_Max)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Pipeline cache data size {} exceeds runtime buffer limit {}.")
                , static_cast<u64>(cacheSize)
                , static_cast<u64>(Limit<usize>::s_Max)
            );
            return MakeUnexpected(Failure{});
        }

        data.resize(static_cast<usize>(cacheSize));
        size_t retrievedSize = cacheSize;
        res = deviceDispatch.vkGetPipelineCacheData(device, pipelineCache, &retrievedSize, data.data());
        if(res == VK_SUCCESS){
            if(retrievedSize > cacheSize || retrievedSize > static_cast<size_t>(Limit<usize>::s_Max)){
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned an invalid pipeline cache data size while serializing."));
                return MakeUnexpected(Failure{});
            }

            data.resize(static_cast<usize>(retrievedSize));
            return data;
        }
        if(res == VK_INCOMPLETE)
            continue;

        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to retrieve pipeline cache data. {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Pipeline cache data kept changing while serializing."));
    return MakeUnexpected(Failure{});
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

    const u32 vendorId = __hidden_vulkan_device_pipeline_cache::ReadPipelineCacheU32(cacheData, __hidden_vulkan_device_pipeline_cache::s_PipelineCacheVendorIdOffset);
    const u32 deviceId = __hidden_vulkan_device_pipeline_cache::ReadPipelineCacheU32(cacheData, __hidden_vulkan_device_pipeline_cache::s_PipelineCacheDeviceIdOffset);
    if(vendorId != properties.vendorID || deviceId != properties.deviceID)
        return PipelineCacheDataValidation::Incompatible;
    if(NWB_MEMCMP(cacheData.data() + __hidden_vulkan_device_pipeline_cache::s_PipelineCacheUuidOffset, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
        return PipelineCacheDataValidation::Incompatible;

    return PipelineCacheDataValidation::Usable;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GraphicsBytes> Device::loadPipelineCacheData(){
    GraphicsBytes data{m_context.objectArena};
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty())
        return MakeUnexpected(Failure{});
    if(!m_filesystemFactory && !::VolumeSegmentExists(m_pipelineCacheDirectory, m_pipelineCacheVolumeName))
        return MakeUnexpected(Failure{});

    auto volume = __hidden_vulkan_device_pipeline_cache::MountPipelineCacheVolume(
            m_pipelineCacheDirectory,
            m_pipelineCacheVolumeName,
            false,
            Filesystem::VolumeUsage::RuntimeReadOnly,
            m_filesystemFactory
        );
    if(!volume){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' from '{}'.")
            , StringConvert(m_pipelineCacheVolumeName)
            , PathToString<tchar>(m_pipelineCacheDirectory)
        );
        return MakeUnexpected(Failure{});
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!(*volume)->fileExists(cachePath))
        return MakeUnexpected(Failure{});
    if(!(*volume)->readFile(cachePath, data)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to read pipeline cache data from runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return MakeUnexpected(Failure{});
    }
    const VulkanDetail::PipelineCacheDataValidation::Enum validation = VulkanDetail::ValidatePipelineCacheData(
        BinaryByteView{ data.data(), data.size() },
        m_context.physicalDeviceProperties
    );
    if(validation != VulkanDetail::PipelineCacheDataValidation::Usable){
        if(validation == VulkanDetail::PipelineCacheDataValidation::Malformed)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring malformed pipeline cache data in runtime volume '{}'.")
                , StringConvert(m_pipelineCacheVolumeName)
            );
        else
            NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Discarding incompatible pipeline cache data in runtime volume '{}'; starting empty.")
                , StringConvert(m_pipelineCacheVolumeName)
            );
        return MakeUnexpected(Failure{});
    }

    if(!(*volume)->unmount()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to unmount pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return MakeUnexpected(Failure{});
    }

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Loaded pipeline cache runtime volume '{}' ({} bytes).")
        , StringConvert(m_pipelineCacheVolumeName)
        , data.size()
    );
    return data;
}

void Device::savePipelineCacheData(){
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty() || !m_context.pipelineCache)
        return;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_PipelineCacheSaveArena);
    const auto cacheData = __hidden_vulkan_device_pipeline_cache::RetrievePipelineCacheData(m_context.deviceDispatch, m_context.device, m_context.pipelineCache, scratchArena);
    if(!cacheData)
        return;
    if(cacheData->empty())
        return;

    const VulkanDetail::PipelineCacheDataValidation::Enum validation = VulkanDetail::ValidatePipelineCacheData(
        BinaryByteView{ cacheData->data(), cacheData->size() },
        m_context.physicalDeviceProperties
    );
    if(validation != VulkanDetail::PipelineCacheDataValidation::Usable){
        if(validation == VulkanDetail::PipelineCacheDataValidation::Malformed)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned malformed pipeline cache data; skipping cache write."));
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned incompatible pipeline cache data; skipping cache write."));
        return;
    }

    auto volume = __hidden_vulkan_device_pipeline_cache::MountPipelineCacheVolume(
            m_pipelineCacheDirectory,
            m_pipelineCacheVolumeName,
            true,
            Filesystem::VolumeUsage::RuntimeReadWrite,
            m_filesystemFactory
        );
    if(!volume){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' for write at '{}'.")
            , StringConvert(m_pipelineCacheVolumeName)
            , PathToString<tchar>(m_pipelineCacheDirectory)
        );
        if(m_filesystemFactory)
            return;
        if(!Filesystem::RemoveVolumeSegments(m_pipelineCacheDirectory, m_pipelineCacheVolumeName)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to remove unusable pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
            return;
        }
        volume = __hidden_vulkan_device_pipeline_cache::MountPipelineCacheVolume(
                m_pipelineCacheDirectory,
                m_pipelineCacheVolumeName,
                true,
                Filesystem::VolumeUsage::RuntimeReadWrite,
                m_filesystemFactory
            );
        if(!volume){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to recreate pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
            return;
        }
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!(*volume)->writeFile(cachePath, *cacheData)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to write pipeline cache data to runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return;
    }
    if(!(*volume)->flush()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to flush pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return;
    }
    if(!(*volume)->unmount()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to unmount pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return;
    }

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Saved pipeline cache runtime volume '{}' ({} bytes).")
        , StringConvert(m_pipelineCacheVolumeName)
        , cacheData->size()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

