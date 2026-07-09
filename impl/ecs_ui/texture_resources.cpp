// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <global/core/graphics/backend_selection.h>
#include <global/core/graphics/module.h>
#include <global/text_utils.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <global/core/common/log.h>

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

static Name UiTextureName(const usize uniqueId){
    static constexpr AStringView s_Prefix("ecs_ui/imgui_texture_");
    char idBuffer[s_TextureNameIdBufferBytes] = {};
    const AStringView idText = FormatDecimal(uniqueId, idBuffer);
    if(idText.empty())
        return Name("ecs_ui/imgui_texture");

    char nameBuffer[s_TextureNameBufferBytes] = {};
    const usize nameSize = s_Prefix.size() + idText.size();
    if(nameSize > sizeof(nameBuffer))
        return Name("ecs_ui/imgui_texture");

    NWB_MEMCPY(nameBuffer, sizeof(nameBuffer), s_Prefix.data(), s_Prefix.size());
    NWB_MEMCPY(nameBuffer + s_Prefix.size(), sizeof(nameBuffer) - s_Prefix.size(), idText.data(), idText.size());
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSystem::processTextureRequests(Core::CommandList& commandList, ImDrawData& drawData){
#if defined(IMGUI_HAS_TEXTURES)
    if(!drawData.Textures)
        return true;

    for(i32 i = 0; i < drawData.Textures->Size; ++i){
        ImTextureData* textureData = drawData.Textures->Data[i];
        if(!textureData)
            continue;

        switch(textureData->Status){
        case ImTextureStatus_WantCreate:
        case ImTextureStatus_WantUpdates:
            if(!createOrRefreshTexture(commandList, *textureData))
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
    static_cast<void>(commandList);
    static_cast<void>(drawData);
#endif
    return true;
}

bool UiSystem::createOrRefreshTexture(Core::CommandList& commandList, ImTextureData& textureData){
    UiTextureResource* resource = static_cast<UiTextureResource*>(textureData.BackendUserData);
    const void* uploadPixels = nullptr;
    usize uploadRowPitch = 0u;
    if(!__hidden_ui::BuildUploadPixels(textureData, m_textureUploadScratch, uploadPixels, uploadRowPitch))
        return false;

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

        Core::BindingSetDesc bindingSetDesc(m_arena);
        bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_IMGUI_BINDING_TEXTURE,
            createdResource->texture.get(),
            Core::Format::RGBA8_UNORM,
            Core::s_AllSubresources,
            Core::TextureDimension::Texture2D
        ));
        bindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_IMGUI_BINDING_SAMPLER, m_sampler.get()));

        createdResource->bindingSet = m_graphics.getDevice()->createBindingSet(bindingSetDesc, m_bindingLayout);
        if(!createdResource->bindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create ImGui texture binding set"));
            return false;
        }

        createdResource->width = textureWidth;
        createdResource->height = textureHeight;
        resource = createdResource.get();
        m_textures.push_back(Move(createdResource));
        textureData.BackendUserData = resource;
        textureData.SetTexID(__hidden_ui::TextureIdFromResource(resource));
    }

    commandList.writeTexture(resource->texture.get(), 0u, 0u, uploadPixels, uploadRowPitch);
    textureData.SetStatus(ImTextureStatus_OK);
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
        if(it != m_textures.end())
            m_textures.erase(it);
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

Core::BindingSet* UiSystem::bindingSetForTexture(const ImTextureID textureId)const{
    UiTextureResource* resource = textureResourceFromId(textureId);
    if(!resource)
        resource = fallbackTextureResource();
    return resource ? resource->bindingSet.get() : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

