// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/task_graph/task_desc.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Core::GpuGraphResourceDesc TextureResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Texture)
    ;
    return desc;
}

[[nodiscard]] inline Core::GpuGraphResourceDesc BufferResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Buffer)
    ;
    return desc;
}

template<typename Plan>
[[nodiscard]] inline bool GatherImportedOutputBufferResourceSet(
    Core::GpuTaskGraph& graph,
    const Plan& plan,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    if(!plan.captured || plan.outputBuffers.empty())
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(plan.outputBuffers.size());
    for(const Core::BufferHandle& buffer : plan.outputBuffers){
        if(!buffer)
            return false;
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
        if(!resource.valid()){
            const Name bufferIdentity = buffer->getCreationDescription().debugName;
            if(!bufferIdentity)
                return false;
            resource = graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, label));
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}

[[nodiscard]] inline Core::GpuGraphResourceDesc HazardDomainDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::HazardDomain)
    ;
    return desc;
}

[[nodiscard]] inline Core::GpuTaskResourceUse ReadUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = hasIndependentStateSource,
    };
}


[[nodiscard]] inline Core::GpuTaskResourceUse ReadTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
){
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.textureSubresources = subresources;
    return result;
}

[[nodiscard]] inline Core::GpuTaskResourceUse WriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Write,
    };
}

[[nodiscard]] inline Core::GpuTaskResourceUse WriteTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state
){
    Core::GpuTaskResourceUse result = WriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

[[nodiscard]] inline Core::GpuGraphResourceDesc AccelStructResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::AccelStruct)
    ;
    return desc;
}

[[nodiscard]] inline Core::GpuTaskResourceUse ReadWriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
}

[[nodiscard]] inline Core::GpuTaskResourceUse ReadWriteTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state
){
    Core::GpuTaskResourceUse result = ReadWriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

