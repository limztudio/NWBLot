// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "loader.h"

#include "texture_mip_decoder.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
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
static Atomic<bool> s_BasisTranscoderInitialized = false;

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

[[nodiscard]] static bool SupportsTextureFormat(Core::Device& device, const Core::Format::Enum format)noexcept{
    return (device.queryFormatSupport(format) & s_RequiredTextureFormatSupport) == s_RequiredTextureFormatSupport;
}

[[nodiscard]] static Core::Format::Enum SelectUploadFormat(Core::Device& device, const Texture& textureAsset)noexcept{
    if(textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4){
        if(textureAsset.alphaMode() == TextureAlphaMode::Opaque){
            if(SupportsTextureFormat(device, Core::Format::ASTC_4x4_FLOAT))
                return Core::Format::ASTC_4x4_FLOAT;
            if(SupportsTextureFormat(device, Core::Format::BC6H_UFLOAT))
                return Core::Format::BC6H_UFLOAT;
        }
        return SupportsTextureFormat(device, Core::Format::RGBA16_FLOAT) ? Core::Format::RGBA16_FLOAT : Core::Format::UNKNOWN;
    }
    if(textureAsset.payloadFormat() != TexturePayloadFormat::UastcLdr4x4)
        return Core::Format::UNKNOWN;

    if(SupportsTextureFormat(device, Core::Format::ASTC_4x4_UNORM))
        return Core::Format::ASTC_4x4_UNORM;

    if(SupportsTextureFormat(device, Core::Format::BC7_UNORM))
        return Core::Format::BC7_UNORM;

    return SupportsTextureFormat(device, Core::Format::RGBA8_UNORM) ? Core::Format::RGBA8_UNORM : Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectHdrOpaqueUploadFallback(Core::Device& device, const Core::Format::Enum failedFormat)noexcept{
    if(failedFormat == Core::Format::ASTC_4x4_FLOAT && SupportsTextureFormat(device, Core::Format::BC6H_UFLOAT))
        return Core::Format::BC6H_UFLOAT;
    if(failedFormat != Core::Format::RGBA16_FLOAT && SupportsTextureFormat(device, Core::Format::RGBA16_FLOAT))
        return Core::Format::RGBA16_FLOAT;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectLdrUploadFallback(Core::Device& device, const Core::Format::Enum failedFormat)noexcept{
    if(failedFormat == Core::Format::ASTC_4x4_UNORM && SupportsTextureFormat(device, Core::Format::BC7_UNORM))
        return Core::Format::BC7_UNORM;
    if(failedFormat != Core::Format::RGBA8_UNORM && SupportsTextureFormat(device, Core::Format::RGBA8_UNORM))
        return Core::Format::RGBA8_UNORM;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static Expected<CreatedTexture> CreateTextureWithFormatFallback(
    Core::GraphicsRuntime& graphics,
    const Core::TextureDesc& textureDesc,
    Core::Device& device,
    const Core::Format::Enum failedFormat,
    Core::Format::Enum (&selectFallback)(Core::Device&, Core::Format::Enum)noexcept
){
    Core::TextureDesc fallbackDesc = textureDesc;
    Core::Format::Enum format = failedFormat;
    Expected<Core::TextureHandle> texture = MakeUnexpected(Failure{});
    for(;;){
        const Core::Format::Enum fallbackFormat = selectFallback(device, format);
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

[[nodiscard]] static Core::GraphicsRuntime::TextureUploadRegion MakeMipUploadRegion(const NotNull<const u8*> data, const usize dataSize, const usize rowPitch, const usize sliceByteCount, const u32 arraySlice, const u32 mipLevel)noexcept{
    return Core::GraphicsRuntime::TextureUploadRegion{ .data = data.get(), .dataSize = dataSize, .rowPitch = rowPitch, .depthPitch = sliceByteCount, .arraySlice = arraySlice, .mipLevel = mipLevel, };
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
    default: return Core::GpuDescriptorClass::kCount;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TextureAssetLoader::Create(TextureGpuResource& outResource, const Texture& textureAsset, const Name& debugName, Core::GraphicsRuntime& graphics, const TStringView ownerName){
    const TStringView owner = ownerName;
    NWB_ASSERT(!outResource.valid());
    if(outResource.texture || outResource.sampledImageHeapHandle.valid() || outResource.format != Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture resource is partially initialized; release it before recreating"), owner);
        return false;
    }
    // Texture::loadBinary already validated the cooked payload; keep a debug-only invariant here.
    NWB_ASSERT(textureAsset.validatePayload());

    Core::Device& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot load texture '{}' without an initialized descriptor heap"), owner, StringConvert(textureAsset.virtualPath().resolvedText()));
        return false;
    }

    Core::Format::Enum format = __hidden_texture_loader::SelectUploadFormat(device, textureAsset);
    if(format == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: device cannot sample the required texture format for '{}'"), owner, StringConvert(textureAsset.virtualPath().resolvedText()));
        return false;
    }

    const Name imageName = debugName ? debugName : textureAsset.virtualPath();
    const Core::TextureDimension::Enum textureDimension = __hidden_texture_loader::ToCoreTextureDimension(textureAsset.dimension());
    const Core::GpuDescriptorClass::Enum descriptorClass = __hidden_texture_loader::ToSampledImageDescriptorClass(textureAsset.dimension());
    // Texture::loadBinary already validated the cooked dimension; keep a debug-only invariant here.
    NWB_ASSERT(textureDimension != Core::TextureDimension::Unknown && descriptorClass != Core::GpuDescriptorClass::kCount);

    Core::TextureDesc textureDesc;
    textureDesc
        .setWidth(textureAsset.width())
        .setHeight(textureAsset.height())
        .setMipLevels(static_cast<u32>(textureAsset.mipLevels().size()))
        .setFormat(format)
        .setDimension(textureDimension)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        // Static decoded assets are immediately sampled by Graphics/Compute, while sizeable batches may use a dedicated Transfer producer.  Declare all three consumer/producer transports before creation so the graph-owned upload can choose the automatic Transfer -> Compute -> Graphics route safely.
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
            graphics,
            textureDesc,
            device,
            format,
            textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4 ? __hidden_texture_loader::SelectLdrUploadFallback : __hidden_texture_loader::SelectHdrOpaqueUploadFallback
        );
        if(fallbackResult){
            format = fallbackResult->format;
            textureResult = Move(fallbackResult->texture);
        }
        else
            textureResult = MakeUnexpected(fallbackResult.error());
    }
    if(!textureResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create texture '{}'"), owner, StringConvert(imageName.resolvedText()));
        return false;
    }
    Core::TextureHandle texture = Move(*textureResult);

    __hidden_texture_loader::InitializeBasisTranscoder();

    Core::Alloc::ScratchArena scratchArena(AssetsTextureArenaScope::s_UploadScratchArena);
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
    const bool isVolumeTexture = textureAsset.dimension() == TextureDimension::Texture3D;
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex)
        uploadRegionReserveCount += isVolumeTexture ? 1u : static_cast<usize>(textureAsset.mipLevels()[mipIndex].sliceCount);
    uploadRegions.reserve(uploadRegionReserveCount);
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex){
        const TextureMipLevel& mip = textureAsset.mipLevels()[mipIndex];
        const TextureDecodedMipUpload& decoded = decodedMips[mipIndex];
        NWB_ASSERT(!decoded.bytes.empty() && decoded.rowPitch != 0u && decoded.sliceByteCount != 0u && mip.sliceCount != 0u);

        if(textureAsset.dimension() == TextureDimension::Texture3D){
            uploadRegions.emplace_back(__hidden_texture_loader::MakeMipUploadRegion(MakeNotNull(decoded.bytes.data()), decoded.bytes.size(), decoded.rowPitch, decoded.sliceByteCount, 0u, static_cast<u32>(mipIndex)));
            continue;
        }

        for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
            uploadRegions.emplace_back(__hidden_texture_loader::MakeMipUploadRegion(
                MakeNotNull(decoded.bytes.data() + static_cast<usize>(sliceIndex) * decoded.sliceByteCount),
                decoded.sliceByteCount,
                decoded.rowPitch,
                decoded.sliceByteCount,
                sliceIndex,
                static_cast<u32>(mipIndex)
            ));
        }
    }

    // Resolve descriptor failures before accepting the upload; publish the whole resource before releasing this lease.
    const Expected<Core::GpuDescriptorHandle> sampledImageHandleResult = heap.allocate(descriptorClass);
    if(!sampledImageHandleResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to allocate a bindless sampled-image slot for texture '{}'"), owner, StringConvert(imageName.resolvedText()));
        return false;
    }
    const Core::GpuDescriptorHandle sampledImageHandle = *sampledImageHandleResult;
    Core::GpuDescriptorRetirementGuard releaseDescriptor(heap, sampledImageHandle);

    if(!heap.write(sampledImageHandle, Core::DescriptorWriteItem::TextureSrv(0u, texture.get(), format, Core::s_AllSubresources, textureDimension))){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to write the bindless sampled-image slot for texture '{}'"), owner, StringConvert(imageName.resolvedText()));
        return false;
    }

    Core::QueueSubmissionToken uploadToken;
    if(!graphics.uploadTextureBatch(Core::GraphicsRuntime::TextureUploadBatchDesc{
        .destination = texture,
        .regions = uploadRegions.data(),
        .regionCount = uploadRegions.size(),
        .acceptedToken = &uploadToken,
        .finalState = Core::ResourceStates::ShaderResource,
        // The loader has just created the image. Its descriptor state is the post-upload contract, not the native VkImage layout before this batch records its first write.
        .physicalInitialState = Core::ResourceStates::Unknown,
        .hasPhysicalInitialState = true,
    }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to submit graph-owned texture upload for '{}'"), owner, StringConvert(imageName.resolvedText()));
        return false;
    }

    outResource.texture = Move(texture);
    outResource.sampledImageHeapHandle = sampledImageHandle;
    outResource.format = format;
    outResource.readinessToken = uploadToken;
    releaseDescriptor.release();
    return true;
}

bool TextureAssetLoader::Load(TextureGpuResource& outResource, const Core::Assets::AssetRef<Texture>& textureAsset, const Name& debugName, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, const TStringView ownerName){
    const TStringView owner = ownerName;
    if(!Core::Assets::AssetManager::CheckLoaderEnter(textureAsset, outResource, owner, Texture::s_AssetTypeText))
        return outResource.valid();

    const Name& textureVirtualPath = textureAsset.name();

    auto loadedAsset = assetManager.loadTypedSync<Texture>(textureVirtualPath, owner, Texture::s_AssetTypeText);
    const Texture* loadedTexture = loadedAsset ? loadedAsset->get() : nullptr;
    if(!loadedTexture)
        return false;

    return Create(outResource, *loadedTexture, debugName, graphics, owner);
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

