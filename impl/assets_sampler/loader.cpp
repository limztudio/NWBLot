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
    Core::Graphics& graphics,
    const tchar* const ownerName
){
    const tchar* const owner = ownerName ? ownerName : NWB_TEXT("SamplerAssetLoader");
    const Name samplerName = debugName ? debugName : samplerAsset.virtualPath();
    if(outResource.valid())
        return true;
    if(outResource.sampler || outResource.samplerHeapHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: sampler resource is partially initialized; release it before recreating"), owner);
        return false;
    }
    if(!samplerAsset.validatePayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: sampler '{}' has an invalid cooked description")
            , owner
            , StringConvert(samplerName.c_str())
        );
        return false;
    }

    Core::Device& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot load sampler '{}' without an initialized descriptor heap")
            , owner
            , StringConvert(samplerName.c_str())
        );
        return false;
    }

    Core::SamplerHandle sampler = device.createSampler(samplerAsset.description());
    if(!sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create sampler '{}'"), owner, StringConvert(samplerName.c_str()));
        return false;
    }

    const Core::GpuDescriptorHandle samplerHandle = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!samplerHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to allocate a bindless sampler slot for '{}'"), owner, StringConvert(samplerName.c_str()));
        return false;
    }
    if(!heap.write(samplerHandle, Core::DescriptorWriteItem::Sampler(0u, sampler.get()))){
        heap.free(samplerHandle);
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to write the bindless sampler slot for '{}'"), owner, StringConvert(samplerName.c_str()));
        return false;
    }

    outResource.sampler = Move(sampler);
    outResource.samplerHeapHandle = samplerHandle;
    return true;
}

bool SamplerAssetLoader::Load(
    SamplerGpuResource& outResource,
    const Name& samplerVirtualPath,
    const Name& debugName,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    const tchar* const ownerName
){
    const tchar* const owner = ownerName ? ownerName : NWB_TEXT("SamplerAssetLoader");
    if(outResource.valid())
        return true;
    if(!samplerVirtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: sampler virtual path is empty"), owner);
        return false;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager.loadSync(Sampler::AssetTypeName(), samplerVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load sampler asset '{}'"), owner, StringConvert(samplerVirtualPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Sampler::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: asset '{}' is not a sampler"), owner, StringConvert(samplerVirtualPath.c_str()));
        return false;
    }

    return Create(outResource, static_cast<const Sampler&>(*loadedAsset), debugName, graphics, owner);
}

void SamplerAssetLoader::Release(SamplerGpuResource& inOutResource, Core::Graphics& graphics){
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
