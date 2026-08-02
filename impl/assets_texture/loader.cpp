// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "loader.h"

#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/vulkan/backend_context.h>
#include <global/sync.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_UastcBlockWidth = 4u;
static constexpr u32 s_UastcBlockHeight = 4u;
static constexpr u32 s_UastcBytesPerBlock = 16u;
static constexpr u32 s_RgbaBytesPerTexel = 4u;
static constexpr Core::FormatSupport::Mask s_RequiredTextureFormatSupport =
    Core::FormatSupport::Texture
    | Core::FormatSupport::ShaderSample
;

Futex s_BasisTranscoderInitializationMutex;
bool s_BasisTranscoderInitialized = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void InitializeBasisTranscoder(){
    ScopedLock lock(s_BasisTranscoderInitializationMutex);
    if(s_BasisTranscoderInitialized)
        return;

    basist::basisu_transcoder_init();
    s_BasisTranscoderInitialized = true;
}

[[nodiscard]] static bool SupportsTextureFormat(Core::Device& device, const Core::Format::Enum format){
    return (device.queryFormatSupport(format) & s_RequiredTextureFormatSupport) == s_RequiredTextureFormatSupport;
}

[[nodiscard]] static Core::Format::Enum SelectUploadFormat(Core::Device& device, const TextureColorSpace::Enum colorSpace){
    const Core::Format::Enum astcFormat = colorSpace == TextureColorSpace::Srgb
        ? Core::Format::ASTC_4x4_UNORM_SRGB
        : Core::Format::ASTC_4x4_UNORM
    ;
    if(SupportsTextureFormat(device, astcFormat))
        return astcFormat;

    const Core::Format::Enum rgbaFormat = colorSpace == TextureColorSpace::Srgb
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

[[nodiscard]] static bool IsAstc4x4Format(const Core::Format::Enum format){
    return format == Core::Format::ASTC_4x4_UNORM || format == Core::Format::ASTC_4x4_UNORM_SRGB;
}

[[nodiscard]] static bool DecodeTextureSliceAsAstc(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    u8* const outUploadBytes,
    const usize uploadByteCount
){
    if(
        !outUploadBytes
        || mip.sliceCount == 0u
        || sliceIndex >= mip.sliceCount
        || (mip.sizeBytes % mip.sliceCount) != 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: invalid UASTC slice layout"));
        return false;
    }
    const u64 sliceSizeBytes = mip.sizeBytes / mip.sliceCount;
    if(sliceSizeBytes != uploadByteCount || sliceSizeBytes > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: ASTC UASTC slice size is invalid"));
        return false;
    }

    const u64 sourceOffset = mip.offsetBytes + static_cast<u64>(sliceIndex) * sliceSizeBytes;
    const u8* const sourceData = textureAsset.uastcBlocks().data() + static_cast<usize>(sourceOffset);
    for(u64 blockOffset = 0u; blockOffset < sliceSizeBytes; blockOffset += s_UastcBytesPerBlock){
        basist::uastc_block sourceBlock;
        NWB_MEMCPY(
            &sourceBlock,
            sizeof(sourceBlock),
            sourceData + static_cast<usize>(blockOffset),
            sizeof(sourceBlock)
        );
        if(!basist::transcode_uastc_to_astc(sourceBlock, outUploadBytes + static_cast<usize>(blockOffset))){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC-to-ASTC transcoding failed"));
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool DecodeTextureSliceAsRgba(
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 sliceIndex,
    u8* const outUploadBytes,
    const usize uploadByteCount
){
    const u64 texelCount = static_cast<u64>(mip.width) * static_cast<u64>(mip.height);
    if(texelCount > Limit<usize>::s_Max / s_RgbaBytesPerTexel){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: RGBA8 fallback mip size exceeds addressable memory"));
        return false;
    }
    const u64 expectedUploadByteCount = texelCount * s_RgbaBytesPerTexel;
    if(
        !outUploadBytes
        || expectedUploadByteCount != uploadByteCount
        || mip.sliceCount == 0u
        || sliceIndex >= mip.sliceCount
        || (mip.sizeBytes % mip.sliceCount) != 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: invalid RGBA8 UASTC slice layout"));
        return false;
    }

    const bool srgb = textureAsset.colorSpace() == TextureColorSpace::Srgb;
    const u64 sourceSliceBytes = mip.sizeBytes / mip.sliceCount;
    const u64 sourceOffset = mip.offsetBytes + static_cast<u64>(sliceIndex) * sourceSliceBytes;
    const u8* const sourceData = textureAsset.uastcBlocks().data() + static_cast<usize>(sourceOffset);
    for(u32 blockY = 0u; blockY < mip.blockCountY; ++blockY){
        for(u32 blockX = 0u; blockX < mip.blockCountX; ++blockX){
            const u64 blockIndex = static_cast<u64>(blockY) * static_cast<u64>(mip.blockCountX) + blockX;
            const u64 blockOffset = blockIndex * s_UastcBytesPerBlock;
            basist::uastc_block sourceBlock;
            NWB_MEMCPY(
                &sourceBlock,
                sizeof(sourceBlock),
                sourceData + static_cast<usize>(blockOffset),
                sizeof(sourceBlock)
            );

            basist::color32 decodedTexels[s_UastcBlockWidth * s_UastcBlockHeight];
            if(!basist::unpack_uastc(sourceBlock, decodedTexels, srgb)){
                NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: UASTC-to-RGBA8 decoding failed"));
                return false;
            }

            for(u32 localY = 0u; localY < s_UastcBlockHeight; ++localY){
                const u64 destinationY = static_cast<u64>(blockY) * s_UastcBlockHeight + localY;
                if(destinationY >= mip.height)
                    break;

                for(u32 localX = 0u; localX < s_UastcBlockWidth; ++localX){
                    const u64 destinationX = static_cast<u64>(blockX) * s_UastcBlockWidth + localX;
                    if(destinationX >= mip.width)
                        break;

                    const usize sourceTexelIndex = static_cast<usize>(localY * s_UastcBlockWidth + localX);
                    const usize destinationByteOffset = static_cast<usize>(
                        (destinationY * static_cast<u64>(mip.width) + destinationX) * s_RgbaBytesPerTexel
                    );
                    const basist::color32& sourceTexel = decodedTexels[sourceTexelIndex];
                    outUploadBytes[destinationByteOffset + 0u] = sourceTexel.r;
                    outUploadBytes[destinationByteOffset + 1u] = sourceTexel.g;
                    outUploadBytes[destinationByteOffset + 2u] = sourceTexel.b;
                    outUploadBytes[destinationByteOffset + 3u] = sourceTexel.a;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] static bool UploadTextureMip(
    Core::CommandList& commandList,
    Core::Texture& texture,
    const Texture& textureAsset,
    const TextureMipLevel& mip,
    const u32 mipLevel,
    const Core::Format::Enum format,
    Vector<u8, Core::Alloc::ScratchArena>& scratchBytes
){
    usize rowPitch = 0u;
    usize sliceUploadByteCount = 0u;
    if(IsAstc4x4Format(format)){
        const u64 rowPitch64 = static_cast<u64>(mip.blockCountX) * s_UastcBytesPerBlock;
        if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / mip.blockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: ASTC mip row pitch exceeds addressable memory"));
            return false;
        }
        rowPitch = static_cast<usize>(rowPitch64);
        sliceUploadByteCount = static_cast<usize>(rowPitch64 * mip.blockCountY);
    }
    else{
        const u64 rowPitch64 = static_cast<u64>(mip.width) * s_RgbaBytesPerTexel;
        if(rowPitch64 > Limit<usize>::s_Max || rowPitch64 > Limit<u64>::s_Max / mip.height){
            NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: RGBA8 mip row pitch exceeds addressable memory"));
            return false;
        }
        rowPitch = static_cast<usize>(rowPitch64);
        sliceUploadByteCount = static_cast<usize>(rowPitch64 * mip.height);
    }

    if(mip.sliceCount == 0u || sliceUploadByteCount > Limit<usize>::s_Max / mip.sliceCount){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetLoader: texture mip upload size exceeds addressable memory"));
        return false;
    }
    scratchBytes.resize(sliceUploadByteCount * mip.sliceCount);
    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        u8* const destination = scratchBytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount;
        const bool decoded = IsAstc4x4Format(format)
            ? DecodeTextureSliceAsAstc(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount)
            : DecodeTextureSliceAsRgba(textureAsset, mip, sliceIndex, destination, sliceUploadByteCount)
        ;
        if(!decoded)
            return false;
    }

    if(textureAsset.dimension() == TextureDimension::Texture3D){
        commandList.writeTexture(&texture, 0u, mipLevel, scratchBytes.data(), rowPitch, sliceUploadByteCount);
        return true;
    }

    for(u32 sliceIndex = 0u; sliceIndex < mip.sliceCount; ++sliceIndex){
        const u8* const source = scratchBytes.data() + static_cast<usize>(sliceIndex) * sliceUploadByteCount;
        commandList.writeTexture(&texture, sliceIndex, mipLevel, source, rowPitch, sliceUploadByteCount);
    }
    return true;
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
    Core::Graphics& graphics,
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
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture '{}' has invalid cooked UASTC data")
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

    Core::Format::Enum format = __hidden_texture_loader::SelectUploadFormat(device, textureAsset.colorSpace());
    if(format == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: device cannot filter ASTC 4x4 or RGBA8 for texture '{}'")
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
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName(imageName)
    ;
    if(textureAsset.dimension() == TextureDimension::TextureCube)
        textureDesc.setArraySize(6u);
    else if(textureAsset.dimension() == TextureDimension::Texture3D)
        textureDesc.setDepth(textureAsset.depth());

    Core::TextureHandle texture = graphics.createTexture(textureDesc);
    if(!texture && __hidden_texture_loader::IsAstc4x4Format(format)){
        const Core::Format::Enum rgbaFallback = __hidden_texture_loader::SelectRgbaUploadFormat(device, textureAsset.colorSpace());
        if(rgbaFallback != Core::Format::UNKNOWN){
            textureDesc.setFormat(rgbaFallback);
            texture = graphics.createTexture(textureDesc);
            format = rgbaFallback;
        }
    }
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    __hidden_texture_loader::InitializeBasisTranscoder();

    Core::CommandListHandle commandList = device.createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create an upload command list for texture '{}'"), owner, StringConvert(imageName.c_str()));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(AssetsTextureArenaScope::s_UploadScratchArena);
    Vector<u8, Core::Alloc::ScratchArena> uploadBytes{scratchArena};
    commandList->open();
    for(usize mipIndex = 0u; mipIndex < textureAsset.mipLevels().size(); ++mipIndex){
        if(!__hidden_texture_loader::UploadTextureMip(
            *commandList,
            *texture,
            textureAsset,
            textureAsset.mipLevels()[mipIndex],
            static_cast<u32>(mipIndex),
            format,
            uploadBytes
        )){
            commandList->close();
            return false;
        }
    }
    commandList->close();

    Core::CommandList* commandLists[]{ commandList.get() };
    bool submitted = false;
    device.executeCommandLists(commandLists, LengthOf(commandLists), Core::CommandQueue::Graphics, &submitted);
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to submit texture upload for '{}'"), owner, StringConvert(imageName.c_str()));
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
    const Name& textureVirtualPath,
    const Name& debugName,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    const tchar* const ownerName
){
    const tchar* const owner = ownerName ? ownerName : NWB_TEXT("TextureAssetLoader");
    if(outResource.valid())
        return true;
    if(!textureVirtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: texture virtual path is empty"), owner);
        return false;
    }

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

void TextureAssetLoader::Release(TextureGpuResource& inOutResource, Core::Graphics& graphics){
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
