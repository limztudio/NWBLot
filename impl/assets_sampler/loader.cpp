// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "loader.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SamplerAssetLoader::Create(
    SamplerGpuResource& outResource,
    const Sampler& samplerAsset,
    const Name& debugName,
    Core::GraphicsRuntime& graphics,
    const NotNull<const tchar*> ownerName
){
    const NotNull<const tchar*> owner = ownerName;
    const Name samplerName = debugName ? debugName : samplerAsset.virtualPath();
    NWB_ASSERT(!outResource.valid());
    if(outResource.valid())
        return true;
    if(outResource.sampler || outResource.samplerHeapHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: sampler resource is partially initialized; release it before recreating"), owner.get());
        return false;
    }
    // Sampler::loadBinary already validated the cooked description; keep a debug-only invariant here.
    NWB_ASSERT(samplerAsset.validatePayload());

    Core::Device& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot load sampler '{}' without an initialized descriptor heap")
            , owner.get()
            , StringConvert(samplerName.c_str())
        );
        return false;
    }

    Core::SamplerHandle sampler = device.createSampler(samplerAsset.description());
    if(!sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create sampler '{}'"), owner.get(), StringConvert(samplerName.c_str()));
        return false;
    }

    const Core::GpuDescriptorHandle samplerHandle = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!samplerHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to allocate a bindless sampler slot for '{}'"), owner.get(), StringConvert(samplerName.c_str()));
        return false;
    }
    if(!heap.write(samplerHandle, Core::DescriptorWriteItem::Sampler(0u, sampler.get()))){
        heap.free(samplerHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to write the bindless sampler slot for '{}'"), owner.get(), StringConvert(samplerName.c_str()));
        return false;
    }

    outResource.sampler = Move(sampler);
    outResource.samplerHeapHandle = samplerHandle;
    return true;
}

bool SamplerAssetLoader::Load(
    SamplerGpuResource& outResource,
    const Core::Assets::AssetRef<Sampler>& samplerAsset,
    const Name& debugName,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    const NotNull<const tchar*> ownerName
){
    const NotNull<const tchar*> owner = ownerName;
    if(!Core::Assets::AssetManager::CheckLoaderEnter(samplerAsset, outResource, owner, MakeNotNull("sampler")))
        return outResource.valid();

    const Name& samplerVirtualPath = samplerAsset.name();

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Sampler* loadedSampler = assetManager.loadTypedSync<Sampler>(
        samplerVirtualPath,
        loadedAsset,
        MakeNotNull(NWB_TEXT("SamplerAssetLoader::Load")),
        owner,
        MakeNotNull("sampler")
    );
    if(!loadedSampler)
        return false;

    return Create(outResource, *loadedSampler, debugName, graphics, owner);
}

void SamplerAssetLoader::Release(SamplerGpuResource& inOutResource, Core::GraphicsRuntime& graphics){
    if(inOutResource.samplerHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
        if(heap.isInitialized())
            heap.free(inOutResource.samplerHeapHandle);
        inOutResource.samplerHeapHandle = Core::GpuDescriptorHandle::invalid();
    }

    inOutResource.sampler.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

