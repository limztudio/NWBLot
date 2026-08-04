// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/assets/manager.h>
#include <core/graphics/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns one texture asset's device-local image and its persistent global sampled-image heap entry. Release it before
// its Graphics owner tears down so the heap can retire the descriptor and retain the image through in-flight work.
struct TextureGpuResource final : NoCopy{
    Core::TextureHandle texture;
    Core::GpuDescriptorHandle sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::Format::Enum format = Core::Format::UNKNOWN;

    [[nodiscard]] bool valid()const{
        return texture != nullptr && sampledImageHeapHandle.valid() && format != Core::Format::UNKNOWN;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureAssetLoader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Creates, uploads, and registers a static 2D, cube, or 3D texture asset. LDR UASTC uses ASTC 4x4 when available,
// otherwise portable RGBA8. Opaque HDR UASTC uses ASTC HDR 4x4, then BC6H, then RGBA16_FLOAT. HDR textures with
// constant or companion-stream alpha decode and merge into RGBA16_FLOAT because one sampled-image descriptor owns
// the complete RGBA result.
[[nodiscard]] bool Create(
    TextureGpuResource& outResource,
    const Texture& textureAsset,
    const Name& debugName,
    Core::Graphics& graphics,
    const tchar* ownerName
);

// Loads a cooked Texture asset through the normal asset manager, then delegates to Create().
[[nodiscard]] bool Load(
    TextureGpuResource& outResource,
    const Name& textureVirtualPath,
    const Name& debugName,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    const tchar* ownerName
);

// Frees the global descriptor first, then releases the owner's TextureHandle. Safe to call on an empty resource.
void Release(TextureGpuResource& inOutResource, Core::Graphics& graphics);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

