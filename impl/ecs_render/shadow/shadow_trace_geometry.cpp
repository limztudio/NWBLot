// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_trace_geometry.h"

#include <impl/ecs_render/mesh/mesh_system.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_trace_geometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshSnapshot = ECSRenderDetail::MeshRayTracingResourceSnapshot;
using BufferRecord = ShadowTraceBufferRecord;
inline constexpr Array<Core::BufferHandle MeshSnapshot::*, s_ShadowTraceMemberCount> s_BufferMembers = { &MeshSnapshot::positionBuffer, &MeshSnapshot::triangleIndexBuffer, &MeshSnapshot::attributeBuffer, &MeshSnapshot::swBvhNodeBuffer, };

class BufferIndex final : NoCopy{
private:
    struct InlineEntry{
        Core::Buffer* buffer = nullptr;
        BufferRecord record;
    };


private:
    using IndexMap = HashMap<Core::Buffer*, BufferRecord, Core::Alloc::ScratchArena>;


public:
    BufferIndex(Core::Alloc::ScratchArena& scratchArena, const usize maximumBucketCount)noexcept
        : m_scratchArena(scratchArena)
        , m_maximumBucketCount(maximumBucketCount)
    {}


public:
    void add(Core::Buffer* const buffer, const u32 memberIndex, const usize meshIndex){
        BufferRecord& record = findOrAppend(buffer);
        if(record.firstMesh[memberIndex] == s_ShadowTraceMissingIndex)
            record.firstMesh[memberIndex] = meshIndex;
    }

    [[nodiscard]] BufferRecord* find(Core::Buffer* const buffer)noexcept{
        if(m_map){
            const auto found = m_map->find(buffer);
            return found == m_map->end() ? nullptr : &found.value();
        }
        for(InlineEntry& entry : m_inlineEntries){
            if(entry.buffer == buffer)
                return &entry.record;
        }
        return nullptr;
    }


private:
    [[nodiscard]] BufferRecord& findOrAppend(Core::Buffer* const buffer){
        if(BufferRecord* const found = find(buffer))
            return *found;
        if(!m_map){
            if(m_inlineEntries.size() < s_ShadowTraceInlineCapacity){
                m_inlineEntries.push_back(InlineEntry{ .buffer = buffer, .record = {} });
                return m_inlineEntries.back().record;
            }
            m_map.emplace(m_maximumBucketCount, IndexMap::hasher{}, IndexMap::key_equal{}, IndexMap::allocator_type(m_scratchArena));
            for(const InlineEntry& entry : m_inlineEntries){
                if(!m_map->emplace(entry.buffer, entry.record).second)
                    TerminateInvariant();
            }
        }
        return m_map->try_emplace(buffer).first.value();
    }


private:
    FixedVector<InlineEntry, s_ShadowTraceInlineCapacity> m_inlineEntries;
    Optional<IndexMap> m_map;
    Core::Alloc::ScratchArena& m_scratchArena;
    usize m_maximumBucketCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FreezePreparedShadowTraceGeometryBuffers(
    const Span<const ECSRenderDetail::MeshRayTracingResourceSnapshot> meshes,
    const ShadowTraceGeometrySelection& selection,
    Core::Alloc::ScratchArena& scratchArena,
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& acceptedBuffers,
    PreparedShadowTraceGeometryBufferVector& outPrepared
){
    outPrepared.clear();
    using namespace __hidden_shadow_trace_geometry;
    constexpr usize s_BucketsPerMesh = LengthOf(s_BufferMembers) * 2u;
    if(MultiplyOverflows<usize>(meshes.size(), s_BucketsPerMesh))
        return false;

    BufferIndex bufferIndex(scratchArena, meshes.size() * s_BucketsPerMesh);
    for(usize meshIndex = 0u; meshIndex < meshes.size(); ++meshIndex){
        const MeshSnapshot& mesh = meshes[meshIndex];
        for(u32 memberIndex = 0u; memberIndex < LengthOf(s_BufferMembers); ++memberIndex)
            bufferIndex.add((mesh.*s_BufferMembers[memberIndex]).get(), memberIndex, meshIndex);
    }

    // Keep accepted invisible meshes; prune only resources whose mesh was removed.
    usize retainedCount = 0u;
    for(usize acceptedIndex = 0u; acceptedIndex < acceptedBuffers.size(); ++acceptedIndex){
        BufferRecord* const record = bufferIndex.find(acceptedBuffers[acceptedIndex].get());
        if(!record)
            continue;
        record->accepted = true;
        if(retainedCount != acceptedIndex)
            acceptedBuffers[retainedCount] = Move(acceptedBuffers[acceptedIndex]);
        ++retainedCount;
    }
    acceptedBuffers.resize(retainedCount);

    if(!selection.includeHardware && !selection.includeSoftware)
        return true;

    const auto appendBuffer = [&](const Core::BufferHandle& buffer, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh, const AStringView identitySuffix, const u8 role){
        if(!buffer)
            return false;
        BufferRecord& record = *MakeNotNull(bufferIndex.find(buffer.get()));
        if(record.preparedIndex != s_ShadowTraceMissingIndex){
            outPrepared[record.preparedIndex].roles |= role;
            return true;
        }

        const auto identity = DeriveName(mesh.meshName, identitySuffix);
        if(!identity)
            return false;
        const b8 normalizedByAcceptedPacket = record.accepted;
        outPrepared.push_back(PreparedShadowTraceGeometryBuffer{
            .buffer = buffer,
            .identity = *identity,
            // Fresh buffers keep creation state; only normalized ones enter as ShaderResource.
            .initialState = normalizedByAcceptedPacket ? Core::ResourceStates::ShaderResource : buffer->getCreationDescription().initialState, .roles = role, .normalizationPending = !normalizedByAcceptedPacket,
        });
        record.preparedIndex = outPrepared.size() - 1u;
        return true;
    };
    const auto appendSelected = [&](const Span<const NotNull<Core::Buffer*>> selectedBuffers, const u32 memberIndex, const AStringView identitySuffix, const u8 role){
        for(const NotNull<Core::Buffer*> selectedBuffer : selectedBuffers){
            const BufferRecord* const record = bufferIndex.find(selectedBuffer.get());
            if(!record || record->firstMesh[memberIndex] == s_ShadowTraceMissingIndex)
                return false;
            const MeshSnapshot& mesh = meshes[record->firstMesh[memberIndex]];
            if(!appendBuffer(mesh.*s_BufferMembers[memberIndex], mesh, identitySuffix, role))
                return false;
        }
        return true;
    };
    // Keep off-screen rebuild inputs frozen so later frames never import stale states.
    const auto appendPendingBlasBuildInputs = [&]{
        for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh : meshes){
            if(!mesh.runtimeMesh && !mesh.blasBuildPending)
                continue;
            if(
                !appendBuffer(mesh.positionBuffer, mesh, s_ShadowTraceHardwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::HardwarePosition)
                || !appendBuffer(mesh.triangleIndexBuffer, mesh, s_ShadowTraceHardwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::HardwareIndex)
            )
                return false;
        }
        return true;
    };
    const auto appendPendingSwBvhBuildInputs = [&]{
        for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh : meshes){
            if(!mesh.runtimeMesh && !mesh.swBvhBuildPending)
                continue;
            if(
                !appendBuffer(mesh.swBvhNodeBuffer, mesh, s_ShadowTraceSoftwareNodeIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareNode)
                || !appendBuffer(mesh.positionBuffer, mesh, s_ShadowTraceSoftwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwarePosition)
                || !appendBuffer(mesh.triangleIndexBuffer, mesh, s_ShadowTraceSoftwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareIndex)
            )
                return false;
        }
        return true;
    };

    b8 collected = true;
    if(selection.includeHardware){
        collected =
            appendSelected(selection.hardwarePositions, s_ShadowTracePositionMember, s_ShadowTraceHardwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::HardwarePosition)
            && appendSelected(selection.hardwareIndices, s_ShadowTraceIndexMember, s_ShadowTraceHardwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::HardwareIndex)
            && appendSelected(selection.hardwareAttributes, s_ShadowTraceAttributeMember, s_ShadowTraceHardwareAttributeIdentitySuffix, PreparedShadowTraceGeometryRole::HardwareAttribute)
            && appendPendingBlasBuildInputs()
        ;
    }
    if(collected && selection.includeSoftware){
        collected =
            appendSelected(selection.softwareNodes, s_ShadowTraceNodeMember, s_ShadowTraceSoftwareNodeIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareNode)
            && appendSelected(selection.softwarePositions, s_ShadowTracePositionMember, s_ShadowTraceSoftwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwarePosition)
            && appendSelected(selection.softwareIndices, s_ShadowTraceIndexMember, s_ShadowTraceSoftwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareIndex)
            && appendSelected(selection.softwareAttributes, s_ShadowTraceAttributeMember, s_ShadowTraceSoftwareAttributeIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareAttribute)
            && appendPendingSwBvhBuildInputs()
        ;
    }
    if(!collected){
        outPrepared.clear();
        return false;
    }

    if(AddOverflows<usize>(acceptedBuffers.size(), outPrepared.size())){
        outPrepared.clear();
        return false;
    }
    const usize acceptedCapacity = acceptedBuffers.size() + outPrepared.size();
    if(acceptedCapacity > acceptedBuffers.max_size()){
        outPrepared.clear();
        return false;
    }
    // Acceptance publishes non-normalized handles; reserve the full union to stay allocation-free.
    acceptedBuffers.reserve(acceptedCapacity);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

