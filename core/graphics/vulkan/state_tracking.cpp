// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "state_tracking_detail.h"

#include <core/common/log.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StateTracker::StateTracker(const VulkanContext& context)
    : m_permanentTextureStates(0, Hasher<Texture*>(), EqualTo<Texture*>(), context.objectArena)
    , m_permanentBufferStates(0, Hasher<Buffer*>(), EqualTo<Buffer*>(), context.objectArena)
    , m_attemptPermanentTextureStates(0, Hasher<Texture*>(), EqualTo<Texture*>(), context.objectArena)
    , m_attemptPermanentBufferStates(0, Hasher<Buffer*>(), EqualTo<Buffer*>(), context.objectArena)
    , m_textureStates(0, TextureSubresourceStateKeyHasher(), TextureSubresourceStateKeyEqualTo(), context.objectArena)
    , m_bufferStates(0, Hasher<Buffer*>(), EqualTo<Buffer*>(), context.objectArena)
    , m_textureUavBarriers(0, Hasher<Texture*>(), EqualTo<Texture*>(), context.objectArena)
    , m_bufferUavBarriers(0, Hasher<Buffer*>(), EqualTo<Buffer*>(), context.objectArena)
    , m_context(context)
{}
StateTracker::~StateTracker(){}

void StateTracker::reset(){
    m_textureStates.clear();
    m_bufferStates.clear();
}

void StateTracker::beginRecordingAttempt(){
    NWB_ASSERT(!m_recordingAttemptActive);
    m_attemptPermanentTextureStates.clear();
    m_attemptPermanentBufferStates.clear();
    m_attemptPermanentTextureStates.reserve(m_permanentTextureStates.size());
    m_attemptPermanentBufferStates.reserve(m_permanentBufferStates.size());
    for(auto it = m_permanentTextureStates.begin(); it != m_permanentTextureStates.end(); ++it)
        m_attemptPermanentTextureStates.insert_or_assign(it->first, it.value());
    for(auto it = m_permanentBufferStates.begin(); it != m_permanentBufferStates.end(); ++it)
        m_attemptPermanentBufferStates.insert_or_assign(it->first, it.value());
    m_recordingAttemptActive = true;
}

void StateTracker::commitRecordingAttempt(){
    m_attemptPermanentTextureStates.clear();
    m_attemptPermanentBufferStates.clear();
    m_recordingAttemptActive = false;
}

void StateTracker::rollbackRecordingAttempt(){
    if(!m_recordingAttemptActive)
        return;

    m_permanentTextureStates.clear();
    m_permanentBufferStates.clear();
    m_permanentTextureStates.reserve(m_attemptPermanentTextureStates.size());
    m_permanentBufferStates.reserve(m_attemptPermanentBufferStates.size());
    for(auto it = m_attemptPermanentTextureStates.begin(); it != m_attemptPermanentTextureStates.end(); ++it)
        m_permanentTextureStates.insert_or_assign(it->first, it.value());
    for(auto it = m_attemptPermanentBufferStates.begin(); it != m_attemptPermanentBufferStates.end(); ++it)
        m_permanentBufferStates.insert_or_assign(it->first, it.value());

    m_attemptPermanentTextureStates.clear();
    m_attemptPermanentBufferStates.clear();
    m_recordingAttemptActive = false;
}

void StateTracker::setPermanentTextureState(Texture& texture, ResourceStates::Mask state){
    if(state == ResourceStates::Unknown)
        return;

    const auto existing = m_permanentTextureStates.find(&texture);
    if(existing != m_permanentTextureStates.end())
        return;

    if(!m_permanentTextureStates.emplace(
        &texture,
        PermanentTextureStateValue{
            state,
            TextureHandle(&texture, TextureHandle::deleter_type(&texture.m_context.objectArena))
        }
    ).second)
        NWB_ASSERT(false);
}

void StateTracker::setPermanentBufferState(Buffer& buffer, ResourceStates::Mask state){
    if(state == ResourceStates::Unknown)
        return;

    const auto existing = m_permanentBufferStates.find(&buffer);
    if(existing != m_permanentBufferStates.end())
        return;

    if(!m_permanentBufferStates.emplace(
        &buffer,
        PermanentBufferStateValue{
            state,
            BufferHandle(&buffer, BufferHandle::deleter_type(&buffer.m_context.objectArena))
        }
    ).second)
        NWB_ASSERT(false);
}

bool StateTracker::isPermanentTexture(Texture& texture)const{
    return m_permanentTextureStates.find(&texture) != m_permanentTextureStates.end();
}

bool StateTracker::isPermanentBuffer(Buffer& buffer)const{
    return m_permanentBufferStates.find(&buffer) != m_permanentBufferStates.end();
}

ResourceStates::Mask StateTracker::getPermanentTextureState(Texture* texture)const{
    if(!texture)
        return ResourceStates::Unknown;

    const auto existing = m_permanentTextureStates.find(texture);
    return existing != m_permanentTextureStates.end() ? existing.value().state : ResourceStates::Unknown;
}

ResourceStates::Mask StateTracker::getPermanentBufferState(Buffer* buffer)const{
    if(!buffer)
        return ResourceStates::Unknown;

    const auto existing = m_permanentBufferStates.find(buffer);
    return existing != m_permanentBufferStates.end() ? existing.value().state : ResourceStates::Unknown;
}

ResourceStates::Mask StateTracker::getTextureState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const{
    if(!texture)
        return ResourceStates::Unknown;

    auto permIt = m_permanentTextureStates.find(texture);
    if(permIt != m_permanentTextureStates.end())
        return permIt.value().state;

    ResourceStates::Mask state = ResourceStates::Unknown;
    return getTransientTextureState(*texture, arraySlice, mipLevel, state) ? state : ResourceStates::Unknown;
}

ResourceStates::Mask StateTracker::getBufferState(Buffer* buffer)const{
    if(!buffer)
        return ResourceStates::Unknown;

    auto permIt = m_permanentBufferStates.find(buffer);
    if(permIt != m_permanentBufferStates.end())
        return permIt.value().state;

    ResourceStates::Mask state = ResourceStates::Unknown;
    return getTransientBufferState(*buffer, state) ? state : ResourceStates::Unknown;
}

bool StateTracker::hasExplicitTextureSubresourceState(
    Texture* const texture,
    const ArraySlice arraySlice,
    const MipLevel mipLevel
)const{
    if(!texture)
        return false;
    if(m_permanentTextureStates.find(texture) != m_permanentTextureStates.end())
        return true;
    return m_textureStates.find(TextureSubresourceStateKey{ texture, mipLevel, arraySlice }) != m_textureStates.end();
}

bool StateTracker::hasExplicitBufferState(Buffer* const buffer)const{
    if(!buffer)
        return false;
    return m_permanentBufferStates.find(buffer) != m_permanentBufferStates.end()
        || m_bufferStates.find(buffer) != m_bufferStates.end()
    ;
}

bool StateTracker::getTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;

    const TextureDesc& desc = texture.getDescription();
    if(mipLevel >= desc.mipLevels || arraySlice >= desc.arraySize)
        return false;

    return getResolvedTransientTextureState(texture, arraySlice, mipLevel, outState);
}

bool StateTracker::getResolvedTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;

    const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
    auto it = m_textureStates.find(key);
    if(it != m_textureStates.end()){
        outState = it.value();
        return true;
    }

    if(texture.isRetainedSubresourceStateKnown(arraySlice, mipLevel))
        outState = texture.m_desc.initialState;

    return true;
}

bool StateTracker::getTransientBufferState(Buffer& buffer, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;

    auto it = m_bufferStates.find(&buffer);
    if(it != m_bufferStates.end()){
        outState = it.value();
        return true;
    }

    const BufferDesc& desc = buffer.getDescription();
    if(desc.keepInitialState)
        outState = desc.initialState;

    return true;
}

void StateTracker::beginTrackingTexture(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    if(!texture)
        return;

    if(m_permanentTextureStates.find(texture) != m_permanentTextureStates.end())
        return;

    beginTrackingTransientTexture(*texture, subresources, state);
}

void StateTracker::beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state){
    if(!buffer)
        return;

    if(m_permanentBufferStates.find(buffer) != m_permanentBufferStates.end())
        return;

    beginTrackingTransientBuffer(*buffer, state);
}

void StateTracker::appendKeepInitialStateBarriers(
    TrackedCommandBuffer& commandBuffer,
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& imageBarriers,
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& bufferBarriers
){
    for(auto it = m_textureStates.begin(); it != m_textureStates.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        if(!key.texture)
            continue;

        const TextureDesc& desc = key.texture->getDescription();
        const ResourceStates::Mask currentState = it.value();
        if(!desc.keepInitialState)
            continue;

        auto* texture = key.texture;
        if(currentState == desc.initialState){
            commandBuffer.appendRetainedTextureStateCommit(*texture, key.mipLevel, key.arraySlice);
            continue;
        }

        imageBarriers.push_back(VulkanStateTrackingDetail::BuildTextureStateBarrier(
            texture->m_image,
            texture->m_aspectMask,
            TextureSubresourceSet(key.mipLevel, 1u, key.arraySlice, 1u),
            currentState,
            desc.initialState,
            m_context.extensions.KHR_ray_tracing_pipeline
        ));
        it.value() = desc.initialState;
        commandBuffer.appendRetainedTextureStateCommit(*texture, key.mipLevel, key.arraySlice);
    }

    for(auto it = m_bufferStates.begin(); it != m_bufferStates.end(); ++it){
        Buffer* bufferResource = it->first;
        if(!bufferResource)
            continue;

        const BufferDesc& desc = bufferResource->getDescription();
        const ResourceStates::Mask currentState = it.value();
        if(!desc.keepInitialState || currentState == desc.initialState)
            continue;

        auto* buffer = bufferResource;
        auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
        barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(currentState != ResourceStates::Unknown ? currentState : ResourceStates::Common, m_context.extensions.KHR_ray_tracing_pipeline);
        barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(currentState != ResourceStates::Unknown ? currentState : ResourceStates::Common);
        barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(desc.initialState, m_context.extensions.KHR_ray_tracing_pipeline);
        barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(desc.initialState);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer->m_buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(barrier);
        commandBuffer.retainResource(*buffer);
        it.value() = desc.initialState;
    }
}

bool StateTracker::isUavBarrierEnabledForTexture(Texture& texture)const{
    const auto found = m_textureUavBarriers.find(&texture);
    return found == m_textureUavBarriers.end() || found.value();
}

bool StateTracker::isUavBarrierEnabledForBuffer(Buffer& buffer)const{
    const auto found = m_bufferUavBarriers.find(&buffer);
    return found == m_bufferUavBarriers.end() || found.value().enableBarriers;
}

void StateTracker::beginTrackingTransientTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    beginTrackingResolvedTransientTexture(texture, resolvedSubresources, state);
}

void StateTracker::beginTrackingResolvedTransientTexture(Texture& texture, const TextureSubresourceSet& resolvedSubresources, ResourceStates::Mask state){
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);

    ::ContainerDetail::ReserveAdditionalCapacity(m_textureStates, subresourceCount);

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
            m_textureStates.insert_or_assign(key, state);
        }
    }
}

void StateTracker::beginTrackingTransientBuffer(Buffer& buffer, ResourceStates::Mask state){
    m_bufferStates.insert_or_assign(&buffer, state);
}

void StateTracker::setEnableUavBarriersForTexture(Texture& texture, bool enableBarriers){
    m_textureUavBarriers.insert_or_assign(&texture, enableBarriers);
}

void StateTracker::setEnableUavBarriersForBuffer(Buffer& buffer, bool enableBarriers){
    auto found = m_bufferUavBarriers.find(&buffer);
    if(found != m_bufferUavBarriers.end()){
        found.value().enableBarriers = enableBarriers;
        return;
    }

    if(!m_bufferUavBarriers.emplace(
        &buffer,
        BufferUavBarrierPolicyValue{
            enableBarriers,
            BufferHandle(&buffer, BufferHandle::deleter_type(&m_context.objectArena))
        }
    ).second)
        NWB_ASSERT(false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List Tracking Accessors


void CommandList::setEnableUavBarriersForTexture(Texture* texture, bool enableBarriers){
    if(!texture)
        return;
    m_stateTracker.setEnableUavBarriersForTexture(*texture, enableBarriers);
}

void CommandList::setEnableUavBarriersForBuffer(Buffer* buffer, bool enableBarriers){
    if(!buffer)
        return;
    constexpr const tchar* s_OperationName = NWB_TEXT("set buffer UAV-barrier policy");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    if(!m_device.isBufferReadyForGpuUse(buffer)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("buffer is not ready for GPU access"));
        return;
    }
    m_stateTracker.setEnableUavBarriersForBuffer(*buffer, enableBarriers);
}

void CommandList::beginTrackingTextureState(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    m_stateTracker.beginTrackingTexture(texture, subresources, stateBits);
}

void CommandList::beginTrackingBufferState(Buffer* buffer, ResourceStates::Mask stateBits){
    if(!buffer)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("begin tracking buffer state")))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(NWB_TEXT("begin tracking buffer state"), NWB_TEXT("initial state cannot be unknown"));
        return;
    }
    if(!m_device.isBufferReadyForGpuUse(buffer)){
        rejectCommandRecording(NWB_TEXT("begin tracking buffer state"), NWB_TEXT("buffer is not ready for GPU access"));
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        rejectCommandRecording(
            NWB_TEXT("begin tracking buffer state"),
            NWB_TEXT("initial state conflicts with the permanent buffer state")
        );
        return;
    }

    m_stateTracker.beginTrackingBuffer(buffer, stateBits);
    retainResource(buffer);
}

ResourceStates::Mask CommandList::getTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel){
    return m_stateTracker.getTextureState(texture, arraySlice, mipLevel);
}

ResourceStates::Mask CommandList::getBufferState(Buffer* buffer){
    return m_stateTracker.getBufferState(buffer);
}

ResourceStates::Mask CommandList::getPermanentTextureState(Texture* texture)const{
    return m_stateTracker.getPermanentTextureState(texture);
}

ResourceStates::Mask CommandList::getPermanentBufferState(Buffer* buffer)const{
    return m_stateTracker.getPermanentBufferState(buffer);
}

bool CommandList::hasExplicitTextureSubresourceState(
    Texture* const texture,
    const ArraySlice arraySlice,
    const MipLevel mipLevel
)const{
    return m_stateTracker.hasExplicitTextureSubresourceState(texture, arraySlice, mipLevel);
}

bool CommandList::hasExplicitBufferState(Buffer* const buffer)const{
    return m_stateTracker.hasExplicitBufferState(buffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

