// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "host_readback_sync.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_FINAL)
namespace __hidden_host_readback_sync{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static thread_local usize s_AppendedBarrierCount = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool HasBufferDeviceWriteState(const ResourceStates::Mask states)noexcept{
    constexpr ResourceStates::Mask s_BufferDeviceWriteStates = ResourceStates::UnorderedAccess
        | ResourceStates::StreamOut
        | ResourceStates::CopyDest
        | ResourceStates::ResolveDest
        | ResourceStates::AccelStructWrite
        | ResourceStates::AccelStructBuildBlas
        | ResourceStates::OpacityMicromapWrite
        | ResourceStates::ConvertCoopVecMatrixOutput
    ;
    return (states & s_BufferDeviceWriteStates) != ResourceStates::Unknown;
}

VkBufferMemoryBarrier2 BuildHostReadBufferBarrier(const VkBuffer buffer)noexcept{
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

void CollectUniquePhysicalQueueFamilyIndices(
    const GpuPhysicalQueueTopology& topology,
    Vector<u32, Alloc::ScratchArena>& familyIndices
){
    familyIndices.clear();
    if(!topology.queues)
        return;

    familyIndices.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const u32 familyIndex = topology.queues[queueIndex].familyIndex;
        if(familyIndex == VK_QUEUE_FAMILY_IGNORED)
            continue;

        bool duplicate = false;
        for(const u32 existingFamilyIndex : familyIndices){
            if(existingFamilyIndex == familyIndex){
                duplicate = true;
                break;
            }
        }
        if(!duplicate)
            familyIndices.push_back(familyIndex);
    }
}

#if !defined(NWB_FINAL)
void ResetHostReadbackBarrierAppendCountForTesting()noexcept{
    __hidden_host_readback_sync::s_AppendedBarrierCount = 0u;
}

usize GetHostReadbackBarrierAppendCountForTesting()noexcept{
    return __hidden_host_readback_sync::s_AppendedBarrierCount;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HostReadbackBarrierTracker::HostReadbackBarrierTracker(Alloc::GlobalArena& arena)
    : m_buffers(arena)
{}

bool HostReadbackBarrierTracker::registerBuffer(const VkBuffer buffer){
    if(buffer == VK_NULL_HANDLE)
        return false;

    for(const VkBuffer trackedBuffer : m_buffers){
        if(trackedBuffer == buffer)
            return false;
    }

    m_buffers.push_back(buffer);
    return true;
}

void HostReadbackBarrierTracker::registerDeviceOwnedBuffer(const VkBuffer buffer){
    if(!registerBuffer(buffer))
        return;
}

void HostReadbackBarrierTracker::appendBarriers(
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& barriers
)const{
    barriers.reserve(barriers.size() + m_buffers.size());
    for(const VkBuffer buffer : m_buffers)
        barriers.push_back(BuildHostReadBufferBarrier(buffer));
#if !defined(NWB_FINAL)
    __hidden_host_readback_sync::s_AppendedBarrierCount += m_buffers.size();
#endif
}

void HostReadbackBarrierTracker::clear(){
    m_buffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

