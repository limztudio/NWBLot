// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <global/core/ecs/world.h>
#include <global/core/graphics/backend_selection.h>
#include <global/core/graphics/module.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FallbackDeltaSeconds = 1.0f / 60.0f;
static constexpr f32 s_DefaultFramebufferScale = 1.0f;

static void DrawCallbackResetRenderState(const ImDrawList*, const ImDrawCmd*){}

static bool HasTextureRequests(const ImDrawData& drawData){
#if defined(IMGUI_HAS_TEXTURES)
    if(!drawData.Textures)
        return false;

    for(i32 i = 0; i < drawData.Textures->Size; ++i){
        const ImTextureData* textureData = drawData.Textures->Data[i];
        if(!textureData)
            continue;

        switch(textureData->Status){
        case ImTextureStatus_WantCreate:
        case ImTextureStatus_WantUpdates:
        case ImTextureStatus_WantDestroy:
            return true;
        case ImTextureStatus_OK:
        case ImTextureStatus_Destroyed:
        default:
            break;
        }
    }
#else
    static_cast<void>(drawData);
#endif

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSystem::UiSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::InputDispatcher& input,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_input(input)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_textures(arena)
    , m_textureUploadScratch(arena)
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
    platformIO.DrawCallback_ResetRenderState = __hidden_ui::DrawCallbackResetRenderState;

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

    m_deltaSeconds = IsFinite(delta) && delta > 0.0f ? delta : __hidden_ui::s_FallbackDeltaSeconds;

    i32 windowWidth = 0;
    i32 windowHeight = 0;
    m_graphics.getWindowDimensions(windowWidth, windowHeight);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<f32>(Max(windowWidth, 0)),
        static_cast<f32>(Max(windowHeight, 0))
    );
    io.DisplayFramebufferScale = ImVec2(__hidden_ui::s_DefaultFramebufferScale, __hidden_ui::s_DefaultFramebufferScale);
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

bool UiSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;

    if(!ensureFrameCommandLists())
        return false;

    Core::Framebuffer* framebuffer = m_graphics.getCurrentFramebuffer();
    return !framebuffer || ensureRenderResources(framebuffer);
}

bool UiSystem::ensureFrameCommandLists(){
    auto& device = *m_graphics.getDevice();

    if(!m_prepareCommandList){
        m_prepareCommandList = device.createCommandList();
        if(!m_prepareCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create preparation command list"));
            return false;
        }
    }

    if(!m_renderCommandList){
        m_renderCommandList = device.createCommandList();
        if(!m_renderCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create render command list"));
            return false;
        }
    }

    return true;
}

bool UiSystem::prepareResources(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return false;

    setCurrentContext();
    if(m_frameStarted && !m_frameFinished)
        finishFrame();
    if(!m_frameFinished)
        return true;

    ImDrawData* drawData = ImGui::GetDrawData();
    if(!drawData)
        return true;

    const SIMDVector framebufferExtent = VectorMultiply(
        VectorSet(drawData->DisplaySize.x, drawData->DisplaySize.y, 0.0f, 0.0f),
        VectorSet(drawData->FramebufferScale.x, drawData->FramebufferScale.y, 0.0f, 0.0f)
    );
    const i32 framebufferWidth = static_cast<i32>(VectorGetX(framebufferExtent));
    const i32 framebufferHeight = static_cast<i32>(VectorGetY(framebufferExtent));
    if(framebufferWidth <= 0 || framebufferHeight <= 0){
        m_frameStarted = false;
        m_frameFinished = false;
        return true;
    }

    if(!ensureRenderResources(framebuffer))
        return false;

    if(__hidden_ui::HasTextureRequests(*drawData)){
        auto& device = *m_graphics.getDevice();

        if(!m_prepareCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: preparation command list was not validated"));
            return false;
        }

        m_prepareCommandList->open();
        const bool texturesReady = processTextureRequests(*m_prepareCommandList, *drawData);
        m_prepareCommandList->close();
        if(!texturesReady)
            return false;

        Core::CommandList* commandLists[] = { m_prepareCommandList.get() };
        device.executeCommandLists(commandLists, 1);
    }

    if(!ensureBuffers(
        static_cast<usize>(drawData->TotalVtxCount),
        static_cast<usize>(drawData->TotalIdxCount)
    ))
        return false;

    if(drawData->TotalVtxCount <= 0 || drawData->TotalIdxCount <= 0)
        return true;

    if(!m_renderCommandList){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: render command list was not validated"));
        return false;
    }

    return true;
}

void UiSystem::render(Core::Framebuffer* framebuffer){
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

    const SIMDVector framebufferExtent = VectorMultiply(
        VectorSet(drawData->DisplaySize.x, drawData->DisplaySize.y, 0.0f, 0.0f),
        VectorSet(drawData->FramebufferScale.x, drawData->FramebufferScale.y, 0.0f, 0.0f)
    );
    const i32 framebufferWidth = static_cast<i32>(VectorGetX(framebufferExtent));
    const i32 framebufferHeight = static_cast<i32>(VectorGetY(framebufferExtent));
    if(framebufferWidth <= 0 || framebufferHeight <= 0){
        m_frameStarted = false;
        m_frameFinished = false;
        return;
    }

    if(!m_pipeline)
        return;

    auto* device = m_graphics.getDevice();
    if(drawData->TotalVtxCount <= 0 || drawData->TotalIdxCount <= 0){
        m_frameStarted = false;
        m_frameFinished = false;
        return;
    }

    Core::CommandList* commandList = m_renderCommandList.get();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: render command list was not prepared"));
        return;
    }

    commandList->open();
    const bool success = uploadDrawBuffers(*commandList, *drawData);
    if(success)
        renderDrawData(*commandList, framebuffer, *drawData);

    commandList->endRenderPass();
    commandList->close();
    if(success){
        Core::CommandList* commandLists[] = { commandList };
        device->executeCommandLists(commandLists, 1);
        m_frameStarted = false;
        m_frameFinished = false;
    }
}

void UiSystem::backBufferResizing(){
    m_prepareCommandList.reset();
    m_renderCommandList.reset();
    m_pipeline.reset();
}

bool UiSystem::uploadDrawBuffers(Core::CommandList& commandList, ImDrawData& drawData){
    const usize vertexCount = static_cast<usize>(drawData.TotalVtxCount);
    const usize indexCount = static_cast<usize>(drawData.TotalIdxCount);
    if(vertexCount == 0 || indexCount == 0)
        return true;

    if(!drawBuffersReady(vertexCount, indexCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: draw buffers were not prepared before render"));
        return false;
    }

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

void UiSystem::renderDrawData(Core::CommandList& commandList, Core::Framebuffer* framebuffer, ImDrawData& drawData){
    if(drawData.TotalVtxCount <= 0 || drawData.TotalIdxCount <= 0)
        return;

    const SIMDVector displayMin = VectorSet(drawData.DisplayPos.x, drawData.DisplayPos.y, 0.0f, 0.0f);
    const SIMDVector displaySize = VectorSet(drawData.DisplaySize.x, drawData.DisplaySize.y, 0.0f, 0.0f);
    const SIMDVector displayMax = VectorAdd(displayMin, displaySize);
    const f32 left = VectorGetX(displayMin);
    const f32 right = VectorGetX(displayMax);
    const f32 top = VectorGetY(displayMin);
    const f32 bottom = VectorGetY(displayMax);

    UiPushConstants pushConstants;
    pushConstants.scaleTranslate = Float4(
        2.0f / (right - left),
        2.0f / (top - bottom),
        (right + left) / (left - right),
        (top + bottom) / (bottom - top)
    );

    const SIMDVector framebufferExtent = VectorMultiply(
        displaySize,
        VectorSet(drawData.FramebufferScale.x, drawData.FramebufferScale.y, 0.0f, 0.0f)
    );
    const i32 framebufferWidth = static_cast<i32>(VectorGetX(framebufferExtent));
    const i32 framebufferHeight = static_cast<i32>(VectorGetY(framebufferExtent));
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

            const SIMDVector clipRect = VectorMultiply(
                VectorSubtract(
                    VectorSet(drawCommand.ClipRect.x, drawCommand.ClipRect.y, drawCommand.ClipRect.z, drawCommand.ClipRect.w),
                    VectorSet(clipOffset.x, clipOffset.y, clipOffset.x, clipOffset.y)
                ),
                VectorSet(clipScale.x, clipScale.y, clipScale.x, clipScale.y)
            );
            const f32 clipMinX = VectorGetX(clipRect);
            const f32 clipMinY = VectorGetY(clipRect);
            const f32 clipMaxX = VectorGetZ(clipRect);
            const f32 clipMaxY = VectorGetW(clipRect);
            if(clipMaxX <= clipMinX || clipMaxY <= clipMinY)
                continue;

            const i32 scissorMinX = Max(static_cast<i32>(clipMinX), 0);
            const i32 scissorMinY = Max(static_cast<i32>(clipMinY), 0);
            const i32 scissorMaxX = Min(static_cast<i32>(Ceil(clipMaxX)), framebufferWidth);
            const i32 scissorMaxY = Min(static_cast<i32>(Ceil(clipMaxY)), framebufferHeight);
            if(scissorMaxX <= scissorMinX || scissorMaxY <= scissorMinY)
                continue;

            Core::BindingSet* bindingSet = bindingSetForTexture(drawCommand.GetTexID());
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
                .addVertexBuffer(Core::VertexBufferBinding().setBuffer(m_vertexBuffer.get()).setSlot(NWB_IMGUI_VERTEX_BUFFER_INDEX).setOffset(0u))
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

