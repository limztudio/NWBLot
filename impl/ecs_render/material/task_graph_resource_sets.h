// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/sampled_texture_graph_resources.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>

#include <global/allocation_size.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GatherPreparedMaterialGeometryUses(
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& outResourceUses
){
    outResourceUses.clear();
    if(drawItemSetCount != 0u && !drawItemSets)
        return false;

    constexpr usize s_MaxDrawItemCount = Limit<usize>::s_Max / NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    usize drawItemCount = 0u;
    for(usize drawItemSetIndex = 0u; drawItemSetIndex < drawItemSetCount; ++drawItemSetIndex){
        const MaterialPassDrawItems* const drawItems = drawItemSets[drawItemSetIndex];
        if(!drawItems)
            return false;
        for(const usize count : { drawItems->meshDrawItems.size(), drawItems->computeDrawItems.size() }){
            if(count > s_MaxDrawItemCount - drawItemCount)
                return false;
            drawItemCount += count;
        }
    }
    if(drawItemCount == 0u)
        return true;

    using MeshSourceRef = NotNull<const MaterialPassMeshResourceSnapshot*>;
    Vector<MeshSourceRef, Core::Alloc::ScratchArena> uniqueMeshes{ scratchArena };
    uniqueMeshes.reserve(drawItemCount);
    {
        // Draw snapshots own these buffers for the whole call. Identify a complete source tuple before reserving
        // per-buffer storage, so repeated instances cost one pointer each instead of a full buffer-table entry.
        const auto hashSources = [](const MeshSourceRef& mesh){
            usize hash = 0u;
            ForEachMaterialPassMeshSourceBuffer(*mesh, [&](const Core::BufferHandle& buffer){
                HashCombine(hash, buffer.get());
            });
            return hash;
        };
        const auto equalSources = [](const MeshSourceRef& lhs, const MeshSourceRef& rhs){
            if(lhs.get() == rhs.get())
                return true;

            Core::Buffer* rhsBuffers[NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT] = {};
            usize sourceIndex = 0u;
            ForEachMaterialPassMeshSourceBuffer(*rhs, [&](const Core::BufferHandle& buffer){
                rhsBuffers[sourceIndex++] = buffer.get();
            });
            sourceIndex = 0u;
            bool equal = true;
            ForEachMaterialPassMeshSourceBuffer(*lhs, [&](const Core::BufferHandle& buffer){
                equal = (buffer.get() == rhsBuffers[sourceIndex++]) && equal;
            });
            return equal;
        };
        using MeshSourceSet = HashSet<MeshSourceRef, RemoveConst_T<decltype(hashSources)>, RemoveConst_T<decltype(equalSources)>, Core::Alloc::ScratchArena>;
        Optional<MeshSourceSet> meshSources;
        if(drawItemCount > 1u)
            meshSources.emplace(AddSize(drawItemCount, drawItemCount), hashSources, equalSources, scratchArena);
        const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
            const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
            // Descriptors and counts belong to each draw and must also be valid on a repeated source tuple.
            if(!mesh.valid())
                return false;
            if(!meshSources || meshSources->insert(MeshSourceRef(&mesh)).second)
                uniqueMeshes.push_back(MeshSourceRef(&mesh));
            return true;
        };
        for(usize drawItemSetIndex = 0u; drawItemSetIndex < drawItemSetCount; ++drawItemSetIndex){
            const MaterialPassDrawItems* const drawItems = drawItemSets[drawItemSetIndex];
            for(const MaterialPassDrawItem& drawItem : drawItems->meshDrawItems){
                if(!appendDrawItem(drawItem))
                    return false;
            }
            for(const MaterialPassDrawItem& drawItem : drawItems->computeDrawItems){
                if(!appendDrawItem(drawItem))
                    return false;
            }
        }
    }

    const usize sourceBufferCapacity = uniqueMeshes.size() * NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    outResourceUses.reserve(sourceBufferCapacity);
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> sourceBuffers{ scratchArena };
    sourceBuffers.reserve(sourceBufferCapacity);
    {
        HashSet<Core::Buffer*, Hasher<Core::Buffer*>, EqualTo<Core::Buffer*>, Core::Alloc::ScratchArena> sourceBufferIdentities(
            AddSize(sourceBufferCapacity, sourceBufferCapacity), Hasher<Core::Buffer*>(), EqualTo<Core::Buffer*>(), scratchArena
        );
        for(const MeshSourceRef& mesh : uniqueMeshes){
            ForEachMaterialPassMeshSourceBuffer(*mesh, [&](const Core::BufferHandle& buffer){
                if(sourceBufferIdentities.insert(buffer.get()).second)
                    sourceBuffers.push_back(buffer);
            });
        }
    }

    outResourceUses.resize(sourceBuffers.size());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid()){
            outResourceUses.clear();
            return false;
        }
        for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex)
            outResourceUses[bufferIndex].resource = declarations.findImportedBuffer(sourceBuffers[bufferIndex]);
    }
    for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex){
        const Core::BufferHandle& buffer = sourceBuffers[bufferIndex];
        Core::GpuGraphResourceId resource = outResourceUses[bufferIndex].resource;
        if(!resource.valid()){
            const Name identity = buffer->getCreationDescription().debugName;
            if(!identity){
                outResourceUses.clear();
                return false;
            }
            resource = graph.importBuffer(buffer, BufferResourceDesc(identity, "Prepared Material Geometry"));
        }
        if(!resource.valid()){
            outResourceUses.clear();
            return false;
        }
        outResourceUses[bufferIndex] = ReadUse(resource, Core::ResourceStates::ShaderResource);
    }
    return true;
}

// Material geometry is dynamically enumerable from the frozen draw snapshot. Keep collection/import compatibility in
// the established helper above, then give the graph one immutable named collection so a consuming task can declare
// the whole bindless geometry set without retaining its own per-buffer use list.
[[nodiscard]] inline bool GatherPreparedMaterialGeometryResourceSet(
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    if(!GatherPreparedMaterialGeometryUses(
        graph,
        drawItemSets,
        drawItemSetCount,
        scratchArena,
        resourceUses
    ))
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(resourceUses.size());
    for(const Core::GpuTaskResourceUse& use : resourceUses)
        members.push_back(use.resource);

    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}


[[nodiscard]] inline bool GatherRegularSharedComputeEmulationResource(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan,
    const AStringView label,
    Core::GpuGraphResourceId& outResource
){
    outResource = {};
    if(!plan.captured || !plan.outputBuffer)
        return false;

    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return false;
        outResource = declarations.findImportedBuffer(plan.outputBuffer);
    }
    if(!outResource.valid()){
        const Name identity = plan.outputBuffer->getCreationDescription().debugName;
        if(!identity)
            return false;
        outResource = graph.importBuffer(plan.outputBuffer, BufferResourceDesc(identity, label));
    }
    return outResource.valid();
}


[[nodiscard]] inline bool GatherPreparedMaterialSampledTextureResourceSet(
    RendererMaterialSystem& materialSystem,
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena> sampledTextures{ scratchArena };
    if(!materialSystem.gatherPreparedMaterialPassSampledTextures(
        drawItemSets,
        drawItemSetCount,
        sampledTextures,
        scratchArena
    ))
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(sampledTextures.size());
    if(ImportMaterialSampledTextureResources(
        graph, sampledTextures.data(), sampledTextures.size(), "Prepared Material Sampled Texture", members
    ) != SampledTextureImportResult::Success)
        return false;
    if(members.empty())
        return true;

    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

