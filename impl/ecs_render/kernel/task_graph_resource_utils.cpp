// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuGraphResourceDesc TextureResourceDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Texture);
    return desc;
}

Core::GpuGraphResourceDesc BufferResourceDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer);
    return desc;
}

Expected<Core::GpuGraphResourceId> ImportExternalTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    const Name& identity,
    const AStringView label,
    const Optional<Core::ResourceStates::Mask> initialState,
    const Optional<Core::ResourceStates::Mask> externalFinalState
){
    Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);
    if(initialState.has_value())
        desc.setInitialState(*initialState);
    if(externalFinalState.has_value())
        desc.setExternalFinalState(*externalFinalState);
    return graph.importTexture(texture, desc);
}

Expected<Core::GpuGraphResourceId> ImportExternalBuffer(
    Core::GpuTaskGraph& graph,
    const Core::BufferHandle& buffer,
    const Name& identity,
    const AStringView label,
    const Optional<Core::ResourceStates::Mask> initialState,
    const Optional<Core::ResourceStates::Mask> externalFinalState
){
    Core::GpuGraphResourceDesc desc = BufferResourceDesc(identity, label);
    if(initialState.has_value())
        desc.setInitialState(*initialState);
    if(externalFinalState.has_value())
        desc.setExternalFinalState(*externalFinalState);
    return graph.importBuffer(buffer, desc);
}

Expected<Core::GpuGraphResourceId> FindOrImportTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    const AStringView resourceLabel,
    const Optional<Core::ResourceStates::Mask> initialState
){
    if(!texture)
        return MakeUnexpected(Failure{});
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = declarations.findImportedTexture(texture);
        if(existing.valid())
            return existing;
    }
    Core::GpuGraphResourceDesc desc = TextureResourceDesc(texture->getCreationDescription().name, resourceLabel);
    if(initialState.has_value())
        desc.setInitialState(*initialState);
    return graph.importTexture(texture, desc);
}

Expected<Core::GpuGraphResourceId> FindOrImportBuffer(
    Core::GpuTaskGraph& graph,
    const Core::BufferHandle& buffer,
    const AStringView resourceLabel
){
    if(!buffer)
        return MakeUnexpected(Failure{});
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = declarations.findImportedBuffer(buffer);
        if(existing.valid())
            return existing;
    }
    const Name bufferIdentity = buffer->getCreationDescription().debugName;
    if(!bufferIdentity)
        return MakeUnexpected(Failure{});
    return graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, resourceLabel));
}

Core::GpuGraphResourceDesc HazardDomainDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::HazardDomain);
    return desc;
}

Core::GpuTaskResourceUse ReadBufferUse(
    const Core::GpuGraphResourceId resource,
    const Core::BufferRange& range,
    const Core::ResourceStates::Mask state,
    const b8 hasIndependentStateSource
)noexcept{
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.bufferRange = range;
    return result;
}

Core::GpuTaskResourceUse ReadTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state,
    const b8 hasIndependentStateSource
)noexcept{
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.textureSubresources = subresources;
    return result;
}

Core::GpuTaskResourceUse WriteTextureUse(const Core::GpuGraphResourceId resource, const Core::TextureSubresourceSet& subresources, const Core::ResourceStates::Mask state)noexcept{
    Core::GpuTaskResourceUse result = WriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

Core::GpuGraphResourceDesc AccelStructResourceDesc(const Name& identity, const AStringView label)noexcept{
    Core::GpuGraphResourceDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::AccelStruct);
    return desc;
}

Core::GpuTaskResourceUse ReadWriteTextureUse(const Core::GpuGraphResourceId resource, const Core::TextureSubresourceSet& subresources, const Core::ResourceStates::Mask state)noexcept{
    Core::GpuTaskResourceUse result = ReadWriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

Core::GpuTaskDesc TaskDesc(const Name& identity, const AStringView label, const Core::GpuTaskId& dependency)noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setScheduling(scheduling);
    if(dependency.valid())
        desc.setDependencies({ &dependency, 1u });
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

