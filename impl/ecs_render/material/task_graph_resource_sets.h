// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>


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

    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> sourceBuffers{ scratchArena };
    const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        if(!mesh.valid())
            return false;

        bool buffersReady = true;
        ForEachMaterialPassMeshSourceBuffer(mesh, [&](const Core::BufferHandle& buffer){
            if(!buffersReady)
                return;
            if(!buffer){
                buffersReady = false;
                return;
            }
            for(const Core::BufferHandle& existing : sourceBuffers){
                if(existing.get() == buffer.get())
                    return;
            }
            sourceBuffers.push_back(buffer);
        });
        return buffersReady;
    };
    for(usize drawItemSetIndex = 0u; drawItemSetIndex < drawItemSetCount; ++drawItemSetIndex){
        const MaterialPassDrawItems* const drawItems = drawItemSets[drawItemSetIndex];
        if(!drawItems)
            return false;
        for(const MaterialPassDrawItem& drawItem : drawItems->meshDrawItems){
            if(!appendDrawItem(drawItem))
                return false;
        }
        for(const MaterialPassDrawItem& drawItem : drawItems->computeDrawItems){
            if(!appendDrawItem(drawItem))
                return false;
        }
    }

    outResourceUses.reserve(sourceBuffers.size());
    for(const Core::BufferHandle& buffer : sourceBuffers){
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
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
        outResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
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

    outResource = graph.findImportedBuffer(plan.outputBuffer);
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
        sampledTextures
    ))
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(sampledTextures.size());
    for(const Core::TextureHandle& texture : sampledTextures){
        Core::GpuGraphResourceId resource = graph.findImportedTexture(texture);
        if(!resource.valid()){
            const Name textureIdentity = texture->getCreationDescription().name;
            if(!textureIdentity)
                return false;
            resource = graph.importTexture(
                texture,
                TextureResourceDesc(textureIdentity, "Prepared Material Sampled Texture")
            );
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
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

