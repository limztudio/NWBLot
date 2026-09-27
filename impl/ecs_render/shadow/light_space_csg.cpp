// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_csg.h"

#include <impl/ecs_render/mesh/mesh_system.h>

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BeginLightSpaceCsgGather(LightSpaceCsgState& state, Core::ECS::World& world, const usize capacity, const bool hardware){
    state.gathering = HasCsgFrameCandidates(world);
    state.hardware = hardware;
    state.snapshot.hasCsg = false;
    state.snapshot.identity = 0u;
    state.snapshot.receiverRanges.clear();
    state.snapshot.cutters.clear();
    state.receivers.clear();
    state.instances.clear();
    state.dynamicBounds.clear();
    state.bytes.clear();
    if(!state.gathering)
        return;
    state.receivers.reserve(capacity);
    state.instances.reserve(capacity);
    state.dynamicBounds.reserve(capacity);
}

void AppendLightSpaceCsgReceiver(
    LightSpaceCsgState& state, const Core::ECS::EntityID entity, const bool transparent,
    const SIMDMatrix& objectToWorld, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh){
    if(!state.gathering)
        return;
    CsgShadowReceiverInput receiver;
    receiver.entity = entity;
    receiver.receiverPass = transparent ? CsgReceiverPass::Transparent : CsgReceiverPass::Opaque;
    const SIMDVector localMin = LoadFloatInt(mesh.csgLocalBounds.minBounds);
    const SIMDVector localMax = LoadFloatInt(mesh.csgLocalBounds.maxBounds);
    SIMDVector worldMin{};
    SIMDVector worldMax{};
    receiver.boundsValid =
        CsgReceiverBoundsCanCull(mesh.csgLocalBounds) && !mesh.runtimeMesh
        && AabbTests::Transform(objectToWorld, localMin, localMax, worldMin, worldMax)
    ;
    StoreFloat(worldMin, receiver.worldMin);
    StoreFloat(worldMax, receiver.worldMax);
    state.receivers.push_back(receiver);

    LightSpaceCsgInstanceGpu instance;
    SIMDVector determinant;
    StoreFloat(MatrixInverse(&determinant, objectToWorld), instance.worldToObject);
    instance.primitiveCount = mesh.meshletPrimitiveIndexCount / 3u;
    if(mesh.runtimeLocalBoundsBuffer && mesh.runtimeLocalBoundsHeapHandle.valid()){
        instance.runtimeBoundsSlot = mesh.runtimeLocalBoundsHeapHandle.slot();
        state.dynamicBounds.push_back(mesh.runtimeLocalBoundsBuffer);
    }
    if(!state.hardware && mesh.swBvhNodeHeapHandle.valid())
        instance.meshRootSlot = mesh.swBvhNodeHeapHandle.slot();
    StoreFloat(localMin, instance.localMin);
    StoreFloat(localMax, instance.localMax);
    state.instances.push_back(instance);
}

bool FinishLightSpaceCsgGather(
    LightSpaceCsgState& state, Core::ECS::World& world, const CsgShapeRegistry& registry, Core::Alloc::ScratchArena& scratchArena){
    if(!state.gathering)
        return true;
    if(state.receivers.size() != state.instances.size())
        return false;
    if(!BuildCsgShadowSnapshot(world, registry, state.receivers.data(), state.receivers.size(), scratchArena, state.snapshot))
        return false;
    if(!state.snapshot.hasCsg)
        return true;
    if(state.snapshot.receiverRanges.size() != state.instances.size())
        return false;
    const usize rangeBytes = state.snapshot.receiverRanges.size() * sizeof(CsgShadowReceiverRangeGpu);
    const usize cutterBytes = state.snapshot.cutters.size() * sizeof(CsgCutterGpuData);
    const usize instanceBytes = state.instances.size() * sizeof(LightSpaceCsgInstanceGpu);
    constexpr usize s_HeaderBytes = NWB_CSG_SHADOW_CONTEXT_BYTES;
    const usize totalBytes = s_HeaderBytes + rangeBytes + cutterBytes + instanceBytes;
    if(totalBytes > Limit<u32>::s_Max)
        return false;
    state.bytes.resize(totalBytes);
    const u32 header[] = {
        static_cast<u32>(state.instances.size()), static_cast<u32>(s_HeaderBytes),
        static_cast<u32>(s_HeaderBytes + rangeBytes), static_cast<u32>(state.snapshot.cutters.size()),
        static_cast<u32>(s_HeaderBytes + rangeBytes + cutterBytes), 0u, 0u, 0u,
    };
    static_assert(sizeof(header) == s_HeaderBytes);
    NWB_MEMCPY(state.bytes.data(), state.bytes.size(), header, sizeof(header));
    NWB_MEMCPY(state.bytes.data() + s_HeaderBytes, rangeBytes, state.snapshot.receiverRanges.data(), rangeBytes);
    if(cutterBytes != 0u)
        NWB_MEMCPY(state.bytes.data() + s_HeaderBytes + rangeBytes, cutterBytes, state.snapshot.cutters.data(), cutterBytes);
    NWB_MEMCPY(state.bytes.data() + s_HeaderBytes + rangeBytes + cutterBytes, instanceBytes, state.instances.data(), instanceBytes);
    Fnv64AppendBuffer(state.snapshot.identity, reinterpret_cast<const u8*>(state.instances.data()), instanceBytes);
    if(!state.limitationLogged){
        for(const auto& range : state.snapshot.receiverRanges){
            if((range.flags & NWB_CSG_SHADOW_RECEIVER_UNSUPPORTED) == 0u)
                continue;
            state.limitationLogged = true;
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: unsupported or over-budget CSG shadow cutters retain conservative receiver shadows"));
            break;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

