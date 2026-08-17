// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/task_graph.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FallbackDeltaSeconds = 1.0f / 60.0f;
static constexpr f32 s_DefaultFramebufferScale = 1.0f;
static constexpr usize s_TransferPreferredUploadMinimumBytes = 1024u * 1024u;
static constexpr usize s_UploadAlignmentBytes = sizeof(u32);
static constexpr Name s_TaskGraphDeclarationArena("impl/ecs_ui/task_graph");

static void DrawCallbackResetRenderState(const ImDrawList*, const ImDrawCmd*){}

[[nodiscard]] static SIMDVector BuildUiScaleTranslate(const SIMDVector displayMin, const SIMDVector displaySize){
    const SIMDVector displayMax = VectorAdd(displayMin, displaySize);
    const SIMDVector displayExtent = VectorSubtract(displayMax, displayMin);
    const SIMDVector displaySum = VectorAdd(displayMax, displayMin);
    const SIMDVector numerators = VectorMergeX(
        VectorReplicate(2.0f),
        VectorReplicate(2.0f),
        displaySum,
        VectorSplatY(displaySum)
    );
    const SIMDVector denominators = VectorMultiply(
        VectorSwizzle<0, 1, 0, 1>(displayExtent),
        VectorSet(1.0f, -1.0f, -1.0f, 1.0f)
    );
    return VectorDivide(numerators, denominators);
}

static void GetFramebufferExtent(const ImDrawData& drawData, i32& outWidth, i32& outHeight){
    const SIMDVector framebufferExtent = VectorMultiply(
        VectorSet(drawData.DisplaySize.x, drawData.DisplaySize.y, 0.0f, 0.0f),
        VectorSet(drawData.FramebufferScale.x, drawData.FramebufferScale.y, 0.0f, 0.0f)
    );
    outWidth = static_cast<i32>(VectorGetX(framebufferExtent));
    outHeight = static_cast<i32>(VectorGetY(framebufferExtent));
}

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

static bool HasPendingTextureUploads(const ImDrawData& drawData){
#if defined(IMGUI_HAS_TEXTURES)
    if(!drawData.Textures)
        return false;

    for(i32 i = 0; i < drawData.Textures->Size; ++i){
        const ImTextureData* const textureData = drawData.Textures->Data[i];
        if(
            textureData
            && (
                textureData->Status == ImTextureStatus_WantCreate
                || textureData->Status == ImTextureStatus_WantUpdates
            )
        )
            return true;
    }
#else
    static_cast<void>(drawData);
#endif

    return false;
}

[[nodiscard]] static bool AlignUploadBytes(const usize byteCount, usize& outAlignedBytes){
    outAlignedBytes = 0u;
    if(byteCount > Limit<usize>::s_Max - (s_UploadAlignmentBytes - 1u))
        return false;
    outAlignedBytes = (byteCount + (s_UploadAlignmentBytes - 1u)) & ~(s_UploadAlignmentBytes - 1u);
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
    // Preserve the setup-upload policy for the frame graph: tiny UI deltas stay on Graphics, while a large font
    // or texture refresh may use Transfer first and a dedicated Compute queue second.
    scheduling.cost = preferDedicatedTransport ? Core::GpuTaskCostHint::Medium : Core::GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = preferDedicatedTransport;
    scheduling.avoidQueueCrossing = !preferDedicatedTransport;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    // UI buffers and textures are created with the full graph-upload sharing contract below. A large immutable
    // upload may therefore use an explicitly registered same-class physical queue, including an opt-in alternate
    // Vulkan family, while the terminal overlay remains on primary Graphics.
    scheduling.allowSameClassQueueRouting = preferDedicatedTransport;
    scheduling.preferNonPrimarySameClassQueue = preferDedicatedTransport;
    scheduling.allowCrossFamilySameClassQueueRouting = preferDedicatedTransport;
    // Built-in graph uploads capture immutable blobs and create a fresh native command list, so independent vertex
    // and index packets may record together once their shared scene-output dependency is ready.
    scheduling.allowParallelRecording = true;
    return scheduling;
}

// This is intentionally broader than the immutable overlay task. An ImGui callback is opaque to the backend, and
// the graph must preserve the existing primary-Graphics command-list contract while capability tracking remains on.
[[nodiscard]] static Core::GpuQueueRequest OpaquePresentationQueueRequest(){
    return Core::GpuQueueRequest{
        static_cast<Core::GpuQueueCapability::Mask>(
            static_cast<u8>(Core::GpuQueueCapability::Graphics)
            | static_cast<u8>(Core::GpuQueueCapability::Compute)
            | static_cast<u8>(Core::GpuQueueCapability::Transfer)
        ),
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

[[nodiscard]] static Core::GpuGraphResourceDesc BufferResourceDesc(
    const Name& identity,
    const AStringView label,
    const Core::BufferDesc& bufferDesc
){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Buffer)
        .setInitialState(bufferDesc.initialState)
        .setQueueSharing(bufferDesc.queueSharing)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadBufferUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Read,
    };
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadTextureUse(const Core::GpuGraphResourceId resource){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
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

[[nodiscard]] static bool IsTaskGraphResetCallback(const ImDrawCmd& drawCommand){
    if(!drawCommand.UserCallback)
        return false;

    const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    return drawCommand.UserCallback == ImDrawCallback_ResetRenderState
        || drawCommand.UserCallback == platformIO.DrawCallback_ResetRenderState
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UiSystem::TaskGraphRenderTask{
    struct Payload{
        UiSystem* ui = nullptr;
        Core::Framebuffer* framebuffer = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return payload.ui && payload.ui->recordTaskGraphPresentation(commandList, payload.framebuffer);
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.ui)
            payload.ui->confirmTaskGraphPresentationSubmission();
    }
};


struct UiSystem::TaskGraphUploadCompletionTask{
    struct Payload{
        UiSystem* ui = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        return payload.ui && payload.ui->recordTaskGraphUploadCompletion();
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.ui)
            payload.ui->confirmTaskGraphPresentationSubmission();
    }
};


struct UiSystem::StandaloneTextureUploadCompletionTask{
    struct Payload{
        UiSystem* ui = nullptr;
        bool uploadsPrepared = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        return payload.ui && payload.uploadsPrepared;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.ui && token.valid())
            payload.ui->m_textureUploadBatch.complete(true);
    }

    static void discarded(Payload& payload){
        if(payload.ui)
            payload.ui->m_textureUploadBatch.complete(false);
    }
};


// An arbitrary ImDrawCmd callback owns opaque user state and therefore cannot enter the immutable overlay packet.
// The standalone graph recorder is synchronous, so this compatibility task may invoke it against the still-live
// ImGui arrays while the graph retains and orders every ordinary UI resource around that opaque operation.
struct UiSystem::StandaloneLegacyPresentationTask{
    struct Payload{
        UiSystem* ui = nullptr;
        Core::Framebuffer* framebuffer = nullptr;
        ImDrawData* drawData = nullptr;
        u64 frameGeneration = 0u;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return payload.ui && payload.ui->recordStandaloneLegacyTaskGraphPresentation(
            commandList,
            payload.framebuffer,
            payload.drawData,
            payload.frameGeneration
        );
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.ui && token.valid())
            payload.ui->confirmTaskGraphPresentationSubmission();
    }

    static void discarded(Payload& payload){
        if(payload.ui)
            payload.ui->discardStandaloneLegacyTaskGraphPresentation();
    }
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
    , m_textureUploadBatch(arena)
    , m_textureUploadScratch(arena)
    , m_taskGraphVertexUpload(arena)
    , m_taskGraphIndexUpload(arena)
    , m_taskGraphDrawCommands(arena)
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
    m_graphics.setTaskGraphPresentationContributor(this);
}

UiSystem::~UiSystem(){
    m_graphics.clearTaskGraphPresentationContributor(*this);

    if(m_inputRegistered){
        m_input.removeHandler(*this);
        m_inputRegistered = false;
    }

    if(m_imguiContext){
        setCurrentContext();
        ImGuiIO& io = ImGui::GetIO();
        if(io.BackendRendererUserData == this)
            io.BackendRendererUserData = nullptr;
        invalidateResources();
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

    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationHasWork = false;
    m_taskGraphPresentationClaimed = false;
    m_taskGraphLegacyPresentationClaimed = false;
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();

    ++m_frameGeneration;
    if(m_frameGeneration == 0u)
        ++m_frameGeneration;

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

    Core::Framebuffer* framebuffer = m_graphics.getCurrentFramebuffer();
    return !framebuffer || ensureRenderResources(framebuffer);
}

void UiSystem::invalidateResources(){
    if(m_imguiContext){
        setCurrentContext();
#if defined(IMGUI_HAS_TEXTURES)
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        for(ImTextureData* textureData : platformIO.Textures){
            if(!textureData)
                continue;

            textureData->BackendUserData = nullptr;
            textureData->SetTexID(ImTextureID_Invalid);
            textureData->SetStatus(ImTextureStatus_Destroyed);
        }
#endif
    }

    // Descriptor heap entries retain the backing textures/sampler through their deferred-free quarantine. Graphics
    // invalidates render passes while the device is still live, so retire descriptors before releasing those resources.
    releaseDescriptorHeapResources();
    m_textures.clear();
    m_textureUploadBatch.reset();
    m_textureUploadScratch.clear();
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();

    m_renderCommandList.reset();
    m_bindingLayout.reset();
    m_sampler.reset();
    m_vertexShader.reset();
    m_pixelShader.reset();
    m_inputLayout.reset();
    m_pipeline.reset();
    m_vertexBuffer.reset();
    m_indexBuffer.reset();
    m_vertexBufferCapacity = 0u;
    m_indexBufferCapacity = 0u;
    m_frameStarted = false;
    m_frameFinished = false;
    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationHasWork = false;
    m_taskGraphPresentationClaimed = false;
    m_taskGraphLegacyPresentationClaimed = false;
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphPresentationGraphGeneration = 0u;
}

bool UiSystem::ensureRenderCommandList(){
    auto& device = m_graphics.getDevice();

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
    if(m_taskGraphPresentationPrepared)
        return true;

    // A world without RendererSystem still has a complete immutable overlay snapshot, so prefer the standalone
    // graph path. An arbitrary ImGui callback cannot be retained safely; only that exceptional case falls through
    // to the direct raster compatibility preparation below.
    if(prepareTaskGraphPresentation(framebuffer))
        return true;
    return prepareFrameResources(framebuffer, false);
}

bool UiSystem::prepareFrameResources(Core::Framebuffer* framebuffer, const bool graphOwnsUploads){
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

    i32 framebufferWidth = 0;
    i32 framebufferHeight = 0;
    __hidden_ui::GetFramebufferExtent(*drawData, framebufferWidth, framebufferHeight);
    if(framebufferWidth <= 0 || framebufferHeight <= 0){
        m_frameStarted = false;
        m_frameFinished = false;
        return true;
    }

    if(!ensureRenderResources(framebuffer))
        return false;

    if(drawData->TotalVtxCount < 0 || drawData->TotalIdxCount < 0){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui reported negative draw-buffer counts"));
        return false;
    }

    if(__hidden_ui::HasTextureRequests(*drawData)){
        // Both the renderer-owned and standalone graph routes retain prepared textures until task declaration.
        // The exceptional direct raster fallback still uses the same declaration-time resource preparation.
        if(!prepareTextureRequests(*drawData))
            return false;
    }

    const usize vertexCount = static_cast<usize>(drawData->TotalVtxCount);
    const usize indexCount = static_cast<usize>(drawData->TotalIdxCount);
    usize bufferIndexCount = indexCount;
    if(graphOwnsUploads && indexCount > 0u){
        if(indexCount > Limit<usize>::s_Max / sizeof(ImDrawIdx)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: graph-owned ImGui index upload exceeds addressable memory"));
            return false;
        }
        usize paddedIndexBytes = 0u;
        if(!__hidden_ui::AlignUploadBytes(indexCount * sizeof(ImDrawIdx), paddedIndexBytes)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: graph-owned ImGui index upload size overflows"));
            return false;
        }
        bufferIndexCount = paddedIndexBytes / sizeof(ImDrawIdx);
    }

    if(!ensureBuffers(vertexCount, bufferIndexCount))
        return false;

    if(drawData->TotalVtxCount <= 0 || drawData->TotalIdxCount <= 0)
        return true;

    // The renderer can abandon a graph after graph-owned preparation. Keep the direct fallback command list ready
    // before that declaration so render() only records against prepared persistent resources.
    if(!ensureRenderCommandList())
        return false;

    if(graphOwnsUploads && !prepareTaskGraphDrawUploads(*drawData))
        return false;

    return true;
}

void UiSystem::clearTaskGraphDrawSnapshot()noexcept{
    m_taskGraphDrawCommands.clear();
    m_taskGraphDrawSnapshot = {};
}

bool UiSystem::prepareTaskGraphDrawUploads(ImDrawData& drawData){
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();

    const auto discardPreparedUploads = [this](){
        m_taskGraphVertexUpload.clear();
        m_taskGraphIndexUpload.clear();
        clearTaskGraphDrawSnapshot();
    };

    if(drawData.TotalVtxCount <= 0 || drawData.TotalIdxCount <= 0)
        return true;

    if(!m_vertexBuffer || !m_indexBuffer || !m_pipeline || !m_samplerHeapHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: graph-owned ImGui overlay resources are unavailable during preflight"));
        return false;
    }

    i32 framebufferWidth = 0;
    i32 framebufferHeight = 0;
    __hidden_ui::GetFramebufferExtent(drawData, framebufferWidth, framebufferHeight);
    if(framebufferWidth <= 0 || framebufferHeight <= 0){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: graph-owned ImGui overlay has an invalid framebuffer extent"));
        return false;
    }

    const usize vertexCount = static_cast<usize>(drawData.TotalVtxCount);
    const usize indexCount = static_cast<usize>(drawData.TotalIdxCount);
    if(
        vertexCount > Limit<usize>::s_Max / sizeof(ImDrawVert)
        || indexCount > Limit<usize>::s_Max / sizeof(ImDrawIdx)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: graph-owned ImGui draw upload exceeds addressable memory"));
        return false;
    }

    const usize vertexBytes = vertexCount * sizeof(ImDrawVert);
    const usize indexBytes = indexCount * sizeof(ImDrawIdx);
    usize paddedIndexBytes = 0u;
    if(
        (vertexBytes & (__hidden_ui::s_UploadAlignmentBytes - 1u)) != 0u
        || !__hidden_ui::AlignUploadBytes(indexBytes, paddedIndexBytes)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui draw upload cannot satisfy native copy alignment"));
        return false;
    }

    m_taskGraphVertexUpload.resize(vertexBytes);
    m_taskGraphIndexUpload.assign(paddedIndexBytes, 0u);
    if(
        m_taskGraphVertexUpload.size() != vertexBytes
        || m_taskGraphIndexUpload.size() != paddedIndexBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to retain graph-owned ImGui draw upload bytes"));
        discardPreparedUploads();
        return false;
    }

    if(drawData.CmdListsCount < 0 || (drawData.CmdListsCount > 0 && !drawData.CmdLists.Data)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui reported invalid draw-list storage"));
        discardPreparedUploads();
        return false;
    }

    TaskGraphDrawSnapshot snapshot;
    snapshot.vertexBuffer = m_vertexBuffer;
    snapshot.indexBuffer = m_indexBuffer;
    snapshot.pipeline = m_pipeline;
    snapshot.samplerHeapHandle = m_samplerHeapHandle;
    snapshot.framebufferWidth = framebufferWidth;
    snapshot.framebufferHeight = framebufferHeight;
    StoreFloat(__hidden_ui::BuildUiScaleTranslate(
        VectorSet(drawData.DisplayPos.x, drawData.DisplayPos.y, 0.0f, 0.0f),
        VectorSet(drawData.DisplaySize.x, drawData.DisplaySize.y, 0.0f, 0.0f)
    ), &snapshot.pushConstants.scaleTranslate);
    snapshot.pushConstants.presentationMode = static_cast<u32>(
        m_graphics.isHDR10OutputActive()
            ? Core::SwapChainOutputMode::HDR10
            : Core::SwapChainOutputMode::SDR
    );
    snapshot.pushConstants.samplerSlot = snapshot.samplerHeapHandle.slot();

    usize vertexOffset = 0u;
    usize indexOffset = 0u;
    for(i32 listIndex = 0; listIndex < drawData.CmdListsCount; ++listIndex){
        const ImDrawList* const drawList = drawData.CmdLists[listIndex];
        if(!drawList)
            continue;

        if(
            drawList->VtxBuffer.Size < 0
            || drawList->IdxBuffer.Size < 0
            || drawList->CmdBuffer.Size < 0
            || (drawList->CmdBuffer.Size > 0 && !drawList->CmdBuffer.Data)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui reported invalid graph-owned draw-list storage"));
            discardPreparedUploads();
            return false;
        }
        const usize drawListVertexCount = static_cast<usize>(drawList->VtxBuffer.Size);
        const usize drawListIndexCount = static_cast<usize>(drawList->IdxBuffer.Size);
        const usize drawListVertexBytes = drawListVertexCount * sizeof(ImDrawVert);
        const usize drawListIndexBytes = drawListIndexCount * sizeof(ImDrawIdx);
        if(
            (drawListVertexBytes != 0u && !drawList->VtxBuffer.Data)
            || (drawListIndexBytes != 0u && !drawList->IdxBuffer.Data)
            || drawListVertexBytes > vertexBytes - vertexOffset
            || drawListIndexBytes > indexBytes - indexOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui draw-list data changed during graph upload preflight"));
            discardPreparedUploads();
            return false;
        }

        if(drawListVertexBytes != 0u){
            NWB_MEMCPY(
                m_taskGraphVertexUpload.data() + vertexOffset,
                m_taskGraphVertexUpload.size() - vertexOffset,
                drawList->VtxBuffer.Data,
                drawListVertexBytes
            );
        }
        if(drawListIndexBytes != 0u){
            NWB_MEMCPY(
                m_taskGraphIndexUpload.data() + indexOffset,
                m_taskGraphIndexUpload.size() - indexOffset,
                drawList->IdxBuffer.Data,
                drawListIndexBytes
            );
        }

        const u64 globalVertexOffset = vertexOffset / sizeof(ImDrawVert);
        const u64 globalIndexOffset = indexOffset / sizeof(ImDrawIdx);
        for(i32 commandIndex = 0; commandIndex < drawList->CmdBuffer.Size; ++commandIndex){
            const ImDrawCmd& drawCommand = drawList->CmdBuffer[commandIndex];
            if(drawCommand.UserCallback){
                if(__hidden_ui::IsTaskGraphResetCallback(drawCommand))
                    continue;

                NWB_LOGGER_WARNING(NWB_TEXT("UiSystem: graph-owned ImGui overlay cannot safely record a custom draw callback"));
                discardPreparedUploads();
                return false;
            }

            const SIMDVector clipRect = VectorMultiply(
                VectorSubtract(
                    VectorSet(drawCommand.ClipRect.x, drawCommand.ClipRect.y, drawCommand.ClipRect.z, drawCommand.ClipRect.w),
                    VectorSet(drawData.DisplayPos.x, drawData.DisplayPos.y, drawData.DisplayPos.x, drawData.DisplayPos.y)
                ),
                VectorSet(
                    drawData.FramebufferScale.x,
                    drawData.FramebufferScale.y,
                    drawData.FramebufferScale.x,
                    drawData.FramebufferScale.y
                )
            );
            // Preserve ImGui's asymmetric clip bounds: lower-bound min lanes and upper-bound max lanes.
            const SIMDVector clampedClipRect = VectorSelect(
                VectorMin(
                    clipRect,
                    VectorSet(
                        static_cast<f32>(framebufferWidth),
                        static_cast<f32>(framebufferHeight),
                        static_cast<f32>(framebufferWidth),
                        static_cast<f32>(framebufferHeight)
                    )
                ),
                VectorMax(clipRect, s_SIMDZero),
                s_SIMDMaskXY
            );
            const f32 clipMinX = VectorGetX(clampedClipRect);
            const f32 clipMinY = VectorGetY(clampedClipRect);
            const f32 clipMaxX = VectorGetZ(clampedClipRect);
            const f32 clipMaxY = VectorGetW(clampedClipRect);
            if(clipMaxX <= clipMinX || clipMaxY <= clipMinY)
                continue;

            const usize commandIndexOffset = static_cast<usize>(drawCommand.IdxOffset);
            const usize commandElementCount = static_cast<usize>(drawCommand.ElemCount);
            const usize commandVertexOffset = static_cast<usize>(drawCommand.VtxOffset);
            if(
                commandIndexOffset > drawListIndexCount
                || commandElementCount > drawListIndexCount - commandIndexOffset
                || commandVertexOffset > drawListVertexCount
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui draw command exceeds its graph-owned upload snapshot"));
                discardPreparedUploads();
                return false;
            }

            const u64 startIndexLocation = globalIndexOffset + commandIndexOffset;
            const u64 startVertexLocation = globalVertexOffset + commandVertexOffset;
            if(
                startIndexLocation > Limit<u32>::s_Max
                || startVertexLocation > Limit<u32>::s_Max
                || commandElementCount > Limit<u32>::s_Max
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui graph-owned draw command exceeds native index limits"));
                discardPreparedUploads();
                return false;
            }

            UiTextureResource* const textureResource = textureResourceForDraw(drawCommand.GetTexID());
            if(
                !textureResource
                || !textureResource->texture
                || !textureResource->sampledImageHeapHandle.valid()
                || textureResource->sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: graph-owned ImGui draw command has no declared sampled texture"));
                discardPreparedUploads();
                return false;
            }

            const usize priorCommandCount = m_taskGraphDrawCommands.size();
            m_taskGraphDrawCommands.push_back(TaskGraphDrawCommand{
                .texture = textureResource->texture,
                .textureHeapHandle = textureResource->sampledImageHeapHandle,
                .clipMinX = clipMinX,
                .clipMinY = clipMinY,
                .clipMaxX = clipMaxX,
                .clipMaxY = clipMaxY,
                .elementCount = static_cast<u32>(commandElementCount),
                .startIndexLocation = static_cast<u32>(startIndexLocation),
                .startVertexLocation = static_cast<u32>(startVertexLocation),
            });
            if(m_taskGraphDrawCommands.size() != priorCommandCount + 1u){
                NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to retain graph-owned ImGui draw command"));
                discardPreparedUploads();
                return false;
            }
        }

        vertexOffset += drawListVertexBytes;
        indexOffset += drawListIndexBytes;
    }
    if(vertexOffset != vertexBytes || indexOffset != indexBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: ImGui draw-list counts no longer match its graph upload payload"));
        discardPreparedUploads();
        return false;
    }

    snapshot.valid = true;
    m_taskGraphDrawSnapshot = Move(snapshot);
    m_taskGraphDrawUploadsPrepared = true;
    return true;
}

bool UiSystem::declareTaskGraphDrawUploads(
    Core::GpuTaskGraph& graph,
    const Core::GpuGraphResourceId& vertexBuffer,
    const Core::GpuGraphResourceId& indexBuffer,
    const Core::GpuTaskId previousTask,
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena>& outTasks
){
    if(
        !m_taskGraphDrawUploadsPrepared
        || !vertexBuffer.valid()
        || !indexBuffer.valid()
        || m_taskGraphVertexUpload.empty()
        || m_taskGraphIndexUpload.empty()
    )
        return false;

    const auto declareUpload = [&](const Name& identity,
                                   const AStringView label,
                                   const UiTextureUploadVector& bytes,
                                   const Core::GpuGraphResourceId destination){
        const Core::GpuUploadBlobId blob = graph.copyUploadData(
            bytes.data(),
            bytes.size(),
            __hidden_ui::s_UploadAlignmentBytes
        );
        if(!blob.valid())
            return Core::GpuTaskId{};

        Core::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(__hidden_ui::UploadQueueRequest())
            .setScheduling(__hidden_ui::UploadScheduling(bytes.size()))
        ;
        if(previousTask.valid())
            desc.setDependencies(&previousTask, 1u);
        return graph.addUploadBufferTask(
            desc,
            Core::GpuUploadBufferTaskDesc{
                .source = blob,
                .destination = destination,
                // The dynamic UI buffers restore Common as their keep-initial-state contract when each packet closes.
                .finalState = Core::ResourceStates::Common,
            }
        );
    };

    const Core::GpuTaskId vertexUpload = declareUpload(
        Name("ui.imgui_vertex_upload"),
        "ImGui Vertex Upload",
        m_taskGraphVertexUpload,
        vertexBuffer
    );
    const Core::GpuTaskId indexUpload = declareUpload(
        Name("ui.imgui_index_upload"),
        "ImGui Index Upload",
        m_taskGraphIndexUpload,
        indexBuffer
    );
    if(!vertexUpload.valid() || !indexUpload.valid())
        return false;

    outTasks.push_back(vertexUpload);
    outTasks.push_back(indexUpload);
    return true;
}

bool UiSystem::prepareTaskGraphPresentation(Core::Framebuffer* framebuffer){
    if(m_taskGraphPresentationPrepared)
        return true;
    if(!prepareFrameResources(framebuffer, true))
        return false;

    setCurrentContext();
    ImDrawData* const drawData = ImGui::GetDrawData();
    const bool hasDrawWork =
        drawData
        && drawData->TotalVtxCount > 0
        && drawData->TotalIdxCount > 0
        && m_pipeline
        && m_taskGraphDrawUploadsPrepared
        && m_taskGraphDrawSnapshot.valid
        && !m_taskGraphDrawCommands.empty()
    ;
    // A font/texture update can arrive before any visible ImGui command. Keep it in the shared graph with a
    // terminal completion packet rather than sending a direct preparation list after scene presentation.
    m_taskGraphPresentationHasWork = m_frameFinished && drawData && (
        hasDrawWork || __hidden_ui::HasPendingTextureUploads(*drawData)
    );
    m_taskGraphPresentationPrepared = true;
    return true;
}

bool UiSystem::hasTaskGraphPresentationWork()const{
    return m_taskGraphPresentationPrepared && m_taskGraphPresentationHasWork;
}

Core::GpuTaskId UiSystem::declareTaskGraphPresentation(
    Core::GpuTaskGraph& graph,
    Core::Framebuffer* const framebuffer,
    const Core::GpuGraphResourceId backbuffer,
    const Core::GpuTaskId previousTask
){
    // A failed optional tail may rebuild the renderer graph in the same ImGui frame.  Retain only a same-generation
    // claim; graph-owned blobs and imported IDs from the discarded attempt must never suppress the retry.
    if(
        m_taskGraphPresentationClaimed
        && m_taskGraphPresentationGraphGeneration != graph.generation()
    ){
        m_taskGraphPresentationClaimed = false;
        m_taskGraphPresentationGraphGeneration = 0u;
        m_textureUploadBatch.reset();
    }
    if(
        !m_taskGraphPresentationPrepared
        || !m_taskGraphPresentationHasWork
        || m_taskGraphPresentationClaimed
        || !framebuffer
        || !backbuffer.valid()
    )
        return {};

    ImDrawData* const drawData = ImGui::GetDrawData();
    if(!drawData)
        return {};

    Core::Alloc::ScratchArena scratchArena(__hidden_ui::s_TaskGraphDeclarationArena);
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> uploadTasks(scratchArena);
    uploadTasks.reserve(2u);
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> uploadedTextures(scratchArena);
    uploadedTextures.reserve(m_textures.size());
    m_textureUploadBatch.reset();

    const bool hasDrawWork =
        m_taskGraphDrawUploadsPrepared
        && m_taskGraphDrawSnapshot.valid
        && !m_taskGraphDrawCommands.empty()
    ;
    if(!hasDrawWork){
        if(
            !declareTaskGraphTextureUploads(
                graph,
                *drawData,
                previousTask,
                uploadTasks,
                uploadedTextures
            )
            || uploadTasks.empty()
        ){
            m_textureUploadBatch.reset();
            return {};
        }

        Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> dependencies(scratchArena);
        dependencies.reserve((previousTask.valid() ? 1u : 0u) + uploadTasks.size());
        if(previousTask.valid())
            dependencies.push_back(previousTask);
        for(const Core::GpuTaskId task : uploadTasks)
            dependencies.push_back(task);

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses(scratchArena);
        resourceUses.reserve(uploadedTextures.size());
        for(const Core::GpuGraphResourceId texture : uploadedTextures){
            bool alreadyDeclared = false;
            for(const Core::GpuTaskResourceUse& use : resourceUses){
                if(use.resource == texture){
                    alreadyDeclared = true;
                    break;
                }
            }
            if(!alreadyDeclared)
                resourceUses.push_back(__hidden_ui::ReadTextureUse(texture));
        }

        Core::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Core::GpuTaskCostHint::Tiny;
        scheduling.avoidQueueCrossing = true;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        Core::GpuTaskDesc desc;
        desc
            .setIdentity(Name("ui.imgui_upload_completion"))
            .setMarkerLabel("ImGui Upload Completion")
            .setQueue(Core::GpuQueueRequest{
                Core::GpuQueueCapability::Graphics,
                Core::GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling)
            .setDependencies(dependencies.data(), dependencies.size())
            // Return any Transfer-owned font/texture update to the Graphics consumer queue even when this frame has
            // no visible draw command.  The no-op record body intentionally exists to lower this final handoff.
            .setResourceUses(resourceUses.data(), resourceUses.size())
        ;
        const Core::GpuTaskId task = graph.addTask<TaskGraphUploadCompletionTask>(
            desc,
            TaskGraphUploadCompletionTask::Payload{
                .ui = this,
            }
        );
        if(task.valid()){
            m_taskGraphPresentationClaimed = true;
            m_taskGraphPresentationGraphGeneration = graph.generation();
        }
        else
            m_textureUploadBatch.reset();
        return task;
    }

    if(
        !m_taskGraphDrawSnapshot.vertexBuffer
        || !m_taskGraphDrawSnapshot.indexBuffer
        || !m_taskGraphDrawSnapshot.pipeline
        || !m_taskGraphDrawSnapshot.samplerHeapHandle.valid()
    )
        return {};
    const Core::GpuGraphResourceId vertexBuffer = graph.importBuffer(
        m_taskGraphDrawSnapshot.vertexBuffer,
        __hidden_ui::BufferResourceDesc(
            Name("ui.imgui_vertices"),
            "ImGui Vertices",
            m_taskGraphDrawSnapshot.vertexBuffer->getDescription()
        )
    );
    const Core::GpuGraphResourceId indexBuffer = graph.importBuffer(
        m_taskGraphDrawSnapshot.indexBuffer,
        __hidden_ui::BufferResourceDesc(
            Name("ui.imgui_indices"),
            "ImGui Indices",
            m_taskGraphDrawSnapshot.indexBuffer->getDescription()
        )
    );
    if(!vertexBuffer.valid() || !indexBuffer.valid())
        return {};

    if(
        !declareTaskGraphDrawUploads(graph, vertexBuffer, indexBuffer, previousTask, uploadTasks)
        || !declareTaskGraphTextureUploads(
            graph,
            *drawData,
            previousTask,
            uploadTasks,
            uploadedTextures
        )
    ){
        m_textureUploadBatch.reset();
        return {};
    }

    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> dependencies(scratchArena);
    dependencies.reserve((previousTask.valid() ? 1u : 0u) + uploadTasks.size());
    if(previousTask.valid())
        dependencies.push_back(previousTask);
    for(const Core::GpuTaskId task : uploadTasks)
        dependencies.push_back(task);

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses(scratchArena);
    resourceUses.reserve(3u + uploadedTextures.size() + m_taskGraphDrawCommands.size());
    resourceUses.push_back(Core::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            // The swap-chain texture restores its keep-initial-state Present layout when this command list closes.
            // The graph hazard domain carries the authoritative ordering with the deferred scene-output packet.
            .requiredState = Core::ResourceStates::Present,
            .access = Core::GpuTaskResourceAccess::Write,
    });
    resourceUses.push_back(__hidden_ui::ReadBufferUse(vertexBuffer, Core::ResourceStates::VertexBuffer));
    resourceUses.push_back(__hidden_ui::ReadBufferUse(indexBuffer, Core::ResourceStates::IndexBuffer));

    // A requested texture may not appear in this frame's draw commands. Still declare it on the terminal Graphics
    // task so a Transfer upload publishes ShaderResource state and ownership for the next frame's UI consumer.
    for(const Core::GpuGraphResourceId texture : uploadedTextures){
        bool alreadyDeclared = false;
        for(const Core::GpuTaskResourceUse& use : resourceUses){
            if(use.resource == texture){
                alreadyDeclared = true;
                break;
            }
        }
        if(!alreadyDeclared)
            resourceUses.push_back(__hidden_ui::ReadTextureUse(texture));
    }

    const auto appendDrawTextureUse = [&](const Core::TextureHandle& texture){
        if(!texture)
            return true;
        Core::GpuGraphResourceId textureResource = graph.findImportedTexture(texture);
        if(!textureResource.valid()){
            textureResource = graph.importTexture(
                texture,
                __hidden_ui::TextureResourceDesc(texture->getDescription())
            );
        }
        if(!textureResource.valid())
            return false;
        for(const Core::GpuTaskResourceUse& use : resourceUses){
            if(use.resource == textureResource)
                return true;
        }
        resourceUses.push_back(__hidden_ui::ReadTextureUse(textureResource));
        return true;
    };
    for(const TaskGraphDrawCommand& drawCommand : m_taskGraphDrawCommands){
        if(!appendDrawTextureUse(drawCommand.texture)){
            m_textureUploadBatch.reset();
            return {};
        }
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("ui.imgui_overlay"))
        .setMarkerLabel("ImGui Overlay")
        .setQueue(Core::GpuQueueRequest{
            Core::GpuQueueCapability::Graphics,
            Core::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setDependencies(dependencies.data(), dependencies.size())
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    const Core::GpuTaskId task = graph.addTask<TaskGraphRenderTask>(
        desc,
        TaskGraphRenderTask::Payload{
            .ui = this,
            .framebuffer = framebuffer,
        }
    );
    if(task.valid()){
        m_taskGraphPresentationClaimed = true;
        m_taskGraphPresentationGraphGeneration = graph.generation();
    }
    else
        m_textureUploadBatch.reset();
    return task;
}

bool UiSystem::submitStandaloneTaskGraphPresentation(Core::Framebuffer* const framebuffer){
    if(
        !framebuffer
        || !m_frameFinished
        || !m_taskGraphPresentationPrepared
        || !m_taskGraphPresentationHasWork
    )
        return false;

    const Core::GpuPhysicalQueueId graphicsQueue =
        m_graphics.getDevice().getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)
    ;
    if(!graphicsQueue.valid())
        return false;

    // RendererSystem may already have declared this optional tail before a later graph packet rejected. The accepted
    // path resets m_frameFinished before UiSystem::render() reaches here, so a still-live claim is necessarily an
    // abandoned declaration and can be rebuilt as this independent graph.
    m_taskGraphPresentationClaimed = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    struct StandalonePresentationContext{
        UiSystem* ui = nullptr;
        Core::Framebuffer* framebuffer = nullptr;
    };
    StandalonePresentationContext context{
        .ui = this,
        .framebuffer = framebuffer,
    };
    Core::QueueSubmissionToken submissionToken;
    const bool submitted = m_graphics.submitStandaloneTaskGraph(
        &context,
        [](void* const rawContext, Core::GpuTaskGraph& graph){
            StandalonePresentationContext* const context =
                static_cast<StandalonePresentationContext*>(rawContext)
            ;
            if(!context || !context->ui || !context->framebuffer)
                return Core::GpuTaskId{};

            const Core::GpuGraphResourceId backbuffer = graph.importHazardDomain(
                Core::GpuGraphResourceDesc{}
                    .setIdentity(Name("ui.imgui_standalone_presentation.backbuffer"))
                    .setMarkerLabel("Standalone ImGui Presentation Back Buffer")
                    .setType(Core::GpuGraphResourceType::HazardDomain)
            );
            if(!backbuffer.valid())
                return Core::GpuTaskId{};
            return context->ui->declareTaskGraphPresentation(
                graph,
                context->framebuffer,
                backbuffer,
                {}
            );
        },
        submissionToken,
        graphicsQueue
    );
    if(submitted && submissionToken.valid())
        return true;

    // A failed standalone attempt must leave the direct compatibility path able to consume the live ImGui list.
    // The task graph itself discards incomplete immutable upload work; clearing this claim only releases graph IDs.
    m_taskGraphPresentationClaimed = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    return false;
}

Core::GpuTaskId UiSystem::declareStandaloneLegacyTaskGraphPresentation(
    Core::GpuTaskGraph& graph,
    Core::Framebuffer* const framebuffer,
    ImDrawData* const drawData,
    const u64 frameGeneration
){
    if(
        !framebuffer
        || !drawData
        || !m_frameFinished
        || m_taskGraphLegacyPresentationClaimed
        || frameGeneration == 0u
        || frameGeneration != m_frameGeneration
        || drawData->TotalVtxCount <= 0
        || drawData->TotalIdxCount <= 0
        || !m_pipeline
        || !m_samplerHeapHandle.valid()
        || !drawBuffersReady(
            static_cast<usize>(drawData->TotalVtxCount),
            static_cast<usize>(drawData->TotalIdxCount)
        )
        || drawData->CmdListsCount < 0
        || (drawData->CmdListsCount > 0 && !drawData->CmdLists.Data)
    )
        return {};

    const Core::GpuGraphResourceId backbuffer = graph.importHazardDomain(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name("ui.imgui_standalone_legacy_presentation.backbuffer"))
            .setMarkerLabel("Standalone ImGui Legacy Presentation Back Buffer")
            .setType(Core::GpuGraphResourceType::HazardDomain)
    );
    const Core::GpuGraphResourceId opaqueCallbackDomain = graph.importHazardDomain(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name("ui.imgui_standalone_legacy_presentation.callback"))
            .setMarkerLabel("ImGui Opaque Callback Domain")
            .setType(Core::GpuGraphResourceType::HazardDomain)
    );
    const Core::GpuGraphResourceId vertexBuffer = graph.importBuffer(
        m_vertexBuffer,
        __hidden_ui::BufferResourceDesc(
            Name("ui.imgui_standalone_legacy_vertices"),
            "Standalone ImGui Legacy Vertices",
            m_vertexBuffer->getDescription()
        )
    );
    const Core::GpuGraphResourceId indexBuffer = graph.importBuffer(
        m_indexBuffer,
        __hidden_ui::BufferResourceDesc(
            Name("ui.imgui_standalone_legacy_indices"),
            "Standalone ImGui Legacy Indices",
            m_indexBuffer->getDescription()
        )
    );
    if(
        !backbuffer.valid()
        || !opaqueCallbackDomain.valid()
        || !vertexBuffer.valid()
        || !indexBuffer.valid()
    )
        return {};

    Core::Alloc::ScratchArena scratchArena(__hidden_ui::s_TaskGraphDeclarationArena);
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> uploadTasks(scratchArena);
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> uploadedTextures(scratchArena);
    uploadTasks.reserve(2u);
    uploadedTextures.reserve(m_textures.size());
    if(!declareTaskGraphTextureUploads(graph, *drawData, {}, uploadTasks, uploadedTextures)){
        m_textureUploadBatch.reset();
        return {};
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses(scratchArena);
    resourceUses.reserve(4u + uploadedTextures.size() + static_cast<usize>(drawData->CmdListsCount));
    resourceUses.push_back(Core::GpuTaskResourceUse{
        .resource = backbuffer,
        .range = {},
        .requiredState = Core::ResourceStates::Present,
        .access = Core::GpuTaskResourceAccess::Write,
    });
    // The opaque task writes its live ImGui bytes before binding the same buffers for rasterization.  The callback
    // retains that intra-task CopyDest-to-Vertex/Index transition; the graph owns packet ordering and final use.
    resourceUses.push_back(Core::GpuTaskResourceUse{
        .resource = vertexBuffer,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    });
    resourceUses.push_back(Core::GpuTaskResourceUse{
        .resource = indexBuffer,
        .range = {},
        .requiredState = Core::ResourceStates::IndexBuffer,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    });
    // A custom ImDrawCmd callback can bind arbitrary user resources.  Keep that operation explicit and serial
    // without pretending its private state can be converted into an immutable resource declaration.
    resourceUses.push_back(Core::GpuTaskResourceUse{
        .resource = opaqueCallbackDomain,
        .range = {},
        .requiredState = Core::ResourceStates::Unknown,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    });

    const auto appendTextureUse = [&](const Core::GpuGraphResourceId texture){
        if(!texture.valid())
            return false;
        for(const Core::GpuTaskResourceUse& use : resourceUses){
            if(use.resource == texture)
                return true;
        }
        resourceUses.push_back(__hidden_ui::ReadTextureUse(texture));
        return true;
    };
    for(const Core::GpuGraphResourceId texture : uploadedTextures){
        if(!appendTextureUse(texture)){
            m_textureUploadBatch.reset();
            return {};
        }
    }

    for(i32 listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex){
        const ImDrawList* const drawList = drawData->CmdLists[listIndex];
        if(!drawList)
            continue;
        if(drawList->CmdBuffer.Size < 0 || (drawList->CmdBuffer.Size > 0 && !drawList->CmdBuffer.Data)){
            m_textureUploadBatch.reset();
            return {};
        }

        for(i32 commandIndex = 0; commandIndex < drawList->CmdBuffer.Size; ++commandIndex){
            const ImDrawCmd& drawCommand = drawList->CmdBuffer[commandIndex];
            if(drawCommand.UserCallback)
                continue;

            UiTextureResource* const textureResource = textureResourceForDraw(drawCommand.GetTexID());
            const Core::GpuGraphResourceId texture = textureResource
                ? importTaskGraphTexture(graph, *textureResource)
                : Core::GpuGraphResourceId{}
            ;
            if(!appendTextureUse(texture)){
                m_textureUploadBatch.reset();
                return {};
            }
        }
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("ui.imgui_standalone_legacy_presentation"))
        .setMarkerLabel("Standalone ImGui Opaque Callback Presentation")
        .setQueue(__hidden_ui::OpaquePresentationQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(uploadTasks.data(), uploadTasks.size())
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    const Core::GpuTaskId task = graph.addTask<StandaloneLegacyPresentationTask>(
        desc,
        StandaloneLegacyPresentationTask::Payload{
            .ui = this,
            .framebuffer = framebuffer,
            .drawData = drawData,
            .frameGeneration = frameGeneration,
        }
    );
    if(task.valid())
        m_taskGraphLegacyPresentationClaimed = true;
    else
        m_textureUploadBatch.reset();
    return task;
}

bool UiSystem::submitStandaloneLegacyTaskGraphPresentation(Core::Framebuffer* const framebuffer){
    if(!framebuffer || !m_frameFinished || m_taskGraphLegacyPresentationClaimed)
        return false;

    setCurrentContext();
    ImDrawData* const drawData = ImGui::GetDrawData();
    if(!drawData)
        return false;

    const Core::GpuPhysicalQueueId graphicsQueue =
        m_graphics.getDevice().getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)
    ;
    if(!graphicsQueue.valid())
        return false;

    struct StandaloneLegacyPresentationContext{
        UiSystem* ui = nullptr;
        Core::Framebuffer* framebuffer = nullptr;
        ImDrawData* drawData = nullptr;
        u64 frameGeneration = 0u;
    };
    StandaloneLegacyPresentationContext context{
        .ui = this,
        .framebuffer = framebuffer,
        .drawData = drawData,
        .frameGeneration = m_frameGeneration,
    };
    Core::QueueSubmissionToken submissionToken;
    const bool submitted = m_graphics.submitStandaloneTaskGraph(
        &context,
        [](void* const rawContext, Core::GpuTaskGraph& graph){
            StandaloneLegacyPresentationContext* const context =
                static_cast<StandaloneLegacyPresentationContext*>(rawContext)
            ;
            if(!context || !context->ui)
                return Core::GpuTaskId{};
            return context->ui->declareStandaloneLegacyTaskGraphPresentation(
                graph,
                context->framebuffer,
                context->drawData,
                context->frameGeneration
            );
        },
        submissionToken,
        graphicsQueue
    );
    if(submitted && submissionToken.valid())
        return true;

    // This is the final availability fallback: graph compilation/recording failure must not drop an editor overlay.
    // The rejected graph leaves every texture request pending for the existing direct path below.
    m_taskGraphLegacyPresentationClaimed = false;
    m_textureUploadBatch.complete(false);
    return false;
}

Core::GpuTaskId UiSystem::declareStandaloneTextureUploadGraph(Core::GpuTaskGraph& graph){
    if(!m_frameFinished)
        return {};

    setCurrentContext();
    ImDrawData* const drawData = ImGui::GetDrawData();
    if(!drawData)
        return {};

    Core::Alloc::ScratchArena scratchArena(__hidden_ui::s_TaskGraphDeclarationArena);
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> uploadTasks(scratchArena);
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> uploadedTextures(scratchArena);
    uploadTasks.reserve(2u);
    uploadedTextures.reserve(m_textures.size());
    if(
        !declareTaskGraphTextureUploads(
            graph,
            *drawData,
            {},
            uploadTasks,
            uploadedTextures
        )
        || uploadTasks.empty()
    ){
        m_textureUploadBatch.reset();
        return {};
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses(scratchArena);
    resourceUses.reserve(uploadedTextures.size());
    for(const Core::GpuGraphResourceId texture : uploadedTextures){
        bool alreadyDeclared = false;
        for(const Core::GpuTaskResourceUse& use : resourceUses){
            if(use.resource == texture){
                alreadyDeclared = true;
                break;
            }
        }
        if(!alreadyDeclared)
            resourceUses.push_back(__hidden_ui::ReadTextureUse(texture));
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("ui.imgui_standalone_texture_upload_completion"))
        .setMarkerLabel("ImGui Standalone Texture Upload Completion")
        .setQueue(Core::GpuQueueRequest{
            Core::GpuQueueCapability::Graphics,
            Core::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setDependencies(uploadTasks.data(), uploadTasks.size())
        // The direct raster fallback runs after this graph returns, so its first texture sampling must observe the
        // compiler-owned ShaderResource handoff even if an upload preferred a separate transport.
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    const Core::GpuTaskId completionTask = graph.addTask<StandaloneTextureUploadCompletionTask>(
        desc,
        StandaloneTextureUploadCompletionTask::Payload{
            .ui = this,
            .uploadsPrepared = true,
        }
    );
    if(!completionTask.valid())
        m_textureUploadBatch.reset();
    return completionTask;
}

bool UiSystem::recordTaskGraphDrawSnapshot(Core::CommandList& commandList, Core::Framebuffer* const framebuffer){
    const TaskGraphDrawSnapshot& snapshot = m_taskGraphDrawSnapshot;
    if(
        !framebuffer
        || !snapshot.valid
        || !snapshot.vertexBuffer
        || !snapshot.indexBuffer
        || !snapshot.pipeline
        || !snapshot.samplerHeapHandle.valid()
        || snapshot.samplerHeapHandle.descriptorClass() != Core::GpuDescriptorClass::Sampler
        || snapshot.framebufferWidth <= 0
        || snapshot.framebufferHeight <= 0
        || m_taskGraphDrawCommands.empty()
    )
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    const Core::Viewport viewport(
        0.0f,
        static_cast<f32>(snapshot.framebufferWidth),
        0.0f,
        static_cast<f32>(snapshot.framebufferHeight),
        0.0f,
        1.0f
    );
    const Core::Format::Enum indexFormat = sizeof(ImDrawIdx) == sizeof(u16)
        ? Core::Format::R16_UINT
        : Core::Format::R32_UINT
    ;

    for(const TaskGraphDrawCommand& drawCommand : m_taskGraphDrawCommands){
        if(
            !drawCommand.texture
            || !drawCommand.textureHeapHandle.valid()
            || drawCommand.textureHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage
        )
            return false;

        const i32 scissorMinX = Max(static_cast<i32>(drawCommand.clipMinX), 0);
        const i32 scissorMinY = Max(static_cast<i32>(drawCommand.clipMinY), 0);
        const i32 scissorMaxX = Min(static_cast<i32>(Ceil(drawCommand.clipMaxX)), snapshot.framebufferWidth);
        const i32 scissorMaxY = Min(static_cast<i32>(Ceil(drawCommand.clipMaxY)), snapshot.framebufferHeight);
        if(scissorMaxX <= scissorMinX || scissorMaxY <= scissorMinY)
            continue;

        Core::ViewportState viewportState;
        viewportState.addViewport(viewport);
        viewportState.addScissorRect(Core::Rect(scissorMinX, scissorMaxX, scissorMinY, scissorMaxY));

        Core::GraphicsState graphicsState;
        graphicsState
            .setPipeline(snapshot.pipeline.get())
            .setFramebuffer(framebuffer)
            .setViewport(viewportState)
            .addVertexBuffer(
                Core::VertexBufferBinding()
                    .setBuffer(snapshot.vertexBuffer.get())
                    .setSlot(NWB_IMGUI_VERTEX_BUFFER_INDEX)
                    .setOffset(0u)
            )
            .setIndexBuffer(
                Core::IndexBufferBinding()
                    .setBuffer(snapshot.indexBuffer.get())
                    .setFormat(indexFormat)
                    .setOffset(0u)
            )
        ;
        commandList.setGraphicsState(graphicsState);
        heap.bindGraphics(commandList, *snapshot.pipeline);

        UiPushConstants pushConstants = snapshot.pushConstants;
        pushConstants.textureSlot = drawCommand.textureHeapHandle.slot();
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

        Core::DrawArguments drawArguments;
        drawArguments
            .setVertexCount(drawCommand.elementCount)
            .setStartIndexLocation(drawCommand.startIndexLocation)
            .setStartVertexLocation(drawCommand.startVertexLocation)
        ;
        commandList.drawIndexed(drawArguments);
    }

    return true;
}

bool UiSystem::recordTaskGraphPresentation(Core::CommandList& commandList, Core::Framebuffer* const framebuffer){
    if(
        !framebuffer
        || !m_taskGraphPresentationClaimed
        || !m_taskGraphDrawUploadsPrepared
        || !m_taskGraphDrawSnapshot.valid
    )
        return false;

    if(!m_frameFinished)
        return false;

    // Vertex/index bytes and the command stream reached immutable graph-owned storage before declaration. This
    // terminal task only consumes the declared VertexBuffer/IndexBuffer and sampled-texture states.
    if(!recordTaskGraphDrawSnapshot(commandList, framebuffer))
        return false;
    commandList.endRenderPass();
    return true;
}

bool UiSystem::recordStandaloneLegacyTaskGraphPresentation(
    Core::CommandList& commandList,
    Core::Framebuffer* const framebuffer,
    ImDrawData* const drawData,
    const u64 frameGeneration
){
    if(
        !framebuffer
        || !drawData
        || !m_taskGraphLegacyPresentationClaimed
        || !m_frameFinished
        || frameGeneration == 0u
        || frameGeneration != m_frameGeneration
    )
        return false;

    // submitStandaloneTaskGraph() records synchronously. The pointer/generation guard turns the opaque callback
    // boundary into a fail-closed record contract if another ImGui frame unexpectedly replaces the live arrays.
    setCurrentContext();
    if(ImGui::GetDrawData() != drawData)
        return false;

    if(!uploadDrawBuffers(commandList, *drawData))
        return false;
    renderDrawData(commandList, framebuffer, *drawData);
    commandList.endRenderPass();
    return true;
}

bool UiSystem::recordTaskGraphUploadCompletion()const{
    return m_taskGraphPresentationClaimed && m_frameFinished;
}

void UiSystem::confirmTaskGraphPresentationSubmission()noexcept{
    // The overlay has explicit dependencies on every texture upload.  Publishing the ImGui status only here keeps
    // a rejected upload visible to the next frame instead of claiming a texture was updated before its packet ran.
    m_textureUploadBatch.complete(true);
    m_frameStarted = false;
    m_frameFinished = false;
    m_taskGraphPresentationHasWork = false;
    m_taskGraphPresentationClaimed = false;
    m_taskGraphLegacyPresentationClaimed = false;
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();
}

void UiSystem::discardStandaloneLegacyTaskGraphPresentation()noexcept{
    // A failed opaque packet may have reached recording after it prepared graph-owned texture blobs. Keep every
    // status pending so the direct availability fallback can retry from the live ImGui frame.
    m_textureUploadBatch.complete(false);
    m_taskGraphLegacyPresentationClaimed = false;
}

bool UiSystem::submitPreparedLegacyTextureUploads(ImDrawData& drawData){
    // prepareFrameResources() has already created/refreshed every requested texture and its descriptor. The direct
    // fallback only submits the retained upload requests after a renderer-owned graph did not consume them.
    if(!__hidden_ui::HasPendingTextureUploads(drawData))
        return true;

    const Core::GpuPhysicalQueueId graphicsQueue =
        m_graphics.getDevice().getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)
    ;
    if(!graphicsQueue.valid()){
        m_textureUploadBatch.complete(false);
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: primary Graphics queue is unavailable for standalone ImGui texture completion"));
        return false;
    }

    Core::QueueSubmissionToken submissionToken;
    const bool submitted = m_graphics.submitStandaloneTaskGraph(
        this,
        [](void* const userData, Core::GpuTaskGraph& graph){
            return static_cast<UiSystem*>(userData)->declareStandaloneTextureUploadGraph(graph);
        },
        submissionToken,
        graphicsQueue
    );
    if(!submitted || !submissionToken.valid()){
        // Compilation/recording failure can occur before the completion task is available to discard this batch.
        // Keeping every request pending makes the next direct fallback frame retry the same immutable graph inputs.
        m_textureUploadBatch.complete(false);
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to submit graph-owned ImGui texture uploads"));
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

    i32 framebufferWidth = 0;
    i32 framebufferHeight = 0;
    __hidden_ui::GetFramebufferExtent(*drawData, framebufferWidth, framebufferHeight);
    if(framebufferWidth <= 0 || framebufferHeight <= 0){
        m_frameStarted = false;
        m_frameFinished = false;
        return;
    }

    // A renderer-owned declaration is not necessarily an accepted submission. If it was abandoned, or this world
    // has no RendererSystem at all, rebuild the immutable overlay as one standalone graph before considering the
    // direct compatibility raster path.
    if(m_taskGraphPresentationPrepared && m_taskGraphPresentationHasWork){
        if(submitStandaloneTaskGraphPresentation(framebuffer))
            return;
        NWB_LOGGER_WARNING(NWB_TEXT("UiSystem: standalone graph presentation failed; retaining direct raster fallback"));
    }

    if(drawData->TotalVtxCount > 0 && drawData->TotalIdxCount > 0){
        // Custom callbacks remain opaque, but the standalone graph records them synchronously against the live
        // ImGui arrays. That preserves callback ABI while moving command-list ownership, texture uploads, and the
        // submit transaction into the graph. Only a rejected opaque graph reaches the direct availability fallback.
        if(submitStandaloneLegacyTaskGraphPresentation(framebuffer))
            return;
        NWB_LOGGER_WARNING(NWB_TEXT("UiSystem: standalone legacy ImGui graph presentation failed; retaining direct raster fallback"));
    }

    if(!submitPreparedLegacyTextureUploads(*drawData))
        return;

    if(!m_pipeline)
        return;

    auto& device = m_graphics.getDevice();
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
    NWB_ASSERT(commandList);

    commandList->open();
    const bool success = uploadDrawBuffers(*commandList, *drawData);
    if(success)
        renderDrawData(*commandList, framebuffer, *drawData);

    commandList->endRenderPass();
    commandList->close();
    if(success){
        Core::CommandList* commandLists[] = { commandList };
        device.executeCommandLists(commandLists, 1);
        m_frameStarted = false;
        m_frameFinished = false;
        m_taskGraphPresentationPrepared = false;
        m_taskGraphPresentationHasWork = false;
        m_taskGraphPresentationClaimed = false;
        m_taskGraphLegacyPresentationClaimed = false;
        m_taskGraphDrawUploadsPrepared = false;
        m_taskGraphPresentationGraphGeneration = 0u;
        m_taskGraphVertexUpload.clear();
        m_taskGraphIndexUpload.clear();
        clearTaskGraphDrawSnapshot();
    }
}

void UiSystem::backBufferResizing(){
    m_renderCommandList.reset();
    m_pipeline.reset();
    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationHasWork = false;
    m_taskGraphPresentationClaimed = false;
    m_taskGraphLegacyPresentationClaimed = false;
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();
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
    auto& device = m_graphics.getDevice();
    if(!m_samplerHeapHandle.valid())
        return;
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return;

    const SIMDVector displayMin = VectorSet(drawData.DisplayPos.x, drawData.DisplayPos.y, 0.0f, 0.0f);
    const SIMDVector displaySize = VectorSet(drawData.DisplaySize.x, drawData.DisplaySize.y, 0.0f, 0.0f);

    UiPushConstants pushConstants;
    StoreFloat(__hidden_ui::BuildUiScaleTranslate(displayMin, displaySize), &pushConstants.scaleTranslate);
    pushConstants.presentationMode = static_cast<u32>(
        m_graphics.isHDR10OutputActive()
            ? Core::SwapChainOutputMode::HDR10
            : Core::SwapChainOutputMode::SDR
    );

    i32 framebufferWidth = 0;
    i32 framebufferHeight = 0;
    __hidden_ui::GetFramebufferExtent(drawData, framebufferWidth, framebufferHeight);
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

            UiTextureResource* textureResource = textureResourceForDraw(drawCommand.GetTexID());
            if(!textureResource || !textureResource->sampledImageHeapHandle.valid())
                continue;

            pushConstants.textureSlot = textureResource->sampledImageHeapHandle.slot();
            pushConstants.samplerSlot = m_samplerHeapHandle.slot();

            Core::ViewportState viewportState;
            viewportState.addViewport(viewport);
            viewportState.addScissorRect(Core::Rect(scissorMinX, scissorMaxX, scissorMinY, scissorMaxY));

            Core::GraphicsState graphicsState;
            graphicsState
                .setPipeline(m_pipeline.get())
                .setFramebuffer(framebuffer)
                .setViewport(viewportState)
                .addVertexBuffer(Core::VertexBufferBinding().setBuffer(m_vertexBuffer.get()).setSlot(NWB_IMGUI_VERTEX_BUFFER_INDEX).setOffset(0u))
                .setIndexBuffer(Core::IndexBufferBinding().setBuffer(m_indexBuffer.get()).setFormat(indexFormat).setOffset(0u))
            ;

            commandList.setGraphicsState(graphicsState);
            // The heap bind must follow setGraphicsState(), which installs the UI pipeline layout and selects the
            // global resource/sampler descriptor-buffer blocks.
            heap.bindGraphics(commandList, *m_pipeline);
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

