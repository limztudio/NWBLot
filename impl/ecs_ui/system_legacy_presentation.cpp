// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "ui_internal.h"

#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Name s_TaskGraphDeclarationArena("impl/ecs_ui/task_graph");

static void DrawCallbackResetRenderState(const ImDrawList*, const ImDrawCmd*){}

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
[[nodiscard]] static Core::GpuTaskResourceUse ReadTextureUse(const Core::GpuGraphResourceId resource){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
}


static void AppendTextureReadUse(
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
    const Core::GpuGraphResourceId texture){
    for(const Core::GpuTaskResourceUse& use : resourceUses){
        if(use.resource == texture)
            return;
    }
    resourceUses.push_back(ReadTextureUse(texture));
}

[[nodiscard]] static bool ValidAcquiredPresentationFrame(const Core::AcquiredPresentationFrame& frame){
    if(!frame.valid())
        return false;

    const Core::FramebufferDesc& framebufferDesc = frame.framebuffer->getDescription();
    return framebufferDesc.colorAttachments.size() == 1u
        && framebufferDesc.colorAttachments[0].texture == frame.backBuffer.texture.get()
        && !framebufferDesc.depthAttachment.valid()
        && !framebufferDesc.shadingRateAttachment.valid()
    ;
}

[[nodiscard]] static bool GraphBindsAcquiredPresentationTexture(
    const Core::GpuTaskGraph::DeclarationReadView& graph,
    const Core::AcquiredPresentationFrame& frame,
    const Core::GpuGraphResourceId backbuffer
){
    return backbuffer.valid() && graph.textureForResource(backbuffer) == frame.backBuffer.texture.get();
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UiSystem::TaskGraphRenderTask{
    struct Payload{
        UiSystem* ui = nullptr;
        Core::AcquiredPresentationFrame frame;
        Core::GpuGraphResourceId backbuffer;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        return payload.ui && payload.ui->recordTaskGraphPresentation(
            commandList,
            payload.frame,
            payload.backbuffer,
            context
        );
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.ui && token.valid())
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
        if(payload.ui && token.valid())
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


// Opaque callbacks cannot enter the immutable packet; record them synchronously.
struct UiSystem::StandaloneLegacyPresentationTask{
    struct Payload{
        UiSystem* ui = nullptr;
        Core::AcquiredPresentationFrame frame;
        Core::GpuGraphResourceId backbuffer;
        ImDrawData* drawData = nullptr;
        u64 frameGeneration = 0u;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        return payload.ui && payload.ui->recordStandaloneLegacyTaskGraphPresentation(
            commandList,
            payload.frame,
            payload.backbuffer,
            payload.drawData,
            payload.frameGeneration,
            context
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
    Core::GraphicsRuntime& graphics,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSystem::submitStandaloneLegacyTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame){
    if(
        !__hidden_ui::ValidAcquiredPresentationFrame(frame)
        || !m_frameFinished
        || m_taskGraphLegacyPresentationClaimed
    )
        return false;

    setCurrentContext();
    ImDrawData* const drawData = ImGui::GetDrawData();
    if(!drawData)
        return false;

    const Core::GpuPhysicalQueueId graphicsQueue =
        m_graphics.getDevice().getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)
    ;
    if(!graphicsQueue.valid()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("UiSystem: primary Graphics queue is unavailable for synchronous ImGui presentation; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return false;
    }

    struct StandaloneLegacyPresentationContext{
        UiSystem* ui = nullptr;
        Core::AcquiredPresentationFrame frame;
        ImDrawData* drawData = nullptr;
        u64 frameGeneration = 0u;
    };
    StandaloneLegacyPresentationContext context{
        .ui = this,
        .frame = frame,
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
                context->frame,
                context->drawData,
                context->frameGeneration
            );
        },
        submissionToken,
        graphicsQueue
    );
    if(submitted && submissionToken.valid())
        return true;

    if(!m_frameFinished){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("UiSystem: standalone legacy presentation failed after its terminal packet was accepted; requesting recreation"));
        m_graphics.requestDeviceRecreation();
        return false;
    }

    // Rejected graphs leave texture requests pending; caller decides retry versus terminal.
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
    for(const Core::GpuGraphResourceId texture : uploadedTextures)
        __hidden_ui::AppendTextureReadUse(resourceUses, texture);

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
        // Next retained frame observes the compiler-owned ShaderResource handoff.
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

bool UiSystem::recordTaskGraphDrawSnapshot(
    Core::CommandList& commandList,
    const Core::AcquiredPresentationFrame& frame,
    const Core::GpuGraphResourceId backbuffer,
    const Core::GpuTaskRecordContext& context
){
    const TaskGraphDrawSnapshot& snapshot = m_taskGraphDrawSnapshot;
    if(
        !__hidden_ui::ValidAcquiredPresentationFrame(frame)
        || !__hidden_ui::GraphBindsAcquiredPresentationTexture(context.declarations, frame, backbuffer)
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

    Core::Framebuffer* const framebuffer = frame.framebuffer.get();
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

bool UiSystem::recordTaskGraphPresentation(
    Core::CommandList& commandList,
    const Core::AcquiredPresentationFrame& frame,
    const Core::GpuGraphResourceId backbuffer,
    const Core::GpuTaskRecordContext& context
){
    if(
        !m_taskGraphPresentationClaimed
        || !m_taskGraphDrawUploadsPrepared
        || !m_taskGraphDrawSnapshot.valid
    )
        return false;

    if(!m_frameFinished)
        return false;

    // Bytes reached immutable storage before declaration; terminal consumes declared states.
    if(!recordTaskGraphDrawSnapshot(commandList, frame, backbuffer, context))
        return false;
    commandList.endRenderPass();
    return true;
}

bool UiSystem::recordStandaloneLegacyTaskGraphPresentation(
    Core::CommandList& commandList,
    const Core::AcquiredPresentationFrame& frame,
    const Core::GpuGraphResourceId backbuffer,
    ImDrawData* const drawData,
    const u64 frameGeneration,
    const Core::GpuTaskRecordContext& context
){
    if(
        !__hidden_ui::ValidAcquiredPresentationFrame(frame)
        || !__hidden_ui::GraphBindsAcquiredPresentationTexture(context.declarations, frame, backbuffer)
        || !drawData
        || !m_taskGraphLegacyPresentationClaimed
        || !m_frameFinished
        || frameGeneration == 0u
        || frameGeneration != m_frameGeneration
    )
        return false;

    // Standalone records synchronously; pointer/generation guard keeps the callback fail-closed.
    setCurrentContext();
    if(ImGui::GetDrawData() != drawData)
        return false;

    if(!uploadDrawBuffers(commandList, *drawData))
        return false;
    renderDrawData(commandList, frame.framebuffer.get(), *drawData);
    commandList.endRenderPass();
    return true;
}

bool UiSystem::recordTaskGraphUploadCompletion()const{
    return m_taskGraphPresentationClaimed && m_frameFinished;
}

void UiSystem::confirmTaskGraphPresentationSubmission()noexcept{
    // Overlay depends on every upload; publish status here so rejected uploads stay visible.
    m_textureUploadBatch.complete(true);
    m_frameStarted = false;
    m_frameFinished = false;
    m_taskGraphPresentationRetryPending = false;
    m_taskGraphPresentationHasWork = false;
    m_taskGraphPresentationClaimed = false;
    m_taskGraphLegacyPresentationClaimed = false;
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();
}

void UiSystem::retainTaskGraphPresentationForRetry()noexcept{
    // Rejected terminals never consumed live arrays; release generation state only.
    m_textureUploadBatch.complete(false);
    m_taskGraphPresentationRetryPending = true;
    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationHasWork = false;
    m_taskGraphPresentationClaimed = false;
    m_taskGraphLegacyPresentationClaimed = false;
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    m_taskGraphPresentationFrame = {};
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();
}

void UiSystem::discardStandaloneLegacyTaskGraphPresentation()noexcept{
    // Failed opaque packets may have prepared blobs; keep statuses pending.
    m_textureUploadBatch.complete(false);
    m_taskGraphLegacyPresentationClaimed = false;
}

bool UiSystem::submitPreparedLegacyTextureUploads(ImDrawData& drawData){
    // Textures already created; texture-only graph runs only without raster work.
    if(!__hidden_ui::HasPendingTextureUploads(drawData))
        return true;

    const Core::GpuPhysicalQueueId graphicsQueue =
        m_graphics.getDevice().getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)
    ;
    if(!graphicsQueue.valid()){
        m_textureUploadBatch.complete(false);
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("UiSystem: primary Graphics queue is unavailable for standalone ImGui texture completion; requesting recreation"));
        m_graphics.requestDeviceRecreation();
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
        // Early compile failure may precede the discard task; keep requests pending for rebuild.
        m_textureUploadBatch.complete(false);
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to submit graph-owned ImGui texture uploads"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

