// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/custom.h>
#include <core/assets/asset_manager.h>
#include <core/ecs/system.h>
#include <core/graphics/graphics.h>
#include <core/input/input.h>

#include <imgui.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSystem final : public Core::ECS::ISystem, public Core::IRenderPass, public Core::IInputEventHandler{
public:
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;

public:
    UiSystem(
        Core::Alloc::CustomArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::InputDispatcher& input,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~UiSystem()override;

public:
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual void render(Core::IFramebuffer* framebuffer)override;
    virtual void backBufferResizing()override;

public:
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;
    virtual bool keyboardCharInput(u32 unicode, i32 mods)override;
    virtual bool mousePosUpdate(f64 xpos, f64 ypos)override;
    virtual bool mouseButtonUpdate(i32 button, i32 action, i32 mods)override;
    virtual bool mouseScrollUpdate(f64 xoffset, f64 yoffset)override;

public:
    [[nodiscard]] bool wantsKeyboardCapture()const noexcept{ return m_wantsKeyboardCapture; }
    [[nodiscard]] bool wantsMouseCapture()const noexcept{ return m_wantsMouseCapture; }
    [[nodiscard]] bool wantsTextInput()const noexcept{ return m_wantsTextInput; }

private:
    struct UiTextureResource{
        Core::TextureHandle texture;
        Core::BindingSetHandle bindingSet;
        u32 width = 0;
        u32 height = 0;
    };
    using UiTextureResourcePtr = Core::CustomUniquePtr<UiTextureResource>;
    using UiTextureResourceVector = Vector<UiTextureResourcePtr, Core::Alloc::CustomAllocator<UiTextureResourcePtr>>;
    using UiTextureUploadVector = Vector<u8, Core::Alloc::CustomAllocator<u8>>;

    struct UiPushConstants{
        Float4 scaleTranslate = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    };

private:
    void setCurrentContext()const;
    void beginFrame(f32 delta);
    void finishFrame();
    [[nodiscard]] bool ensureRenderResources(Core::IFramebuffer* framebuffer);
    [[nodiscard]] bool ensureShadersLoaded();
    [[nodiscard]] bool ensureInputLayout();
    [[nodiscard]] bool ensureBuffers(usize vertexCount, usize indexCount);
    [[nodiscard]] bool processTextureRequests(Core::ICommandList& commandList, ImDrawData& drawData);
    [[nodiscard]] bool createOrRefreshTexture(Core::ICommandList& commandList, ImTextureData& textureData);
    void destroyTexture(ImTextureData& textureData);
    [[nodiscard]] UiTextureResource* textureResourceFromId(ImTextureID textureId)const;
    [[nodiscard]] UiTextureResource* fallbackTextureResource()const{
        return m_textures.empty() ? nullptr : m_textures.front().get();
    }
    [[nodiscard]] Core::IBindingSet* bindingSetForTexture(ImTextureID textureId)const;
    [[nodiscard]] bool uploadDrawBuffers(Core::ICommandList& commandList, ImDrawData& drawData);
    void renderDrawData(Core::ICommandList& commandList, Core::IFramebuffer* framebuffer, ImDrawData& drawData);

private:
    Core::Alloc::CustomArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::InputDispatcher& m_input;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

    ImGuiContext* m_imguiContext = nullptr;
    Core::BindingLayoutHandle m_bindingLayout;
    Core::SamplerHandle m_sampler;
    Core::ShaderHandle m_vertexShader;
    Core::ShaderHandle m_pixelShader;
    Core::InputLayoutHandle m_inputLayout;
    Core::GraphicsPipelineHandle m_pipeline;
    Core::BufferHandle m_vertexBuffer;
    Core::BufferHandle m_indexBuffer;
    UiTextureResourceVector m_textures;
    UiTextureUploadVector m_textureUploadScratch;
    usize m_vertexBufferCapacity = 0;
    usize m_indexBufferCapacity = 0;
    f32 m_deltaSeconds = 0.0f;
    bool m_inputRegistered = false;
    bool m_frameStarted = false;
    bool m_frameFinished = false;
    bool m_wantsKeyboardCapture = false;
    bool m_wantsMouseCapture = false;
    bool m_wantsTextInput = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

