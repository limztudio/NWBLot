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


[[nodiscard]] inline Expected<Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>> GatherPreparedMaterialGeometryUses(
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses(scratchArena);
    if(drawItemSetCount != 0u && !drawItemSets)
        return MakeUnexpected(Failure{});

    // Indexed raster reads its decoded cache; only mesh/compute stages and cache producers consume source streams.
    constexpr usize s_MaxDrawItemCount = Limit<usize>::s_Max / NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    usize drawItemCount = 0u;
    for(usize drawItemSetIndex = 0u; drawItemSetIndex < drawItemSetCount; ++drawItemSetIndex){
        const MaterialPassDrawItems* const drawItems = drawItemSets[drawItemSetIndex];
        if(!drawItems)
            return MakeUnexpected(Failure{});
        for(const usize count : { drawItems->meshDrawItems.size(), drawItems->computeDrawItems.size() }){
            if(count > s_MaxDrawItemCount - drawItemCount)
                return MakeUnexpected(Failure{});
            drawItemCount += count;
        }
    }
    if(drawItemCount == 0u)
        return resourceUses;

    using MeshSourceRef = NotNull<const MaterialPassMeshResourceSnapshot*>;
    Vector<MeshSourceRef, Core::Alloc::ScratchArena> uniqueMeshes{ scratchArena };
    uniqueMeshes.reserve(drawItemCount);
    {
        // Identify source tuples first so repeated instances cost one pointer each.
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
        using MeshSourceSet = HashSet<MeshSourceRef, Core::Alloc::ScratchArena, RemoveConst_T<decltype(hashSources)>, RemoveConst_T<decltype(equalSources)>>;
        Optional<MeshSourceSet> meshSources;
        if(drawItemCount > 1u)
            meshSources.emplace(AddSize(drawItemCount, drawItemCount), hashSources, equalSources, scratchArena);
        const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
            const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
            // Descriptors stay valid on repeated source tuples.
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
                    return MakeUnexpected(Failure{});
            }
            for(const MaterialPassDrawItem& drawItem : drawItems->computeDrawItems){
                if(!appendDrawItem(drawItem))
                    return MakeUnexpected(Failure{});
            }
        }
    }

    const usize sourceBufferCapacity = uniqueMeshes.size() * NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    resourceUses.reserve(sourceBufferCapacity);
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> sourceBuffers{ scratchArena };
    sourceBuffers.reserve(sourceBufferCapacity);
    {
        HashSet<Core::Buffer*, Core::Alloc::ScratchArena, Hasher<Core::Buffer*>, EqualTo<Core::Buffer*>> sourceBufferIdentities(
            AddSize(sourceBufferCapacity, sourceBufferCapacity), Hasher<Core::Buffer*>(), EqualTo<Core::Buffer*>(), scratchArena
        );
        for(const MeshSourceRef& mesh : uniqueMeshes){
            ForEachMaterialPassMeshSourceBuffer(*mesh, [&](const Core::BufferHandle& buffer){
                if(sourceBufferIdentities.insert(buffer.get()).second)
                    sourceBuffers.push_back(buffer);
            });
        }
    }

    resourceUses.resize(sourceBuffers.size());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid()){
            return MakeUnexpected(Failure{});
        }
        for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex)
            resourceUses[bufferIndex].resource = declarations.findImportedBuffer(sourceBuffers[bufferIndex]);
    }
    for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex){
        const Core::BufferHandle& buffer = sourceBuffers[bufferIndex];
        Core::GpuGraphResourceId resource = resourceUses[bufferIndex].resource;
        if(!resource.valid()){
            const Name identity = buffer->getCreationDescription().debugName;
            if(!identity){
                return MakeUnexpected(Failure{});
            }
            resource = graph.importBuffer(buffer, BufferResourceDesc(identity, "Prepared Material Geometry"));
        }
        if(!resource.valid()){
            return MakeUnexpected(Failure{});
        }
        resourceUses[bufferIndex] = ReadUse(resource, Core::ResourceStates::ShaderResource);
    }
    return resourceUses;
}

// Material geometry enumerates from the frozen snapshot; give the graph one immutable collection.
[[nodiscard]] inline Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialGeometryResourceSet(
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label
){
    Core::GpuGraphResourceSetId resource{};
    const auto resourceUses = GatherPreparedMaterialGeometryUses(graph, drawItemSets, drawItemSetCount, scratchArena);
    if(!resourceUses)
        return MakeUnexpected(Failure{});

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(resourceUses->size());
    for(const Core::GpuTaskResourceUse& use : *resourceUses)
        members.push_back(use.resource);

    resource = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    if(!resource.valid())
        return MakeUnexpected(Failure{});
    return resource;
}


[[nodiscard]] inline Expected<Core::GpuGraphResourceId> GatherRegularSharedComputeEmulationResource(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan,
    const AStringView label
){
    Core::GpuGraphResourceId resource{};
    if(!plan.captured || !plan.outputBuffer)
        return MakeUnexpected(Failure{});

    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        resource = declarations.findImportedBuffer(plan.outputBuffer);
    }
    if(!resource.valid()){
        const Name identity = plan.outputBuffer->getCreationDescription().debugName;
        if(!identity)
            return MakeUnexpected(Failure{});
        resource = graph.importBuffer(plan.outputBuffer, BufferResourceDesc(identity, label));
    }
    if(!resource.valid())
        return MakeUnexpected(Failure{});
    return resource;
}


[[nodiscard]] inline Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialSampledTextureResourceSet(
    RendererMaterialSystem& materialSystem,
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label
){
    Core::GpuGraphResourceSetId resource{};
    const auto sampledTextures = materialSystem.gatherPreparedMaterialPassSampledTextures(drawItemSets, drawItemSetCount, scratchArena);
    if(!sampledTextures)
        return MakeUnexpected(Failure{});

    const auto members = ImportMaterialSampledTextureResources(
        graph, sampledTextures->data(), sampledTextures->size(), "Prepared Material Sampled Texture", scratchArena
    );
    if(!members)
        return MakeUnexpected(Failure{});
    if(members->empty())
        return resource;

    resource = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members->data(), members->size())
    );
    if(!resource.valid())
        return MakeUnexpected(Failure{});
    return resource;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

