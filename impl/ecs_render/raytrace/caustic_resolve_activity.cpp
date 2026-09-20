// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_resolve_activity.h"

#include "raytracing_system.h"
#include "renderer_raytracing_state.h"

#include <impl/assets/graphics/caustic/resolve_binding_slots.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/vulkan/backend.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveCausticActivityLayout(const u32 halfWidth, const u32 halfHeight, CausticResolveActivityLayout& output)noexcept{
    output = {};
    if(halfWidth == 0u || halfHeight == 0u || halfWidth > Limit<i32>::s_Max || halfHeight > Limit<i32>::s_Max)
        return false;
    const u32 tilesX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 tilesY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u64 byteSize = static_cast<u64>(tilesX) * tilesY * sizeof(u32);
    if(byteSize > static_cast<u64>(Limit<u32>::s_Max) + 1u)
        return false;
    output = { byteSize, tilesX, tilesY };
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CausticResolveActivitySnapshot::valid()const noexcept{
    if(halfWidth == 0u || halfHeight == 0u)
        return false;
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        if(
            !buffers[index] || !descriptors[index].valid()
            || descriptors[index].descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        )
            return false;
    }
    return true;
}

bool CausticResolveActivitySnapshot::matches(const CausticResolveActivitySnapshot& other)const noexcept{
    if(halfWidth != other.halfWidth || halfHeight != other.halfHeight)
        return false;
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        if(buffers[index] != other.buffers[index] || descriptors[index] != other.descriptors[index])
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::releaseCausticResolveActivity(){
    auto& activity = m_rayTracingState.m_causticResolve.m_activity;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        for(const Core::GpuDescriptorHandle descriptor : activity.descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
    }
    activity = {};
}

bool RendererRayTracingSystem::prepareCausticResolveActivity(const u32 halfWidth, const u32 halfHeight){
    const auto& current = m_rayTracingState.m_causticResolve.m_activity;
    if(current.valid() && current.halfWidth == halfWidth && current.halfHeight == halfHeight)
        return true;
    releaseCausticResolveActivity();
    CausticResolveActivityLayout layout;
    if(!ResolveCausticActivityLayout(halfWidth, halfHeight, layout))
        return false;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    auto& activity = m_rayTracingState.m_causticResolve.m_activity;
    const Name names[] = { Name("caustic_resolve_activity_a"), Name("caustic_resolve_activity_b") };
    for(u32 index = 0u; index < LengthOf(activity.buffers); ++index){
        Core::BufferDesc desc;
        desc
            .setByteSize(layout.bufferByteSize)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
            .setDebugName(names[index])
        ;
        activity.buffers[index] = m_graphics.createBuffer(desc);
        if(activity.buffers[index])
            activity.descriptors[index] = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(
            !activity.buffers[index] || !activity.descriptors[index].valid()
            || !heap.write(activity.descriptors[index], Core::DescriptorWriteItem::RawBuffer_UAV(0u, activity.buffers[index].get()))
        ){
            releaseCausticResolveActivity();
            return false;
        }
    }
    activity.halfWidth = halfWidth;
    activity.halfHeight = halfHeight;
    return true;
}

CausticResolveActivitySnapshot RendererRayTracingSystem::causticResolveActivitySnapshot(const DeferredFrameTargets& targets)const{
    const auto& activity = m_rayTracingState.m_causticResolve.m_activity;
    if(
        !activity.valid()
        || activity.halfWidth != DivideUp(targets.width, 2u)
        || activity.halfHeight != DivideUp(targets.height, 2u)
    )
        return {};
    return activity;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

