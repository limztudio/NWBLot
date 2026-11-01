// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "live_state_buffers.h"

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshSkinningRestToSkinnedCopyByteCounts> MeshSkinningSystem::ResolveRestToSkinnedCopyByteCounts(const MeshSkinningRuntimeInstance& instance){
    MeshSkinningRestToSkinnedCopyByteCounts counts;
    if(!TryResolveMeshSkinningRestToSkinnedCopyByteCounts(instance.restPositions.size(), instance.restNormals.size(), instance.restTangents.size(), counts)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: rest stream payload byte size is empty or overflows"));
        return MakeUnexpected(Failure{});
    }
    return counts;
}

void MeshSkinningSystem::collectLiveSkinningStateBuffers(Core::Alloc::ScratchArena& scratchArena, Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers)const{
    MeshSkinningStateBufferCollector collector(scratchArena, outBuffers);
    m_world.view<SkinnedMeshBindingComponent>().each([&](Core::ECS::EntityID, const SkinnedMeshBindingComponent& binding){
        if(!binding.runtimeMesh.valid())
            return;

        const MeshSkinningRuntimeInstance* const instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
        if(!instance)
            return;

        MeshSkinningStateBufferResources resources;
        const auto foundResources = m_runtimeResources.find(instance->handle.value);
        if(foundResources != m_runtimeResources.end()){
            resources = {
                .editRevision = foundResources.value().editRevision,
                .skinBuffer = &foundResources.value().skinBuffer,
                .jointPaletteBuffer = &foundResources.value().jointPaletteBuffer,
                .bindlessResourceSlotsBuffer = &foundResources.value().bindlessResourceSlotsBuffer,
            };
        }
        collector.collect(*instance, resources);
    });
}

b8 MeshSkinningSystem::replaceAcceptedSkinningState(const Core::CommandListResourceStateHandoff& state, Core::Alloc::ScratchArena& scratchArena){
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& liveBuffers = m_frameLiveBuffers;
    ScopeExit releaseLiveBuffers([&]()noexcept{ liveBuffers.clear(); });

    collectLiveSkinningStateBuffers(scratchArena, liveBuffers);
    return m_acceptedSkinningState.replaceBufferSubset(state, Span<const Core::BufferHandle>(liveBuffers), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

