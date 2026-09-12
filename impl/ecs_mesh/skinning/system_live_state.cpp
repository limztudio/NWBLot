// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "arena_names.h"
#include "graph_resource_uses.h"
#include "live_state_buffers.h"
#include "resource_names.h"
#include "runtime_mesh_liveness.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>
#include <impl/ecs_skeleton/runtime_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningSystem::resolveRestToSkinnedCopyByteCounts(
    const MeshSkinningRuntimeInstance& instance,
    usize& outPositionBytes,
    usize& outNormalBytes,
    usize& outTangentBytes
){
    const auto resolvePayloadBytes = [](const usize count, const usize stride, usize& outBytes, const tchar* label){
        outBytes = 0u;
        if(stride != 0u && TryMultiply<usize>(count, stride, outBytes))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: {} payload byte size overflows"), label);
        return false;
    };
    return
        resolvePayloadBytes(instance.restPositions.size(), sizeof(Float3U), outPositionBytes, NWB_TEXT("rest position"))
        && resolvePayloadBytes(instance.restNormals.size(), sizeof(Half4U), outNormalBytes, NWB_TEXT("rest normal"))
        && resolvePayloadBytes(instance.restTangents.size(), sizeof(Half4U), outTangentBytes, NWB_TEXT("rest tangent"))
    ;
}

void MeshSkinningSystem::collectLiveSkinningStateBuffers(
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers,
    Core::Alloc::ScratchArena& scratchArena
)const{
    MeshSkinningStateBufferCollector collector(scratchArena, outBuffers);
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID, const SkinnedMeshBindingComponent& binding){
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
            collector.collect(instance, resources);
        }
    );
}

bool MeshSkinningSystem::replaceAcceptedSkinningState(
    const Core::CommandListResourceStateHandoff& state,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> liveBuffers(m_arena);
    collectLiveSkinningStateBuffers(liveBuffers, scratchArena);
    return m_acceptedSkinningState.replaceBufferSubset(state, liveBuffers.data(), liveBuffers.size(), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

