// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_csg.h"

#include <impl/ecs_render/shadow/light_space_shadow_diagnostics.adhd.h>

#include <impl/ecs_render/shadow/light_space_capture_identity_builder.adhd.h>

#include <impl/ecs_render/shadow/light_space_csg_context_packer.adhd.h>

#include <impl/ecs_render/mesh/mesh_system.h>

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/algorithm.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 BuildLightSpaceCsgCaptureIdentity(
    const LightSpaceCsgState& state,
    const Span<const NwbRtInstanceMaterialGpu> materials, const Span<const InstanceGpuData> instances,
    const Span<const u8> materialBytes,
    const Span<const Core::TextureHandle> textures
)noexcept{
    NWB_ASSERT(materials.size() == instances.size());
    u64 identity = LightSpaceCaptureIdentityBuilder().buildCsgBaseIdentity(
        state.captureGeometryIdentity,
        state.snapshot.contentIdentity,
        materials,
        instances,
        materialBytes,
        textures.size()
    );
    for(const Core::TextureHandle& texture : textures)
        Fnv64AppendValue(identity, texture.get());
    return identity;
}

void BeginLightSpaceCsgGather(LightSpaceCsgState& state, Core::ECS::World& world, const usize capacity, const b8 hardware){
    state.gathering = HasCsgFrameCandidates(world);
    state.hardware = hardware;
    state.snapshot.hasCsg = false;
    state.snapshot.identity = 0u;
    state.snapshot.contentIdentity = 0u;
    state.captureGeometryIdentity = s_Fnv64OffsetBasis;
    state.captureGeometryTrusted = true;
    state.snapshot.receiverRanges.clear();
    state.snapshot.cutters.clear();
    state.receivers.clear();
    state.instances.clear();
    state.dynamicBounds.clear();
    state.bytes.clear();
    state.solidTriangleMasks.clear();
    if(!state.gathering)
        return;
    state.receivers.reserve(capacity);
    state.instances.reserve(capacity);
    state.dynamicBounds.reserve(capacity);
    state.solidTriangleMasks.reserve(capacity);
}

void AppendLightSpaceCsgReceiver(
    LightSpaceCsgState& state,
    const Core::ECS::EntityID entity,
    const b8 transparent,
    const SIMDMatrix& objectToWorld,
    const SIMDMatrix& worldToObject,
    const SIMDVectorSingle localMin,
    const SIMDVectorSingle localMax,
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
){
    if(!state.gathering)
        return;
    LightSpaceCaptureIdentityBuilder().updateReceiverGeometry(
        entity,
        transparent,
        mesh.meshName,
        reinterpret_cast<u64>(mesh.positionBuffer.get()),
        reinterpret_cast<u64>(mesh.triangleIndexBuffer.get()),
        reinterpret_cast<u64>(mesh.attributeBuffer.get()),
        mesh.meshletCount,
        reinterpret_cast<u64>(mesh.meshletDescBuffer.get()),
        reinterpret_cast<u64>(mesh.meshletLocalBoundsBuffer.get()),
        mesh.meshletDescHeapHandle.value,
        mesh.meshletLocalBoundsHeapHandle.value,
        reinterpret_cast<u64>(mesh.runtimeLocalBoundsBuffer.get()),
        mesh.runtimeLocalBoundsHeapHandle.value,
        reinterpret_cast<u64>(mesh.swBvhNodeBuffer.get()),
        mesh.swBvhNodeHeapHandle.value,
        mesh.meshletPrimitiveIndexCount,
        mesh.runtimeMeshVersion,
        mesh.runtimeGeometryContentRevision,
        mesh.runtimeMesh,
        state.captureGeometryIdentity,
        state.captureGeometryTrusted
    );

    CsgShadowReceiverInput receiver;
    receiver.entity = entity;
    receiver.receiverPass = transparent ? CsgReceiverPass::Transparent : CsgReceiverPass::Opaque;
    SIMDVectorSingle worldMin{};
    SIMDVectorSingle worldMax{};
    receiver.boundsValid = false;
    if(CsgReceiverBoundsCanCull(mesh.csgLocalBounds) && !mesh.runtimeMesh){
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(worldBounds){
            worldMin = worldBounds->minBounds;
            worldMax = worldBounds->maxBounds;
            receiver.boundsValid = true;
        }
    }
    StoreFloat(worldMin, receiver.worldMin);
    StoreFloat(worldMax, receiver.worldMax);
    state.receivers.push_back(receiver);

    const b8 hasRuntimeBounds = mesh.runtimeLocalBoundsBuffer && mesh.runtimeLocalBoundsHeapHandle.valid();
    if(hasRuntimeBounds)
        state.dynamicBounds.push_back(mesh.runtimeLocalBoundsBuffer);
    state.instances.push_back(BuildLightSpaceCsgInstanceGpu(
        worldToObject,
        localMin,
        localMax,
        mesh.meshletPrimitiveIndexCount,
        hasRuntimeBounds,
        hasRuntimeBounds ? mesh.runtimeLocalBoundsHeapHandle.slot() : s_LightSpaceInvalidGpuDescriptorSlot,
        state.hardware,
        mesh.swBvhNodeHeapHandle.valid(),
        mesh.swBvhNodeHeapHandle.valid() ? mesh.swBvhNodeHeapHandle.slot() : s_LightSpaceInvalidGpuDescriptorSlot
    ));
    state.solidTriangleMasks.push_back(mesh.solidTriangleWords);
}

b8 FinishLightSpaceCsgGather(const CsgShapeRegistry& registry, LightSpaceCsgState& state, Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena){
    if(!state.gathering)
        return true;
    if(state.receivers.size() != state.instances.size())
        return false;
    if(!BuildCsgShadowSnapshot(registry, state.receivers, world, scratchArena, state.snapshot))
        return false;
    if(!state.snapshot.hasCsg)
        return true;
    static_assert(sizeof(LightSpaceCsgContextHeader) == NWB_CSG_RAY_CONTEXT_BYTES);
    static_assert(sizeof(u32) == NWB_CSG_RAY_MEMBERSHIP_WORD_BYTES);
    if(!LightSpaceCsgContextPacker().pack(
        Span<const CsgShadowReceiverRangeGpu>(state.snapshot.receiverRanges.data(), state.snapshot.receiverRanges.size()),
        Span<const CsgCutterGpuData>(state.snapshot.cutters.data(), state.snapshot.cutters.size()),
        Span<const LightSpaceCsgInstanceGpu>(state.instances.data(), state.instances.size()),
        Span<const Span<const u32>>(state.solidTriangleMasks.data(), state.solidTriangleMasks.size()),
        state.bytes
    ))
        return false;
    LightSpaceCaptureIdentityBuilder().updateCsgInstances(
        Span<const LightSpaceCsgInstanceGpu>(state.instances.data(), state.instances.size()),
        state.snapshot.identity
    );
    LightSpaceShadowLogReporter{}.reportUnsupportedLimitations(
        Span<const CsgShadowReceiverRangeGpu>(state.snapshot.receiverRanges.data(), state.snapshot.receiverRanges.size()),
        state.limitationLogged
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

