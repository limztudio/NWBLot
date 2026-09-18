// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "loader.h"

#include "texture_mip_decoder.h"

#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <global/sync.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureFormat::s_UastcBlockHeight;
using TextureFormat::s_UastcBlockWidth;
using TextureFormat::s_UastcBytesPerBlock;
static_assert(sizeof(basist::half_float) == sizeof(u16), "Basis HDR output must use 16-bit half components");
static constexpr Core::FormatSupport::Mask s_RequiredTextureFormatSupport =
    Core::FormatSupport::Texture
    | Core::FormatSupport::ShaderSample
;

Futex s_BasisTranscoderInitializationMutex;
Atomic<bool> s_BasisTranscoderInitialized = false;


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

[[nodiscard]] static bool SupportsTextureFormat(Core::Device& device, const Core::Format::Enum format){
    return (device.queryFormatSupport(format) & s_RequiredTextureFormatSupport) == s_RequiredTextureFormatSupport;
}

[[nodiscard]] static Core::Format::Enum PickColorSpaceFormat(
    Core::Device& device,
    const TextureColorSpace::Enum colorSpace,
    const Core::Format::Enum srgbFormat,
    const Core::Format::Enum linearFormat
){
    const Core::Format::Enum format = colorSpace == TextureColorSpace::Srgb ? srgbFormat : linearFormat;
    return SupportsTextureFormat(device, format) ? format : Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectUploadFormat(
    Core::Device& device,
    const Texture& textureAsset
){
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

    const Core::Format::Enum astcFormat = PickColorSpaceFormat(
        device,
        textureAsset.colorSpace(),
        Core::Format::ASTC_4x4_UNORM_SRGB,
        Core::Format::ASTC_4x4_UNORM
    );
    if(astcFormat != Core::Format::UNKNOWN)
        return astcFormat;

    const Core::Format::Enum bcFormat = PickColorSpaceFormat(
        device,
        textureAsset.colorSpace(),
        Core::Format::BC7_UNORM_SRGB,
        Core::Format::BC7_UNORM
    );
    if(bcFormat != Core::Format::UNKNOWN)
        return bcFormat;

    return PickColorSpaceFormat(
        device,
        textureAsset.colorSpace(),
        Core::Format::RGBA8_UNORM_SRGB,
        Core::Format::RGBA8_UNORM
    );
}

[[nodiscard]] static Core::Format::Enum SelectRgbaUploadFormat(Core::Device& device, const TextureColorSpace::Enum colorSpace){
    return PickColorSpaceFormat(device, colorSpace, Core::Format::RGBA8_UNORM_SRGB, Core::Format::RGBA8_UNORM);
}

[[nodiscard]] static Core::Format::Enum SelectHdrOpaqueUploadFallback(
    Core::Device& device,
    const Core::Format::Enum failedFormat
){
    if(failedFormat == Core::Format::ASTC_4x4_FLOAT && SupportsTextureFormat(device, Core::Format::BC6H_UFLOAT))
        return Core::Format::BC6H_UFLOAT;
    if(failedFormat != Core::Format::RGBA16_FLOAT && SupportsTextureFormat(device, Core::Format::RGBA16_FLOAT))
        return Core::Format::RGBA16_FLOAT;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectLdrUploadFallback(
    Core::Device& device,
    const Core::Format::Enum failedFormat,
    const TextureColorSpace::Enum colorSpace
){
    const Core::Format::Enum bcFormat = PickColorSpaceFormat(
        device,
        colorSpace,
        Core::Format::BC7_UNORM_SRGB,
        Core::Format::BC7_UNORM
    );
    if(Core::Format::IsAstc4x4LdrFormat(failedFormat) && bcFormat != Core::Format::UNKNOWN)
        return bcFormat;

    const Core::Format::Enum rgbaFormat = SelectRgbaUploadFormat(device, colorSpace);
    if(failedFormat != rgbaFormat)
        return rgbaFormat;
    return Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::TextureHandle CreateTextureWithFormatFallback(
    Core::GraphicsRuntime& graphics,
    Core::TextureDesc& textureDesc,
    Core::Device& device,
    Core::Format::Enum& inOutFormat,
    Core::Format::Enum (*selectFallback)(Core::Device&, Core::Format::Enum, const void*),
    const void* fallbackContext
){
    Core::TextureHandle texture = graphics.createTexture(textureDesc);
    while(!texture){
        const Core::Format::Enum fallbackFormat = selectFallback(device, inOutFormat, fallbackContext);
        if(fallbackFormat == Core::Format::UNKNOWN)
            break;
        textureDesc.setFormat(fallbackFormat);
        inOutFormat = fallbackFormat;
        texture = graphics.createTexture(textureDesc);
    }
    return texture;
}

[[nodiscard]] static Core::Format::Enum SelectLdrFallbackThunk(Core::Device& device, Core::Format::Enum failedFormat, const void* context){
    const TextureColorSpace::Enum* colorSpace = static_cast<const TextureColorSpace::Enum*>(context);
    return SelectLdrUploadFallback(device, failedFormat, *colorSpace);
}

[[nodiscard]] static Core::Format::Enum SelectHdrOpaqueFallbackThunk(Core::Device& device, Core::Format::Enum failedFormat, const void*){
    return SelectHdrOpaqueUploadFallback(device, failedFormat);
}

[[nodiscard]] static Core::GraphicsRuntime::TextureUploadRegion MakeMipUploadRegion(
    const u8* data,
    const usize dataSize,
    const u32 rowPitch,
    const u32 sliceByteCount,
    const u32 arraySlice,
    const u32 mipLevel
){
    return Core::GraphicsRuntime::TextureUploadRegion{
        .data = data,
        .dataSize = dataSize,
        .rowPitch = rowPitch,
        .depthPitch = sliceByteCount,
        .arraySlice = arraySlice,
        .mipLevel = mipLevel,
    };
}

[[nodiscard]] static Core::TextureDimension::Enum ToCoreTextureDimension(const TextureDimension::Enum dimension){
    switch(dimension){
    case TextureDimension::Texture2D: return Core::TextureDimension::Texture2D;
    case TextureDimension::TextureCube: return Core::TextureDimension::TextureCube;
    case TextureDimension::Texture3D: return Core::TextureDimension::Texture3D;
    default: return Core::TextureDimension::Unknown;
    }
}

[[nodiscard]] static Core::GpuDescriptorClass::Enum ToSampledImageDescriptorClass(const TextureDimension::Enum dimension){
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


bool TextureAssetLoader::Create(
    TextureGpuResource& outResource,
    const Texture& textureAsset,
    const Name& debugName,
    Core::GraphicsRuntime& graphics,
    const tchar* const ownerName
){
    const tchar* const owner = ownerName ? ownerName : NWB_TEXT("TextureAssetLoader");
    NWB_ASSERT(!outResource.valid());
    if(outResource.valid())
        return true;
    if(outResource.texture || outResource.sampledImageHeapHandle.valid() || outResource.format != Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture resource is partially initialized; release it before recreating"), owner);
        return false;
    }
    // Texture::loadBinary already validated the cooked payload; keep a debug-only invariant here.
    NWB_ASSERT(textureAsset.validatePayload());

    Core::Device& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot load texture '{}' without an initialized descriptor heap")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
        return false;
    }

    Core::Format::Enum format = __hidden_texture_loader::SelectUploadFormat(device, textureAsset);
    if(format == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: device cannot sample the required texture format for '{}'")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
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
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName(imageName)
    ;
    if(textureAsset.dimension() == TextureDimension::TextureCube)
        textureDesc.setArraySize(TextureFormat::s_TextureCubeFaceCount);
    else if(textureAsset.dimension() == TextureDimension::Texture3D)
        textureDesc.setDepth(textureAsset.depth());

    Core::TextureHandle texture = graphics.createTexture(textureDesc);
    if(
        !texture
        && textureAsset.payloadFormat() == TexturePayloadFormat::UastcLdr4x4
        && Core::Format::IsLdrCompressedFormat(format)
    ){
        const TextureColorSpace::Enum colorSpace = textureAsset.colorSpace();
        texture = __hidden_texture_loader::CreateTextureWithFormatFallback(
            graphics,
            textureDesc,
            device,
            format,
            &__hidden_texture_loader::SelectLdrFallbackThunk,
            &colorSpace
        );
    }
    if(
        !texture
        && textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4
        && textureAsset.alphaMode() == TextureAlphaMode::Opaque
    ){
        texture = __hidden_texture_loader::CreateTextureWithFormatFallback(
            graphics,
            textureDesc,
            device,
            format,
            &__hidden_texture_loader::SelectHdrOpaqueFallbackThunk,
            nullptr
        );
    }
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    __hidden_texture_loader::InitializeBasisTranscoder();

    Core::Alloc::ScratchArena scratchArena(AssetsTextureArenaScope::s_UploadScratchArena);
    TextureMipDecoder mipDecoder;
    Vector<TextureDecodedMipUpload, Core::Alloc::ScratchArena> decodedMips{scratchArena};
    decodedMips.reserve(textureAsset.mipLevels().size());
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex){
        decodedMips.emplace_back(scratchArena);
        if(!mipDecoder.decode(
            textureAsset,
            textureAsset.mipLevels()[mipIndex],
            static_cast<u32>(mipIndex),
            format,
            decodedMips.back()
        )){
            return false;
        }
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
        if(
            decoded.bytes.empty()
            || decoded.rowPitch == 0u
            || decoded.sliceByteCount == 0u
            || mip.sliceCount == 0u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: decoded texture '{}' mip {} has an empty upload region")
                , owner
                , StringConvert(imageName.c_str())
                , static_cast<u32>(mipIndex)
            );
            return false;
        }

        if(textureAsset.dimension() == TextureDimension::Texture3D){
            uploadRegions.emplace_back(__hidden_texture_loader::MakeMipUploadRegion(
                decoded.bytes.data(),
                decoded.bytes.size(),
                decoded.rowPitch,
                decoded.sliceByteCount,
                0u,
                static_cast<u32>(mipIndex)
            ));
            continue;
        }

        for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
            uploadRegions.emplace_back(__hidden_texture_loader::MakeMipUploadRegion(
                decoded.bytes.data() + static_cast<usize>(sliceIndex) * decoded.sliceByteCount,
                decoded.sliceByteCount,
                decoded.rowPitch,
                decoded.sliceByteCount,
                sliceIndex,
                static_cast<u32>(mipIndex)
            ));
        }
    }

    Core::QueueSubmissionToken uploadToken;
    if(!graphics.uploadTextureBatch(Core::GraphicsRuntime::TextureUploadBatchDesc{
        .destination = texture,
        .regions = uploadRegions.data(),
        .regionCount = uploadRegions.size(),
        .finalState = Core::ResourceStates::ShaderResource,
        // The loader has just created the image. Its descriptor state is the post-upload contract, not the native VkImage layout before this batch records its first write.
        .physicalInitialState = Core::ResourceStates::Unknown,
        .acceptedToken = &uploadToken,
        .hasPhysicalInitialState = true,
    })){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to submit graph-owned texture upload for '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    const Core::GpuDescriptorHandle sampledImageHandle = heap.allocate(descriptorClass);
    if(!sampledImageHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to allocate a bindless sampled-image slot for texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }
    if(!heap.write(sampledImageHandle, Core::DescriptorWriteItem::Texture_SRV(
        0u,
        texture.get(),
        format,
        Core::s_AllSubresources,
        textureDimension
    ))){
        heap.free(sampledImageHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to write the bindless sampled-image slot for texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    outResource.texture = Move(texture);
    outResource.sampledImageHeapHandle = sampledImageHandle;
    outResource.format = format;
    return true;
}

bool TextureAssetLoader::Load(
    TextureGpuResource& outResource,
    const Core::Assets::AssetRef<Texture>& textureAsset,
    const Name& debugName,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    const tchar* const ownerName
){
    const tchar* const owner = ownerName ? ownerName : NWB_TEXT("TextureAssetLoader");
    if(!Core::Assets::AssetManager::CheckLoaderEnter(textureAsset, outResource, owner, "texture"))
        return outResource.valid();

    const Name& textureVirtualPath = textureAsset.name();

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Texture* loadedTexture = assetManager.loadTypedSync<Texture>(
        textureVirtualPath,
        loadedAsset,
        MakeNotNull(NWB_TEXT("TextureAssetLoader::Load")),
        owner,
        "texture"
    );
    if(!loadedTexture)
        return false;

    return Create(outResource, *loadedTexture, debugName, graphics, owner);
}

void TextureAssetLoader::Release(TextureGpuResource& inOutResource, Core::GraphicsRuntime& graphics){
    if(inOutResource.sampledImageHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
        if(heap.isInitialized())
            heap.free(inOutResource.sampledImageHeapHandle);
        inOutResource.sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid();
    }

    inOutResource.texture.reset();
    inOutResource.format = Core::Format::UNKNOWN;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

