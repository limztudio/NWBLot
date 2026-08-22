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
static bool ValidatePipelineCacheData(const CacheDataVector& cacheData, const VkPhysicalDeviceProperties& properties){
    static_assert(IsSame_V<typename CacheDataVector::value_type, u8>, "pipeline cache data must be byte-addressable");

    if(cacheData.size() < sizeof(VkPipelineCacheHeaderVersionOne))
        return false;

    VkPipelineCacheHeaderVersionOne header{};
    NWB_MEMCPY(&header, sizeof(header), cacheData.data(), sizeof(header));

    if(header.headerSize < sizeof(VkPipelineCacheHeaderVersionOne))
        return false;
    if(header.headerSize > cacheData.size())
        return false;
    if(header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
        return false;
    if(header.vendorID != properties.vendorID || header.deviceID != properties.deviceID)
        return false;
    if(NWB_MEMCMP(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
        return false;

    return true;
}

template<typename CacheDataVector>
static bool RetrievePipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, CacheDataVector& outData){
    static_assert(IsSame_V<typename CacheDataVector::value_type, u8>, "pipeline cache data must be byte-addressable");

    outData.clear();

    for(usize attempt = 0; attempt < s_PipelineCacheDataMaxAttempts; ++attempt){
        size_t cacheSize = 0;
        VkResult res = vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr);
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
        res = vkGetPipelineCacheData(device, pipelineCache, &retrievedSize, outData.data());
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
    if(!__hidden_vulkan_device_pipeline_cache::ValidatePipelineCacheData(outData, m_context.physicalDeviceProperties)){
        outData.clear();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring incompatible pipeline cache data in runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Loaded pipeline cache runtime volume '{}' ({} bytes).")
        , StringConvert(m_pipelineCacheVolumeName)
        , outData.size()
    );
    return true;
}

VkDescriptorSetLayout Device::getOrCreateEmptyDescriptorBufferSetLayout()const{
    // Lazily create immutable empty layouts under the cache mutex.
    if(m_context.emptyDescriptorBufferSetLayout != VK_NULL_HANDLE)
        return m_context.emptyDescriptorBufferSetLayout;

    if(!m_context.extensions.EXT_descriptor_buffer)
        return VK_NULL_HANDLE;

    auto layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = 0;
    layoutInfo.pBindings = nullptr;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    const VkResult res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create empty descriptor-buffer set layout. {}"), ResultToString(res));
        return VK_NULL_HANDLE;
    }
    const_cast<VkDescriptorSetLayout&>(m_context.emptyDescriptorBufferSetLayout) = setLayout;
    return setLayout;
}

void Device::savePipelineCacheData(){
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty() || !m_context.pipelineCache)
        return;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_PipelineCacheSaveArena);
    Vector<u8, Alloc::ScratchArena> cacheData{scratchArena};
    if(!__hidden_vulkan_device_pipeline_cache::RetrievePipelineCacheData(m_context.device, m_context.pipelineCache, cacheData))
        return;
    if(cacheData.empty())
        return;

    if(!__hidden_vulkan_device_pipeline_cache::ValidatePipelineCacheData(cacheData, m_context.physicalDeviceProperties)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned incompatible pipeline cache data; skipping runtime cache write."));
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

