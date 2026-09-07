// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sampled_texture_graph_resources.h"

#include <core/graphics/vulkan/backend.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sampled_texture_graph_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SampledTextureRequests final : NoCopy{
private:
    struct Request{
        const Core::TextureHandle* source = nullptr;
        Core::GpuGraphResourceId resource;
    };
    using RequestIndex = HashMap<Core::Texture*, Request, Core::Alloc::ScratchArena>;
    static constexpr usize s_InlineCount = 32u;


public:
    explicit SampledTextureRequests(Core::Alloc::ScratchArena& scratchArena);
    SampledTextureRequests(SampledTextureRequests&&) = delete;


public:
    void add(const Core::TextureHandle& texture);
    [[nodiscard]] Request* find(Core::Texture* texture);
    [[nodiscard]] bool resolveExisting(const Core::GpuTaskGraph& graph);


private:
    void promote();


private:
    Core::Alloc::ScratchArena& m_scratchArena;
    Core::Texture* m_inlineTextures[s_InlineCount];
    Request m_inlineRequests[s_InlineCount];
    usize m_inlineCount = 0u;
    Optional<RequestIndex> m_requests;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SampledTextureRequests::SampledTextureRequests(Core::Alloc::ScratchArena& scratchArena)
    : m_scratchArena(scratchArena)
{}

void SampledTextureRequests::add(const Core::TextureHandle& texture){
    if(!texture)
        return;
    if(m_requests){
        m_requests->try_emplace(texture.get(), Request{ &texture, {} });
        return;
    }
    if(find(texture.get()))
        return;
    if(m_inlineCount == s_InlineCount){
        promote();
        m_requests->try_emplace(texture.get(), Request{ &texture, {} });
        return;
    }
    m_inlineTextures[m_inlineCount] = texture.get();
    m_inlineRequests[m_inlineCount].source = &texture;
    ++m_inlineCount;
}

SampledTextureRequests::Request* SampledTextureRequests::find(Core::Texture* const texture){
    if(m_requests){
        const auto found = m_requests->find(texture);
        return found == m_requests->end() ? nullptr : &found.value();
    }
    for(usize index = 0u; index < m_inlineCount; ++index){
        if(m_inlineTextures[index] == texture)
            return &m_inlineRequests[index];
    }
    return nullptr;
}

bool SampledTextureRequests::resolveExisting(const Core::GpuTaskGraph& graph){
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    if(!declarations.valid())
        return false;
    if(m_requests){
        // Index only the requested pointers. The first matching declaration wins, including aliases whose
        // metadata intentionally differs from the texture's creation name or this operation's marker label.
        usize unresolvedCount = m_requests->size();
        const usize resourceCount = declarations.resourceCount();
        const u64 generation = declarations.generation();
        for(usize index = 0u; index < resourceCount && unresolvedCount != 0u; ++index){
            const Core::GpuGraphResourceId resource{ static_cast<u32>(index), generation };
            Core::Texture* const texture = declarations.textureForResource(resource);
            if(!texture)
                continue;
            Request* const request = find(texture);
            if(request && !request->resource.valid()){
                request->resource = resource;
                --unresolvedCount;
            }
        }
    }
    else{
        for(usize index = 0u; index < m_inlineCount; ++index){
            Request& request = m_inlineRequests[index];
            request.resource = declarations.findImportedTexture(*request.source);
        }
    }
    return true;
}

void SampledTextureRequests::promote(){
    RequestIndex requests(s_InlineCount * 4u, m_scratchArena);
    for(usize index = 0u; index < m_inlineCount; ++index)
        requests.emplace(m_inlineTextures[index], m_inlineRequests[index]);
    static_assert(IsNothrowMoveConstructible_V<RequestIndex>);
    m_requests.emplace(Move(requests));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SampledTextureImportResult::Enum AppendSampledTextureResource(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    const AStringView markerLabel,
    Core::GpuGraphResourceId& resource,
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& outResources){
    if(!resource.valid()){
        if(!texture)
            return SampledTextureImportResult::MissingIdentity;
        const Name identity = texture->getCreationDescription().name;
        if(!identity)
            return SampledTextureImportResult::MissingIdentity;
        resource = graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, markerLabel));
        if(!resource.valid())
            return SampledTextureImportResult::ImportFailed;
    }
    outResources.push_back(resource);
    return SampledTextureImportResult::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SampledTextureImportResult::Enum ImportMaterialSampledTextureResources(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle* const textures,
    const usize textureCount,
    const AStringView markerLabel,
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& outResources,
    Core::Alloc::ScratchArena& scratchArena){
    if(textureCount == 0u)
        return SampledTextureImportResult::Success;
    if(textureCount == 1u){
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return SampledTextureImportResult::GraphUnavailable;
            resource = declarations.findImportedTexture(textures[0u]);
        }
        return __hidden_sampled_texture_graph_resources::AppendSampledTextureResource(
            graph, textures[0u], markerLabel, resource, outResources
        );
    }

    __hidden_sampled_texture_graph_resources::SampledTextureRequests requests(scratchArena);
    for(usize index = 0u; index < textureCount; ++index)
        requests.add(textures[index]);
    // No declaration view survives this point: missing textures still use the graph's complete import contract
    // in request order, and a failure leaves exactly the successfully appended prefix visible to the caller.
    if(!requests.resolveExisting(graph))
        return SampledTextureImportResult::GraphUnavailable;
    for(usize textureIndex = 0u; textureIndex < textureCount; ++textureIndex){
        const Core::TextureHandle& texture = textures[textureIndex];
        if(!texture)
            return SampledTextureImportResult::MissingIdentity;
        auto* const request = requests.find(texture.get());
        NWB_ASSERT(request);
        const auto result = __hidden_sampled_texture_graph_resources::AppendSampledTextureResource(
            graph, texture, markerLabel, request->resource, outResources
        );
        if(result != SampledTextureImportResult::Success)
            return result;
    }
    return SampledTextureImportResult::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

