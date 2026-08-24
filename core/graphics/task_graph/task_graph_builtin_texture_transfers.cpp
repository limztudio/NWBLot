// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiled_graph.h"

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_builtin_texture_transfers{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CopyTextureTask{
    struct Copy{
        GpuGraphResourceId sourceResource;
        TextureHandle source;
        TextureSlice sourceSlice;
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        TextureSlice destinationSlice;
    };

    struct Payload{
        explicit Payload(GraphicsArena& arena)
            : copies(arena)
        {}

        GraphicsVector<Copy> copies;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.copies.empty())
            return false;

        for(const Copy& copy : payload.copies){
            if(!copy.source || !copy.destination)
                return false;
            if(
                context.commandIrCapture
                && !context.commandIrCapture->captureCopyTexture(
                    context.task,
                    context.packet,
                    context.queue,
                    copy.sourceResource,
                    copy.sourceSlice,
                    copy.destinationResource,
                    copy.destinationSlice
                )
            )
                return false;
            commandList.endRenderPass();
            commandList.copyTexture(
                copy.destination.get(),
                copy.destinationSlice,
                copy.source.get(),
                copy.sourceSlice
            );
        }
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.acceptedToken)
            *payload.acceptedToken = {};
    }
};

struct ResolveTextureTask{
    struct Resolve{
        GpuGraphResourceId sourceResource;
        TextureHandle source;
        TextureSubresourceSet sourceSubresources;
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        TextureSubresourceSet destinationSubresources;
    };

    struct Payload{
        explicit Payload(GraphicsArena& arena)
            : resolves(arena)
        {}

        GraphicsVector<Resolve> resolves;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(payload.resolves.empty() || context.commandIrCapture)
            return false;

        for(const Resolve& resolve : payload.resolves){
            if(!resolve.source || !resolve.destination)
                return false;
            commandList.endRenderPass();
            commandList.resolveTexture(
                resolve.destination.get(),
                resolve.destinationSubresources,
                resolve.source.get(),
                resolve.sourceSubresources
            );
        }
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.acceptedToken)
            *payload.acceptedToken = {};
    }
};
[[nodiscard]] static bool ValidTextureRange(const TextureSubresourceSet& range)noexcept{
    return range.numMipLevels != 0u && range.numArraySlices != 0u;
}

template<typename ResourceDesc>
[[nodiscard]] static bool BuiltInTaskCanMaterializeRetainedState(
    const ResourceDesc& resourceDesc,
    const ResourceStates::Mask graphInitialState,
    const ResourceStates::Mask externalFinalState
)noexcept{
    // Retained resources restore to their descriptor state when a native packet closes. The graph may still use a
    // different built-in state when it explicitly starts from that descriptor state: compiler-owned barriers then
    // establish the primitive state and the recorded packet exports the restored state for the next packet. A
    // mismatched graph declaration has no native source that this helper can prove, and a terminal external state
    // must agree with the close-time restoration before the graph publishes its handoff.
    if(!resourceDesc.keepInitialState)
        return true;
    return resourceDesc.initialState != ResourceStates::Unknown
        && graphInitialState == resourceDesc.initialState
        && (
            externalFinalState == ResourceStates::Unknown
            || externalFinalState == resourceDesc.initialState
        )
    ;
}

[[nodiscard]] static bool CopyOrClearTextureDestinationCanMaterializeRetainedState(
    const TextureDesc& resourceDesc,
    const ResourceStates::Mask graphInitialState,
    const ResourceStates::Mask externalFinalState
)noexcept{
    if(!resourceDesc.keepInitialState)
        return true;
    if(
        resourceDesc.initialState == ResourceStates::Unknown
        || (
            externalFinalState != ResourceStates::Unknown
            && externalFinalState != resourceDesc.initialState
        )
    )
        return false;
    // An Unknown write-only destination never invents an input state. Fresh managed subresources lower from
    // Undefined; accepted retained subresources are restored to descriptor state at packet close and reused by
    // StateTracker on later packets.
    return graphInitialState == ResourceStates::Unknown || graphInitialState == resourceDesc.initialState;
}

[[nodiscard]] static TextureDimension::Enum CopyTextureImageType(const TextureDimension::Enum dimension)noexcept{
    switch(dimension){
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
        return TextureDimension::Texture1D;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
        return TextureDimension::Texture2D;
    case TextureDimension::Texture3D:
        return TextureDimension::Texture3D;
    default:
        return TextureDimension::Unknown;
    }
}

[[nodiscard]] static bool CopyTextureContractValid(
    const TextureDesc& sourceDesc,
    const TextureSlice& sourceSlice,
    const TextureDesc& destinationDesc,
    const TextureSlice& destinationSlice,
    TextureSlice& outResolvedSourceSlice,
    TextureSlice& outResolvedDestinationSlice
)noexcept{
    const TextureDimension::Enum sourceImageType = CopyTextureImageType(sourceDesc.dimension);
    const TextureDimension::Enum destinationImageType = CopyTextureImageType(destinationDesc.dimension);
    GraphicsBackend::VulkanDetail::TextureFormatBlockLayout sourceLayout;
    GraphicsBackend::VulkanDetail::TextureFormatBlockLayout destinationLayout;
    if(
        sourceImageType == TextureDimension::Unknown
        || destinationImageType == TextureDimension::Unknown
        || sourceImageType != destinationImageType
        || sourceDesc.format != destinationDesc.format
        || sourceDesc.sampleCount != destinationDesc.sampleCount
        || !GraphicsBackend::VulkanDetail::GetTextureFormatBlockLayout(GetFormatInfo(sourceDesc.format), sourceLayout)
        || !GraphicsBackend::VulkanDetail::GetTextureFormatBlockLayout(
            GetFormatInfo(destinationDesc.format),
            destinationLayout
        )
        || !GraphicsBackend::VulkanDetail::IsTextureSliceInBounds(
            sourceDesc,
            sourceSlice,
            sourceLayout,
            &outResolvedSourceSlice
        )
        || !GraphicsBackend::VulkanDetail::IsTextureSliceInBounds(
            destinationDesc,
            destinationSlice,
            destinationLayout,
            &outResolvedDestinationSlice
        )
    )
        return false;

    return outResolvedSourceSlice.width == outResolvedDestinationSlice.width
        && outResolvedSourceSlice.height == outResolvedDestinationSlice.height
        && outResolvedSourceSlice.depth == outResolvedDestinationSlice.depth
    ;
}

[[nodiscard]] static bool ResolveTextureContractValid(
    const TextureDesc& sourceDesc,
    const TextureSubresourceSet& sourceSubresources,
    const TextureDesc& destinationDesc,
    const TextureSubresourceSet& destinationSubresources,
    TextureSubresourceSet& outResolvedSourceSubresources,
    TextureSubresourceSet& outResolvedDestinationSubresources
)noexcept{
    const TextureDimension::Enum sourceImageType = CopyTextureImageType(sourceDesc.dimension);
    const TextureDimension::Enum destinationImageType = CopyTextureImageType(destinationDesc.dimension);
    if(
        sourceImageType == TextureDimension::Unknown
        || destinationImageType == TextureDimension::Unknown
        || sourceImageType != destinationImageType
        || sourceDesc.sampleCount <= 1u
        || destinationDesc.sampleCount != 1u
        || sourceDesc.format != destinationDesc.format
    )
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(sourceDesc.format);
    if(formatInfo.hasDepth || formatInfo.hasStencil)
        return false;

    outResolvedSourceSubresources = sourceSubresources.resolve(
        sourceDesc,
        TextureSubresourceMipResolve::Range
    );
    outResolvedDestinationSubresources = destinationSubresources.resolve(
        destinationDesc,
        TextureSubresourceMipResolve::Range
    );
    if(
        !ValidTextureRange(outResolvedSourceSubresources)
        || !ValidTextureRange(outResolvedDestinationSubresources)
        || outResolvedSourceSubresources.numMipLevels != outResolvedDestinationSubresources.numMipLevels
        || outResolvedSourceSubresources.numArraySlices != outResolvedDestinationSubresources.numArraySlices
    )
        return false;

    for(MipLevel mipOffset = 0u; mipOffset < outResolvedSourceSubresources.numMipLevels; ++mipOffset){
        const MipLevel sourceMipLevel = outResolvedSourceSubresources.baseMipLevel + mipOffset;
        const MipLevel destinationMipLevel = outResolvedDestinationSubresources.baseMipLevel + mipOffset;
        const u32 sourceWidth = Max<u32>(sourceDesc.width >> sourceMipLevel, 1u);
        const u32 sourceHeight = Max<u32>(sourceDesc.height >> sourceMipLevel, 1u);
        const u32 sourceDepth = sourceDesc.dimension == TextureDimension::Texture3D
            ? Max<u32>(sourceDesc.depth >> sourceMipLevel, 1u)
            : 1u
        ;
        const u32 destinationWidth = Max<u32>(destinationDesc.width >> destinationMipLevel, 1u);
        const u32 destinationHeight = Max<u32>(destinationDesc.height >> destinationMipLevel, 1u);
        const u32 destinationDepth = destinationDesc.dimension == TextureDimension::Texture3D
            ? Max<u32>(destinationDesc.depth >> destinationMipLevel, 1u)
            : 1u
        ;
        if(
            sourceWidth != destinationWidth
            || sourceHeight != destinationHeight
            || sourceDepth != destinationDepth
        )
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc){
    if(copyDesc.acceptedToken)
        *copyDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !copyDesc.regions
        || copyDesc.regionCount == 0u
        || copyDesc.regionCount > Limit<u32>::s_Max
        || copyDesc.regionCount > Limit<usize>::s_Max / 2u
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    using CopyTask = __hidden_gpu_task_graph_builtin_texture_transfers::CopyTextureTask;
    CopyTask::Payload* const payload = NewArenaObject<CopyTask::Payload>(m_arena, m_arena);
    if(!payload)
        return {};
    payload->copies.reserve(copyDesc.regionCount);
    payload->acceptedToken = copyDesc.acceptedToken;

    GraphicsVector<GpuTaskResourceUse> resourceUses(m_arena);
    resourceUses.reserve(copyDesc.regionCount * 2u);
    const auto appendResourceUse = [&](
        const GpuGraphResourceId resource,
        const TextureSubresourceSet& subresources,
        const ResourceStates::Mask state,
        const GpuTaskResourceAccess::Enum access
    ){
        for(const GpuTaskResourceUse& existing : resourceUses){
            if(existing.resource != resource)
                continue;

            // A primitive copy cannot safely read and write the same imported image inside one task. Keep that
            // sequencing explicit in separate tasks instead of silently weakening its graph declarations.
            if(existing.requiredState != state || existing.access != access)
                return false;
        }
        resourceUses.push_back(GpuTaskResourceUse{
            .resource = resource,
            .range = { .textureSubresources = subresources },
            .requiredState = state,
            .access = access,
        });
        return true;
    };

    bool valid = true;
    bool requiresGraphicsQueue = false;
    for(usize regionIndex = 0u; regionIndex < copyDesc.regionCount && valid; ++regionIndex){
        const GpuCopyTextureTaskRegion& region = copyDesc.regions[regionIndex];
        if(!validResource(region.source) || !validResource(region.destination)){
            valid = false;
            break;
        }
        const GpuGraphResourceNode& sourceResource = m_resources[region.source.index];
        const GpuGraphResourceNode& destinationResource = m_resources[region.destination.index];
        TextureSlice resolvedSourceSlice;
        TextureSlice resolvedDestinationSlice;
        valid = region.source != region.destination
            && sourceResource.type == GpuGraphResourceType::Texture
            && destinationResource.type == GpuGraphResourceType::Texture
            && sourceResource.texture
            && destinationResource.texture
            && __hidden_gpu_task_graph_builtin_texture_transfers::BuiltInTaskCanMaterializeRetainedState(
                sourceResource.texture->getDescription(),
                sourceResource.initialState,
                sourceResource.externalFinalState
            )
            && __hidden_gpu_task_graph_builtin_texture_transfers::CopyOrClearTextureDestinationCanMaterializeRetainedState(
                destinationResource.texture->getDescription(),
                destinationResource.initialState,
                destinationResource.externalFinalState
            )
            && __hidden_gpu_task_graph_builtin_texture_transfers::CopyTextureContractValid(
                sourceResource.texture->getDescription(),
                region.sourceSlice,
                destinationResource.texture->getDescription(),
                region.destinationSlice,
                resolvedSourceSlice,
                resolvedDestinationSlice
            )
            && appendResourceUse(
                region.source,
                TextureSubresourceSet(resolvedSourceSlice.mipLevel, 1u, resolvedSourceSlice.arraySlice, 1u),
                ResourceStates::CopySource,
                GpuTaskResourceAccess::Read
            )
            && appendResourceUse(
                region.destination,
                TextureSubresourceSet(resolvedDestinationSlice.mipLevel, 1u, resolvedDestinationSlice.arraySlice, 1u),
                ResourceStates::CopyDest,
                GpuTaskResourceAccess::Write
            )
        ;
        if(valid){
            const TextureDesc& sourceDesc = sourceResource.texture->getDescription();
            const FormatInfo& formatInfo = GetFormatInfo(sourceDesc.format);
            requiresGraphicsQueue = requiresGraphicsQueue || (
                sourceDesc.sampleCount > 1u
                && (formatInfo.hasDepth || formatInfo.hasStencil)
            );
            payload->copies.push_back(CopyTask::Copy{
                .sourceResource = region.source,
                .source = sourceResource.texture,
                .sourceSlice = resolvedSourceSlice,
                .destinationResource = region.destination,
                .destination = destinationResource.texture,
                .destinationSlice = resolvedDestinationSlice,
            });
        }
    }
    if(!valid){
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<CopyTask>,
            &DestroyPayload<CopyTask::Payload>
        );
        return {};
    }

    GpuTaskDesc resolvedDesc = desc;
    if(requiresGraphicsQueue)
        resolvedDesc.queue.requiredCapabilities |= GpuQueueCapability::Graphics;
    resolvedDesc.setResourceUses(resourceUses.data(), resourceUses.size());
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<CopyTask>,
        &AcceptPayload<CopyTask>,
        &DiscardPayload<CopyTask>,
        &DestroyPayload<CopyTask::Payload>,
        sizeof(CopyTask::Payload)
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<CopyTask>,
            &DestroyPayload<CopyTask::Payload>
        );
    return task;
}

GpuTaskId GpuTaskGraph::addResolveTextureTask(
    const GpuTaskDesc& desc,
    const GpuResolveTextureTaskDesc& resolveDesc
){
    if(resolveDesc.acceptedToken)
        *resolveDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !resolveDesc.regions
        || resolveDesc.regionCount == 0u
        || resolveDesc.regionCount > Limit<u32>::s_Max
        || resolveDesc.regionCount > Limit<usize>::s_Max / 2u
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
    )
        return {};

    using ResolveTask = __hidden_gpu_task_graph_builtin_texture_transfers::ResolveTextureTask;
    ResolveTask::Payload* const payload = NewArenaObject<ResolveTask::Payload>(m_arena, m_arena);
    if(!payload)
        return {};
    payload->resolves.reserve(resolveDesc.regionCount);
    payload->acceptedToken = resolveDesc.acceptedToken;

    GraphicsVector<GpuTaskResourceUse> resourceUses(m_arena);
    resourceUses.reserve(resolveDesc.regionCount * 2u);
    const auto appendResourceUse = [&](
        const GpuGraphResourceId resource,
        const TextureSubresourceSet& subresources,
        const ResourceStates::Mask state,
        const GpuTaskResourceAccess::Enum access
    ){
        for(const GpuTaskResourceUse& existing : resourceUses){
            if(existing.resource != resource)
                continue;
            // A resolve source cannot also be a destination in one task: the required states need an explicit
            // inter-task boundary rather than an ambiguous packet prologue.
            if(existing.requiredState != state || existing.access != access)
                return false;
        }
        resourceUses.push_back(GpuTaskResourceUse{
            .resource = resource,
            .range = { .textureSubresources = subresources },
            .requiredState = state,
            .access = access,
        });
        return true;
    };

    bool valid = true;
    for(usize regionIndex = 0u; regionIndex < resolveDesc.regionCount && valid; ++regionIndex){
        const GpuResolveTextureTaskRegion& region = resolveDesc.regions[regionIndex];
        if(!validResource(region.source) || !validResource(region.destination)){
            valid = false;
            break;
        }
        const GpuGraphResourceNode& sourceResource = m_resources[region.source.index];
        const GpuGraphResourceNode& destinationResource = m_resources[region.destination.index];
        TextureSubresourceSet resolvedSourceSubresources;
        TextureSubresourceSet resolvedDestinationSubresources;
        valid = region.source != region.destination
            && sourceResource.type == GpuGraphResourceType::Texture
            && destinationResource.type == GpuGraphResourceType::Texture
            && sourceResource.texture
            && destinationResource.texture
            && __hidden_gpu_task_graph_builtin_texture_transfers::BuiltInTaskCanMaterializeRetainedState(
                sourceResource.texture->getDescription(),
                sourceResource.initialState,
                sourceResource.externalFinalState
            )
            && __hidden_gpu_task_graph_builtin_texture_transfers::BuiltInTaskCanMaterializeRetainedState(
                destinationResource.texture->getDescription(),
                destinationResource.initialState,
                destinationResource.externalFinalState
            )
            && __hidden_gpu_task_graph_builtin_texture_transfers::ResolveTextureContractValid(
                sourceResource.texture->getDescription(),
                region.sourceSubresources,
                destinationResource.texture->getDescription(),
                region.destinationSubresources,
                resolvedSourceSubresources,
                resolvedDestinationSubresources
            )
            && appendResourceUse(
                region.source,
                resolvedSourceSubresources,
                ResourceStates::ResolveSource,
                GpuTaskResourceAccess::Read
            )
            && appendResourceUse(
                region.destination,
                resolvedDestinationSubresources,
                ResourceStates::ResolveDest,
                GpuTaskResourceAccess::Write
            )
        ;
        if(valid){
            payload->resolves.push_back(ResolveTask::Resolve{
                .sourceResource = region.source,
                .source = sourceResource.texture,
                .sourceSubresources = resolvedSourceSubresources,
                .destinationResource = region.destination,
                .destination = destinationResource.texture,
                .destinationSubresources = resolvedDestinationSubresources,
            });
        }
    }
    if(!valid){
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<ResolveTask>,
            &DestroyPayload<ResolveTask::Payload>
        );
        return {};
    }

    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(resourceUses.data(), resourceUses.size());
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ResolveTask>,
        &AcceptPayload<ResolveTask>,
        &DiscardPayload<ResolveTask>,
        &DestroyPayload<ResolveTask::Payload>,
        sizeof(ResolveTask::Payload)
    );
    if(!task.valid()){
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<ResolveTask>,
            &DestroyPayload<ResolveTask::Payload>
        );
    }
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

