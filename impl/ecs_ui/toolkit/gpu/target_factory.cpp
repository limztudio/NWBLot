// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "target_factory.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_target{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TargetReadyTask{
    struct Payload{
        GpuVersion<GpuTargetVersion> target;
        Core::GpuGraphResourceId color;
    };


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::Graphics, true };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context)noexcept{
        return context.declarations.textureForResource(payload.color) == payload.target->m_color.get() && !commands.commandRecordingFailed();
    }
};

[[nodiscard]] static Expected<Core::GpuTaskId> DeclareTargetInitialization(void* const rawTarget, Core::GpuTaskGraph& graph){
    const auto& target = *MakeNotNull(static_cast<GpuVersion<GpuTargetVersion>*>(rawTarget));
    const auto colorResult = graph.importTexture(
        target->m_color,
        Core::GpuGraphResourceDesc().setIdentity(s_GpuTargetInitTextureName).setMarkerLabel(s_GpuTargetInitMarkerLabel)
            .setType(Core::GpuGraphResourceType::Texture).setInitialState(Core::ResourceStates::Unknown)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    if(!colorResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId color = *colorResult;
    const auto clearResult = graph.addClearTextureTask(
        Core::GpuTaskDesc().setIdentity(s_GpuTargetInitClearTaskName).setMarkerLabel(s_GpuTargetInitClearMarkerLabel),
        Core::GpuClearTextureTaskDesc{ .destination = color, .valueType = Core::GpuClearTextureTaskValueType::Float, .floatValue = Core::Color(0.0f, 0.0f, 0.0f, 0.0f), });
    if(!clearResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId clear = *clearResult;
    const Core::GpuTaskResourceUse use{ color, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read };
    const auto readyResult = graph.addTask<TargetReadyTask>(
        Core::GpuTaskDesc().setIdentity(s_GpuTargetInitReadyTaskName).setMarkerLabel(s_GpuTargetInitReadyMarkerLabel).setDependencies({ &clear, 1u }).setResourceUses({ &use, 1u }),
        TargetReadyTask::Payload{ target, color });
    if(!readyResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId ready = *readyResult;
    return ready;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuVersion<GpuTargetVersion>> GpuTargetFactory::create(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, const u32 width, const u32 height)const{
    auto targetResult = MakeGpuVersion<GpuTargetVersion>(arena, graphics);
    if(!targetResult)
        return MakeUnexpected(targetResult.error());
    GpuVersion<GpuTargetVersion> target = Move(*targetResult);
    Core::TextureDesc description;
    description
        .setName(s_GpuLayerTextureName)
        .setWidth(width).setHeight(height)
        .setFormat(Core::Format::RGBA16_FLOAT)
        .setIsRenderTarget(true)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    Core::Device& device = graphics.getDevice();
    auto colorResult = device.createTexture(description);
    if(colorResult)
        target->m_color = Move(*colorResult);
    else
        target->m_color.reset();
    if(!colorResult)
        return MakeUnexpected(Failure{});
    auto framebufferResult = device.createFramebuffer(Core::FramebufferDesc().addColorAttachment(target->m_color.get()));
    if(framebufferResult)
        target->m_framebuffer = Move(*framebufferResult);
    else
        target->m_framebuffer.reset();
    if(!framebufferResult)
        return MakeUnexpected(Failure{});
    const Core::GpuPhysicalQueueId queue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    // A creation descriptor does not transition a fresh native image. Initialize before publishing its bindless view.
    if(!queue.valid())
        return MakeUnexpected(Failure{});
    const auto submission = graphics.standaloneGraph().submitStandaloneTaskGraph(&target, __hidden_ui_gpu_target::DeclareTargetInitialization, queue);
    if(!submission)
        return MakeUnexpected(submission.error());
    target->m_readinessToken = *submission;
    if(!target->m_readinessToken.valid() || !target->m_readinessToken.hasPhysicalQueueIdentity())
        return MakeUnexpected(Failure{});
    if(!GpuSampledTextures::prepareDescriptor(graphics, target->m_color, Core::Format::RGBA16_FLOAT, target->m_sampledImage))
        return MakeUnexpected(Failure{});
    return Move(target);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

