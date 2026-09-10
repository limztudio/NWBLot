// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"
#include "texture_submission.h"

#include <core/alloc/general.h>
#include <core/assets/global.h>
#include <core/ecs/system.h>
#include <core/graphics/api.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/task/gpu/presentation_contributor.h>
#include <core/input/module.h>

#include <imgui.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskRecordContext;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public Core::IInputEventHandler
    , public Core::IGpuTaskGraphPresentationContributor
{
public:
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;

public:
    UiSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::GraphicsRuntime& graphics,
        Core::InputDispatcher& input,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~UiSystem()override;

public:
    [[nodiscard]] virtual Core::CpuTaskOptions taskOptions()const override{
        return { .cost = Core::CpuTaskCost::Light, .target = Core::CpuTaskTarget::MainThread };
    }

    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void backBufferResizing()override;

    // Optional contributor turning the finished ImGui draw list into terminal graph work.
    virtual bool prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame)override;
    [[nodiscard]] virtual bool hasTaskGraphPresentationWork()const override;
    [[nodiscard]] virtual Core::GpuTaskId declareTaskGraphPresentation(
        Core::GpuTaskGraph& graph,
        const Core::AcquiredPresentationFrame& frame,
        Core::GpuGraphResourceId backbuffer,
        Core::GpuTaskId previousTask
    )override;

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
    struct TaskGraphRenderTask;
    struct TaskGraphUploadCompletionTask;
    struct StandaloneTextureUploadCompletionTask;
    struct StandaloneLegacyPresentationTask;

    struct UiTextureResource{
        Core::TextureHandle texture;
        // One sampled-image heap entry per dynamic texture; retire before resource leaves.
        Core::GpuDescriptorHandle sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid();
        // New textures start at Unknown; only an accepted upload publishes ShaderResource.
        bool initialUploadAccepted = false;
        // One import per graph generation; generation check rejects IDs across rebuilds.
        Core::GpuGraphResourceId taskGraphResource;
        u64 taskGraphGeneration = 0u;
        u32 width = 0;
        u32 height = 0;
    };
    using UiTextureResourcePtr = Core::GlobalUniquePtr<UiTextureResource>;
    using UiTextureResourceVector = Vector<UiTextureResourcePtr, Core::Alloc::GlobalArena>;
    using UiTextureUploadVector = Vector<u8, Core::Alloc::GlobalArena>;

    struct UiPushConstants{
        Float4 scaleTranslate = Float4(0.0f, 0.0f, 0.0f, 0.0f);
        u32 textureSlot = 0u;
        u32 samplerSlot = 0u;
        u32 presentationMode = static_cast<u32>(Core::SwapChainOutputMode::SDR);
        u32 padding = 0u;
    };
    static_assert(sizeof(UiPushConstants) == sizeof(f32) * 4u + sizeof(u32) * 4u, "Ui push constants must match the ImGui shader block");

    // Declarations must not re-read transient ImGui arrays; draws capture texture and offsets.
    struct TaskGraphDrawCommand{
        Core::TextureHandle texture;
        bool textureInitialUploadAccepted = false;
        Core::GpuDescriptorHandle textureHeapHandle = Core::GpuDescriptorHandle::invalid();
        f32 clipMinX = 0.0f;
        f32 clipMinY = 0.0f;
        f32 clipMaxX = 0.0f;
        f32 clipMaxY = 0.0f;
        u32 elementCount = 0u;
        u32 startIndexLocation = 0u;
        u32 startVertexLocation = 0u;
    };
    using TaskGraphDrawCommandVector = Vector<TaskGraphDrawCommand, Core::Alloc::GlobalArena>;

    struct TaskGraphDrawSnapshot{
        UiPushConstants pushConstants;
        Core::BufferHandle vertexBuffer;
        Core::BufferHandle indexBuffer;
        Core::GraphicsPipelineHandle pipeline;
        Core::GpuDescriptorHandle samplerHeapHandle = Core::GpuDescriptorHandle::invalid();
        i32 framebufferWidth = 0;
        i32 framebufferHeight = 0;
        bool valid = false;
    };
    static_assert(sizeof(TaskGraphDrawSnapshot) == 96u, "Task graph draw snapshots should stay compact");

private:
    void setCurrentContext()const;
    void beginFrame(f32 delta);
    void finishFrame();
    [[nodiscard]] bool prepareFrameResources(const Core::AcquiredPresentationFrame& frame, bool graphOwnsUploads);
    [[nodiscard]] bool submitStandaloneTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame);
    [[nodiscard]] bool submitStandaloneLegacyTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame);
    [[nodiscard]] Core::GpuTaskId declareStandaloneLegacyTaskGraphPresentation(
        Core::GpuTaskGraph& graph,
        const Core::AcquiredPresentationFrame& frame,
        ImDrawData* drawData,
        u64 frameGeneration
    );
    [[nodiscard]] bool recordTaskGraphPresentation(
        Core::CommandList& commandList,
        const Core::AcquiredPresentationFrame& frame,
        Core::GpuGraphResourceId backbuffer,
        const Core::GpuTaskRecordContext& context
    );
    [[nodiscard]] bool recordStandaloneLegacyTaskGraphPresentation(
        Core::CommandList& commandList,
        const Core::AcquiredPresentationFrame& frame,
        Core::GpuGraphResourceId backbuffer,
        ImDrawData* drawData,
        u64 frameGeneration,
        const Core::GpuTaskRecordContext& context
    );
    [[nodiscard]] bool recordTaskGraphUploadCompletion()const;
    void confirmTaskGraphPresentationSubmission()noexcept;
    void retainTaskGraphPresentationForRetry()noexcept;
    void discardStandaloneLegacyTaskGraphPresentation()noexcept;
    void clearTaskGraphDrawSnapshot()noexcept;
    [[nodiscard]] bool ensureRenderResources(Core::Framebuffer* framebuffer);
    [[nodiscard]] bool ensureShadersLoaded();
    [[nodiscard]] bool ensureInputLayout();
    [[nodiscard]] bool ensureBuffers(usize vertexCount, usize indexCount);
    [[nodiscard]] bool drawBuffersReady(usize vertexCount, usize indexCount)const;
    [[nodiscard]] bool prepareTaskGraphDrawUploads(ImDrawData& drawData);
    [[nodiscard]] bool recordTaskGraphDrawSnapshot(
        Core::CommandList& commandList,
        const Core::AcquiredPresentationFrame& frame,
        Core::GpuGraphResourceId backbuffer,
        const Core::GpuTaskRecordContext& context
    );
    [[nodiscard]] bool declareTaskGraphDrawUploads(
        Core::GpuTaskGraph& graph,
        const Core::GpuGraphResourceId& vertexBuffer,
        const Core::GpuGraphResourceId& indexBuffer,
        Core::GpuTaskId previousTask,
        Vector<Core::GpuTaskId, Core::Alloc::ScratchArena>& outTasks
    );
    [[nodiscard]] bool submitPreparedLegacyTextureUploads(ImDrawData& drawData);
    [[nodiscard]] bool prepareTextureRequests(ImDrawData& drawData);
    [[nodiscard]] bool createOrRefreshTexture(ImTextureData& textureData);
    [[nodiscard]] Core::GpuGraphResourceId importTaskGraphTexture(
        Core::GpuTaskGraph& graph,
        UiTextureResource& resource
    );
    [[nodiscard]] bool declareTaskGraphTextureUploads(
        Core::GpuTaskGraph& graph,
        ImDrawData& drawData,
        Core::GpuTaskId previousTask,
        Vector<Core::GpuTaskId, Core::Alloc::ScratchArena>& outTasks,
        Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& outResources
    );
    [[nodiscard]] Core::GpuTaskId declareStandaloneTextureUploadGraph(Core::GpuTaskGraph& graph);
    void destroyTexture(ImTextureData& textureData);
    [[nodiscard]] UiTextureResource* textureResourceFromId(ImTextureID textureId)const;
    [[nodiscard]] UiTextureResource* fallbackTextureResource()const{
        return m_textures.empty() ? nullptr : m_textures.front().get();
    }
    [[nodiscard]] UiTextureResource* textureResourceForDraw(ImTextureID textureId)const;
    [[nodiscard]] bool ensureSamplerHeapHandle();
    [[nodiscard]] bool registerTextureHeapHandle(UiTextureResource& resource);
    void releaseTextureHeapHandle(UiTextureResource& resource);
    void releaseDescriptorHeapResources();
    [[nodiscard]] bool uploadDrawBuffers(Core::CommandList& commandList, ImDrawData& drawData);
    void renderDrawData(Core::CommandList& commandList, Core::Framebuffer* framebuffer, ImDrawData& drawData);

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::InputDispatcher& m_input;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

    ImGuiContext* m_imguiContext = nullptr;
    Core::BindingLayoutHandle m_bindingLayout;
    Core::SamplerHandle m_sampler;
    Core::GpuDescriptorHandle m_samplerHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::ShaderHandle m_vertexShader;
    Core::ShaderHandle m_pixelShader;
    Core::InputLayoutHandle m_inputLayout;
    Core::GraphicsPipelineHandle m_pipeline;
    Core::BufferHandle m_vertexBuffer;
    Core::BufferHandle m_indexBuffer;
    UiTextureResourceVector m_textures;
    UiTextureUploadBatch m_textureUploadBatch;
    UiTextureUploadVector m_textureUploadScratch;
    // Declaration snapshots transient upload bytes and draw commands; graph retains inputs.
    UiTextureUploadVector m_taskGraphVertexUpload;
    UiTextureUploadVector m_taskGraphIndexUpload;
    TaskGraphDrawCommandVector m_taskGraphDrawCommands;
    TaskGraphDrawSnapshot m_taskGraphDrawSnapshot;
    Core::AcquiredPresentationFrame m_taskGraphPresentationFrame;
    usize m_vertexBufferCapacity = 0;
    usize m_indexBufferCapacity = 0;
    u64 m_taskGraphPresentationGraphGeneration = 0u;
    u64 m_frameGeneration = 0u;
    f32 m_deltaSeconds = 0.0f;
    bool m_inputRegistered = false;
    bool m_frameStarted = false;
    bool m_frameFinished = false;
    // Rejected graphs retain live arrays until a later frame rebuilds their snapshot.
    bool m_taskGraphPresentationRetryPending = false;
    // These frame-local flags keep one graph-generation claim for the draw list and retained uploads.
    bool m_taskGraphPresentationPrepared = false;
    bool m_taskGraphPresentationHasWork = false;
    bool m_taskGraphPresentationClaimed = false;
    // Opaque callback route records live ImGui synchronously; keep it separate.
    bool m_taskGraphLegacyPresentationClaimed = false;
    bool m_taskGraphDrawUploadsPrepared = false;
    bool m_wantsKeyboardCapture = false;
    bool m_wantsMouseCapture = false;
    bool m_wantsTextInput = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

