// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/scheduler_submission_bindings.h>
#include <core/graphics/gpu_timing.h>
#include <core/perf/timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_submission_binding_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

struct SubmissionBindingFixture{
    TestArena testArena;
    Graphics::GpuTaskGraph graph;
    SingleQueueCompile compileState;
    Graphics::Perf::TimingRecorder timingSink;
    Graphics::GpuTimingRecorder timingRecorder;
    Graphics::GraphicsVector<Graphics::GlobalUniquePtr<Graphics::GpuTimingSubmissionTicket>> tickets;
    Graphics::GraphicsVector<Graphics::GpuTaskId> tasks;
    u32 hookInvocationCount = 0u;


    SubmissionBindingFixture();


    [[nodiscard]] bool prepare(usize taskCount, usize mergeGroupSize = 1u);
    [[nodiscard]] Graphics::QueueSubmissionPreSubmitHook hook(u64 identity);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SubmissionBindingFixture::SubmissionBindingFixture()
    : graph(testArena.arena)
    , compileState(testArena)
    , timingSink(testArena.arena)
    , timingRecorder(testArena.arena, timingSink)
    , tickets(testArena.arena)
    , tasks(testArena.arena)
{}

[[nodiscard]] bool SubmissionBindingFixture::prepare(const usize taskCount, const usize mergeGroupSize){
    tasks.reserve(taskCount);
    tickets.reserve(taskCount);
    const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Graphics };
    for(usize index = 0u; index < taskCount; ++index){
        char text[32u] = {};
        const AStringView suffix = FormatDecimal(index, text);
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.mergeWithPrevious = index % mergeGroupSize != 0u;
        const Graphics::GpuTaskId task = AddTaskWithCommands(
            graph,
            DeriveName(Name("tests/submission_bindings/task/"), suffix),
            "Submission Binding",
            commands,
            scheduling,
            {},
            scheduling.mergeWithPrevious ? &tasks.back() : nullptr,
            scheduling.mergeWithPrevious ? 1u : 0u
        );
        if(!task.valid())
            return false;
        tasks.push_back(task);
        tickets.push_back(Graphics::MakeGlobalUnique<Graphics::GpuTimingSubmissionTicket>(testArena.arena, timingRecorder));
        if(!tickets.back())
            return false;
    }
    return compileState.compile(graph);
}

[[nodiscard]] Graphics::QueueSubmissionPreSubmitHook SubmissionBindingFixture::hook(const u64 identity){
    Graphics::QueueSubmissionPreSubmitHook result;
    result.context = &hookInvocationCount;
    result.identity = identity;
    result.invoke = [](void* const context, u64, const Graphics::GpuPhysicalQueueId&)->Expected<Graphics::QueueSubmissionNativeSignal>{
        ++*static_cast<u32*>(context);
        return MakeUnexpected(Failure{});
    };
    return result;
}

void BenchmarkSubmissionBindings(const usize packetCount, const usize bindingCount){
    constexpr usize s_Repetitions = 8u;
    SubmissionBindingFixture fixture;
    ASSERT_TRUE(fixture.prepare(packetCount));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.compileState.compiledGraph);
    ASSERT_EQ(plan.packetCount(), packetCount);
    const Graphics::GpuSubmissionPacketRange range = plan.allPacketRange();
    Graphics::GraphicsVector<Graphics::GpuTaskGraphTaskTimingTicket> timingBindings(fixture.testArena.arena);
    Graphics::GraphicsVector<Graphics::GpuTaskGraphTaskSubmissionHook> hookBindings(fixture.testArena.arena);
    timingBindings.reserve(bindingCount);
    hookBindings.reserve(bindingCount);
    u64 expectedHookSum = 0u;
    for(usize index = 0u; index < bindingCount; ++index){
        const usize taskIndex = packetCount - 1u - index * packetCount / bindingCount;
        timingBindings.push_back({ .task = fixture.tasks[taskIndex], .timingTicket = fixture.tickets[taskIndex].get() });
        hookBindings.push_back({ .task = fixture.tasks[taskIndex], .hook = fixture.hook(taskIndex + 1u) });
        expectedHookSum += taskIndex + 1u;
    }
    u64 resolveNanoseconds = 0u;
    u64 ownershipNanoseconds = 0u;
    u64 lookupNanoseconds = 0u;
    usize scratchPeakBytes = 0u;
    usize scratchAllocationCount = 0u;
    for(usize repetition = 0u; repetition < s_Repetitions; ++repetition){
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        {
            Graphics::GpuTaskSubmissionDetail::TaskSubmissionBindings resolved(scratch);
            Timer begin = TimerNow();
            ASSERT_TRUE(resolved.resolve(
                declarations, plan, range, timingBindings.data(), timingBindings.size(), hookBindings.data(), hookBindings.size()
            ));
            resolveNanoseconds += DurationInNS<u64>(TimerNow(), begin);
            bool ownershipValid = true;
            begin = TimerNow();
            if(!resolved.timingTickets.empty()){
                for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex){
                    ownershipValid = ownershipValid && resolved.validateOwnedTimingTicket(
                        plan.packetIdAt(packetIndex), fixture.tickets[packetIndex].get()
                    );
                }
            }
            ownershipNanoseconds += DurationInNS<u64>(TimerNow(), begin);
            ASSERT_TRUE(ownershipValid);
            Vector<Graphics::GpuTimingSubmissionTicket*, Graphics::Alloc::ScratchArena> packetTickets(scratch);
            packetTickets.reserve(resolved.timingTickets.size());
            usize resolvedTicketCount = 0u;
            u64 hookSum = 0u;
            begin = TimerNow();
            for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex){
                const Graphics::QueueSubmissionPreSubmitHook* hook = nullptr;
                hook = resolved.collectPacket(plan.packetIdAt(packetIndex), packetTickets);
                resolvedTicketCount += packetTickets.size();
                if(hook)
                    hookSum += hook->identity;
            }
            lookupNanoseconds += DurationInNS<u64>(TimerNow(), begin);
            EXPECT_EQ(resolvedTicketCount, bindingCount);
            EXPECT_EQ(hookSum, expectedHookSum);
        }
        EXPECT_EQ(scratch.memoryStats().usedBytes, 0u);
        scratchPeakBytes = Max(scratchPeakBytes, scratch.memoryStats().peakUsedBytes);
        scratchAllocationCount = Max(scratchAllocationCount, scratch.memoryStats().allocationCount);
    }
    EXPECT_EQ(fixture.hookInvocationCount, 0u);
    char text[32u] = {};
    const AStringView resolveElapsed = FormatDecimal(resolveNanoseconds, text);
    text[resolveElapsed.size()] = '\0';
    testing::Test::RecordProperty("resolve_ns", text);
    const AStringView ownershipElapsed = FormatDecimal(ownershipNanoseconds, text);
    text[ownershipElapsed.size()] = '\0';
    testing::Test::RecordProperty("ownership_comparison_ns", text);
    const AStringView lookupElapsed = FormatDecimal(lookupNanoseconds, text);
    text[lookupElapsed.size()] = '\0';
    testing::Test::RecordProperty("lookup_ns", text);
    const AStringView elapsed = FormatDecimal(resolveNanoseconds + ownershipNanoseconds + lookupNanoseconds, text);
    text[elapsed.size()] = '\0';
    testing::Test::RecordProperty("elapsed_ns", text);
    testing::Test::RecordProperty("repetitions", static_cast<i32>(s_Repetitions));
    testing::Test::RecordProperty("packet_count", static_cast<i32>(packetCount));
    testing::Test::RecordProperty("binding_count", static_cast<i32>(bindingCount));
    testing::Test::RecordProperty("scratch_peak_bytes", static_cast<i32>(scratchPeakBytes));
    testing::Test::RecordProperty("scratch_allocation_count", static_cast<i32>(scratchAllocationCount));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskSubmissionBindings, PreservesMergedAliasesAndReversedBindingOrderWithoutInvokingHooks){
    SubmissionBindingFixture fixture;
    ASSERT_TRUE(fixture.prepare(48u, 3u));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.compileState.compiledGraph);
    ASSERT_EQ(plan.packetCount(), 16u);
    Graphics::GraphicsVector<Graphics::GpuTaskGraphTaskTimingTicket> timingBindings(fixture.testArena.arena);
    Graphics::GraphicsVector<Graphics::GpuTaskGraphTaskSubmissionHook> hookBindings(fixture.testArena.arena);
    timingBindings.reserve(fixture.tasks.size());
    hookBindings.reserve(plan.packetCount());
    for(usize reverseIndex = fixture.tasks.size(); reverseIndex != 0u; --reverseIndex){
        const usize index = reverseIndex - 1u;
        const usize first = index - index % 3u;
        timingBindings.push_back({
            .task = fixture.tasks[index],
            .timingTicket = fixture.tickets[first + (index % 3u == 1u ? 1u : 0u)].get(),
        });
        if(index % 3u == 1u)
            hookBindings.push_back({ .task = fixture.tasks[index], .hook = fixture.hook(index + 1u) });
    }
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    Graphics::GpuTaskSubmissionDetail::TaskSubmissionBindings resolved(scratch);
    ASSERT_TRUE(resolved.resolve(
        declarations, plan, plan.allPacketRange(), timingBindings.data(), timingBindings.size(), hookBindings.data(), hookBindings.size()
    ));
    Vector<Graphics::GpuTimingSubmissionTicket*, Graphics::Alloc::ScratchArena> packetTickets(scratch);
    packetTickets.reserve(resolved.timingTickets.size());
    const usize collectAllocationCount = scratch.memoryStats().allocationCount;
    for(usize packetIndex = 0u; packetIndex < plan.packetCount(); ++packetIndex){
        const usize first = packetIndex * 3u;
        const Graphics::GpuSubmissionPacketId packet = plan.packetForTask(fixture.tasks[first]);
        ASSERT_EQ(packet, plan.packetIdAt(packetIndex));
        const Graphics::QueueSubmissionPreSubmitHook* hook = nullptr;
        hook = resolved.collectPacket(packet, packetTickets);
        ASSERT_EQ(packetTickets.size(), 2u);
        EXPECT_EQ(packetTickets[0u], fixture.tickets[first].get());
        EXPECT_EQ(packetTickets[1u], fixture.tickets[first + 1u].get());
        ASSERT_NE(hook, nullptr);
        EXPECT_EQ(hook->context, &fixture.hookInvocationCount);
        EXPECT_EQ(hook->identity, first + 2u);
        EXPECT_TRUE(resolved.validateOwnedTimingTicket(packet, fixture.tickets[first].get()));
        EXPECT_TRUE(resolved.validateOwnedTimingTicket(packet, nullptr));
        EXPECT_TRUE(resolved.validateOwnedTimingTicket(packet, fixture.tickets[first + 2u].get()));
        EXPECT_FALSE(resolved.validateOwnedTimingTicket(plan.packetIdAt((packetIndex + 1u) % plan.packetCount()), fixture.tickets[first].get()));
    }
    Graphics::GpuSubmissionPacketId stalePacket = plan.packetIdAt(0u);
    ++stalePacket.generation;
    const Graphics::QueueSubmissionPreSubmitHook* hook = nullptr;
    hook = resolved.collectPacket(stalePacket, packetTickets);
    EXPECT_TRUE(packetTickets.empty());
    EXPECT_EQ(hook, nullptr);
    EXPECT_FALSE(resolved.validateOwnedTimingTicket(stalePacket, fixture.tickets[0u].get()));
    EXPECT_EQ(scratch.memoryStats().allocationCount, collectAllocationCount);

    const Graphics::GpuTaskGraphTaskTimingTicket singleTiming{ .task = fixture.tasks[0u], .timingTicket = fixture.tickets[0u].get() };
    const Graphics::GpuTaskGraphTaskSubmissionHook singleHook{ .task = fixture.tasks[1u], .hook = fixture.hook(99u) };
    ASSERT_TRUE(resolved.resolve(declarations, plan, plan.allPacketRange(), &singleTiming, 1u, &singleHook, 1u));
    hook = resolved.collectPacket(plan.packetIdAt(0u), packetTickets);
    ASSERT_EQ(packetTickets.size(), 1u);
    EXPECT_EQ(packetTickets[0u], fixture.tickets[0u].get());
    ASSERT_NE(hook, nullptr);
    EXPECT_EQ(hook->identity, 99u);
    hook = resolved.collectPacket(plan.packetIdAt(15u), packetTickets);
    EXPECT_TRUE(packetTickets.empty());
    EXPECT_EQ(hook, nullptr);
    EXPECT_TRUE(resolved.validateOwnedTimingTicket(plan.packetIdAt(0u), fixture.tickets[3u].get()));

    ASSERT_TRUE(resolved.resolve(declarations, plan, plan.allPacketRange(), nullptr, 0u, nullptr, 0u));
    hook = resolved.collectPacket(plan.packetIdAt(0u), packetTickets);
    EXPECT_TRUE(packetTickets.empty());
    EXPECT_EQ(hook, nullptr);
    EXPECT_TRUE(resolved.validateOwnedTimingTicket(stalePacket, fixture.tickets[0u].get()));
    EXPECT_EQ(fixture.hookInvocationCount, 0u);
}

TEST(GpuTaskSubmissionBindings, RejectsLateDuplicateAnchorsCrossPacketTicketsAndMergedHookCollisions){
    SubmissionBindingFixture fixture;
    ASSERT_TRUE(fixture.prepare(48u, 3u));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.compileState.compiledGraph);
    const Graphics::GpuSubmissionPacketRange range = plan.allPacketRange();
    Graphics::GraphicsVector<Graphics::GpuTaskGraphTaskTimingTicket> timingBindings(fixture.testArena.arena);
    Graphics::GraphicsVector<Graphics::GpuTaskGraphTaskSubmissionHook> hookBindings(fixture.testArena.arena);
    timingBindings.reserve(fixture.tasks.size());
    hookBindings.reserve(plan.packetCount());
    for(usize index = 0u; index < fixture.tasks.size(); ++index)
        timingBindings.push_back({ .task = fixture.tasks[index], .timingTicket = fixture.tickets[index].get() });
    for(usize index = 0u; index < plan.packetCount(); ++index)
        hookBindings.push_back({ .task = fixture.tasks[index * 3u], .hook = fixture.hook(index + 1u) });
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    Graphics::GpuTaskSubmissionDetail::TaskSubmissionBindings resolved(scratch);
    const Graphics::GpuTaskGraphTaskTimingTicket lastTiming = timingBindings.back();
    timingBindings.back().task = timingBindings.front().task;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, timingBindings.data(), timingBindings.size(), nullptr, 0u));
    timingBindings.back() = lastTiming;
    timingBindings.back().timingTicket = timingBindings.front().timingTicket;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, timingBindings.data(), timingBindings.size(), nullptr, 0u));
    timingBindings.back() = lastTiming;
    const Graphics::GpuTaskGraphTaskSubmissionHook lastHook = hookBindings.back();
    hookBindings.back().task = fixture.tasks[1u];
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, nullptr, 0u, hookBindings.data(), hookBindings.size()));
    hookBindings.back().task = hookBindings.front().task;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, nullptr, 0u, hookBindings.data(), hookBindings.size()));
    hookBindings.back() = lastHook;
    ASSERT_TRUE(resolved.resolve(
        declarations, plan, range, timingBindings.data(), timingBindings.size(), hookBindings.data(), hookBindings.size()
    ));
    EXPECT_EQ(fixture.hookInvocationCount, 0u);
}

TEST(GpuTaskSubmissionBindings, RejectsStaleMissingAndOutOfRangeAnchorsAndMalformedBindings){
    SubmissionBindingFixture fixture;
    ASSERT_TRUE(fixture.prepare(2u));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.compileState.compiledGraph);
    const Graphics::GpuSubmissionPacketRange range = plan.allPacketRange();
    Graphics::GpuTaskGraphTaskTimingTicket timing{ .task = fixture.tasks[1u], .timingTicket = fixture.tickets[1u].get() };
    Graphics::GpuTaskGraphTaskSubmissionHook hook{ .task = fixture.tasks[1u], .hook = fixture.hook(2u) };
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    Graphics::GpuTaskSubmissionDetail::TaskSubmissionBindings resolved(scratch);
    Graphics::GpuSubmissionPacketRange prefix = range;
    prefix.packetCount = 1u;
    EXPECT_FALSE(resolved.resolve(declarations, plan, prefix, &timing, 1u, nullptr, 0u));
    EXPECT_FALSE(resolved.resolve(declarations, plan, prefix, nullptr, 0u, &hook, 1u));
    ++timing.task.generation;
    ++hook.task.generation;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, &timing, 1u, nullptr, 0u));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, nullptr, 0u, &hook, 1u));
    timing.task = fixture.tasks[1u];
    hook.task = fixture.tasks[1u];
    timing.task.index = static_cast<u32>(fixture.tasks.size());
    hook.task.index = static_cast<u32>(fixture.tasks.size());
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, &timing, 1u, nullptr, 0u));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, nullptr, 0u, &hook, 1u));
    timing.task = fixture.tasks[1u];
    hook.task = fixture.tasks[1u];
    timing.timingTicket = nullptr;
    hook.hook.invoke = nullptr;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, &timing, 1u, nullptr, 0u));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, nullptr, 0u, &hook, 1u));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, nullptr, 1u, nullptr, 0u));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, nullptr, 0u, nullptr, 1u));
    EXPECT_EQ(fixture.hookInvocationCount, 0u);
}

TEST(GpuTaskSubmissionBindings, DISABLED_TinyBindingBenchmark8Packets){
    BenchmarkSubmissionBindings(8u, 8u);
}

TEST(GpuTaskSubmissionBindings, DISABLED_CrossoverBindingBenchmark64Packets16Bindings){
    BenchmarkSubmissionBindings(64u, 16u);
}

TEST(GpuTaskSubmissionBindings, DISABLED_DenseBindingBenchmark1024Packets){
    BenchmarkSubmissionBindings(1024u, 1024u);
}

TEST(GpuTaskSubmissionBindings, DISABLED_DenseBindingBenchmark4096Packets){
    BenchmarkSubmissionBindings(4096u, 4096u);
}

TEST(GpuTaskSubmissionBindings, DISABLED_SparseBindingBenchmark4096Packets){
    BenchmarkSubmissionBindings(4096u, 16u);
}

TEST(GpuTaskSubmissionBindings, DISABLED_EmptyBindingBenchmark4096Packets){
    BenchmarkSubmissionBindings(4096u, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

