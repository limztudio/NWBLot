// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/task/gpu/task_desc.h>
#include <core/task/gpu/task_graph.h>


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
[[nodiscard]] inline Expected<Core::GpuGraphResourceSetId> GatherImportedOutputBufferResourceSet(
    Core::GpuTaskGraph& graph,
    const Plan& plan,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label
){
    if(!plan.captured || plan.outputBuffers.empty())
        return MakeUnexpected(Failure{});

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(plan.outputBuffers.size());
    for(const Core::BufferHandle& buffer : plan.outputBuffers){
        if(!buffer)
            return MakeUnexpected(Failure{});
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return MakeUnexpected(Failure{});
            resource = declarations.findImportedBuffer(buffer);
        }
        if(!resource.valid()){
            const Name bufferIdentity = buffer->getCreationDescription().debugName;
            if(!bufferIdentity)
                return MakeUnexpected(Failure{});
            resource = graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, label));
        }
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        members.push_back(resource);
    }
    const Core::GpuGraphResourceSetId result = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    if(!result.valid())
        return MakeUnexpected(Failure{});
    return result;
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
)noexcept{
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = hasIndependentStateSource,
    };
}


[[nodiscard]] inline Core::GpuTaskResourceUse ReadBufferUse(
    const Core::GpuGraphResourceId resource,
    const Core::BufferRange& range,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
)noexcept{
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.bufferRange = range;
    return result;
}

[[nodiscard]] inline Core::GpuTaskResourceUse ReadTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
)noexcept{
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.textureSubresources = subresources;
    return result;
}

[[nodiscard]] inline Core::GpuTaskResourceUse WriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
)noexcept{
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
)noexcept{
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
)noexcept{
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
)noexcept{
    Core::GpuTaskResourceUse result = ReadWriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

// The interval producer wrote these aliases; the graph lowers their same-UAV handoff.
template<typename UseVector>
inline void AppendCsgRemovedIntervalUses(
    UseVector& resourceUses,
    const Core::GpuGraphResourceId removedIntervalDepth,
    const Core::GpuGraphResourceId removedIntervalCapNormal,
    const Core::GpuGraphResourceId removedIntervalData,
    const Core::GpuGraphResourceId removedIntervalCount,
    const Core::TextureSubresourceSet& subresources,
    const Core::TextureSubresourceSet& countSubresources
){
    resourceUses.push_back(ReadTextureUse(
        removedIntervalDepth,
        subresources,
        Core::ResourceStates::UnorderedAccess
    ));
    resourceUses.push_back(ReadTextureUse(
        removedIntervalCapNormal,
        subresources,
        Core::ResourceStates::UnorderedAccess
    ));
    resourceUses.push_back(ReadTextureUse(
        removedIntervalData,
        subresources,
        Core::ResourceStates::UnorderedAccess
    ));
    resourceUses.push_back(ReadTextureUse(
        removedIntervalCount,
        countSubresources,
        Core::ResourceStates::UnorderedAccess
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

