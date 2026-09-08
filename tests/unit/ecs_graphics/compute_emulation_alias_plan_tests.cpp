// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute_emulation_alias_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct ComputeEmulationAliasPlanTestsTag>;
using RegularPlan = ECSRenderDetail::OpaqueRegularComputeEmulationGraphPlan;
using AvboitPlan = ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan;
using IntervalPlan = ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan;
using ReceiverPlan = ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphPlan;
using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;


struct AliasPlanContext{
    TestArena m_testArena;
    Core::GraphicsAllocator m_graphicsAllocator{ m_testArena.arena };
    Core::Alloc::CpuTaskScheduler m_cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext m_context{ m_graphicsAllocator, m_cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator m_allocator{ m_context };
    Core::Alloc::GlobalArena m_planArena{ Name("tests/compute_emulation_alias/plan") };
    Core::Alloc::ScratchArena m_inputArena{ Name("tests/compute_emulation_alias/input") };
    Core::Alloc::ScratchArena m_operationArena{ Name("tests/compute_emulation_alias/operation") };
    MaterialPassDrawItems m_regular{ m_inputArena };
    MaterialPassDrawItems m_receivers{ m_inputArena };
    CsgFrameGpuData m_csg{ m_inputArena };
    BufferVector m_buffers{ m_testArena.arena };

    explicit AliasPlanContext(const usize drawCount){
        appendDraws(m_regular, drawCount, MaterialPipelineCsgMode::None);
        appendDraws(m_receivers, drawCount, MaterialPipelineCsgMode::ClipOnly);
        m_csg.receiverRanges.emplace_back();
        m_csg.receiverRanges.back().cutterCount = 1u;
        m_csg.receiverRanges.back().shadingModelId = 7u;
        m_csg.cutters.emplace_back();
        m_csg.cutters.back().parameter0 = Float4(1.f, 2.f, 3.f, 4.f);
        m_csg.workRegion.expandClamped(2, 12, 3, 13, 64u, 64u);
    }

    [[nodiscard]] Core::BufferHandle makeBuffer(const Name& identity){
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
            m_testArena.arena, m_context, m_allocator, Core::BufferDesc{}.setByteSize(512u).setDebugName(identity)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&m_testArena.arena), AdoptRef);
    }

    void appendDraws(MaterialPassDrawItems& draws, const usize count, const MaterialPipelineCsgMode::Enum csgMode){
        draws.computeDrawItems.reserve(count);
        for(usize index = 0u; index < count; ++index){
            const u32 drawIndex = static_cast<u32>(m_buffers.size() / 2u);
            char indexText[32u] = {};
            const Name identity = DeriveName(Name("tests/compute_emulation_alias/mesh"), FormatDecimal(drawIndex, indexText));
            const Core::BufferHandle source = makeBuffer(DeriveName(identity, AStringView("/source")));
            const Core::BufferHandle output = makeBuffer(DeriveName(identity, AStringView("/output")));
            m_buffers.push_back(source);
            m_buffers.push_back(output);
            draws.computeDrawItems.emplace_back();
            MaterialPassDrawItem& draw = draws.computeDrawItems.back();
            draw.meshKey = identity;
            draw.pipelineKey.csgMode = csgMode;
            draw.instanceIndex = drawIndex;
            draw.materialConstantByteOffset = drawIndex * 16u;
            draw.shadingModelId = 2u;
            draw.meshletConeCullScaleSafe = true;
            MaterialPassMeshResourceSnapshot& mesh = draw.meshResources;
            mesh.sourceBuffers = RuntimeMeshBuffers{
                .positionBuffer = source,
                .normalBuffer = source,
                .tangentBuffer = source,
                .uv0Buffer = source,
                .colorBuffer = source,
                .meshletDescBuffer = source,
                .meshletBoundsBuffer = source,
                .meshletPositionRefDeltaBuffer = source,
                .meshletAttributeRefDeltaBuffer = source,
                .meshletLocalVertexRefBuffer = source,
                .meshletPrimitiveIndexBuffer = source,
            };
            for(u32 slot = 0u; slot < LengthOf(mesh.geometryHeapHandles); ++slot)
                mesh.geometryHeapHandles[slot] = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, slot);
            mesh.emulationVertexBuffer = output;
            mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, drawIndex);
            mesh.meshletCount = 1u;
            mesh.meshletPrimitiveIndexCount = 3u;
        }
    }
};


// The four production plans have different capture and matching inputs. Keep benchmark dispatch typed while
// timing the actual APIs; the operation arena is reserved for their caller-owned temporary validation storage.
template<typename Plan>
[[nodiscard]] bool CapturePlan(Plan& plan, AliasPlanContext& context, Core::Alloc::ScratchArena& scratchArena){
    if constexpr(IsSame_V<Plan, RegularPlan> || IsSame_V<Plan, AvboitPlan>)
        return plan.capture(context.m_regular, scratchArena);
    else if constexpr(IsSame_V<Plan, IntervalPlan>)
        return plan.capture(context.m_receivers, context.m_csg, scratchArena);
    else{
        static_assert(IsSame_V<Plan, ReceiverPlan>);
        return plan.capture(context.m_receivers, context.m_regular, context.m_csg, scratchArena);
    }
}

template<typename Plan>
[[nodiscard]] bool MatchesPlan(const Plan& plan, const AliasPlanContext& context, Core::Alloc::ScratchArena& scratchArena){
    if constexpr(IsSame_V<Plan, RegularPlan>)
        return plan.matches(context.m_regular.computeDrawItems);
    else if constexpr(IsSame_V<Plan, ReceiverPlan>)
        return plan.matches(scratchArena);
    else
        return plan.matches();
}

template<typename Plan>
void ExpectCleared(const Plan& plan){
    EXPECT_FALSE(plan.captured);
    EXPECT_TRUE(plan.drawItems.empty());
    EXPECT_TRUE(plan.outputBuffers.empty());
    if constexpr(requires{ plan.meshDrawItems; })
        EXPECT_TRUE(plan.meshDrawItems.empty());
    if constexpr(requires{ plan.outputHeapSlots; })
        EXPECT_TRUE(plan.outputHeapSlots.empty());
    if constexpr(requires{ plan.regularDrawItems; }){
        EXPECT_TRUE(plan.regularDrawItems.empty());
        EXPECT_TRUE(plan.regularOutputBuffers.empty());
    }
    if constexpr(requires{ plan.receiverRanges; }){
        EXPECT_TRUE(plan.receiverRanges.empty());
        EXPECT_TRUE(plan.cutters.empty());
        EXPECT_FALSE(plan.workRegion.valid());
    }
}


TEST(ComputeEmulationAliasPlan, RegularCapturePreservesOrderOwnersAndCurrentDrawValidation){
    AliasPlanContext context(40u);
    context.m_regular.meshDrawItems.push_back(context.m_receivers.computeDrawItems.front());
    RegularPlan plan(context.m_planArena);
    Core::Buffer* const source = context.m_regular.computeDrawItems.front().meshResources.sourceBuffers.positionBuffer.get();
    Core::Buffer* const output = context.m_regular.computeDrawItems.front().meshResources.emulationVertexBuffer.get();
    const u32 sourceReferences = source->getReferenceCount();
    const u32 outputReferences = output->getReferenceCount();
    ASSERT_TRUE(CapturePlan(plan, context, context.m_operationArena));
    ASSERT_EQ(plan.drawItems.size(), 40u);
    ASSERT_EQ(plan.meshDrawItems.size(), 1u);
    EXPECT_EQ(source->getReferenceCount(), sourceReferences + 11u);
    EXPECT_EQ(output->getReferenceCount(), outputReferences + 2u);
    for(usize index = 0u; index < plan.drawItems.size(); ++index){
        EXPECT_TRUE(plan.drawItems[index].meshResources.valid());
        EXPECT_EQ(plan.drawItems[index].meshKey, context.m_regular.computeDrawItems[index].meshKey);
        EXPECT_EQ(plan.outputBuffers[index].get(), context.m_regular.computeDrawItems[index].meshResources.emulationVertexBuffer.get());
    }
    EXPECT_TRUE(MatchesPlan(plan, context, context.m_operationArena));
    ++context.m_regular.computeDrawItems.back().instanceIndex;
    EXPECT_FALSE(MatchesPlan(plan, context, context.m_operationArena));
    --context.m_regular.computeDrawItems.back().instanceIndex;
    const auto previousHeapHandle = context.m_regular.computeDrawItems.back().meshResources.emulationVertexHeapHandle;
    context.m_regular.computeDrawItems.back().meshResources.emulationVertexHeapHandle = Core::GpuDescriptorHandle::invalid();
    EXPECT_FALSE(MatchesPlan(plan, context, context.m_operationArena));
    context.m_regular.computeDrawItems.back().meshResources.emulationVertexHeapHandle = previousHeapHandle;
    {
        MaterialPassDrawItems materialized(context.m_inputArena);
        plan.materialize(materialized);
        ASSERT_EQ(materialized.computeDrawItems.size(), 40u);
        ASSERT_EQ(materialized.meshDrawItems.size(), 1u);
        EXPECT_EQ(materialized.computeDrawItems.back().instanceIndex, context.m_regular.computeDrawItems.back().instanceIndex);
        EXPECT_EQ(source->getReferenceCount(), sourceReferences + 22u);
        EXPECT_EQ(output->getReferenceCount(), outputReferences + 3u);
    }
    plan.reset();
    ExpectCleared(plan);
    EXPECT_EQ(source->getReferenceCount(), sourceReferences);
    EXPECT_EQ(output->getReferenceCount(), outputReferences);
    EXPECT_FALSE(MatchesPlan(plan, context, context.m_operationArena));
}

TEST(ComputeEmulationAliasPlan, PointerAndHeapSlotAliasPoliciesRemainDistinctAndResetRejectedPlans){
    AliasPlanContext context(40u);
    RegularPlan regular(context.m_planArena);
    AvboitPlan avboit(context.m_planArena);
    IntervalPlan interval(context.m_planArena);
    ReceiverPlan receiver(context.m_planArena);
    const Core::BufferHandle regularLast = context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer;
    const Core::BufferHandle receiverLast = context.m_receivers.computeDrawItems.back().meshResources.emulationVertexBuffer;
    ASSERT_TRUE(CapturePlan(regular, context, context.m_operationArena));
    ASSERT_TRUE(CapturePlan(avboit, context, context.m_operationArena));
    ASSERT_TRUE(CapturePlan(interval, context, context.m_operationArena));
    ASSERT_TRUE(CapturePlan(receiver, context, context.m_operationArena));
    context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer =
        context.m_regular.computeDrawItems.front().meshResources.emulationVertexBuffer;
    context.m_receivers.computeDrawItems.back().meshResources.emulationVertexBuffer =
        context.m_receivers.computeDrawItems.front().meshResources.emulationVertexBuffer;
    EXPECT_FALSE(CapturePlan(regular, context, context.m_operationArena));
    EXPECT_FALSE(CapturePlan(avboit, context, context.m_operationArena));
    EXPECT_FALSE(CapturePlan(interval, context, context.m_operationArena));
    EXPECT_FALSE(CapturePlan(receiver, context, context.m_operationArena));
    ExpectCleared(regular);
    ExpectCleared(avboit);
    ExpectCleared(interval);
    ExpectCleared(receiver);
    context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer = regularLast;
    context.m_receivers.computeDrawItems.back().meshResources.emulationVertexBuffer = receiverLast;
    // Slot identity, rather than the whole encoded descriptor, is the existing AVBOIT/interval alias contract.
    context.m_regular.computeDrawItems.back().meshResources.emulationVertexHeapHandle = Core::GpuDescriptorHandle::make(
        Core::GpuDescriptorClass::SampledImage,
        context.m_regular.computeDrawItems.front().meshResources.emulationVertexHeapHandle.slot()
    );
    context.m_receivers.computeDrawItems.back().meshResources.emulationVertexHeapHandle = Core::GpuDescriptorHandle::make(
        Core::GpuDescriptorClass::SampledImage,
        context.m_receivers.computeDrawItems.front().meshResources.emulationVertexHeapHandle.slot()
    );
    EXPECT_TRUE(CapturePlan(regular, context, context.m_operationArena));
    EXPECT_TRUE(CapturePlan(receiver, context, context.m_operationArena));
    EXPECT_FALSE(CapturePlan(avboit, context, context.m_operationArena));
    EXPECT_FALSE(CapturePlan(interval, context, context.m_operationArena));
    ExpectCleared(avboit);
    ExpectCleared(interval);
}

TEST(ComputeEmulationAliasPlan, EmptyAndLateInvalidInputsClearEveryCapturedPlan){
    for(u32 invalidKind = 0u; invalidKind < 4u; ++invalidKind){
        AliasPlanContext context(40u);
        RegularPlan regular(context.m_planArena);
        AvboitPlan avboit(context.m_planArena);
        IntervalPlan interval(context.m_planArena);
        ReceiverPlan receiver(context.m_planArena);
        ASSERT_TRUE(CapturePlan(regular, context, context.m_operationArena));
        ASSERT_TRUE(CapturePlan(avboit, context, context.m_operationArena));
        ASSERT_TRUE(CapturePlan(interval, context, context.m_operationArena));
        ASSERT_TRUE(CapturePlan(receiver, context, context.m_operationArena));
        if(invalidKind == 0u){
            context.m_regular.computeDrawItems.clear();
            context.m_receivers.computeDrawItems.clear();
        }
        else if(invalidKind == 1u){
            context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer = nullptr;
            context.m_receivers.computeDrawItems.back().meshResources.emulationVertexBuffer = nullptr;
        }
        else if(invalidKind == 2u){
            context.m_regular.computeDrawItems.back().meshResources.emulationVertexHeapHandle = Core::GpuDescriptorHandle::invalid();
            context.m_receivers.computeDrawItems.back().meshResources.emulationVertexHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        else{
            context.m_regular.computeDrawItems.back().pipelineKey.csgMode = MaterialPipelineCsgMode::ClipOnly;
            context.m_receivers.computeDrawItems.back().pipelineKey.csgMode = MaterialPipelineCsgMode::None;
        }
        EXPECT_FALSE(CapturePlan(regular, context, context.m_operationArena));
        EXPECT_FALSE(CapturePlan(avboit, context, context.m_operationArena));
        EXPECT_FALSE(CapturePlan(interval, context, context.m_operationArena));
        EXPECT_FALSE(CapturePlan(receiver, context, context.m_operationArena));
        ExpectCleared(regular);
        ExpectCleared(avboit);
        ExpectCleared(interval);
        ExpectCleared(receiver);
    }
}

TEST(ComputeEmulationAliasPlan, CsgPlansRetainFrozenFramePayloadsAndRequireBothWorkArrays){
    AliasPlanContext context(40u);
    context.m_receivers.meshDrawItems.push_back(context.m_regular.computeDrawItems.front());
    IntervalPlan interval(context.m_planArena);
    ReceiverPlan receiver(context.m_planArena);
    ASSERT_TRUE(CapturePlan(interval, context, context.m_operationArena));
    ASSERT_TRUE(CapturePlan(receiver, context, context.m_operationArena));
    context.m_csg.receiverRanges[0u].shadingModelId = 19u;
    context.m_csg.workRegion.expandFull();
    EXPECT_EQ(interval.receiverRanges[0u].shadingModelId, 7u);
    EXPECT_EQ(receiver.receiverRanges[0u].shadingModelId, 7u);
    EXPECT_TRUE(interval.workRegion.bounded());
    EXPECT_TRUE(receiver.workRegion.bounded());
    {
        MaterialPassDrawItems materialized(context.m_inputArena);
        CsgFrameGpuData frame(context.m_inputArena);
        interval.materialize(materialized, frame);
        ASSERT_EQ(materialized.computeDrawItems.size(), 40u);
        ASSERT_EQ(materialized.meshDrawItems.size(), 1u);
        ASSERT_EQ(frame.receiverRanges.size(), 1u);
        ASSERT_EQ(frame.cutters.size(), 1u);
        EXPECT_EQ(frame.receiverRanges[0u].shadingModelId, 7u);
        EXPECT_EQ(frame.workRegion.minX, 2u);
        EXPECT_EQ(frame.workRegion.maxY, 13u);
        receiver.materialize(materialized, frame);
        EXPECT_EQ(materialized.computeDrawItems.back().meshKey, context.m_receivers.computeDrawItems.back().meshKey);
        EXPECT_EQ(frame.receiverRanges[0u].shadingModelId, 7u);
    }
    context.m_csg.cutters.clear();
    EXPECT_FALSE(CapturePlan(interval, context, context.m_operationArena));
    EXPECT_FALSE(CapturePlan(receiver, context, context.m_operationArena));
    ExpectCleared(interval);
    ExpectCleared(receiver);
    context.m_csg.cutters.emplace_back();
    context.m_csg.receiverRanges.clear();
    EXPECT_FALSE(CapturePlan(interval, context, context.m_operationArena));
    EXPECT_FALSE(CapturePlan(receiver, context, context.m_operationArena));
    ExpectCleared(interval);
    ExpectCleared(receiver);
}

TEST(ComputeEmulationAliasPlan, ReceiverRejectsCrossStreamAliasesAndRechecksMutableIntersections){
    AliasPlanContext context(40u);
    ReceiverPlan plan(context.m_planArena);
    // Repetition within the regular stream is permitted; only receiver ownership must be disjoint from it.
    context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer =
        context.m_regular.computeDrawItems.front().meshResources.emulationVertexBuffer;
    ASSERT_TRUE(CapturePlan(plan, context, context.m_operationArena));
    EXPECT_TRUE(MatchesPlan(plan, context, context.m_operationArena));
    const Core::BufferHandle previous = plan.regularOutputBuffers.back();
    plan.regularDrawItems.back().meshResources.emulationVertexBuffer = plan.outputBuffers.back();
    plan.regularOutputBuffers.back() = plan.outputBuffers.back();
    EXPECT_FALSE(MatchesPlan(plan, context, context.m_operationArena));
    plan.regularDrawItems.back().meshResources.emulationVertexBuffer = previous;
    plan.regularOutputBuffers.back() = previous;
    EXPECT_TRUE(MatchesPlan(plan, context, context.m_operationArena));
    const Core::BufferHandle replacement = context.makeBuffer(Name("tests/compute_emulation_alias/replacement"));
    plan.drawItems.front().meshResources.emulationVertexBuffer = replacement;
    plan.outputBuffers.front() = replacement;
    EXPECT_TRUE(MatchesPlan(plan, context, context.m_operationArena));
    plan.regularDrawItems.back().meshResources.emulationVertexBuffer = replacement;
    plan.regularOutputBuffers.back() = replacement;
    EXPECT_FALSE(MatchesPlan(plan, context, context.m_operationArena));
    context.m_receivers.computeDrawItems.back().meshResources.emulationVertexBuffer =
        context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer;
    EXPECT_FALSE(CapturePlan(plan, context, context.m_operationArena));
    ExpectCleared(plan);
}

TEST(ComputeEmulationAliasPlan, AvboitAndIntervalMatchesObserveCurrentBuffersSlotsAndCardinality){
    AliasPlanContext context(40u);
    AvboitPlan avboit(context.m_planArena);
    IntervalPlan interval(context.m_planArena);
    ASSERT_TRUE(CapturePlan(avboit, context, context.m_operationArena));
    ASSERT_TRUE(CapturePlan(interval, context, context.m_operationArena));
    EXPECT_TRUE(MatchesPlan(avboit, context, context.m_operationArena));
    EXPECT_TRUE(MatchesPlan(interval, context, context.m_operationArena));
    const auto avboitHandle = avboit.drawItems.back().meshResources.emulationVertexHeapHandle;
    const auto intervalHandle = interval.drawItems.back().meshResources.emulationVertexHeapHandle;
    avboit.drawItems.back().meshResources.emulationVertexHeapHandle = Core::GpuDescriptorHandle::make(
        Core::GpuDescriptorClass::StorageBuffer, avboitHandle.slot() + 100u
    );
    interval.drawItems.back().meshResources.emulationVertexHeapHandle = Core::GpuDescriptorHandle::make(
        Core::GpuDescriptorClass::StorageBuffer, intervalHandle.slot() + 100u
    );
    EXPECT_FALSE(MatchesPlan(avboit, context, context.m_operationArena));
    EXPECT_FALSE(MatchesPlan(interval, context, context.m_operationArena));
    avboit.drawItems.back().meshResources.emulationVertexHeapHandle = avboitHandle;
    interval.drawItems.back().meshResources.emulationVertexHeapHandle = intervalHandle;
    avboit.outputBuffers.pop_back();
    interval.outputHeapSlots.pop_back();
    EXPECT_FALSE(MatchesPlan(avboit, context, context.m_operationArena));
    EXPECT_FALSE(MatchesPlan(interval, context, context.m_operationArena));
}


TEST(ComputeEmulationAliasPlan, ReceiverMatchesAllowsMirroredDuplicatesWithinEitherCurrentStream){
    AliasPlanContext context(40u);
    ReceiverPlan plan(context.m_planArena);
    ASSERT_TRUE(CapturePlan(plan, context, context.m_operationArena));
    plan.drawItems.back().meshResources.emulationVertexBuffer = plan.outputBuffers.front();
    plan.outputBuffers.back() = plan.outputBuffers.front();
    plan.regularDrawItems.back().meshResources.emulationVertexBuffer = plan.regularOutputBuffers.front();
    plan.regularOutputBuffers.back() = plan.regularOutputBuffers.front();
    EXPECT_TRUE(MatchesPlan(plan, context, context.m_operationArena));
    plan.regularDrawItems.back().meshResources.emulationVertexBuffer = plan.outputBuffers.front();
    plan.regularOutputBuffers.back() = plan.outputBuffers.front();
    EXPECT_FALSE(MatchesPlan(plan, context, context.m_operationArena));
}

TEST(ComputeEmulationAliasPlan, SmallValidationUsesNoScratchAllocation){
    for(const usize count : { 0u, 1u, 8u, 16u, 32u }){
        AliasPlanContext context(count);
        RegularPlan regular(context.m_planArena);
        AvboitPlan avboit(context.m_planArena);
        IntervalPlan interval(context.m_planArena);
        ReceiverPlan receiver(context.m_planArena);
        const ArenaMemoryStats before = context.m_operationArena.memoryStats();
        EXPECT_EQ(CapturePlan(regular, context, context.m_operationArena), count != 0u);
        EXPECT_EQ(CapturePlan(avboit, context, context.m_operationArena), count != 0u);
        EXPECT_EQ(CapturePlan(interval, context, context.m_operationArena), count != 0u);
        EXPECT_EQ(MatchesPlan(regular, context, context.m_operationArena), count != 0u);
        EXPECT_EQ(MatchesPlan(avboit, context, context.m_operationArena), count != 0u);
        EXPECT_EQ(MatchesPlan(interval, context, context.m_operationArena), count != 0u);
        // Receiver capture seeds both streams into one membership set; keep its total unique inputs at most 32.
        if(count == 32u)
            context.m_regular.computeDrawItems.clear();
        EXPECT_EQ(CapturePlan(receiver, context, context.m_operationArena), count != 0u);
        EXPECT_EQ(MatchesPlan(receiver, context, context.m_operationArena), count != 0u);
        const ArenaMemoryStats after = context.m_operationArena.memoryStats();
        EXPECT_EQ(after.allocationCount, before.allocationCount);
        EXPECT_EQ(after.usedBytes, before.usedBytes);
        EXPECT_EQ(after.reservedBytes, before.reservedBytes);
        EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
    }
}

struct MetricNames{
    NotNull<const char*> capture;
    NotNull<const char*> matches;
    NotNull<const char*> planPeak;
    NotNull<const char*> scratchPeak;
};

inline constexpr MetricNames s_RegularMetrics{
    MakeNotNull("alias_regular_capture_ns"), MakeNotNull("alias_regular_matches_ns"),
    MakeNotNull("alias_regular_plan_peak_bytes"), MakeNotNull("alias_regular_scratch_peak_bytes"),
};
inline constexpr MetricNames s_AvboitMetrics{
    MakeNotNull("alias_avboit_capture_ns"), MakeNotNull("alias_avboit_matches_ns"),
    MakeNotNull("alias_avboit_plan_peak_bytes"), MakeNotNull("alias_avboit_scratch_peak_bytes"),
};
inline constexpr MetricNames s_IntervalMetrics{
    MakeNotNull("alias_interval_capture_ns"), MakeNotNull("alias_interval_matches_ns"),
    MakeNotNull("alias_interval_plan_peak_bytes"), MakeNotNull("alias_interval_scratch_peak_bytes"),
};
inline constexpr MetricNames s_ReceiverMetrics{
    MakeNotNull("alias_receiver_capture_ns"), MakeNotNull("alias_receiver_matches_ns"),
    MakeNotNull("alias_receiver_plan_peak_bytes"), MakeNotNull("alias_receiver_scratch_peak_bytes"),
};

namespace AliasWorkload{
    enum Enum : u8{
        Unique,
        LatePointer,
        LateSlot,
        LateIntersection,
    };
};

static void RecordMetric(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

template<typename Plan>
void MeasurePlan(
    AliasPlanContext& context,
    const MetricNames& metrics,
    const usize iterations,
    const bool expectedCapture){
    Core::Alloc::GlobalArena planArena(Name("tests/compute_emulation_alias/benchmark_plan"));
    Plan plan(planArena);
    u64 captureTime = 0u;
    u64 matchesTime = 0u;
    u64 scratchPeak = 0u;
    usize captures = 0u;
    usize matches = 0u;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        Core::Alloc::ScratchArena scratch(Name("tests/compute_emulation_alias/benchmark_operation"));
        const Timer captureBegin = TimerNow();
        const bool captured = CapturePlan(plan, context, scratch);
        captureTime += DurationInNS<u64>(TimerNow(), captureBegin);
        captures += captured ? 1u : 0u;
        const Timer matchesBegin = TimerNow();
        const bool matched = MatchesPlan(plan, context, scratch);
        matchesTime += DurationInNS<u64>(TimerNow(), matchesBegin);
        matches += matched ? 1u : 0u;
        scratchPeak = Max(scratchPeak, scratch.memoryStats().peakUsedBytes);
    }
    EXPECT_EQ(captures, expectedCapture ? iterations : 0u);
    EXPECT_EQ(matches, expectedCapture ? iterations : 0u);
    EXPECT_EQ(plan.drawItems.size(), expectedCapture ? context.m_regular.computeDrawItems.size() : 0u);
    RecordMetric(metrics.capture, captureTime);
    RecordMetric(metrics.matches, matchesTime);
    RecordMetric(metrics.planPeak, planArena.memoryStats().peakUsedBytes);
    RecordMetric(metrics.scratchPeak, scratchPeak);
}

static void BenchmarkAliasPlans(const usize count, const usize iterations, const AliasWorkload::Enum workload){
    AliasPlanContext context(count);
    if(workload == AliasWorkload::LatePointer){
        context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer =
            context.m_regular.computeDrawItems.front().meshResources.emulationVertexBuffer;
        context.m_receivers.computeDrawItems.back().meshResources.emulationVertexBuffer =
            context.m_receivers.computeDrawItems.front().meshResources.emulationVertexBuffer;
    }
    else if(workload == AliasWorkload::LateSlot){
        context.m_regular.computeDrawItems.back().meshResources.emulationVertexHeapHandle =
            context.m_regular.computeDrawItems.front().meshResources.emulationVertexHeapHandle;
        context.m_receivers.computeDrawItems.back().meshResources.emulationVertexHeapHandle =
            context.m_receivers.computeDrawItems.front().meshResources.emulationVertexHeapHandle;
    }
    else if(workload == AliasWorkload::LateIntersection){
        context.m_receivers.computeDrawItems.back().meshResources.emulationVertexBuffer =
            context.m_regular.computeDrawItems.back().meshResources.emulationVertexBuffer;
    }
    MeasurePlan<RegularPlan>(context, s_RegularMetrics, iterations, workload != AliasWorkload::LatePointer);
    MeasurePlan<AvboitPlan>(
        context, s_AvboitMetrics, iterations, workload != AliasWorkload::LatePointer && workload != AliasWorkload::LateSlot
    );
    MeasurePlan<IntervalPlan>(
        context, s_IntervalMetrics, iterations, workload != AliasWorkload::LatePointer && workload != AliasWorkload::LateSlot
    );
    MeasurePlan<ReceiverPlan>(
        context, s_ReceiverMetrics, iterations, workload != AliasWorkload::LatePointer && workload != AliasWorkload::LateIntersection
    );
    RecordMetric(MakeNotNull("alias_draw_count"), count);
    RecordMetric(MakeNotNull("alias_iterations"), iterations);
    RecordMetric(MakeNotNull("alias_workload"), static_cast<u64>(workload));
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_Unique1){
    BenchmarkAliasPlans(1u, 2048u, AliasWorkload::Unique);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_Unique8){
    BenchmarkAliasPlans(8u, 256u, AliasWorkload::Unique);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_Unique32){
    BenchmarkAliasPlans(32u, 64u, AliasWorkload::Unique);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_Unique512){
    BenchmarkAliasPlans(512u, 4u, AliasWorkload::Unique);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_Unique1024){
    BenchmarkAliasPlans(1024u, 2u, AliasWorkload::Unique);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_LatePointerAlias1024){
    BenchmarkAliasPlans(1024u, 2u, AliasWorkload::LatePointer);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_LateSlotAlias1024){
    BenchmarkAliasPlans(1024u, 2u, AliasWorkload::LateSlot);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_LateCsgIntersection1024){
    BenchmarkAliasPlans(1024u, 2u, AliasWorkload::LateIntersection);
}

TEST(ComputeEmulationAliasPlanBenchmark, DISABLED_MutatedCsgIntersection1024){
    AliasPlanContext context(1024u);
    ReceiverPlan plan(context.m_planArena);
    ASSERT_TRUE(CapturePlan(plan, context, context.m_operationArena));
    plan.regularDrawItems.back().meshResources.emulationVertexBuffer = plan.outputBuffers.back();
    plan.regularOutputBuffers.back() = plan.outputBuffers.back();
    constexpr usize s_Iterations = 4u;
    usize matches = 0u;
    u64 elapsed = 0u;
    u64 scratchPeak = 0u;
    for(usize iteration = 0u; iteration < s_Iterations; ++iteration){
        Core::Alloc::ScratchArena scratch(Name("tests/compute_emulation_alias/mutation_operation"));
        const Timer begin = TimerNow();
        const bool matched = MatchesPlan(plan, context, scratch);
        elapsed += DurationInNS<u64>(TimerNow(), begin);
        matches += matched ? 1u : 0u;
        scratchPeak = Max(scratchPeak, scratch.memoryStats().peakUsedBytes);
    }
    EXPECT_EQ(matches, 0u);
    RecordMetric(MakeNotNull("alias_receiver_matches_ns"), elapsed);
    RecordMetric(MakeNotNull("alias_receiver_scratch_peak_bytes"), scratchPeak);
    RecordMetric(MakeNotNull("alias_draw_count"), 1024u);
    RecordMetric(MakeNotNull("alias_iterations"), s_Iterations);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

