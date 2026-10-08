// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <impl/assets/graphics/avboit/names.h>
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

inline constexpr u32 s_MaxVirtualCount = 257u;
inline constexpr u32 s_CoverageWords = (s_MaxVirtualCount + 31u) / 32u;
inline constexpr u32 s_GuardWords = 4u;
inline constexpr u32 s_WarpWords = s_MaxVirtualCount + s_GuardWords;
inline constexpr u32 s_ControlWords = NWB_AVBOIT_CONTROL_WORD_COUNT + s_GuardWords;
inline constexpr u32 s_ResultWords = s_WarpWords + s_ControlWords;
inline constexpr u32 s_BatchSize = 64u;
inline constexpr u32 s_Sentinel = 0xa5f07c39u;
inline constexpr u32 s_BoundaryCaseCount = 1050u;
inline constexpr u32 s_ExhaustiveCaseCount = 2550u;
inline constexpr u32 s_RandomCaseCount = 512u;
inline constexpr u32 s_TotalCaseCount = s_BoundaryCaseCount + s_ExhaustiveCaseCount + s_RandomCaseCount;
inline constexpr Name s_ProbeName("AvboitDepthWarpProbe");

struct ProbeCase{
    Array<u32, s_CoverageWords> coverage{};
    u32 virtualCount = 1u;
    u32 physicalCount = 1u;
    u32 scenario = 0u;
};

struct ProbeOracle{
    Array<u32, s_MaxVirtualCount> warp{};
    Array<u32, NWB_AVBOIT_CONTROL_WORD_COUNT> control{};
};

[[nodiscard]] static bool Occupied(const ProbeCase& probe, const u32 slice)noexcept{
    return (probe.coverage[slice / 32u] & (1u << (slice % 32u))) != 0u;
}

static void SetOccupied(ProbeCase& probe, const u32 slice)noexcept{
    if(slice < probe.virtualCount)
        probe.coverage[slice / 32u] |= 1u << (slice % 32u);
}

static void PoisonUnusedCoverage(ProbeCase& probe)noexcept{
    // Invalid bits are deliberately occupied; partial last words must not contribute a coarse bin.
    for(u32 slice = probe.virtualCount; slice < s_CoverageWords * 32u; ++slice)
        probe.coverage[slice / 32u] |= 1u << (slice % 32u);
}

[[nodiscard]] static Impl::RendererAvboitPushConstants BuildPush(const ProbeCase& probe, const u32 ordinal, const u32 slots)noexcept{
    Impl::RendererAvboitPushConstants push;
    push.frame[0] = 1000u + ordinal % 997u;
    push.frame[1] = 300u + ordinal % 251u;
    push.frame[2] = (push.frame[0] + 2u) / 3u;
    push.frame[3] = (push.frame[1] + 3u) / 4u;
    push.volume[0] = probe.virtualCount;
    push.volume[1] = probe.physicalCount;
    push.volume[3] = (probe.virtualCount + 31u) / 32u;
    push.heapSlots[0] = slots;
    return push;
}

[[nodiscard]] static ProbeOracle BuildOracle(const ProbeCase& probe, const Impl::RendererAvboitPushConstants& push)noexcept{
    // The reference uses sorted occupied slice positions, independently of the shader's coverage-word algorithm.
    Array<u32, s_MaxVirtualCount> occupiedSlices{};
    u32 occupiedCount = 0u;
    for(u32 slice = 0u; slice < probe.virtualCount; ++slice){
        if(Occupied(probe, slice))
            occupiedSlices[occupiedCount++] = slice;
    }

    Array<u32, s_MaxVirtualCount> distinctBins{};
    u32 binWidth = 1u;
    u32 shift = 0u;
    u32 distinctCount = 0u;
    for(;;){
        distinctCount = 0u;
        for(u32 index = 0u; index < occupiedCount; ++index){
            const u32 bin = occupiedSlices[index] / binWidth;
            if(distinctCount == 0u || distinctBins[distinctCount - 1u] != bin)
                distinctBins[distinctCount++] = bin;
        }
        if(distinctCount <= probe.physicalCount)
            break;
        binWidth *= 2u;
        ++shift;
    }

    ProbeOracle oracle;
    u32 nextBin = 0u;
    u32 precedingRank = 0u;
    for(u32 slice = 0u; slice < probe.virtualCount; ++slice){
        while(nextBin < distinctCount && distinctBins[nextBin] <= slice / binWidth)
            precedingRank = nextBin++;
        oracle.warp[slice] = precedingRank;
    }
    oracle.control = {
        distinctCount == 0u ? 1u : distinctCount,
        shift,
        probe.virtualCount,
        probe.physicalCount,
        push.frame[2], push.frame[3], push.frame[0], push.frame[1],
    };
    return oracle;
}

static void BuildCases(Vector<ProbeCase, Core::Alloc::GlobalArena>& cases){
    constexpr Array<u32, 21u> virtualCounts{ 1u, 2u, 3u, 7u, 8u, 9u, 31u, 32u, 33u, 63u, 64u, 65u, 95u, 96u, 97u, 127u, 128u, 129u, 255u, 256u, 257u };
    constexpr Array<u32, 5u> physicalCounts{ 1u, 2u, 31u, 32u, 64u };
    cases.reserve(s_TotalCaseCount);
    for(const u32 virtualCount : virtualCounts){
        for(const u32 physicalCount : physicalCounts){
            for(u32 pattern = 0u; pattern < 10u; ++pattern){
                ProbeCase probe;
                probe.virtualCount = virtualCount;
                probe.physicalCount = physicalCount;
                probe.scenario = pattern;
                for(u32 slice = 0u; slice < virtualCount; ++slice){
                    const bool occupied = pattern == 1u
                        || (pattern == 2u && slice + 1u == virtualCount)
                        || (pattern == 3u && slice >= virtualCount / 2u && slice < virtualCount / 2u + 3u)
                        || (pattern == 4u && slice % 2u == 1u)
                        || (pattern == 5u && (slice % 32u == 0u || slice % 32u == 31u))
                        || (pattern == 6u && slice < 64u)
                        || (pattern == 7u && slice < 65u)
                        || (pattern == 8u && slice + 64u >= virtualCount)
                        || (pattern == 9u && slice == 0u);
                    if(occupied)
                        SetOccupied(probe, slice);
                }
                PoisonUnusedCoverage(probe);
                cases.push_back(probe);
            }
        }
    }
    for(u32 virtualCount = 1u; virtualCount <= 8u; ++virtualCount){
        for(u32 mask = 0u; mask < (1u << virtualCount); ++mask){
            for(const u32 physicalCount : physicalCounts){
                ProbeCase probe;
                probe.virtualCount = virtualCount;
                probe.physicalCount = physicalCount;
                probe.scenario = 10u;
                probe.coverage[0] = mask;
                PoisonUnusedCoverage(probe);
                cases.push_back(probe);
            }
        }
    }
    u32 random = 0x6b47c2d1u;
    for(u32 index = 0u; index < s_RandomCaseCount; ++index){
        ProbeCase probe;
        probe.virtualCount = 1u + index * 73u % s_MaxVirtualCount;
        probe.physicalCount = physicalCounts[index % physicalCounts.size()];
        probe.scenario = 11u;
        for(u32 slice = 0u; slice < probe.virtualCount; ++slice){
            random ^= random << 13u;
            random ^= random >> 17u;
            random ^= random << 5u;
            if(random % 8u < 1u + index % 7u)
                SetOccupied(probe, slice);
        }
        PoisonUnusedCoverage(probe);
        cases.push_back(probe);
    }
}


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

    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{
        .requiredCapabilities = Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer,
        .requiresPrimaryGraphicsQueue = true,
    };

    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        static_cast<void>(context);
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
            const auto push = BuildPush(probe, payload.firstCase + index, payload.slotsIndex);
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

[[nodiscard]] static Core::GpuTaskId DeclareBatch(void* userData, Core::GpuTaskGraph& graph){
    const auto& payload = *static_cast<const ProbeBatchTask::Payload*>(userData);
    const auto import = [&graph](const Core::BufferHandle& buffer, const AStringView label, const Core::ResourceStates::Mask state){
        Core::GpuGraphResourceDesc desc;
        desc
            .setIdentity(buffer->getCreationDescription().debugName).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(state).setExternalFinalState(state)
        ;
        return graph.importBuffer(buffer, desc);
    };
    // One task owns the transfer/compute transitions within its batch and restores the declared external states.
    const Array<Core::GpuTaskResourceUse, 5u> uses{
        Core::GpuTaskResourceUse{
            .resource = import(payload.coverage, "Depth warp probe coverage", Core::ResourceStates::CopyDest),
            .range = {},
            .requiredState = Core::ResourceStates::CopyDest,
            .access = Core::GpuTaskResourceAccess::ReadWrite,
        },
        Core::GpuTaskResourceUse{
            .resource = import(payload.warp, "Depth warp probe LUT", Core::ResourceStates::CopyDest),
            .range = {},
            .requiredState = Core::ResourceStates::CopyDest,
            .access = Core::GpuTaskResourceAccess::ReadWrite,
        },
        Core::GpuTaskResourceUse{
            .resource = import(payload.control, "Depth warp probe control", Core::ResourceStates::CopyDest),
            .range = {},
            .requiredState = Core::ResourceStates::CopyDest,
            .access = Core::GpuTaskResourceAccess::ReadWrite,
        },
        Core::GpuTaskResourceUse{
            .resource = import(payload.slots, "Depth warp probe heap slots", Core::ResourceStates::ConstantBuffer),
            .range = {},
            .requiredState = Core::ResourceStates::ConstantBuffer,
            .access = Core::GpuTaskResourceAccess::Read,
        },
        Core::GpuTaskResourceUse{
            .resource = import(payload.readback, "Depth warp probe readback", Core::ResourceStates::CopyDest),
            .range = {},
            .requiredState = Core::ResourceStates::CopyDest,
            .access = Core::GpuTaskResourceAccess::Write,
        },
    };
    for(u32 index = 0u; index < uses.size(); ++index){
        if(!uses[index].resource.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph buffer import failed at index {}"), index);
            return {};
        }
    }
    Core::GpuTaskDesc desc;
    desc.setIdentity(s_ProbeName).setMarkerLabel("AVBOIT depth warp boundary probe").setResourceUses(uses.data(), uses.size());
    const auto task = graph.addTask<ProbeBatchTask>(desc, ProbeBatchTask::Payload(payload));
    if(!task.valid())
        NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: graph task declaration failed at case {}"), payload.firstCase);
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DepthWarpProbeProject final : public IProjectEntryCallbacks{
private:
    [[nodiscard]] static Core::BufferHandle CreateBuffer(Core::GraphicsRuntime& graphics, const Name& name, const u32 words, const bool readback){
        Core::BufferDesc desc;
        desc
            .setDebugName(name).setByteSize(words * sizeof(u32)).setStructStride(sizeof(u32))
            .setCanHaveUAVs(!readback).setCpuAccess(readback ? Core::CpuAccessMode::Read : Core::CpuAccessMode::None)
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        return graphics.createBuffer(desc);
    }


public:
    explicit DepthWarpProbeProject(ProjectRuntimeContext& context)noexcept
        : m_context(context)
        , m_cases(context.objectArena)
    {}


public:
    virtual bool onStartup()override{
        BuildCases(m_cases);
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
        if(!Impl::ShaderAssetLoader::Load<Impl::ComputeShader>(
            m_shader,
            Impl::AssetsGraphicsAvboit::s_DepthWarpComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            s_ProbeName,
            graphics,
            m_context.assetManager,
            m_context.shaderPathResolver,
            NWB_TEXT("AvboitDepthWarpProbe")
        ))
            return false;
        Core::BindingLayoutDesc layoutDesc(m_context.objectArena);
        layoutDesc.setVisibility(Core::ShaderType::Compute).addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE));
        m_pushLayout = device.createBindingLayout(layoutDesc);
        if(!m_pushLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: push layout creation failed"));
            return false;
        }
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(m_shader).addBindingLayout(m_pushLayout)
            .addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout())
        ;
        m_pipeline = device.createComputePipeline(pipelineDesc);
        m_coverage = CreateBuffer(graphics, Name("DepthWarpProbeCoverage"), s_CoverageWords, false);
        m_warp = CreateBuffer(graphics, Name("DepthWarpProbeWarp"), s_WarpWords, false);
        m_control = CreateBuffer(graphics, Name("DepthWarpProbeControl"), s_ControlWords, false);
        m_readback = CreateBuffer(graphics, Name("DepthWarpProbeReadback"), s_BatchSize * s_ResultWords, true);
        if(!m_pipeline || !m_coverage || !m_warp || !m_control || !m_readback){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: pipeline or buffer creation failed"));
            return false;
        }
        const Array<Core::BufferHandle, 3u> buffers{ m_coverage, m_warp, m_control };
        for(u32 index = 0u; index < buffers.size(); ++index){
            m_descriptors[index] = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
            if(!m_descriptors[index].valid() || !heap.write(m_descriptors[index], Core::DescriptorWriteItem::StructuredBufferUav(0u, buffers[index].get()))){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: storage descriptor setup failed at index {}"), index);
                return false;
            }
        }
        Impl::DeferredBindlessResourceSlots slots;
        slots.avboitCoverage = m_descriptors[0].slot();
        slots.avboitDepthWarp = m_descriptors[1].slot();
        slots.avboitControl = m_descriptors[2].slot();
        Core::GraphicsRuntime::BufferSetupDesc slotsDesc;
        slotsDesc.bufferDesc
            .setDebugName(Name("DepthWarpProbeSlots")).setByteSize(sizeof(slots)).setIsConstantBuffer(true)
            .enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer)
        ;
        slotsDesc.data = &slots;
        slotsDesc.dataSize = sizeof(slots);
        slotsDesc.queue = Core::CommandQueue::Graphics;
        m_slots = graphics.setupBuffer(slotsDesc);
        m_descriptors[3] = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
        if(!m_slots || !m_descriptors[3].valid() || !heap.write(m_descriptors[3], Core::DescriptorWriteItem::ConstantBuffer(0u, m_slots.get()))){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: slot payload setup failed"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: manifest boundary={} exhaustive={} randomized={} total={} seed=0x6b47c2d1")
            , s_BoundaryCaseCount, s_ExhaustiveCaseCount, s_RandomCaseCount, s_TotalCaseCount
        );
        return true;
    }

    virtual bool onUpdate(const f32 delta)override{
        static_cast<void>(delta);
        if(m_completed)
            return true;
        ProbeBatchTask::Payload payload{ m_coverage, m_warp, m_control, m_slots, m_readback, m_pipeline,
            m_context.graphics.getDevice().getDescriptorHeap(), nullptr, 0u, 0u, m_descriptors[3].slot() };
        for(u32 firstCase = 0u; firstCase < m_cases.size(); firstCase += s_BatchSize){
            payload.cases = m_cases.data() + firstCase;
            payload.firstCase = firstCase;
            payload.caseCount = Min(s_BatchSize, static_cast<u32>(m_cases.size()) - firstCase);
            Core::QueueSubmissionToken token;
            if(!m_context.graphics.submitStandaloneTaskGraph(&payload, DeclareBatch, token)){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: batch submission failed at case {}"), firstCase);
                return false;
            }
            if(!m_context.graphics.getDevice().waitForSubmissionToken(token)){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: batch completion wait failed at case {}"), firstCase);
                return false;
            }
            if(!compareBatch(payload))
                return false;
        }
        m_completed = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitDepthWarpProbe: passed {} cases with full LUT/control comparison and sentinel tails"), s_TotalCaseCount);
        m_context.requestQuit();
        return true;
    }

    virtual void onShutdown()override{
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
    [[nodiscard]] bool compareBatch(const ProbeBatchTask::Payload& payload){
        auto& device = m_context.graphics.getDevice();
        const auto* mapped = static_cast<const u32*>(device.mapBuffer(*m_readback, Core::CpuAccessMode::Read));
        if(!mapped){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: readback mapping failed at case {}"), payload.firstCase);
            return false;
        }
        bool passed = true;
        for(u32 index = 0u; passed && index < payload.caseCount; ++index){
            const ProbeCase& probe = payload.cases[index];
            const u32 ordinal = payload.firstCase + index;
            const auto oracle = BuildOracle(probe, BuildPush(probe, ordinal, payload.slotsIndex));
            const u32* result = mapped + index * s_ResultWords;
            for(u32 word = 0u; word < s_ResultWords; ++word){
                const u32 expected = word < probe.virtualCount ? oracle.warp[word]
                    : word >= s_WarpWords && word < s_WarpWords + NWB_AVBOIT_CONTROL_WORD_COUNT ? oracle.control[word - s_WarpWords]
                    : s_Sentinel;
                if(result[word] != expected){
                    NWB_LOGGER_ERROR(NWB_TEXT("AvboitDepthWarpProbe: mismatch case {} scenario {} virtual {} physical {} word {} expected {} observed {}")
                        , ordinal, probe.scenario, probe.virtualCount, probe.physicalCount, word, expected, result[word]
                    );
                    passed = false;
                    break;
                }
            }
        }
        device.unmapBuffer(*m_readback);
        return passed;
    }


private:
    ProjectRuntimeContext& m_context;
    Vector<ProbeCase, Core::Alloc::GlobalArena> m_cases;
    Core::ShaderHandle m_shader;
    Core::BindingLayoutHandle m_pushLayout;
    Core::ComputePipelineHandle m_pipeline;
    Core::BufferHandle m_coverage;
    Core::BufferHandle m_warp;
    Core::BufferHandle m_control;
    Core::BufferHandle m_slots;
    Core::BufferHandle m_readback;
    Array<Core::GpuDescriptorHandle, 4u> m_descriptors{};
    bool m_completed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 320u, 240u };
}

TStringView NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB AVBOIT Depth Warp Probe");
}

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_avboit_depth_warp_probe::DepthWarpProbeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

