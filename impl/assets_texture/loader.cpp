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
static constexpr u32 s_RgbaBytesPerTexel = 4u;
static constexpr u32 s_Rgba16FloatComponentCount = 4u;
static constexpr u32 s_Rgba16FloatBytesPerTexel = static_cast<u32>(sizeof(basist::half_float) * s_Rgba16FloatComponentCount);
static constexpr u32 s_Rgba16FloatAlphaByteOffset = static_cast<u32>(sizeof(basist::half_float) * (s_Rgba16FloatComponentCount - 1u));
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

    const Core::Format::Enum astcFormat = textureAsset.colorSpace() == TextureColorSpace::Srgb
        ? Core::Format::ASTC_4x4_UNORM_SRGB
        : Core::Format::ASTC_4x4_UNORM
    ;
    if(SupportsTextureFormat(device, astcFormat))
        return astcFormat;

    const Core::Format::Enum bcFormat = textureAsset.colorSpace() == TextureColorSpace::Srgb
        ? Core::Format::BC7_UNORM_SRGB
        : Core::Format::BC7_UNORM
    ;
    if(SupportsTextureFormat(device, bcFormat))
        return bcFormat;

    const Core::Format::Enum rgbaFormat = textureAsset.colorSpace() == TextureColorSpace::Srgb
        ? Core::Format::RGBA8_UNORM_SRGB
        : Core::Format::RGBA8_UNORM
    ;
    return SupportsTextureFormat(device, rgbaFormat) ? rgbaFormat : Core::Format::UNKNOWN;
}

[[nodiscard]] static Core::Format::Enum SelectRgbaUploadFormat(Core::Device& device, const TextureColorSpace::Enum colorSpace){
    const Core::Format::Enum rgbaFormat = colorSpace == TextureColorSpace::Srgb
        ? Core::Format::RGBA8_UNORM_SRGB
        : Core::Format::RGBA8_UNORM
    ;
    return SupportsTextureFormat(device, rgbaFormat) ? rgbaFormat : Core::Format::UNKNOWN;
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

[[nodiscard]] static bool IsAstc4x4LdrFormat(const Core::Format::Enum format){
    return format == Core::Format::ASTC_4x4_UNORM || format == Core::Format::ASTC_4x4_UNORM_SRGB;
}

[[nodiscard]] static bool IsBc7LdrFormat(const Core::Format::Enum format){
    return format == Core::Format::BC7_UNORM || format == Core::Format::BC7_UNORM_SRGB;
}

[[nodiscard]] static bool IsLdrCompressedFormat(const Core::Format::Enum format){
    return IsAstc4x4LdrFormat(format) || IsBc7LdrFormat(format);
}

[[nodiscard]] static Core::Format::Enum SelectLdrUploadFallback(
    Core::Device& device,
    const Core::Format::Enum failedFormat,
    const TextureColorSpace::Enum colorSpace
){
    const Core::Format::Enum bcFormat = colorSpace == TextureColorSpace::Srgb
        ? Core::Format::BC7_UNORM_SRGB
        : Core::Format::BC7_UNORM
    ;
    if(IsAstc4x4LdrFormat(failedFormat) && SupportsTextureFormat(device, bcFormat))
        return bcFormat;

    const Core::Format::Enum rgbaFormat = SelectRgbaUploadFormat(device, colorSpace);
    if(failedFormat != rgbaFormat)
        return rgbaFormat;
    return Core::Format::UNKNOWN;
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
    if(outResource.valid())
        return true;
    if(outResource.texture || outResource.sampledImageHeapHandle.valid() || outResource.format != Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture resource is partially initialized; release it before recreating"), owner);
        return false;
    }
    if(!textureAsset.validatePayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture '{}' has invalid cooked texture data")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
        return false;
    }

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
    if(textureDimension == Core::TextureDimension::Unknown || descriptorClass == Core::GpuDescriptorClass::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture '{}' has an unsupported dimension")
            , owner
            , StringConvert(textureAsset.virtualPath().c_str())
        );
        return false;
    }

    Core::TextureDesc textureDesc;
    textureDesc
        .setWidth(textureAsset.width())
        .setHeight(textureAsset.height())
        .setMipLevels(static_cast<u32>(textureAsset.mipLevels().size()))
        .setFormat(format)
        .setDimension(textureDimension)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        // Static decoded assets are immediately sampled by Graphics/Compute, while sizeable batches may use a
        // dedicated Transfer producer.  Declare all three consumer/producer transports before creation so the
        // graph-owned upload can choose the automatic Transfer -> Compute -> Graphics route safely.
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
        && __hidden_texture_loader::IsLdrCompressedFormat(format)
    ){
        Core::Format::Enum fallbackFormat = __hidden_texture_loader::SelectLdrUploadFallback(
            device,
            format,
            textureAsset.colorSpace()
        );
        while(fallbackFormat != Core::Format::UNKNOWN){
            textureDesc.setFormat(fallbackFormat);
            format = fallbackFormat;
            texture = graphics.createTexture(textureDesc);
            if(texture)
                break;
            fallbackFormat = __hidden_texture_loader::SelectLdrUploadFallback(device, format, textureAsset.colorSpace());
        }
    }
    if(
        !texture
        && textureAsset.payloadFormat() == TexturePayloadFormat::UastcHdr4x4
        && textureAsset.alphaMode() == TextureAlphaMode::Opaque
    ){
        Core::Format::Enum fallbackFormat = __hidden_texture_loader::SelectHdrOpaqueUploadFallback(device, format);
        while(fallbackFormat != Core::Format::UNKNOWN){
            textureDesc.setFormat(fallbackFormat);
            format = fallbackFormat;
            texture = graphics.createTexture(textureDesc);
            if(texture)
                break;
            fallbackFormat = __hidden_texture_loader::SelectHdrOpaqueUploadFallback(device, format);
        }
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
            uploadRegions.emplace_back(Core::GraphicsRuntime::TextureUploadRegion{
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
            uploadRegions.emplace_back(Core::GraphicsRuntime::TextureUploadRegion{
                .data = decoded.bytes.data() + static_cast<usize>(sliceIndex) * decoded.sliceByteCount,
                .dataSize = decoded.sliceByteCount,
                .rowPitch = decoded.rowPitch,
                .depthPitch = decoded.sliceByteCount,
                .arraySlice = sliceIndex,
                .mipLevel = static_cast<u32>(mipIndex),
            });
        }
    }

    Core::QueueSubmissionToken uploadToken;
    if(!graphics.uploadTextureBatch(Core::GraphicsRuntime::TextureUploadBatchDesc{
        .destination = texture,
        .regions = uploadRegions.data(),
        .regionCount = uploadRegions.size(),
        .finalState = Core::ResourceStates::ShaderResource,
        // The loader has just created the image. Its descriptor state is the post-upload contract, not the native
        // VkImage layout before this batch records its first write.
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
    if(!textureAsset.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture asset reference is empty"), owner);
        return false;
    }
    if(outResource.valid())
        return true;

    const Name& textureVirtualPath = textureAsset.name();

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager.loadSync(Texture::AssetTypeName(), textureVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load texture asset '{}'"), owner, StringConvert(textureVirtualPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Texture::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: asset '{}' is not a texture"), owner, StringConvert(textureVirtualPath.c_str()));
        return false;
    }

    return Create(outResource, static_cast<const Texture&>(*loadedAsset), debugName, graphics, owner);
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

