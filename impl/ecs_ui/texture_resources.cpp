// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/task_graph.h>
#include <global/text_utils.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <core/common/log.h>

#include <cstdint>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_TextureNameIdBufferBytes = 32u;
static constexpr usize s_TextureNameBufferBytes = 64u;
static constexpr usize s_RgbaPixelBytes = 4u;
static constexpr usize s_RgbaAlphaByteOffset = 3u;
static constexpr u8 s_OpaqueAlpha = 255u;
static constexpr usize s_TransferPreferredUploadMinimumBytes = 1024u * 1024u;
static constexpr Name s_FallbackTextureName("ecs_ui/imgui_texture");
static constexpr AStringView s_TextureNamePrefix("ecs_ui/imgui_texture_");

static Name UiTextureName(const usize uniqueId){
    char idBuffer[s_TextureNameIdBufferBytes] = {};
    const AStringView idText = FormatDecimal(uniqueId, idBuffer);
    if(idText.empty())
        return s_FallbackTextureName;

    char nameBuffer[s_TextureNameBufferBytes] = {};
    const usize nameSize = s_TextureNamePrefix.size() + idText.size();
    if(nameSize > sizeof(nameBuffer))
        return s_FallbackTextureName;

    NWB_MEMCPY(nameBuffer, sizeof(nameBuffer), s_TextureNamePrefix.data(), s_TextureNamePrefix.size());
    NWB_MEMCPY(nameBuffer + s_TextureNamePrefix.size(), sizeof(nameBuffer) - s_TextureNamePrefix.size(), idText.data(), idText.size());
    return Name(AStringView(nameBuffer, nameSize));
}

static ImTextureID TextureIdFromResource(const void* resource){
    static_assert(sizeof(ImTextureID) >= sizeof(uintptr_t), "ImTextureID must fit a backend texture-resource pointer");
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(resource));
}

template<typename ByteVector>
static bool BuildUploadPixels(ImTextureData& textureData, ByteVector& scratch, const void*& outPixels, usize& outRowPitch){
    outPixels = nullptr;
    outRowPitch = 0u;

    if(textureData.Width <= 0 || textureData.Height <= 0 || !textureData.Pixels){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui texture request has invalid pixel data"));
        return false;
    }

    const usize width = static_cast<usize>(textureData.Width);
    const usize height = static_cast<usize>(textureData.Height);
    if(width > Limit<usize>::s_Max / height || width * height > Limit<usize>::s_Max / s_RgbaPixelBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui texture upload size overflows"));
        return false;
    }

    const usize pixelCount = width * height;
    const usize rowPitch = width * s_RgbaPixelBytes;
    if(textureData.Format == ImTextureFormat_RGBA32){
        outPixels = textureData.Pixels;
        outRowPitch = rowPitch;
        return true;
    }
    if(textureData.Format != ImTextureFormat_Alpha8){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: unsupported ImGui texture format {}"), static_cast<i32>(textureData.Format));
        return false;
    }

    scratch.assign(pixelCount * s_RgbaPixelBytes, s_OpaqueAlpha);
    const u8* src = textureData.Pixels;
    u8* dstAlpha = scratch.data() + s_RgbaAlphaByteOffset;
    for(usize i = 0; i < pixelCount; ++i, dstAlpha += s_RgbaPixelBytes)
        *dstAlpha = src[i];

    outPixels = scratch.data();
    outRowPitch = rowPitch;
    return true;
}

[[nodiscard]] static Core::GpuQueueRequest UploadQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Transfer,
        true,
        true,
    };
}

[[nodiscard]] static Core::GpuTaskSchedulingHint UploadScheduling(const usize byteCount){
    const bool preferDedicatedTransport = byteCount >= s_TransferPreferredUploadMinimumBytes;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = preferDedicatedTransport ? Core::GpuTaskCostHint::Medium : Core::GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = preferDedicatedTransport;
    scheduling.avoidQueueCrossing = !preferDedicatedTransport;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    // Texture bytes are copied into graph-owned blobs before declaration; this built-in upload has no mutable ImGui
    // state during native recording and is safe to join other ready upload packets on worker threads.
    scheduling.allowParallelRecording = true;
    return scheduling;
}

[[nodiscard]] static Core::GpuGraphResourceDesc TextureResourceDesc(const Core::TextureDesc& textureDesc){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(textureDesc.name)
        .setMarkerLabel("ImGui Texture")
        .setType(Core::GpuGraphResourceType::Texture)
        .setInitialState(textureDesc.initialState)
        .setQueueSharing(textureDesc.queueSharing)
    ;
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSystem::ensureSamplerHeapHandle(){
    if(m_samplerHeapHandle.valid())
        return true;
    if(!m_sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register a missing ImGui sampler in the descriptor heap"));
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register the ImGui sampler without an initialized descriptor heap"));
        return false;
    }

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::Sampler(0u, m_sampler.get()))){
        heap.free(handle);
        return false;
    }

    m_samplerHeapHandle = handle;
    return true;
}

bool UiSystem::registerTextureHeapHandle(UiTextureResource& resource){
    if(resource.sampledImageHeapHandle.valid())
        return true;
    if(!resource.texture){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register a missing ImGui texture in the descriptor heap"));
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register an ImGui texture without an initialized descriptor heap"));
        return false;
    }

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::Texture_SRV(
        0u,
        resource.texture.get(),
        Core::Format::RGBA8_UNORM,
        Core::s_AllSubresources,
        Core::TextureDimension::Texture2D
    ))){
        heap.free(handle);
        return false;
    }

    resource.sampledImageHeapHandle = handle;
    return true;
}

void UiSystem::releaseTextureHeapHandle(UiTextureResource& resource){
    if(!resource.sampledImageHeapHandle.valid())
        return;

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized())
        heap.free(resource.sampledImageHeapHandle);
    resource.sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid();
}

void UiSystem::releaseDescriptorHeapResources(){
    for(const UiTextureResourcePtr& resource : m_textures){
        if(resource)
            releaseTextureHeapHandle(*resource);
    }

    if(m_samplerHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        if(heap.isInitialized())
            heap.free(m_samplerHeapHandle);
        m_samplerHeapHandle = Core::GpuDescriptorHandle::invalid();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSystem::prepareTextureRequests(ImDrawData& drawData){
#if defined(IMGUI_HAS_TEXTURES)
    if(!drawData.Textures)
        return true;

    for(i32 i = 0; i < drawData.Textures->Size; ++i){
        ImTextureData* const textureData = drawData.Textures->Data[i];
        if(!textureData)
            continue;

        switch(textureData->Status){
        case ImTextureStatus_WantCreate:
        case ImTextureStatus_WantUpdates:
            if(!createOrRefreshTexture(*textureData))
                return false;
            break;
        case ImTextureStatus_WantDestroy:
            destroyTexture(*textureData);
            textureData->SetStatus(ImTextureStatus_Destroyed);
            break;
        case ImTextureStatus_OK:
        case ImTextureStatus_Destroyed:
        default:
            break;
        }
    }
#else
    static_cast<void>(drawData);
#endif
    return true;
}

bool UiSystem::createOrRefreshTexture(ImTextureData& textureData){
    UiTextureResource* resource = static_cast<UiTextureResource*>(textureData.BackendUserData);
    const void* uploadPixels = nullptr;
    usize uploadRowPitch = 0u;
    // Validate the caller data before allocating a replacement.  The graph path rebuilds this transient conversion
    // when it snapshots immutable upload bytes; the legacy path uses the same helper immediately before recording.
    if(!__hidden_ui::BuildUploadPixels(textureData, m_textureUploadScratch, uploadPixels, uploadRowPitch))
        return false;
    static_cast<void>(uploadPixels);
    static_cast<void>(uploadRowPitch);

    const u32 textureWidth = static_cast<u32>(textureData.Width);
    const u32 textureHeight = static_cast<u32>(textureData.Height);
    if(!resource || resource->width != textureWidth || resource->height != textureHeight){
        if(resource)
            destroyTexture(textureData);

        auto createdResource = Core::MakeGlobalUnique<UiTextureResource>(m_arena);
        if(!createdResource){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to allocate texture resource"));
            return false;
        }

        Core::TextureDesc textureDesc;
        textureDesc
            .setWidth(textureWidth)
            .setHeight(textureHeight)
            .setFormat(Core::Format::RGBA8_UNORM)
            .setInitialState(Core::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setName(__hidden_ui::UiTextureName(static_cast<usize>(textureData.UniqueID)))
        ;

        createdResource->texture = m_graphics.createTexture(textureDesc);
        if(!createdResource->texture){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create ImGui texture"));
            return false;
        }

        if(!registerTextureHeapHandle(*createdResource)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to register ImGui texture in the descriptor heap"));
            return false;
        }

        createdResource->width = textureWidth;
        createdResource->height = textureHeight;
        resource = createdResource.get();
        m_textures.push_back(Move(createdResource));
        textureData.BackendUserData = resource;
        textureData.SetTexID(__hidden_ui::TextureIdFromResource(resource));
    }

    return true;
}

Core::GpuGraphResourceId UiSystem::importTaskGraphTexture(
    Core::GpuTaskGraph& graph,
    UiTextureResource& resource
){
    if(!resource.texture)
        return {};
    if(
        resource.taskGraphGeneration == graph.generation()
        && graph.validResource(resource.taskGraphResource)
    )
        return resource.taskGraphResource;

    const Core::GpuGraphResourceId imported = graph.importTexture(
        resource.texture,
        __hidden_ui::TextureResourceDesc(resource.texture->getDescription())
    );
    if(imported.valid()){
        resource.taskGraphResource = imported;
        resource.taskGraphGeneration = graph.generation();
    }
    return imported;
}

bool UiSystem::declareTaskGraphTextureUploads(
    Core::GpuTaskGraph& graph,
    ImDrawData& drawData,
    const Core::GpuTaskId previousTask,
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena>& outTasks,
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& outResources
){
    m_textureUploadBatch.reset();
#if defined(IMGUI_HAS_TEXTURES)
    if(!drawData.Textures)
        return true;

    for(i32 i = 0; i < drawData.Textures->Size; ++i){
        ImTextureData* const textureData = drawData.Textures->Data[i];
        if(
            !textureData
            || (
                textureData->Status != ImTextureStatus_WantCreate
                && textureData->Status != ImTextureStatus_WantUpdates
            )
        )
            continue;

        UiTextureResource* const resource = static_cast<UiTextureResource*>(textureData->BackendUserData);
        const Core::GpuGraphResourceId destination = resource ? importTaskGraphTexture(graph, *resource) : Core::GpuGraphResourceId{};
        const void* uploadPixels = nullptr;
        usize uploadRowPitch = 0u;
        if(
            !destination.valid()
            || !resource
            || !resource->texture
            || !__hidden_ui::BuildUploadPixels(*textureData, m_textureUploadScratch, uploadPixels, uploadRowPitch)
        ){
            m_textureUploadBatch.reset();
            return false;
        }

        const usize uploadByteCount = static_cast<usize>(textureData->Height) * uploadRowPitch;
        const Core::GpuUploadBlobId blob = graph.copyUploadData(uploadPixels, uploadByteCount, alignof(u32));
        if(!blob.valid()){
            m_textureUploadBatch.reset();
            return false;
        }

        Core::GpuTaskDesc desc;
        desc
            .setIdentity(Name("ui.imgui_texture_upload"))
            .setMarkerLabel("ImGui Texture Upload")
            .setQueue(__hidden_ui::UploadQueueRequest())
            .setScheduling(__hidden_ui::UploadScheduling(uploadByteCount))
        ;
        if(previousTask.valid())
            desc.setDependencies(&previousTask, 1u);
        const Core::GpuTaskId task = graph.addUploadTextureTask(
            desc,
            Core::GpuUploadTextureTaskDesc{
                .source = blob,
                .destination = destination,
                .arraySlice = 0u,
                .mipLevel = 0u,
                .rowPitch = uploadRowPitch,
                .depthPitch = 0u,
                .finalState = Core::ResourceStates::ShaderResource,
            }
        );
        if(!task.valid()){
            m_textureUploadBatch.reset();
            return false;
        }
        m_textureUploadBatch.add(*textureData);
        outTasks.push_back(task);
        outResources.push_back(destination);
    }
#else
    static_cast<void>(graph);
    static_cast<void>(drawData);
    static_cast<void>(outTasks);
    static_cast<void>(outResources);
#endif
    return true;
}

void UiSystem::destroyTexture(ImTextureData& textureData){
    UiTextureResource* resource = static_cast<UiTextureResource*>(textureData.BackendUserData);
    if(!resource && textureData.TexID != ImTextureID_Invalid)
        resource = textureResourceFromId(textureData.TexID);

    if(resource){
        auto it = FindIf(
            m_textures.begin(),
            m_textures.end(),
            [resource](const UiTextureResourcePtr& item){ return item.get() == resource; }
        );
        if(it != m_textures.end()){
            releaseTextureHeapHandle(**it);
            m_textures.erase(it);
        }
    }

    textureData.BackendUserData = nullptr;
    textureData.SetTexID(ImTextureID_Invalid);
}

UiSystem::UiTextureResource* UiSystem::textureResourceFromId(const ImTextureID textureId)const{
    if(textureId == ImTextureID_Invalid)
        return nullptr;

    const auto* candidate = reinterpret_cast<const UiTextureResource*>(static_cast<uintptr_t>(textureId));
    for(const UiTextureResourcePtr& resource : m_textures){
        if(resource.get() == candidate)
            return resource.get();
    }

    return nullptr;
}

UiSystem::UiTextureResource* UiSystem::textureResourceForDraw(const ImTextureID textureId)const{
    UiTextureResource* resource = textureResourceFromId(textureId);
    if(!resource)
        resource = fallbackTextureResource();
    return resource;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

