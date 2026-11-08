// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sampled_texture_resources.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuSampledTextures::PrepareDescriptor(Core::GraphicsRuntime& graphics, const Core::TextureHandle& texture, const Core::Format::Enum format, Core::GpuDescriptorHandle& inOutDescriptor){
    if(inOutDescriptor.valid())
        return true;
    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    const Expected<Core::GpuDescriptorHandle> descriptorResult = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!descriptorResult)
        return false;
    const Core::GpuDescriptorHandle descriptor = *descriptorResult;
    Core::GpuDescriptorRetirementGuard descriptorGuard(heap, descriptor);
    if(!heap.write(descriptor, Core::DescriptorWriteItem::TextureSrv(0u, texture.get(), format, Core::s_AllSubresources, Core::TextureDimension::Texture2D)))
        return false;
    inOutDescriptor = descriptor;
    descriptorGuard.release();
    return true;
}

b8 GpuSampledTextures::CreateAndUploadPage(
    Core::GraphicsRuntime& graphics,
    const GpuPageTextureUploadDesc& desc,
    Core::TextureHandle& outTexture,
    Core::QueueSubmissionToken& outReadinessToken
){
    Core::TextureDesc description;
    description
        .setName(desc.name).setWidth(desc.width).setHeight(desc.height)
        .setFormat(desc.format).setInitialState(Core::ResourceStates::ShaderResource).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    auto textureResult = graphics.getDevice().createTexture(description);
    if(textureResult)
        outTexture = Move(*textureResult);
    else{
        outTexture.reset();
        return false;
    }
    const Core::GraphicsStandaloneService::TextureUploadRegion region{ .data = desc.data, .dataSize = desc.dataSize, .rowPitch = desc.rowPitch, .depthPitch = desc.dataSize };
    if(!graphics.standaloneGraph().uploadTextureBatch(Core::GraphicsStandaloneService::TextureUploadBatchDesc{
        .destination = outTexture,
        .regions = &region,
        .regionCount = 1u,
        .acceptedToken = &outReadinessToken,
        .finalState = Core::ResourceStates::ShaderResource,
        .physicalInitialState = Core::ResourceStates::Unknown,
        .hasPhysicalInitialState = true,
    }))
        return false;
    return outReadinessToken.valid() && outReadinessToken.hasPhysicalQueueIdentity();
}

Expected<Core::GpuGraphResourceId, GpuSampledTextureImportError::Enum> GpuSampledTextures::ImportTexture(const GpuSampledTextureImportDesc& desc, Core::GpuTaskGraph& graph, GpuRasterResourceUses& uses){
    Array<char, s_DecimalTextBufferBytes> identityText{};
    const AStringView suffix = FormatDecimal(desc.index, identityText);
    // Graph imports unify equal identities; every immutable snapshot page needs a distinct texture and completion name.
    const auto readyIdentity = DeriveName(desc.readyPrefix, suffix);
    const auto textureIdentity = DeriveName(desc.texturePrefix, suffix);
    if(!readyIdentity || !textureIdentity)
        return MakeUnexpected(GpuSampledTextureImportError::Identity);
    const auto readyResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc().setIdentity(*readyIdentity).setMarkerLabel(desc.readyMarkerLabel).setToken(desc.readinessToken));
    if(!readyResult){
        NWB_LOGGER_ERROR(
            NWB_TEXT("GpuRenderer: {} {} upload completion import failed (queue={}, generation={}, value={})")
            , desc.completionLabel
            , desc.index
            , desc.readinessToken.physicalQueueIndex
            , desc.readinessToken.deviceGeneration
            , desc.readinessToken.value
        );
        return MakeUnexpected(GpuSampledTextureImportError::Completion);
    }
    const Core::GpuExternalCompletionId ready = *readyResult;
    const auto textureResult = graph.importTexture(
        desc.texture,
        Core::GpuGraphResourceDesc().setIdentity(*textureIdentity).setMarkerLabel(desc.textureMarkerLabel)
            .setType(Core::GpuGraphResourceType::Texture).setInitialAvailabilityCompletion(ready)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    if(!textureResult)
        return MakeUnexpected(GpuSampledTextureImportError::Texture);
    const Core::GpuGraphResourceId texture = *textureResult;
    uses.push_back({ texture, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read });
    return texture;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

