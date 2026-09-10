// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_trace_geometry.h"

#include <impl/ecs_render/mesh/mesh_system.h>

#include <core/graphics/vulkan/backend.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_trace_geometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshSnapshot = ECSRenderDetail::MeshRayTracingResourceSnapshot;
inline constexpr usize s_MissingIndex = Limit<usize>::s_Max;
inline constexpr u32 s_PositionMember = 0u;
inline constexpr u32 s_IndexMember = 1u;
inline constexpr u32 s_AttributeMember = 2u;
inline constexpr u32 s_NodeMember = 3u;
inline constexpr Core::BufferHandle MeshSnapshot::* s_BufferMembers[] = {
    &MeshSnapshot::positionBuffer,
    &MeshSnapshot::triangleIndexBuffer,
    &MeshSnapshot::attributeBuffer,
    &MeshSnapshot::swBvhNodeBuffer,
};

struct BufferRecord{
    usize firstMesh[LengthOf(s_BufferMembers)] = { s_MissingIndex, s_MissingIndex, s_MissingIndex, s_MissingIndex };
    usize preparedIndex = s_MissingIndex;
    bool accepted = false;
};

class BufferIndex final : NoCopy{
private:
    struct InlineEntry{
        Core::Buffer* buffer = nullptr;
        BufferRecord record;
    };
    using IndexMap = HashMap<Core::Buffer*, BufferRecord, Core::Alloc::ScratchArena>;


private:
    static constexpr u32 s_InlineCapacity = 32u;


public:
    BufferIndex(Core::Alloc::ScratchArena& scratchArena, const usize maximumBucketCount)
        : m_scratchArena(scratchArena)
        , m_maximumBucketCount(maximumBucketCount)
    {}


public:
    void add(Core::Buffer* const buffer, const u32 memberIndex, const usize meshIndex){
        BufferRecord& record = findOrAppend(buffer);
        if(record.firstMesh[memberIndex] == s_MissingIndex)
            record.firstMesh[memberIndex] = meshIndex;
    }

    [[nodiscard]] BufferRecord* find(Core::Buffer* const buffer){
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
            if(m_inlineEntries.size() < s_InlineCapacity){
                m_inlineEntries.push_back(InlineEntry{ .buffer = buffer, .record = {} });
                return m_inlineEntries.back().record;
            }
            m_map.emplace(m_maximumBucketCount, IndexMap::hasher{}, IndexMap::key_equal{}, IndexMap::allocator_type(m_scratchArena));
            for(const InlineEntry& entry : m_inlineEntries)
                m_map->emplace(entry.buffer, entry.record);
        }
        return m_map->try_emplace(buffer).first.value();
    }


private:
    FixedVector<InlineEntry, s_InlineCapacity> m_inlineEntries;
    Optional<IndexMap> m_map;
    Core::Alloc::ScratchArena& m_scratchArena;
    usize m_maximumBucketCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FreezePreparedShadowTraceGeometryBuffers(
    const Vector<ECSRenderDetail::MeshRayTracingResourceSnapshot, Core::Alloc::ScratchArena>& meshes,
    const ShadowTraceGeometrySelection& selection,
    Core::Alloc::ScratchArena& scratchArena,
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& acceptedBuffers,
    PreparedShadowTraceGeometryBufferVector& outPrepared){
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

    const auto appendBuffer = [&](
        const Core::BufferHandle& buffer,
        const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh,
        const AStringView identitySuffix,
        const u8 role
    ){
        if(!buffer)
            return false;
        BufferRecord* const record = bufferIndex.find(buffer.get());
        NWB_ASSERT(record);
        if(record->preparedIndex != s_MissingIndex){
            outPrepared[record->preparedIndex].roles |= role;
            return true;
        }

        const Name identity = DeriveName(mesh.meshName, identitySuffix);
        if(!identity)
            return false;
        const bool normalizedByAcceptedPacket = record->accepted;
        outPrepared.push_back(PreparedShadowTraceGeometryBuffer{
            .buffer = buffer,
            .identity = identity,
            // Fresh/preflight-created buffers retain their creation state (normally Common).  Only a buffer that an
            // accepted preparation/prefix tail explicitly normalized may enter the next graph as ShaderResource.
            .initialState = normalizedByAcceptedPacket
                ? Core::ResourceStates::ShaderResource
                : buffer->getCreationDescription().initialState,
            .roles = role,
            .normalizationPending = !normalizedByAcceptedPacket,
        });
        record->preparedIndex = outPrepared.size() - 1u;
        return true;
    };
    const auto appendSelected = [&]<typename SelectedBuffersT>(
        const SelectedBuffersT& selectedBuffers,
        const u32 memberIndex,
        const AStringView identitySuffix,
        const u8 role
    ){
        for(Core::Buffer* const selectedBuffer : selectedBuffers){
            if(!selectedBuffer)
                return false;

            const BufferRecord* const record = bufferIndex.find(selectedBuffer);
            if(!record || record->firstMesh[memberIndex] == s_MissingIndex)
                return false;
            const MeshSnapshot& mesh = meshes[record->firstMesh[memberIndex]];
            if(!appendBuffer(mesh.*s_BufferMembers[memberIndex], mesh, identitySuffix, role))
                return false;
        }
        return true;
    };
    // Shadow Prepare rebuilds runtime and dirty meshes even when they have no scene instance this frame.  Keep those
    // build inputs in the frozen graph set too: otherwise an off-screen build can leave a buffer in BLAS-input/UAV
    // state, then a later frame would import that same physical buffer as Common when it becomes visible.
    const auto appendPendingBlasBuildInputs = [&]{
        for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh : meshes){
            if(!mesh.runtimeMesh && !mesh.blasBuildPending)
                continue;
            if(
                !appendBuffer(
                    mesh.positionBuffer,
                    mesh,
                    AStringView(":shadow_trace_hw_position"),
                    PreparedShadowTraceGeometryRole::HardwarePosition
                )
                || !appendBuffer(
                    mesh.triangleIndexBuffer,
                    mesh,
                    AStringView(":shadow_trace_hw_index"),
                    PreparedShadowTraceGeometryRole::HardwareIndex
                )
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
                !appendBuffer(
                    mesh.swBvhNodeBuffer,
                    mesh,
                    AStringView(":shadow_trace_sw_nodes"),
                    PreparedShadowTraceGeometryRole::SoftwareNode
                )
                || !appendBuffer(
                    mesh.positionBuffer,
                    mesh,
                    AStringView(":shadow_trace_sw_position"),
                    PreparedShadowTraceGeometryRole::SoftwarePosition
                )
                || !appendBuffer(
                    mesh.triangleIndexBuffer,
                    mesh,
                    AStringView(":shadow_trace_sw_index"),
                    PreparedShadowTraceGeometryRole::SoftwareIndex
                )
            )
                return false;
        }
        return true;
    };

    bool collected = true;
    if(selection.includeHardware){
        collected =
            appendSelected(
                selection.hardwarePositions,
                s_PositionMember,
                AStringView(":shadow_trace_hw_position"),
                PreparedShadowTraceGeometryRole::HardwarePosition
            )
            && appendSelected(
                selection.hardwareIndices,
                s_IndexMember,
                AStringView(":shadow_trace_hw_index"),
                PreparedShadowTraceGeometryRole::HardwareIndex
            )
            && appendSelected(
                selection.hardwareAttributes,
                s_AttributeMember,
                AStringView(":shadow_trace_hw_attribute"),
                PreparedShadowTraceGeometryRole::HardwareAttribute
            )
            && appendPendingBlasBuildInputs()
        ;
    }
    if(collected && selection.includeSoftware){
        collected =
            appendSelected(
                selection.softwareNodes,
                s_NodeMember,
                AStringView(":shadow_trace_sw_nodes"),
                PreparedShadowTraceGeometryRole::SoftwareNode
            )
            && appendSelected(
                selection.softwarePositions,
                s_PositionMember,
                AStringView(":shadow_trace_sw_position"),
                PreparedShadowTraceGeometryRole::SoftwarePosition
            )
            && appendSelected(
                selection.softwareIndices,
                s_IndexMember,
                AStringView(":shadow_trace_sw_index"),
                PreparedShadowTraceGeometryRole::SoftwareIndex
            )
            && appendSelected(
                selection.softwareAttributes,
                s_AttributeMember,
                AStringView(":shadow_trace_sw_attribute"),
                PreparedShadowTraceGeometryRole::SoftwareAttribute
            )
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
    // Acceptance only publishes handles whose frozen state was not already normalized. Reserve the full retained
    // union so this noexcept tail remains allocation-free even when invisible accepted meshes occupy existing slots.
    acceptedBuffers.reserve(acceptedCapacity);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

