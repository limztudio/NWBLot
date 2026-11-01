// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "loader.h"

#include "texture_mip_decoder.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>

#include <global/sync.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::FormatSupport::Mask s_RequiredTextureFormatSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderSample;

static Futex s_BasisTranscoderInitializationMutex;
static Atomic<b8> s_BasisTranscoderInitialized = false;

struct CreatedTexture{
    Core::TextureHandle texture;
    Core::Format::Enum format;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void InitializeBasisTranscoder(){
    if(s_BasisTranscoderInitialized.load(MemoryOrder::acquire))
        return;

    ScopedLock lock(s_BasisTranscoderInitializationMutex);
    if(s_BasisTranscoderInitialized.load(MemoryOrder::acquire))
        return;

    basist::basisu_transcoder_init();
    s_BasisTranscoderInitialized.store(true, MemoryOrder::release);
}

[[nodiscard]] static b8 SupportsTextureFormat(const Core::Format::Enum format, Core::Device& device)noexcept{
    return (device.queryFormatSupport(format) & s_RequiredTextureFormatSupport) == s_RequiredTextureFormatSupport;
}

[[nodiscard]] static Core::Format::Enum SelectUploadFormat(const Texture& textureAsset, Core::Device& device)noexcept{
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4){
        if(textureAsset.alphaMode() == TextureAlphaMode::Opaque){
            if(SupportsTextureFormat(Core::Format::ASTC_4x4_FLOAT, device))
                return Core::Format::ASTC_4x4_FLOAT;
            if(SupportsTextureFormat(Core::Format::BC6H_UFLOAT, device))
                return Core::Format::BC6H_UFLOAT;
        }
        return SupportsTextureFormat(Core::Format::RGBA16_FLOAT, device) ? Core::Format::RGBA16_FLOAT : Core::Format::UNKNOWN;
    }
    if(textureAsset.payloadFormat() != TexturePayloadFormat::UastcLdr4x4)
        return Core::Format::UNKNOWN;

    if(SupportsTextureFormat(Core::Format::ASTC_4x4_UNORM, device))
        return Core::Format::ASTC_4x4_UNORM;

    if(SupportsTextureFormat(Core::Format::BC7_UNORM, device))
        return Core::Format::BC7_UNORM;

    return SupportsTextureFormat(Core::Format::RGBA8_UNORM, device) ? Core::Format::RGBA8_UNORM : Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectHdrOpaqueUploadFallback(const Core::Format::Enum failedFormat, Core::Device& device)noexcept{
    if(failedFormat == Core::Format::ASTC_4x4_FLOAT && SupportsTextureFormat(Core::Format::BC6H_UFLOAT, device))
        return Core::Format::BC6H_UFLOAT;
    if(failedFormat != Core::Format::RGBA16_FLOAT && SupportsTextureFormat(Core::Format::RGBA16_FLOAT, device))
        return Core::Format::RGBA16_FLOAT;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectLdrUploadFallback(const Core::Format::Enum failedFormat, Core::Device& device)noexcept{
    if(failedFormat == Core::Format::ASTC_4x4_UNORM && SupportsTextureFormat(Core::Format::BC7_UNORM, device))
        return Core::Format::BC7_UNORM;
    if(failedFormat != Core::Format::RGBA8_UNORM && SupportsTextureFormat(Core::Format::RGBA8_UNORM, device))
        return Core::Format::RGBA8_UNORM;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static Expected<CreatedTexture> CreateTextureWithFormatFallback(
    const Core::TextureDesc& textureDesc,
    const Core::Format::Enum failedFormat,
    Core::Format::Enum (&selectFallback)(Core::Format::Enum, Core::Device&)noexcept,
    Core::GraphicsRuntime& graphics,
    Core::Device& device
){
    Core::TextureDesc fallbackDesc = textureDesc;
    Core::Format::Enum format = failedFormat;
    Expected<Core::TextureHandle> texture = MakeUnexpected(Failure{});
    for(;;){
        const Core::Format::Enum fallbackFormat = selectFallback(format, device);
        if(fallbackFormat == Core::Format::UNKNOWN)
            break;
        fallbackDesc.setFormat(fallbackFormat);
        format = fallbackFormat;
        texture = graphics.createTexture(fallbackDesc);
        if(texture)
            return CreatedTexture{ Move(*texture), format };
    }
    return MakeUnexpected(texture.error());
}

[[nodiscard]] static Core::TextureDimension::Enum ToCoreTextureDimension(const TextureDimension::Enum dimension)noexcept{
    switch(dimension){
    case TextureDimension::Texture2D: return Core::TextureDimension::Texture2D;
    case TextureDimension::TextureCube: return Core::TextureDimension::TextureCube;
    case TextureDimension::Texture3D: return Core::TextureDimension::Texture3D;
    default: return Core::TextureDimension::Unknown;
    }
}

[[nodiscard]] static Core::GpuDescriptorClass::Enum ToSampledImageDescriptorClass(const TextureDimension::Enum dimension)noexcept{
    switch(dimension){
    case TextureDimension::Texture2D: return Core::GpuDescriptorClass::SampledImage;
    case TextureDimension::TextureCube: return Core::GpuDescriptorClass::SampledImageCube;
    case TextureDimension::Texture3D: return Core::GpuDescriptorClass::SampledImage3D;
    default: return Core::GpuDescriptorClass::Count;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TextureAssetLoader::Create(const Texture& textureAsset, const Name& debugName, const TStringView ownerName, Core::GraphicsRuntime& graphics, TextureGpuResource& outResource){
    NWB_ASSERT(!outResource.valid());
    if(outResource.texture || outResource.sampledImageHeapHandle.valid() || outResource.format != Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture resource is partially initialized; release it before recreating"), ownerName);
        return false;
    }
    // Texture::loadBinary already validated the cooked payload; keep a debug-only invariant here.
    NWB_ASSERT(textureAsset.validatePayload());

    Core::Device& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot load texture '{}' without an initialized descriptor heap"), ownerName, ConvertString(textureAsset.virtualPath().resolvedText()));
        return false;
    }

    Core::Format::Enum format = __hidden_texture_loader::SelectUploadFormat(textureAsset, device);
    if(format == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: device cannot sample the required texture format for '{}'"), ownerName, ConvertString(textureAsset.virtualPath().resolvedText()));
        return false;
    }

    const Name imageName = debugName ? debugName : textureAsset.virtualPath();
    const Core::TextureDimension::Enum textureDimension = __hidden_texture_loader::ToCoreTextureDimension(textureAsset.dimension());
    const Core::GpuDescriptorClass::Enum descriptorClass = __hidden_texture_loader::ToSampledImageDescriptorClass(textureAsset.dimension());
    // Texture::loadBinary already validated the cooked dimension; keep a debug-only invariant here.
    NWB_ASSERT(textureDimension != Core::TextureDimension::Unknown && descriptorClass != Core::GpuDescriptorClass::Count);

    Core::TextureDesc textureDesc;
    textureDesc
        .setWidth(textureAsset.width())
        .setHeight(textureAsset.height())
        .setMipLevels(static_cast<u32>(textureAsset.mipLevels().size()))
        .setFormat(format)
        .setDimension(textureDimension)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        // Declare Transfer, Compute, and Graphics transports so graph-owned uploads can route static textures across all three queues.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setName(imageName)
    ;
    if(textureAsset.dimension() == TextureDimension::TextureCube)
        textureDesc.setArraySize(TextureFormat::s_TextureCubeFaceCount);
    else if(textureAsset.dimension() == TextureDimension::Texture3D)
        textureDesc.setDepth(textureAsset.depth());

    auto textureResult = graphics.createTexture(textureDesc);
    if(
        !textureResult
        && ((textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4 && format != Core::Format::RGBA8_UNORM) || (textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4 && textureAsset.alphaMode() == TextureAlphaMode::Opaque))
    ){
        auto fallbackResult = __hidden_texture_loader::CreateTextureWithFormatFallback(
            textureDesc,
            format,
            textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4 ? __hidden_texture_loader::SelectLdrUploadFallback : __hidden_texture_loader::SelectHdrOpaqueUploadFallback,
            graphics,
            device
        );
        if(fallbackResult){
            format = fallbackResult->format;
            textureResult = Move(fallbackResult->texture);
        }
        else
            textureResult = MakeUnexpected(fallbackResult.error());
    }
    if(!textureResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create texture '{}'"), ownerName, ConvertString(imageName.resolvedText()));
        return false;
    }
    Core::TextureHandle texture = Move(*textureResult);

    __hidden_texture_loader::InitializeBasisTranscoder();

    Core::Alloc::ScratchArena scratchArena(s_TextureUploadScratchArena);
    TextureMipDecoder mipDecoder;
    Vector<TextureDecodedMipUpload, Core::Alloc::ScratchArena> decodedMips{scratchArena};
    decodedMips.reserve(textureAsset.mipLevels().size());
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex){
        auto decoded = mipDecoder.decode(textureAsset, textureAsset.mipLevels()[mipIndex], static_cast<u32>(mipIndex), format, scratchArena);
        if(!decoded)
            return false;
        decodedMips.push_back(Move(*decoded));
    }

    Vector<Core::GraphicsRuntime::TextureUploadRegion, Core::Alloc::ScratchArena> uploadRegions{scratchArena};
    usize uploadRegionReserveCount = 0u;
    const b8 isVolumeTexture = textureAsset.dimension() == TextureDimension::Texture3D;
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex)
        uploadRegionReserveCount += isVolumeTexture ? 1u : static_cast<usize>(textureAsset.mipLevels()[mipIndex].sliceCount);
    uploadRegions.reserve(uploadRegionReserveCount);
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex){
        const TextureMipLevel& mip = textureAsset.mipLevels()[mipIndex];
        const TextureDecodedMipUpload& decoded = decodedMips[mipIndex];
        NWB_ASSERT(!decoded.bytes.empty() && decoded.rowPitch != 0u && decoded.sliceByteCount != 0u && mip.sliceCount != 0u);

        if(textureAsset.dimension() == TextureDimension::Texture3D){
            uploadRegions.push_back(Core::GraphicsRuntime::TextureUploadRegion{
                .data = decoded.bytes.data(),
                .dataSize = decoded.bytes.size(),
                .rowPitch = decoded.rowPitch,
                .depthPitch = decoded.sliceByteCount,
                .arraySlice = 0u,
                .mipLevel = static_cast<u32>(mipIndex),
            });
            continue;
        }

        for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
            uploadRegions.push_back(Core::GraphicsRuntime::TextureUploadRegion{
                .data = decoded.bytes.data() + static_cast<usize>(sliceIndex) * decoded.sliceByteCount,
                .dataSize = decoded.sliceByteCount,
                .rowPitch = decoded.rowPitch,
                .depthPitch = decoded.sliceByteCount,
                .arraySlice = sliceIndex,
                .mipLevel = static_cast<u32>(mipIndex),
            });
        }
    }

    // Resolve descriptor failures before accepting the upload; publish the whole resource before releasing this lease.
    const Expected<Core::GpuDescriptorHandle> sampledImageHandleResult = heap.allocate(descriptorClass);
    if(!sampledImageHandleResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to allocate a bindless sampled-image slot for texture '{}'"), ownerName, ConvertString(imageName.resolvedText()));
        return false;
    }
    const Core::GpuDescriptorHandle sampledImageHandle = *sampledImageHandleResult;
    Core::GpuDescriptorRetirementGuard releaseDescriptor(heap, sampledImageHandle);

    if(!heap.write(sampledImageHandle, Core::DescriptorWriteItem::TextureSrv(0u, texture.get(), format, Core::s_AllSubresources, textureDimension))){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to write the bindless sampled-image slot for texture '{}'"), ownerName, ConvertString(imageName.resolvedText()));
        return false;
    }

    Core::QueueSubmissionToken uploadToken;
    if(!graphics.uploadTextureBatch(Core::GraphicsRuntime::TextureUploadBatchDesc{
        .destination = texture,
        .regions = uploadRegions.data(),
        .regionCount = uploadRegions.size(),
        .acceptedToken = &uploadToken,
        .finalState = Core::ResourceStates::ShaderResource,
        // Descriptor state is the post-upload contract, not the native layout before first write.
        .physicalInitialState = Core::ResourceStates::Unknown,
        .hasPhysicalInitialState = true,
    }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to submit graph-owned texture upload for '{}'"), ownerName, ConvertString(imageName.resolvedText()));
        return false;
    }

    outResource.texture = Move(texture);
    outResource.sampledImageHeapHandle = sampledImageHandle;
    outResource.format = format;
    outResource.readinessToken = uploadToken;
    releaseDescriptor.release();
    return true;
}

b8 TextureAssetLoader::Load(const Core::Assets::AssetRef<Texture>& textureAsset, const Name& debugName, const TStringView ownerName, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, TextureGpuResource& outResource){
    if(!Core::Assets::AssetManager::CheckLoaderEnter(textureAsset, outResource, ownerName))
        return outResource.valid();

    const Name& textureVirtualPath = textureAsset.name();

    auto loadedAsset = assetManager.loadTypedSync<Texture>(textureVirtualPath, ownerName);
    const Texture* loadedTexture = loadedAsset ? loadedAsset->get() : nullptr;
    if(!loadedTexture)
        return false;

    return Create(*loadedTexture, debugName, ownerName, graphics, outResource);
}

void TextureAssetLoader::Release(TextureGpuResource& inOutResource, Core::GraphicsRuntime& graphics){
    if(inOutResource.sampledImageHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
        if(heap.isInitialized())
            heap.free(inOutResource.sampledImageHeapHandle);
        inOutResource.sampledImageHeapHandle = Core::GpuDescriptorHandle::Invalid();
    }

    inOutResource.texture.reset();
    inOutResource.format = Core::Format::UNKNOWN;
    inOutResource.readinessToken = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

