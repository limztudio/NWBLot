// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_integration_probe.h"
#include "refraction_tile_probe.h"
#include "avboit_depth_warp_oracle.h"

#include <loader/project_entry.h>

#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_shader/loader.h>
#include <impl/ecs_render/avboit/avboit.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_depth_warp_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Tests::Smoke;

inline constexpr u32 s_MaxVirtualCount = AvboitDepthWarpOracle::s_MaxVirtualCount;
inline constexpr u32 s_CoverageWords = AvboitDepthWarpOracle::s_CoverageWords;
inline constexpr u32 s_GuardWords = 4u;
inline constexpr u32 s_WarpWords = s_MaxVirtualCount + s_GuardWords;
inline constexpr u32 s_ControlWords = NWB_AVBOIT_CONTROL_WORD_COUNT + s_GuardWords;
inline constexpr u32 s_ResultWords = s_WarpWords + s_ControlWords;
inline constexpr u32 s_BatchSize = 64u;
inline constexpr u32 s_Sentinel = 0xa5f07c39u;
inline constexpr u32 s_BoundaryCaseCount = AvboitDepthWarpOracle::s_BoundaryCaseCount;
inline constexpr u32 s_ExhaustiveCaseCount = AvboitDepthWarpOracle::s_ExhaustiveCaseCount;
inline constexpr u32 s_RandomCaseCount = AvboitDepthWarpOracle::s_RandomCaseCount;
inline constexpr u32 s_TotalCaseCount = AvboitDepthWarpOracle::s_TotalCaseCount;
inline constexpr Name s_ProbeName("AvboitDepthWarpProbe");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ProbeCase = AvboitDepthWarpOracle::Case;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ProbeBatchTask{
    struct Payload{
        Core::BufferHandle coverage;
        Core::BufferHandle warp;
        Core::BufferHandle control;
        Core::BufferHandle slots;
        Core::BufferHandle readback;
        Core::ComputePipelineHandle pipeline;
        Core::GpuDescriptorHeap& heap;
        // The project keeps this case storage immutable until the accepted batch token completes.
        const ProbeCase* cases = nullptr;
        u32 firstCase = 0u;
        u32 caseCount = 0u;
        u32 slotsIndex = 0u;
    };


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ .requiredCapabilities = Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, .requiresPrimaryGraphicsQueue = true, };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        for(u32 index = 0u; index < payload.caseCount; ++index){
            const ProbeCase& probe = payload.cases[index];
            commandList.setBufferState(payload.coverage.get(), Core::ResourceStates::CopyDest);
            commandList.setBufferState(payload.warp.get(), Core::ResourceStates::CopyDest);
            commandList.setBufferState(payload.control.get(), Core::ResourceStates::CopyDest);
            commandList.commitBarriers();
            if(!commandList.tryWriteBuffer(*payload.coverage, probe.coverage.data(), sizeof(probe.coverage))){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: coverage upload failed at case {}"), payload.firstCase + index);
                return false;
            }
            commandList.clearBufferUInt(*payload.warp, s_Sentinel);
            commandList.clearBufferUInt(*payload.control, s_Sentinel);
            commandList.setBufferState(payload.coverage.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(payload.warp.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(payload.control.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            Core::ComputeState state;
            state.setPipeline(payload.pipeline.get());
            commandList.setComputeState(state);
            payload.heap.bindCompute(commandList, *payload.pipeline);
            const auto push = AvboitDepthWarpOracle::BuildPush(probe, payload.firstCase + index, payload.slotsIndex);
            commandList.setPushConstants(&push, sizeof(push));
            commandList.dispatch(NWB_AVBOIT_DEPTH_WARP_DISPATCH_GROUP_COUNT_X, 1u, 1u);

            commandList.setBufferState(payload.warp.get(), Core::ResourceStates::CopySource);
            commandList.setBufferState(payload.control.get(), Core::ResourceStates::CopySource);
            commandList.commitBarriers();
            const u64 offset = static_cast<u64>(index) * s_ResultWords * sizeof(u32);
            commandList.copyBuffer(*payload.readback, offset, *payload.warp, 0u, s_WarpWords * sizeof(u32));
            commandList.copyBuffer(*payload.readback, offset + s_WarpWords * sizeof(u32), *payload.control, 0u, s_ControlWords * sizeof(u32));
            if(commandList.commandRecordingFailed()){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: command recording failed at case {}"), payload.firstCase + index);
                return false;
            }
        }
        commandList.setBufferState(payload.coverage.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(payload.warp.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(payload.control.get(), Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        return !commandList.commandRecordingFailed();
    }
};

[[nodiscard]] static Expected<Core::GpuTaskId> DeclareBatch(void* userData, Core::GpuTaskGraph& graph){
    const auto& payload = *MakeNotNull(static_cast<const ProbeBatchTask::Payload*>(userData));
    const auto import = [&graph](const Core::BufferHandle& buffer, const AStringView label, const Core::ResourceStates::Mask state){
        Core::GpuGraphResourceDesc desc;
        desc.setIdentity(buffer->getCreationDescription().debugName).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer).setInitialState(state).setExternalFinalState(state);
        return graph.importBuffer(buffer, desc);
    };
    const Array<Expected<Core::GpuGraphResourceId>, 5u> imported{
        import(payload.coverage, "Depth warp probe coverage", Core::ResourceStates::CopyDest),
        import(payload.warp, "Depth warp probe LUT", Core::ResourceStates::CopyDest),
        import(payload.control, "Depth warp probe control", Core::ResourceStates::CopyDest),
        import(payload.slots, "Depth warp probe heap slots", Core::ResourceStates::ConstantBuffer),
        import(payload.readback, "Depth warp probe readback", Core::ResourceStates::CopyDest),
    };
    for(u32 index = 0u; index < imported.size(); ++index){
        if(!imported[index]){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph buffer import failed at index {}"), index);
            return MakeUnexpected(Failure{});
        }
    }
    // One task owns the transfer/compute transitions within its batch and restores the declared external states.
    const Array<Core::GpuTaskResourceUse, 5u> uses{
        Core::GpuTaskResourceUse{ .resource = *imported[0u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite, },
        Core::GpuTaskResourceUse{ .resource = *imported[1u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite, },
        Core::GpuTaskResourceUse{ .resource = *imported[2u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::ReadWrite, },
        Core::GpuTaskResourceUse{ .resource = *imported[3u], .range = {}, .requiredState = Core::ResourceStates::ConstantBuffer, .access = Core::GpuTaskResourceAccess::Read, },
        Core::GpuTaskResourceUse{ .resource = *imported[4u], .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, },
    };
    for(u32 index = 0u; index < uses.size(); ++index){
        if(!uses[index].resource.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph buffer import failed at index {}"), index);
            return MakeUnexpected(Failure{});
        }
    }
    Core::GpuTaskDesc desc;
    desc.setIdentity(s_ProbeName).setMarkerLabel("AVBOIT depth warp boundary probe").setResourceUses(uses);
    const auto taskResult = graph.addTask<ProbeBatchTask>(desc, ProbeBatchTask::Payload(payload));
    if(!taskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph task declaration failed at case {}"), payload.firstCase);
        return MakeUnexpected(Failure{});
    }
    const auto task = *taskResult;
    if(!task.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph task declaration failed at case {}"), payload.firstCase);
        return MakeUnexpected(Failure{});
    }
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DepthWarpUnfocusedPass final : public Core::IRenderPass{
public:
    explicit DepthWarpUnfocusedPass(Core::GraphicsRuntime& graphics)noexcept : IRenderPass(graphics){}


public:
    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DepthWarpProbeProject final : public IProjectEntryCallbacks{
private:
    [[nodiscard]] static Expected<Core::BufferHandle> CreateBuffer(const Name& name, const u32 words, const b8 readback, Core::GraphicsRuntime& graphics){
        Core::BufferDesc desc;
        desc.setDebugName(name).setByteSize(words * sizeof(u32)).setStructStride(sizeof(u32)).setCanHaveUAVs(!readback).setCpuAccess(readback ? Core::CpuAccessMode::Read : Core::CpuAccessMode::None).enableAutomaticStateTracking(Core::ResourceStates::CopyDest);
        return graphics.createBuffer(desc);
    }


public:
    explicit DepthWarpProbeProject(ProjectRuntimeContext& context)noexcept
        : m_sceneSubsystems(context.objectArena)
        , m_cases(context.objectArena)
        , m_context(context)
        , m_unfocusedPass(context.graphics)
    {}


public:
    virtual b8 onStartup()override{
        AvboitDepthWarpOracle::BuildCases(m_cases);
        if(m_cases.size() != s_TotalCaseCount){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: case manifest count mismatch {}"), m_cases.size());
            return false;
        }
        auto& graphics = m_context.graphics;
        auto& device = graphics.getDevice();
        auto& heap = device.getDescriptorHeap();
        if(!heap.isInitialized()){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: bindless heap is unavailable"));
            return false;
        }
        if(!m_sceneSubsystems.loadProjectStartScene(m_context.assetManager))
            return false;
        const Impl::TransparentSubsystem* const transparentSubsystem = m_sceneSubsystems.findSubsystem<Impl::TransparentSubsystem>();
        const auto depthWarpShaderRef = transparentSubsystem ? transparentSubsystem->avboitDepthWarpCs : Core::Assets::AssetRef<Impl::ComputeShader>{};
        if(!depthWarpShaderRef.valid())
            return false;
        if(!Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(
            depthWarpShaderRef.name(),
            Core::ShaderArchive::s_DefaultVariant,
            s_ProbeName,
            NWB_TEXT("AvboitDepthWarpProbe"),
            graphics,
            m_context.assetManager,
            m_context.shaderPathResolver,
            nullptr,
            m_shader
        ))
            return false;
        Core::BindingLayoutDesc layoutDesc(m_context.objectArena);
        layoutDesc.setVisibility(Core::ShaderType::Compute).addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE));
        auto pushLayoutResult = device.createBindingLayout(layoutDesc);
        if(pushLayoutResult)
            m_pushLayout = Move(*pushLayoutResult);
        else
            m_pushLayout.reset();
        if(!pushLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: push layout creation failed"));
            return false;
        }
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc.setComputeShader(m_shader).addBindingLayout(m_pushLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        auto pipelineResult = device.createComputePipeline(pipelineDesc);
        if(pipelineResult)
            m_pipeline = Move(*pipelineResult);
        else
            m_pipeline.reset();
        auto coverageResult = CreateBuffer(Name("DepthWarpProbeCoverage"), s_CoverageWords, false, graphics);
        if(coverageResult)
            m_coverage = Move(*coverageResult);
        else
            m_coverage.reset();
        auto warpResult = CreateBuffer(Name("DepthWarpProbeWarp"), s_WarpWords, false, graphics);
        if(warpResult)
            m_warp = Move(*warpResult);
        else
            m_warp.reset();
        auto controlResult = CreateBuffer(Name("DepthWarpProbeControl"), s_ControlWords, false, graphics);
        if(controlResult)
            m_control = Move(*controlResult);
        else
            m_control.reset();
        auto readbackResult = CreateBuffer(Name("DepthWarpProbeReadback"), s_BatchSize * s_ResultWords, true, graphics);
        if(readbackResult)
            m_readback = Move(*readbackResult);
        else
            m_readback.reset();
        if(!pipelineResult || !coverageResult || !warpResult || !controlResult || !readbackResult){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: pipeline or buffer creation failed"));
            return false;
        }
        const Array<Core::BufferHandle, 3u> buffers{ m_coverage, m_warp, m_control };
        for(u32 index = 0u; index < buffers.size(); ++index){
            const Expected<Core::GpuDescriptorHandle> descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
            if(descriptor)
                m_descriptors[index] = *descriptor;
            if(!descriptor || !heap.write(m_descriptors[index], Core::DescriptorWriteItem::StructuredBufferUav(0u, buffers[index].get()))){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: storage descriptor setup failed at index {}"), index);
                return false;
            }
        }
        Impl::DeferredBindlessResourceSlots slots;
        slots.avboitCoverage = m_descriptors[0].slot();
        slots.avboitDepthWarp = m_descriptors[1].slot();
        slots.avboitControl = m_descriptors[2].slot();
        Core::GraphicsStandaloneService::BufferSetupDesc slotsDesc;
        slotsDesc.bufferDesc.setDebugName(Name("DepthWarpProbeSlots")).setByteSize(sizeof(slots)).setIsConstantBuffer(true).enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer);
        slotsDesc.data = &slots;
        slotsDesc.dataSize = sizeof(slots);
        slotsDesc.queue = Core::CommandQueue::Graphics;
        auto slotsResult = graphics.standaloneGraph().setupBuffer(slotsDesc);
        if(slotsResult)
            m_slots = Move(*slotsResult);
        else
            m_slots.reset();
        const Expected<Core::GpuDescriptorHandle> slotsDescriptor = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
        if(slotsDescriptor)
            m_descriptors[3] = *slotsDescriptor;
        if(!slotsResult || !slotsDescriptor || !heap.write(m_descriptors[3], Core::DescriptorWriteItem::ConstantBuffer(0u, m_slots.get()))){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: slot payload setup failed"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: manifest boundary={} exhaustive={} randomized={} total={} seed=0x6b47c2d1"), s_BoundaryCaseCount, s_ExhaustiveCaseCount, s_RandomCaseCount, s_TotalCaseCount);
        m_context.graphics.addRenderPassToBack(m_unfocusedPass);
        m_unfocusedPassRegistered = true;
        return true;
    }

    virtual b8 onUpdate(const f32)override{
        if(m_completed)
            return true;
        ProbeBatchTask::Payload payload{ m_coverage, m_warp, m_control, m_slots, m_readback, m_pipeline, m_context.graphics.getDevice().getDescriptorHeap(), nullptr, 0u, 0u, m_descriptors[3].slot() };
        for(u32 firstCase = 0u; firstCase < m_cases.size(); firstCase += s_BatchSize){
            payload.cases = m_cases.data() + firstCase;
            payload.firstCase = firstCase;
            payload.caseCount = Min(s_BatchSize, static_cast<u32>(m_cases.size()) - firstCase);
            const auto token = m_context.graphics.standaloneGraph().submitStandaloneTaskGraph(&payload, DeclareBatch);
            if(!token){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: batch submission failed at case {}"), firstCase);
                return false;
            }
            if(!m_context.graphics.getDevice().waitForSubmissionToken(*token)){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: batch completion wait failed at case {}"), firstCase);
                return false;
            }
            if(!compareBatch(payload))
                return false;
        }
        if(!RunAvboitIntegrationProbe(m_sceneSubsystems, m_context))
            return false;
        if(!RunRefractionTileProbe(m_sceneSubsystems, m_context))
            return false;
        m_completed = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: passed {} cases with full LUT/control comparison and sentinel tails"), s_TotalCaseCount);
        m_context.requestQuit();
        return true;
    }

    virtual void onShutdown()override{
        if(m_unfocusedPassRegistered){
            m_context.graphics.removeRenderPass(m_unfocusedPass);
            m_unfocusedPassRegistered = false;
        }
        if(!m_context.graphics.waitForIdle())
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: shutdown completion wait failed"));
        auto& heap = m_context.graphics.getDevice().getDescriptorHeap();
        for(auto& descriptor : m_descriptors){
            if(descriptor.valid()){
                heap.free(descriptor);
                descriptor = Core::GpuDescriptorHandle::Invalid();
            }
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: shutdown completed={}"), m_completed ? 1u : 0u);
    }


private:
    [[nodiscard]] b8 compareBatch(const ProbeBatchTask::Payload& payload){
        auto& device = m_context.graphics.getDevice();
        const Expected<void*> mappedResult = device.mapBuffer(*m_readback, Core::CpuAccessMode::Read);
        if(!mappedResult){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: readback mapping failed at case {}"), payload.firstCase);
            return false;
        }
        const auto mapped = MakeNotNull(static_cast<const u32*>(*mappedResult));
        b8 passed = true;
        for(u32 index = 0u; passed && index < payload.caseCount; ++index){
            const ProbeCase& probe = payload.cases[index];
            const u32 ordinal = payload.firstCase + index;
            const auto oracle = AvboitDepthWarpOracle::BuildOracle(probe, AvboitDepthWarpOracle::BuildPush(probe, ordinal, payload.slotsIndex));
            const auto result = MakeNotNull(mapped.get() + index * s_ResultWords);
            for(u32 word = 0u; word < s_ResultWords; ++word){
                const u32 expected = word < probe.virtualCount ? oracle.warp[word] : word >= s_WarpWords && word < s_WarpWords + NWB_AVBOIT_CONTROL_WORD_COUNT ? oracle.control[word - s_WarpWords] : s_Sentinel;
                if(result.get()[word] != expected){
                    NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: mismatch case {} scenario {} virtual {} physical {} word {} expected {} observed {}"), ordinal, probe.scenario, probe.virtualCount, probe.physicalCount, word, expected, result.get()[word]);
                    passed = false;
                    break;
                }
            }
        }
        device.unmapBuffer(*m_readback);
        return passed;
    }


private:
    Impl::ActiveSceneSubsystems m_sceneSubsystems;
    Array<Core::GpuDescriptorHandle, 4u> m_descriptors{};
    Vector<ProbeCase, Core::Alloc::GlobalArena> m_cases;
    Core::ShaderHandle m_shader;
    Core::BindingLayoutHandle m_pushLayout;
    Core::ComputePipelineHandle m_pipeline;
    Core::BufferHandle m_coverage;
    Core::BufferHandle m_warp;
    Core::BufferHandle m_control;
    Core::BufferHandle m_slots;
    Core::BufferHandle m_readback;
    ProjectRuntimeContext& m_context;
    DepthWarpUnfocusedPass m_unfocusedPass;
    b8 m_unfocusedPassRegistered = false;
    b8 m_completed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 320u, 240u };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB AVBOIT Depth Warp Probe");
}

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_avboit_depth_warp_probe::DepthWarpProbeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

