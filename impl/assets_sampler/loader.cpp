// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "loader.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SamplerAssetLoader::Create(const Sampler& samplerAsset, const Name& debugName, const TStringView ownerName, Core::GraphicsRuntime& graphics, SamplerGpuResource& outResource){
    const Name samplerName = debugName ? debugName : samplerAsset.virtualPath();
    NWB_ASSERT(!outResource.valid());
    if(outResource.sampler || outResource.samplerHeapHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: sampler resource is partially initialized; release it before recreating"), ownerName);
        return false;
    }
    // Sampler::loadBinary already validated the cooked description; keep a debug-only invariant here.
    NWB_ASSERT(samplerAsset.validatePayload());

    Core::Device& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot load sampler '{}' without an initialized descriptor heap"), ownerName, ConvertString(samplerName.resolvedText()));
        return false;
    }

    auto samplerResult = device.createSampler(samplerAsset.description());
    if(!samplerResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create sampler '{}'"), ownerName, ConvertString(samplerName.resolvedText()));
        return false;
    }
    Core::SamplerHandle sampler = Move(*samplerResult);

    const Expected<Core::GpuDescriptorHandle> samplerHandleResult = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!samplerHandleResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to allocate a bindless sampler slot for '{}'"), ownerName, ConvertString(samplerName.resolvedText()));
        return false;
    }
    const Core::GpuDescriptorHandle samplerHandle = *samplerHandleResult;
    Core::GpuDescriptorRetirementGuard releaseDescriptor(heap, samplerHandle);
    if(!heap.write(samplerHandle, Core::DescriptorWriteItem::Sampler(0u, sampler.get()))){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to write the bindless sampler slot for '{}'"), ownerName, ConvertString(samplerName.resolvedText()));
        return false;
    }

    outResource.sampler = Move(sampler);
    outResource.samplerHeapHandle = samplerHandle;
    releaseDescriptor.release();
    return true;
}

b8 SamplerAssetLoader::Load(const Core::Assets::AssetRef<Sampler>& samplerAsset, const Name& debugName, const TStringView ownerName, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, SamplerGpuResource& outResource){
    if(!Core::Assets::AssetManager::CheckLoaderEnter(samplerAsset, outResource, ownerName))
        return outResource.valid();

    const Name& samplerVirtualPath = samplerAsset.name();

    auto loadedAsset = assetManager.loadTypedSync<Sampler>(samplerVirtualPath, ownerName);
    const Sampler* loadedSampler = loadedAsset ? loadedAsset->get() : nullptr;
    if(!loadedSampler)
        return false;

    return Create(*loadedSampler, debugName, ownerName, graphics, outResource);
}

void SamplerAssetLoader::Release(Core::GraphicsRuntime& graphics, SamplerGpuResource& inOutResource){
    if(inOutResource.samplerHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
        if(heap.isInitialized())
            heap.free(inOutResource.samplerHeapHandle);
        inOutResource.samplerHeapHandle = Core::GpuDescriptorHandle::Invalid();
    }

    inOutResource.sampler.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

