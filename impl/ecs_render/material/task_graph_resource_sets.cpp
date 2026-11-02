// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_sets.h"

#include <global/allocation_size.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>, SampledTextureImportFailure::Enum> ImportMaterialSampledTextureResources(
    const Span<const Core::TextureHandle> textures,
    const AStringView markerLabel,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(scratchArena);
    resources.reserve(textures.size());
    for(const Core::TextureHandle& texture : textures){
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return MakeUnexpected(SampledTextureImportFailure::GraphUnavailable);
            resource = declarations.findImportedTexture(texture);
        }
        if(!resource.valid()){
            if(!texture)
                return MakeUnexpected(SampledTextureImportFailure::MissingIdentity);
            const Name identity = texture->getCreationDescription().name;
            if(!identity)
                return MakeUnexpected(SampledTextureImportFailure::MissingIdentity);
            const auto resourceResult = graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, markerLabel));
            if(!resourceResult)
                return MakeUnexpected(SampledTextureImportFailure::ImportFailed);
            resource = *resourceResult;
        }
        resources.push_back(resource);
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>> GatherPreparedMaterialGeometryUses(const Span<const MaterialPassDrawItemsView> drawItemSets, Core::GpuTaskGraph& graph, Core::Alloc::ScratchArena& scratchArena){
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses(scratchArena);

    // Indexed raster reads its decoded cache; only mesh/compute stages and cache producers consume source streams.
    constexpr usize s_MaxDrawItemCount = Limit<usize>::s_Max / NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    usize drawItemCount = 0u;
    for(const MaterialPassDrawItemsView& drawItems : drawItemSets){
        for(const usize count : { drawItems.meshDrawItems.size(), drawItems.computeDrawItems.size() }){
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
        const auto hashSources = [](const MeshSourceRef& mesh)noexcept{
            usize hash = 0u;
            ForEachMaterialPassMeshSourceBuffer(*mesh, [&](const Core::BufferHandle& buffer)noexcept{
                HashCombine(hash, buffer.get());
            });
            return hash;
        };
        const auto equalSources = [](const MeshSourceRef& lhs, const MeshSourceRef& rhs)noexcept{
            return lhs.get() == rhs.get() || lhs->sourceBuffers.sameBuffers(rhs->sourceBuffers);
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
        for(const MaterialPassDrawItemsView& drawItems : drawItemSets){
            for(const MaterialPassDrawItem& drawItem : drawItems.meshDrawItems){
                if(!appendDrawItem(drawItem))
                    return MakeUnexpected(Failure{});
            }
            for(const MaterialPassDrawItem& drawItem : drawItems.computeDrawItems){
                if(!appendDrawItem(drawItem))
                    return MakeUnexpected(Failure{});
            }
        }
    }

    const usize sourceBufferCapacity = uniqueMeshes.size() * NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    resourceUses.reserve(sourceBufferCapacity);
    using BufferRef = NotNull<const Core::BufferHandle*>;
    Vector<BufferRef, Core::Alloc::ScratchArena> sourceBuffers{ scratchArena };
    sourceBuffers.reserve(sourceBufferCapacity);
    {
        using BufferPtr = NotNull<Core::Buffer*>;
        HashSet<BufferPtr, Core::Alloc::ScratchArena, Hasher<BufferPtr>, EqualTo<BufferPtr>> sourceBufferIdentities(AddSize(sourceBufferCapacity, sourceBufferCapacity), Hasher<BufferPtr>(), EqualTo<BufferPtr>(), scratchArena);
        for(const MeshSourceRef& mesh : uniqueMeshes){
            ForEachMaterialPassMeshSourceBuffer(*mesh, [&](const Core::BufferHandle& buffer){
                if(sourceBufferIdentities.insert(MakeNotNull(buffer.get())).second)
                    sourceBuffers.push_back(BufferRef(&buffer));
            });
        }
    }

    resourceUses.resize(sourceBuffers.size());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex)
            resourceUses[bufferIndex].resource = declarations.findImportedBuffer(*sourceBuffers[bufferIndex]);
    }
    for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex){
        const Core::BufferHandle& buffer = *sourceBuffers[bufferIndex];
        Core::GpuGraphResourceId resource = resourceUses[bufferIndex].resource;
        if(!resource.valid()){
            const Name identity = buffer->getCreationDescription().debugName;
            if(!identity)
                return MakeUnexpected(Failure{});
            const auto importedResource = graph.importBuffer(buffer, BufferResourceDesc(identity, "Prepared Material Geometry"));
            if(!importedResource)
                return MakeUnexpected(Failure{});
            resource = *importedResource;
        }
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        resourceUses[bufferIndex] = ReadUse(resource, Core::ResourceStates::ShaderResource);
    }
    return resourceUses;
}

Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialGeometryResourceSet(
    const Span<const MaterialPassDrawItemsView> drawItemSets,
    const Name& identity,
    const AStringView label,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    Core::GpuGraphResourceSetId resource{};
    const auto resourceUses = GatherPreparedMaterialGeometryUses(drawItemSets, graph, scratchArena);
    if(!resourceUses)
        return MakeUnexpected(Failure{});

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(resourceUses->size());
    for(const Core::GpuTaskResourceUse& use : *resourceUses)
        members.push_back(use.resource);

    const auto resourceResult = graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(identity).setMarkerLabel(label).setMembers(members));
    if(!resourceResult)
        return MakeUnexpected(Failure{});
    resource = *resourceResult;
    return resource;
}

Expected<Core::GpuGraphResourceId> GatherRegularSharedComputeEmulationResource(Core::GpuTaskGraph& graph, const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan, const AStringView label){
    if(!plan.captured || !plan.outputBuffer)
        return MakeUnexpected(Failure{});
    return FindOrImportBuffer(graph, plan.outputBuffer, label);
}

Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialSampledTextureResourceSet(
    const RendererMaterialSystem& materialSystem,
    const Span<const MaterialPassDrawItemsView> drawItemSets,
    const Name& identity,
    const AStringView label,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    Core::GpuGraphResourceSetId resource{};
    const auto sampledTextures = materialSystem.gatherPreparedMaterialPassSampledTextures(drawItemSets, scratchArena);
    if(!sampledTextures)
        return MakeUnexpected(Failure{});

    const auto members = ImportMaterialSampledTextureResources(*sampledTextures, "Prepared Material Sampled Texture", graph, scratchArena);
    if(!members)
        return MakeUnexpected(Failure{});
    if(members->empty())
        return resource;

    const auto resourceResult2 = graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(identity).setMarkerLabel(label).setMembers(*members));
    if(!resourceResult2)
        return MakeUnexpected(Failure{});
    resource = *resourceResult2;
    return resource;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

