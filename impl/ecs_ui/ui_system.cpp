// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_system.h"

#include <core/ecs/world.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <impl/assets_graphics/shader_asset.h>
#include <logger/client/logger.h>

#include <cstddef>
#include <cstdint>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_ui{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_DefaultVertexCapacity = 4096u;
static constexpr usize s_DefaultIndexCapacity = 8192u;


static const Name& UiVertexShaderName(){
    static const Name s("engine/graphics/imgui_vs");
    return s;
}

static const Name& UiPixelShaderName(){
    static const Name s("engine/graphics/imgui_ps");
    return s;
}

static const Name& UiVertexBufferName(){
    static const Name s("ecs_ui/imgui_vertices");
    return s;
}

static const Name& UiIndexBufferName(){
    static const Name s("ecs_ui/imgui_indices");
    return s;
}

static usize NextCapacity(const usize currentCapacity, const usize requiredCapacity, const usize defaultCapacity){
    usize capacity = Max(currentCapacity, defaultCapacity);
    while(capacity < requiredCapacity){
        if(capacity > Limit<usize>::s_Max / 2u)
            return requiredCapacity;
        capacity *= 2u;
    }
    return capacity;
}

static ImTextureID TextureIdFromResource(const void* resource){
    static_assert(sizeof(ImTextureID) >= sizeof(uintptr_t), "ImTextureID must fit a backend texture-resource pointer");
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(resource));
}

static ImGuiKey MapKey(const i32 key){
    if(key >= Core::Key::A && key <= Core::Key::Z)
        return static_cast<ImGuiKey>(ImGuiKey_A + (key - Core::Key::A));
    if(key >= Core::Key::Number0 && key <= Core::Key::Number9)
        return static_cast<ImGuiKey>(ImGuiKey_0 + (key - Core::Key::Number0));
    if(key >= Core::Key::F1 && key <= Core::Key::F12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - Core::Key::F1));

    switch(key){
    case Core::Key::Tab: return ImGuiKey_Tab;
    case Core::Key::Left: return ImGuiKey_LeftArrow;
    case Core::Key::Right: return ImGuiKey_RightArrow;
    case Core::Key::Up: return ImGuiKey_UpArrow;
    case Core::Key::Down: return ImGuiKey_DownArrow;
    case Core::Key::PageUp: return ImGuiKey_PageUp;
    case Core::Key::PageDown: return ImGuiKey_PageDown;
    case Core::Key::Home: return ImGuiKey_Home;
    case Core::Key::End: return ImGuiKey_End;
    case Core::Key::Insert: return ImGuiKey_Insert;
    case Core::Key::Delete: return ImGuiKey_Delete;
    case Core::Key::Backspace: return ImGuiKey_Backspace;
    case Core::Key::Space: return ImGuiKey_Space;
    case Core::Key::Enter: return ImGuiKey_Enter;
    case Core::Key::Escape: return ImGuiKey_Escape;
    case Core::Key::Apostrophe: return ImGuiKey_Apostrophe;
    case Core::Key::Comma: return ImGuiKey_Comma;
    case Core::Key::Minus: return ImGuiKey_Minus;
    case Core::Key::Period: return ImGuiKey_Period;
    case Core::Key::Slash: return ImGuiKey_Slash;
    case Core::Key::Semicolon: return ImGuiKey_Semicolon;
    case Core::Key::Equal: return ImGuiKey_Equal;
    case Core::Key::LeftBracket: return ImGuiKey_LeftBracket;
    case Core::Key::Backslash: return ImGuiKey_Backslash;
    case Core::Key::RightBracket: return ImGuiKey_RightBracket;
    case Core::Key::GraveAccent: return ImGuiKey_GraveAccent;
    case Core::Key::CapsLock: return ImGuiKey_CapsLock;
    case Core::Key::ScrollLock: return ImGuiKey_ScrollLock;
    case Core::Key::NumLock: return ImGuiKey_NumLock;
    case Core::Key::PrintScreen: return ImGuiKey_PrintScreen;
    case Core::Key::Pause: return ImGuiKey_Pause;
    case Core::Key::Keypad0: return ImGuiKey_Keypad0;
    case Core::Key::Keypad1: return ImGuiKey_Keypad1;
    case Core::Key::Keypad2: return ImGuiKey_Keypad2;
    case Core::Key::Keypad3: return ImGuiKey_Keypad3;
    case Core::Key::Keypad4: return ImGuiKey_Keypad4;
    case Core::Key::Keypad5: return ImGuiKey_Keypad5;
    case Core::Key::Keypad6: return ImGuiKey_Keypad6;
    case Core::Key::Keypad7: return ImGuiKey_Keypad7;
    case Core::Key::Keypad8: return ImGuiKey_Keypad8;
    case Core::Key::Keypad9: return ImGuiKey_Keypad9;
    case Core::Key::KeypadDecimal: return ImGuiKey_KeypadDecimal;
    case Core::Key::KeypadDivide: return ImGuiKey_KeypadDivide;
    case Core::Key::KeypadMultiply: return ImGuiKey_KeypadMultiply;
    case Core::Key::KeypadSubtract: return ImGuiKey_KeypadSubtract;
    case Core::Key::KeypadAdd: return ImGuiKey_KeypadAdd;
    case Core::Key::KeypadEnter: return ImGuiKey_KeypadEnter;
    case Core::Key::KeypadEqual: return ImGuiKey_KeypadEqual;
    case Core::Key::LeftShift: return ImGuiKey_LeftShift;
    case Core::Key::LeftControl: return ImGuiKey_LeftCtrl;
    case Core::Key::LeftAlt: return ImGuiKey_LeftAlt;
    case Core::Key::LeftSuper: return ImGuiKey_LeftSuper;
    case Core::Key::RightShift: return ImGuiKey_RightShift;
    case Core::Key::RightControl: return ImGuiKey_RightCtrl;
    case Core::Key::RightAlt: return ImGuiKey_RightAlt;
    case Core::Key::RightSuper: return ImGuiKey_RightSuper;
    case Core::Key::Menu: return ImGuiKey_Menu;
    default: return ImGuiKey_None;
    }
}

static void AddModifierEvents(ImGuiIO& io, const i32 mods){
    io.AddKeyEvent(ImGuiMod_Ctrl, (mods & Core::InputModifier::Control) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mods & Core::InputModifier::Shift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mods & Core::InputModifier::Alt) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mods & Core::InputModifier::Super) != 0);
}

static void DrawCallbackResetRenderState(const ImDrawList*, const ImDrawCmd*){}

static Core::RenderState BuildUiRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().enableScissor().setCullNone();
    renderState.blendState.targets[0]
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::SrcAlpha)
        .setDestBlend(Core::BlendFactor::InvSrcAlpha)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::InvSrcAlpha)
        .setBlendOpAlpha(Core::BlendOp::Add)
    ;
    return renderState;
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
    if(width > Limit<usize>::s_Max / height || width * height > Limit<usize>::s_Max / 4u){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui texture upload size overflows"));
        return false;
    }

    const usize pixelCount = width * height;
    const usize rowPitch = width * 4u;
    if(textureData.Format == ImTextureFormat_RGBA32){
        outPixels = textureData.Pixels;
        outRowPitch = rowPitch;
        return true;
    }
    if(textureData.Format != ImTextureFormat_Alpha8){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: unsupported ImGui texture format {}"), static_cast<i32>(textureData.Format));
        return false;
    }

    scratch.resize(pixelCount * 4u);
    const u8* src = textureData.Pixels;
    u8* dst = scratch.data();
    for(usize i = 0; i < pixelCount; ++i){
        const u8 alpha = src[i];
        dst[0] = 255u;
        dst[1] = 255u;
        dst[2] = 255u;
        dst[3] = alpha;
        dst += 4u;
    }

    outPixels = scratch.data();
    outRowPitch = rowPitch;
    return true;
}

static Core::ShaderHandle CreateShaderFromAsset(
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    UiSystem::ShaderPathResolveCallback& shaderPathResolver,
    const Name& shaderName,
    const Core::ShaderType::Mask shaderType,
    const Name& debugName
){
    if(!shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: shader path resolver is null"));
        return nullptr;
    }

    const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: unsupported shader stage {}"), static_cast<u32>(shaderType));
        return nullptr;
    }

    Name shaderVirtualPath = NAME_NONE;
    if(!shaderPathResolver(shaderName, Core::ShaderArchive::s_DefaultVariant, stageName, shaderVirtualPath)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("UiSystem: failed to resolve shader '{}' stage '{}'"),
            StringConvert(shaderName.c_str()),
            StringConvert(stageName.c_str())
        );
        return nullptr;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager.loadSync(Shader::AssetTypeName(), shaderVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("UiSystem: failed to load shader asset '{}'"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return nullptr;
    }
    if(!loadedAsset || loadedAsset->assetType() != Shader::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("UiSystem: asset '{}' is not a shader"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return nullptr;
    }

    const Shader& shaderAsset = static_cast<const Shader&>(*loadedAsset);
    const Vector<u8>& shaderBinary = shaderAsset.bytecode();
    if(shaderBinary.empty() || (shaderBinary.size() & 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("UiSystem: shader asset '{}' has invalid bytecode"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return nullptr;
    }

    Core::ShaderDesc shaderDesc;
    shaderDesc.setShaderType(shaderType).setDebugName(debugName);
    Core::ShaderHandle shader = graphics.getDevice()->createShader(shaderDesc, shaderBinary.data(), shaderBinary.size());
    if(!shader){
        NWB_LOGGER_ERROR(
            NWB_TEXT("UiSystem: failed to create shader '{}'"),
            StringConvert(debugName.c_str())
        );
        return nullptr;
    }

    return shader;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSystem::UiSystem(
    Core::Alloc::CustomArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::InputDispatcher& input,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_input(input)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_textures(Core::Alloc::CustomAllocator<UiTextureResourcePtr>(arena))
    , m_textureUploadScratch(Core::Alloc::CustomAllocator<u8>(arena))
{
    readAccess<UiComponent>();

    IMGUI_CHECKVERSION();
    m_imguiContext = ImGui::CreateContext();
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "NWB";
    io.BackendRendererName = "NWB Graphics";
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
#if defined(IMGUI_HAS_TEXTURES)
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
#endif
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    platformIO.DrawCallback_ResetRenderState = __hidden_ecs_ui::DrawCallbackResetRenderState;

    ImGui::StyleColorsDark();

    m_input.addHandlerToBack(*this);
    m_inputRegistered = true;
}

UiSystem::~UiSystem(){
    if(m_inputRegistered){
        m_input.removeHandler(*this);
        m_inputRegistered = false;
    }

    if(m_imguiContext){
        setCurrentContext();
        ImGuiIO& io = ImGui::GetIO();
        if(io.BackendRendererUserData == this)
            io.BackendRendererUserData = nullptr;
        m_textures.clear();
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext = nullptr;
    }
}

void UiSystem::setCurrentContext()const{
    ImGui::SetCurrentContext(m_imguiContext);
}

void UiSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    beginFrame(delta);

    UiDrawContext context{ m_world, Core::ECS::ENTITY_ID_INVALID, m_deltaSeconds };
    m_world.view<UiComponent>().each(
        [&context](const Core::ECS::EntityID entity, UiComponent& component){
            if(!component.visible || !component.draw)
                return;

            context.entity = entity;
            component.draw(context);
        }
    );

    finishFrame();
}

void UiSystem::beginFrame(const f32 delta){
    setCurrentContext();

    if(m_frameStarted && !m_frameFinished)
        finishFrame();

    m_deltaSeconds = IsFinite(delta) && delta > 0.0f ? delta : (1.0f / 60.0f);

    i32 windowWidth = 0;
    i32 windowHeight = 0;
    m_graphics.getWindowDimensions(windowWidth, windowHeight);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<f32>(Max(windowWidth, 0)),
        static_cast<f32>(Max(windowHeight, 0))
    );
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = m_deltaSeconds;

    ImGui::NewFrame();
    m_frameStarted = true;
    m_frameFinished = false;
    m_wantsKeyboardCapture = io.WantCaptureKeyboard;
    m_wantsMouseCapture = io.WantCaptureMouse;
    m_wantsTextInput = io.WantTextInput;
}

void UiSystem::finishFrame(){
    if(!m_frameStarted || m_frameFinished)
        return;

    setCurrentContext();
    ImGui::Render();

    const ImGuiIO& io = ImGui::GetIO();
    m_wantsKeyboardCapture = io.WantCaptureKeyboard;
    m_wantsMouseCapture = io.WantCaptureMouse;
    m_wantsTextInput = io.WantTextInput;
    m_frameFinished = true;
}

void UiSystem::render(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return;

    setCurrentContext();
    if(m_frameStarted && !m_frameFinished)
        finishFrame();
    if(!m_frameFinished)
        return;

    ImDrawData* drawData = ImGui::GetDrawData();
    if(!drawData)
        return;

    const i32 framebufferWidth = static_cast<i32>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const i32 framebufferHeight = static_cast<i32>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if(framebufferWidth <= 0 || framebufferHeight <= 0){
        m_frameStarted = false;
        m_frameFinished = false;
        return;
    }

    if(!ensureRenderResources(framebuffer))
        return;

    Core::IDevice* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create command list"));
        return;
    }

    commandList->open();
    const bool success =
        processTextureRequests(*commandList, *drawData)
        && uploadDrawBuffers(*commandList, *drawData)
    ;
    if(success)
        renderDrawData(*commandList, framebuffer, *drawData);

    commandList->endRenderPass();
    commandList->close();
    if(success){
        Core::ICommandList* commandLists[] = { commandList.get() };
        device->executeCommandLists(commandLists, 1);
        m_frameStarted = false;
        m_frameFinished = false;
    }
}

void UiSystem::backBufferResizing(){
    m_pipeline.reset();
}

bool UiSystem::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    __hidden_ecs_ui::AddModifierEvents(io, mods);

    const ImGuiKey imguiKey = __hidden_ecs_ui::MapKey(key);
    if(imguiKey != ImGuiKey_None)
        io.AddKeyEvent(imguiKey, action != Core::InputAction::Release);

    return io.WantCaptureKeyboard;
}

bool UiSystem::keyboardCharInput(const u32 unicode, const i32 mods){
    static_cast<void>(mods);
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    if(unicode > 0u)
        io.AddInputCharacter(unicode);
    return io.WantTextInput || io.WantCaptureKeyboard;
}

bool UiSystem::mousePosUpdate(const f64 xpos, const f64 ypos){
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<f32>(xpos), static_cast<f32>(ypos));
    return io.WantCaptureMouse;
}

bool UiSystem::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods){
    static_cast<void>(mods);
    setCurrentContext();

    if(button < 0 || button >= ImGuiMouseButton_COUNT)
        return false;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, action != Core::InputAction::Release);
    return io.WantCaptureMouse;
}

bool UiSystem::mouseScrollUpdate(const f64 xoffset, const f64 yoffset){
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(static_cast<f32>(xoffset), static_cast<f32>(yoffset));
    return io.WantCaptureMouse;
}

bool UiSystem::ensureRenderResources(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return false;

    Core::IDevice* device = m_graphics.getDevice();
    if(!m_bindingLayout){
        static_assert(sizeof(UiPushConstants) <= Core::s_MaxPushConstantSize, "Ui push constants must fit the portable push constant budget");

        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::AllGraphics);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(UiPushConstants)));

        m_bindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_bindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create binding layout"));
            return false;
        }
    }

    if(!m_sampler){
        Core::SamplerDesc samplerDesc;
        samplerDesc
            .setAllFilters(true)
            .setAllAddressModes(Core::SamplerAddressMode::Clamp)
        ;
        m_sampler = device->createSampler(samplerDesc);
        if(!m_sampler){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create sampler"));
            return false;
        }
    }

    if(!ensureShadersLoaded() || !ensureInputLayout())
        return false;

    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();
    if(m_pipeline && m_pipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(m_inputLayout)
        .setVertexShader(m_vertexShader)
        .setPixelShader(m_pixelShader)
        .setRenderState(__hidden_ecs_ui::BuildUiRenderState())
        .addBindingLayout(m_bindingLayout)
    ;

    m_pipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create graphics pipeline"));
        return false;
    }

    return true;
}

bool UiSystem::ensureShadersLoaded(){
    if(!m_vertexShader){
        m_vertexShader = __hidden_ecs_ui::CreateShaderFromAsset(
            m_graphics,
            m_assetManager,
            m_shaderPathResolver,
            __hidden_ecs_ui::UiVertexShaderName(),
            Core::ShaderType::Vertex,
            Name("ECSUI_ImGuiVS")
        );
        if(!m_vertexShader)
            return false;
    }

    if(!m_pixelShader){
        m_pixelShader = __hidden_ecs_ui::CreateShaderFromAsset(
            m_graphics,
            m_assetManager,
            m_shaderPathResolver,
            __hidden_ecs_ui::UiPixelShaderName(),
            Core::ShaderType::Pixel,
            Name("ECSUI_ImGuiPS")
        );
        if(!m_pixelShader)
            return false;
    }

    return true;
}

bool UiSystem::ensureInputLayout(){
    if(m_inputLayout)
        return true;
    if(!m_vertexShader)
        return false;

    Core::VertexAttributeDesc attributes[3];
    attributes[0]
        .setFormat(Core::Format::RG32_FLOAT)
        .setBufferIndex(0)
        .setOffset(offsetof(ImDrawVert, pos))
        .setElementStride(sizeof(ImDrawVert))
        .setName("POSITION")
    ;
    attributes[1]
        .setFormat(Core::Format::RG32_FLOAT)
        .setBufferIndex(0)
        .setOffset(offsetof(ImDrawVert, uv))
        .setElementStride(sizeof(ImDrawVert))
        .setName("TEXCOORD")
    ;
    attributes[2]
        .setFormat(Core::Format::RGBA8_UNORM)
        .setBufferIndex(0)
        .setOffset(offsetof(ImDrawVert, col))
        .setElementStride(sizeof(ImDrawVert))
        .setName("COLOR")
    ;

    m_inputLayout = m_graphics.getDevice()->createInputLayout(attributes, 3u, m_vertexShader.get());
    if(!m_inputLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create input layout"));
        return false;
    }

    return true;
}

bool UiSystem::ensureBuffers(const usize vertexCount, const usize indexCount){
    if(vertexCount == 0 || indexCount == 0)
        return true;
    if(vertexCount > Limit<usize>::s_Max / sizeof(ImDrawVert) || indexCount > Limit<usize>::s_Max / sizeof(ImDrawIdx)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: draw buffer request overflows addressable memory"));
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();
    if(!m_vertexBuffer || m_vertexBufferCapacity < vertexCount){
        const usize capacity = __hidden_ecs_ui::NextCapacity(
            m_vertexBufferCapacity,
            vertexCount,
            __hidden_ecs_ui::s_DefaultVertexCapacity
        );

        Core::BufferDesc bufferDesc;
        bufferDesc
            .setByteSize(static_cast<u64>(capacity * sizeof(ImDrawVert)))
            .setIsVertexBuffer(true)
            .setDebugName(__hidden_ecs_ui::UiVertexBufferName())
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;

        m_vertexBuffer = device->createBuffer(bufferDesc);
        if(!m_vertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create vertex buffer"));
            return false;
        }
        m_vertexBufferCapacity = capacity;
    }

    if(!m_indexBuffer || m_indexBufferCapacity < indexCount){
        const usize capacity = __hidden_ecs_ui::NextCapacity(
            m_indexBufferCapacity,
            indexCount,
            __hidden_ecs_ui::s_DefaultIndexCapacity
        );

        Core::BufferDesc bufferDesc;
        bufferDesc
            .setByteSize(static_cast<u64>(capacity * sizeof(ImDrawIdx)))
            .setIsIndexBuffer(true)
            .setDebugName(__hidden_ecs_ui::UiIndexBufferName())
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;

        m_indexBuffer = device->createBuffer(bufferDesc);
        if(!m_indexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create index buffer"));
            return false;
        }
        m_indexBufferCapacity = capacity;
    }

    return true;
}

bool UiSystem::processTextureRequests(Core::ICommandList& commandList, ImDrawData& drawData){
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

bool UiSystem::createOrRefreshTexture(Core::ICommandList& commandList, ImTextureData& textureData){
    UiTextureResource* resource = static_cast<UiTextureResource*>(textureData.BackendUserData);
    const void* uploadPixels = nullptr;
    usize uploadRowPitch = 0u;
    if(!__hidden_ecs_ui::BuildUploadPixels(textureData, m_textureUploadScratch, uploadPixels, uploadRowPitch))
        return false;

    const u32 textureWidth = static_cast<u32>(textureData.Width);
    const u32 textureHeight = static_cast<u32>(textureData.Height);
    if(!resource || resource->width != textureWidth || resource->height != textureHeight){
        if(resource)
            destroyTexture(textureData);

        auto createdResource = Core::MakeCustomUnique<UiTextureResource>(m_arena);
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
            .setName(Name(StringFormat("ecs_ui/imgui_texture_{}", textureData.UniqueID)))
        ;

        createdResource->texture = m_graphics.createTexture(textureDesc);
        if(!createdResource->texture){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create ImGui texture"));
            return false;
        }

        Core::BindingSetDesc bindingSetDesc;
        bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
            0,
            createdResource->texture.get(),
            Core::Format::RGBA8_UNORM,
            Core::s_AllSubresources,
            Core::TextureDimension::Texture2D
        ));
        bindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_sampler.get()));

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
        textureData.SetTexID(__hidden_ecs_ui::TextureIdFromResource(resource));
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

Core::IBindingSet* UiSystem::bindingSetForTexture(const ImTextureID textureId)const{
    UiTextureResource* resource = textureResourceFromId(textureId);
    if(!resource)
        resource = fallbackTextureResource();
    return resource ? resource->bindingSet.get() : nullptr;
}

bool UiSystem::uploadDrawBuffers(Core::ICommandList& commandList, ImDrawData& drawData){
    const usize vertexCount = static_cast<usize>(drawData.TotalVtxCount);
    const usize indexCount = static_cast<usize>(drawData.TotalIdxCount);
    if(vertexCount == 0 || indexCount == 0)
        return true;
    if(!ensureBuffers(vertexCount, indexCount))
        return false;

    u64 vertexByteOffset = 0u;
    u64 indexByteOffset = 0u;
    for(i32 i = 0; i < drawData.CmdListsCount; ++i){
        const ImDrawList* drawList = drawData.CmdLists[i];
        if(!drawList)
            continue;

        const usize vertexBytes = static_cast<usize>(drawList->VtxBuffer.Size) * sizeof(ImDrawVert);
        const usize indexBytes = static_cast<usize>(drawList->IdxBuffer.Size) * sizeof(ImDrawIdx);
        if(vertexBytes > 0u)
            commandList.writeBuffer(m_vertexBuffer.get(), drawList->VtxBuffer.Data, vertexBytes, vertexByteOffset);
        if(indexBytes > 0u)
            commandList.writeBuffer(m_indexBuffer.get(), drawList->IdxBuffer.Data, indexBytes, indexByteOffset);

        vertexByteOffset += vertexBytes;
        indexByteOffset += indexBytes;
    }

    return true;
}

void UiSystem::renderDrawData(Core::ICommandList& commandList, Core::IFramebuffer* framebuffer, ImDrawData& drawData){
    if(drawData.TotalVtxCount <= 0 || drawData.TotalIdxCount <= 0)
        return;

    const f32 left = drawData.DisplayPos.x;
    const f32 right = drawData.DisplayPos.x + drawData.DisplaySize.x;
    const f32 top = drawData.DisplayPos.y;
    const f32 bottom = drawData.DisplayPos.y + drawData.DisplaySize.y;

    UiPushConstants pushConstants;
    pushConstants.scaleTranslate = Float4(
        2.0f / (right - left),
        2.0f / (top - bottom),
        (right + left) / (left - right),
        (top + bottom) / (bottom - top)
    );

    const i32 framebufferWidth = static_cast<i32>(drawData.DisplaySize.x * drawData.FramebufferScale.x);
    const i32 framebufferHeight = static_cast<i32>(drawData.DisplaySize.y * drawData.FramebufferScale.y);
    const Core::Viewport viewport(0.0f, static_cast<f32>(framebufferWidth), 0.0f, static_cast<f32>(framebufferHeight), 0.0f, 1.0f);
    const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

    const Core::Format::Enum indexFormat = sizeof(ImDrawIdx) == sizeof(u16)
        ? Core::Format::R16_UINT
        : Core::Format::R32_UINT
    ;

    i32 globalVertexOffset = 0;
    u32 globalIndexOffset = 0u;
    const ImVec2 clipOffset = drawData.DisplayPos;
    const ImVec2 clipScale = drawData.FramebufferScale;

    for(i32 listIndex = 0; listIndex < drawData.CmdListsCount; ++listIndex){
        const ImDrawList* drawList = drawData.CmdLists[listIndex];
        if(!drawList)
            continue;

        for(i32 commandIndex = 0; commandIndex < drawList->CmdBuffer.Size; ++commandIndex){
            const ImDrawCmd& drawCommand = drawList->CmdBuffer[commandIndex];
            if(drawCommand.UserCallback){
                if(
                    drawCommand.UserCallback != ImDrawCallback_ResetRenderState
                    && drawCommand.UserCallback != platformIO.DrawCallback_ResetRenderState
                )
                    drawCommand.UserCallback(drawList, &drawCommand);
                continue;
            }

            const f32 clipMinX = (drawCommand.ClipRect.x - clipOffset.x) * clipScale.x;
            const f32 clipMinY = (drawCommand.ClipRect.y - clipOffset.y) * clipScale.y;
            const f32 clipMaxX = (drawCommand.ClipRect.z - clipOffset.x) * clipScale.x;
            const f32 clipMaxY = (drawCommand.ClipRect.w - clipOffset.y) * clipScale.y;
            if(clipMaxX <= clipMinX || clipMaxY <= clipMinY)
                continue;

            const i32 scissorMinX = Max(static_cast<i32>(clipMinX), 0);
            const i32 scissorMinY = Max(static_cast<i32>(clipMinY), 0);
            const i32 scissorMaxX = Min(static_cast<i32>(Ceil(clipMaxX)), framebufferWidth);
            const i32 scissorMaxY = Min(static_cast<i32>(Ceil(clipMaxY)), framebufferHeight);
            if(scissorMaxX <= scissorMinX || scissorMaxY <= scissorMinY)
                continue;

            Core::IBindingSet* bindingSet = bindingSetForTexture(drawCommand.GetTexID());
            if(!bindingSet)
                continue;

            Core::ViewportState viewportState;
            viewportState.addViewport(viewport);
            viewportState.addScissorRect(Core::Rect(scissorMinX, scissorMaxX, scissorMinY, scissorMaxY));

            Core::GraphicsState graphicsState;
            graphicsState
                .setPipeline(m_pipeline.get())
                .setFramebuffer(framebuffer)
                .setViewport(viewportState)
                .addBindingSet(bindingSet)
                .addVertexBuffer(Core::VertexBufferBinding().setBuffer(m_vertexBuffer.get()).setSlot(0u).setOffset(0u))
                .setIndexBuffer(Core::IndexBufferBinding().setBuffer(m_indexBuffer.get()).setFormat(indexFormat).setOffset(0u))
            ;

            commandList.setGraphicsState(graphicsState);
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

            Core::DrawArguments drawArguments;
            drawArguments
                .setVertexCount(drawCommand.ElemCount)
                .setStartIndexLocation(globalIndexOffset + drawCommand.IdxOffset)
                .setStartVertexLocation(static_cast<u32>(globalVertexOffset + static_cast<i32>(drawCommand.VtxOffset)))
            ;
            commandList.drawIndexed(drawArguments);
        }

        globalVertexOffset += drawList->VtxBuffer.Size;
        globalIndexOffset += static_cast<u32>(drawList->IdxBuffer.Size);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

