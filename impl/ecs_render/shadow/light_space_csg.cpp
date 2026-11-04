// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_csg.h"

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
    const usize instanceCount = instances.size();
    NWB_ASSERT(materials.size() == instanceCount);
    u64 identity = state.captureGeometryIdentity;
    Fnv64AppendValue(identity, state.snapshot.contentIdentity);
    Fnv64AppendValue(identity, instanceCount);
    for(usize index = 0u; index < instanceCount; ++index){
        Fnv64AppendValue(identity, materials[index]);
        Fnv64AppendValue(identity, instances[index].translation.w);
        Fnv64AppendValue(identity, instances[index].geometryHeapSlots);
    }
    Fnv64AppendBuffer(identity, materialBytes);
    Fnv64AppendValue(identity, textures.size());
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
    Fnv64AppendValue(state.captureGeometryIdentity, entity.id);
    Fnv64AppendBool(state.captureGeometryIdentity, transparent);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.meshName.hash());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.positionBuffer.get());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.triangleIndexBuffer.get());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.attributeBuffer.get());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.meshletCount);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.meshletDescBuffer.get());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.meshletLocalBoundsBuffer.get());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.meshletDescHeapHandle.value);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.meshletLocalBoundsHeapHandle.value);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.runtimeLocalBoundsBuffer.get());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.runtimeLocalBoundsHeapHandle);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.swBvhNodeBuffer.get());
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.swBvhNodeHeapHandle);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.meshletPrimitiveIndexCount);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.runtimeMeshVersion);
    Fnv64AppendValue(state.captureGeometryIdentity, mesh.runtimeGeometryContentRevision);
    Fnv64AppendBool(state.captureGeometryIdentity, mesh.runtimeMesh);
    if(mesh.runtimeMesh && mesh.runtimeGeometryContentRevision == 0u)
        state.captureGeometryTrusted = false;

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
    LightSpaceCsgInstanceGpu instance = BuildLightSpaceCsgInstanceGpu(
        mesh.meshletPrimitiveIndexCount,
        hasRuntimeBounds,
        hasRuntimeBounds ? mesh.runtimeLocalBoundsHeapHandle.slot() : s_LightSpaceInvalidGpuDescriptorSlot,
        state.hardware,
        mesh.swBvhNodeHeapHandle.valid(),
        mesh.swBvhNodeHeapHandle.valid() ? mesh.swBvhNodeHeapHandle.slot() : s_LightSpaceInvalidGpuDescriptorSlot
    );
    StoreFloat(worldToObject, instance.worldToObject);
    StoreFloat(localMin, instance.localMin);
    StoreFloat(localMax, instance.localMax);
    state.instances.push_back(instance);
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
    if(state.snapshot.receiverRanges.size() != state.instances.size())
        return false;
    if(state.solidTriangleMasks.size() != state.instances.size())
        return false;
    const usize rangeBytes = state.snapshot.receiverRanges.size() * sizeof(CsgShadowReceiverRangeGpu);
    const usize cutterBytes = state.snapshot.cutters.size() * sizeof(CsgCutterGpuData);
    const usize instanceBytes = state.instances.size() * sizeof(LightSpaceCsgInstanceGpu);
    u32 activeReceiverCount = 0u;
    for(const auto& range : state.snapshot.receiverRanges)
        if((range.flags & NWB_CSG_RAY_RECEIVER_ACTIVE) != 0u)
            ++activeReceiverCount;
    const usize activeReceiverBytes = static_cast<usize>(activeReceiverCount) * sizeof(u32);
    const usize activeReceiverOffset = NWB_CSG_RAY_CONTEXT_BYTES + rangeBytes + cutterBytes + instanceBytes;
    usize totalBytes = activeReceiverOffset + activeReceiverBytes;
    if(totalBytes > Limit<u32>::s_Max)
        return false;
    const usize membershipOffset = totalBytes;
    static_assert(sizeof(u32) == NWB_CSG_RAY_MEMBERSHIP_WORD_BYTES);
    for(usize index = 0u; index < state.instances.size(); ++index){
        if((state.snapshot.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) == 0u)
            continue;
        const Span<const u32> words = state.solidTriangleMasks[index];
        const usize requiredWords = DivideUpWrapping(static_cast<usize>(state.instances[index].primitiveCount), NWB_CSG_RAY_MEMBERSHIP_WORD_BITS);
        if(words.size() != requiredWords || words.empty() || words.size_bytes() > Limit<u32>::s_Max - totalBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG receiver has no valid cooked solid topology or exceeds ray context capacity"));
            return false;
        }
        totalBytes += words.size_bytes();
    }
    state.bytes.resize(totalBytes);
    const LightSpaceCsgContextHeader header = BuildLightSpaceCsgContextHeader(
        state.instances.size(),
        rangeBytes,
        cutterBytes,
        state.snapshot.cutters.size(),
        activeReceiverOffset,
        activeReceiverCount
    );
    static_assert(sizeof(header) == NWB_CSG_RAY_CONTEXT_BYTES);
    NWB_MEMCPY(state.bytes.data(), state.bytes.size(), &header, sizeof(header));
    NWB_MEMCPY(state.bytes.data() + NWB_CSG_RAY_CONTEXT_BYTES, rangeBytes, state.snapshot.receiverRanges.data(), rangeBytes);
    if(cutterBytes != 0u)
        NWB_MEMCPY(state.bytes.data() + NWB_CSG_RAY_CONTEXT_BYTES + rangeBytes, cutterBytes, state.snapshot.cutters.data(), cutterBytes);
    NWB_MEMCPY(state.bytes.data() + NWB_CSG_RAY_CONTEXT_BYTES + rangeBytes + cutterBytes, instanceBytes, state.instances.data(), instanceBytes);
    usize activeOffset = activeReceiverOffset;
    usize maskOffset = membershipOffset;
    for(u32 index = 0u; index < static_cast<u32>(state.snapshot.receiverRanges.size()); ++index){
        if((state.snapshot.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) == 0u)
            continue;
        NWB_MEMCPY(state.bytes.data() + activeOffset, sizeof(index), &index, sizeof(index));
        activeOffset += sizeof(index);
        const Span<const u32> words = state.solidTriangleMasks[index];
        const u32 offset = static_cast<u32>(maskOffset);
        const usize rangeOffset = NWB_CSG_RAY_CONTEXT_BYTES + index * sizeof(CsgShadowReceiverRangeGpu) + offsetof(CsgShadowReceiverRangeGpu, solidTriangleWordOffset);
        NWB_MEMCPY(state.bytes.data() + rangeOffset, sizeof(offset), &offset, sizeof(offset));
        NWB_MEMCPY(state.bytes.data() + maskOffset, words.size_bytes(), words.data(), words.size_bytes());
        maskOffset += words.size_bytes();
    }
    Fnv64AppendBuffer(state.snapshot.identity, Span<const u8>(static_cast<const u8*>(static_cast<const void*>(state.instances.data())), instanceBytes));
    if(!state.limitationLogged){
        for(const auto& range : state.snapshot.receiverRanges){
            if((range.flags & NWB_CSG_RAY_RECEIVER_UNSUPPORTED) == 0u)
                continue;
            state.limitationLogged = true;
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: unsupported or over-budget CSG cutters retain conservative shadows and suppress GI bounce radiance"));
            break;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

