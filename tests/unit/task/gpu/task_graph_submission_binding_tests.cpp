// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>

#include <core/task/gpu/scheduler_submission_bindings.h>
#include <core/graphics/gpu_timing.h>
#include <core/perf/timing.h>


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
    Core::GpuTaskGraph graph;
    SingleQueueCompile compileState;
    Core::Perf::TimingRecorder timingSink;
    Core::GpuTimingRecorder timingRecorder;
    Core::GraphicsVector<Core::GlobalUniquePtr<Core::GpuTimingSubmissionTicket>> tickets;
    Core::GraphicsVector<Core::GpuTaskId> tasks;
    u32 hookInvocationCount = 0u;


public:
    SubmissionBindingFixture();


public:
    [[nodiscard]] b8 prepare(usize taskCount, usize mergeGroupSize = 1u);
    [[nodiscard]] Core::QueueSubmissionPreSubmitHook hook(u64 identity)noexcept;
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

[[nodiscard]] b8 SubmissionBindingFixture::prepare(const usize taskCount, const usize mergeGroupSize){
    tasks.reserve(taskCount);
    tickets.reserve(taskCount);
    const Core::GpuTaskCommandRequirements commands{ Core::GpuQueueCapability::Graphics };
    for(usize index = 0u; index < taskCount; ++index){
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.mergeWithPrevious = index % mergeGroupSize != 0u;
        const auto derivedNameResult = IndexedName(Name(TaskGraphTestUtils::s_GpuCaseSubmissionBindingsTask), index);
        if(!derivedNameResult)
            return false;
        const auto taskResult = AddTaskWithCommands(*derivedNameResult, "Submission Binding", commands, scheduling, {}, scheduling.mergeWithPrevious ? Span(&tasks.back(), 1u) : Span<const Core::GpuTaskId>{}, graph);
        if(!taskResult)
            return false;
        const Core::GpuTaskId task = *taskResult;
        if(!task.valid())
            return false;
        tasks.push_back(task);
        tickets.push_back(Core::MakeGlobalUnique<Core::GpuTimingSubmissionTicket>(testArena.arena, timingRecorder));
        if(!tickets.back())
            return false;
    }
    return compileState.compile(graph);
}

[[nodiscard]] Core::QueueSubmissionPreSubmitHook SubmissionBindingFixture::hook(const u64 identity)noexcept{
    Core::QueueSubmissionPreSubmitHook result;
    result.context = &hookInvocationCount;
    result.identity = identity;
    result.invoke = [](void* const context, u64, const Core::GpuPhysicalQueueId&)noexcept ->Expected<Core::QueueSubmissionNativeSignal>{
        ++*MakeNotNull(static_cast<u32*>(context));
        return MakeUnexpected(Failure{});
    };
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskSubmissionBindings, PreservesMergedAliasesAndReversedBindingOrderWithoutInvokingHooks){
    SubmissionBindingFixture fixture;
    ASSERT_TRUE(fixture.prepare(48u, 3u));
    const Core::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Core::GpuCompiledGraph::ReadView plan(fixture.compileState.compiledGraph);
    ASSERT_EQ(plan.packetCount(), 16u);
    Core::GraphicsVector<Core::GpuTaskGraphTaskTimingTicket> timingBindings(fixture.testArena.arena);
    Core::GraphicsVector<Core::GpuTaskGraphTaskSubmissionHook> hookBindings(fixture.testArena.arena);
    timingBindings.reserve(fixture.tasks.size());
    hookBindings.reserve(plan.packetCount());
    for(usize reverseIndex = fixture.tasks.size(); reverseIndex != 0u; --reverseIndex){
        const usize index = reverseIndex - 1u;
        const usize first = index - index % 3u;
        timingBindings.push_back({ .task = fixture.tasks[index], .timingTicket = fixture.tickets[first + (index % 3u == 1u ? 1u : 0u)].get(), });
        if(index % 3u == 1u)
            hookBindings.push_back({ .task = fixture.tasks[index], .hook = fixture.hook(index + 1u) });
    }
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    Core::GpuTaskSubmissionDetail::TaskSubmissionBindings resolved(scratch);
    ASSERT_TRUE(resolved.resolve(
        declarations,
        plan,
        plan.allPacketRange(),
        Span<const Core::GpuTaskGraphTaskTimingTicket>(timingBindings.data(), timingBindings.size()),
        Span<const Core::GpuTaskGraphTaskSubmissionHook>(hookBindings.data(), hookBindings.size())
    ));
    Vector<NotNull<Core::GpuTimingSubmissionTicket*>, Core::Alloc::ScratchArena> packetTickets(scratch);
    packetTickets.reserve(resolved.timingTickets.size());
    const usize collectAllocationCount = scratch.memoryStats().allocationCount;
    for(usize packetIndex = 0u; packetIndex < plan.packetCount(); ++packetIndex){
        const usize first = packetIndex * 3u;
        const Core::GpuSubmissionPacketId packet = plan.packetForTask(fixture.tasks[first]);
        ASSERT_EQ(packet, plan.packetIdAt(packetIndex));
        const Core::QueueSubmissionPreSubmitHook* hook = nullptr;
        hook = resolved.collectPacket(packet, packetTickets);
        ASSERT_EQ(packetTickets.size(), 2u);
        EXPECT_EQ(packetTickets[0u].get(), fixture.tickets[first].get());
        EXPECT_EQ(packetTickets[1u].get(), fixture.tickets[first + 1u].get());
        ASSERT_NE(hook, nullptr);
        EXPECT_EQ(hook->context, &fixture.hookInvocationCount);
        EXPECT_EQ(hook->identity, first + 2u);
        EXPECT_TRUE(resolved.validateOwnedTimingTicket(packet, fixture.tickets[first].get()));
        EXPECT_TRUE(resolved.validateOwnedTimingTicket(packet, nullptr));
        EXPECT_TRUE(resolved.validateOwnedTimingTicket(packet, fixture.tickets[first + 2u].get()));
        EXPECT_FALSE(resolved.validateOwnedTimingTicket(plan.packetIdAt((packetIndex + 1u) % plan.packetCount()), fixture.tickets[first].get()));
    }
    Core::GpuSubmissionPacketId stalePacket = plan.packetIdAt(0u);
    ++stalePacket.generation;
    const Core::QueueSubmissionPreSubmitHook* hook = nullptr;
    hook = resolved.collectPacket(stalePacket, packetTickets);
    EXPECT_TRUE(packetTickets.empty());
    EXPECT_EQ(hook, nullptr);
    EXPECT_FALSE(resolved.validateOwnedTimingTicket(stalePacket, fixture.tickets[0u].get()));
    EXPECT_EQ(scratch.memoryStats().allocationCount, collectAllocationCount);

    const Core::GpuTaskGraphTaskTimingTicket singleTiming{ .task = fixture.tasks[0u], .timingTicket = fixture.tickets[0u].get() };
    const Core::GpuTaskGraphTaskSubmissionHook singleHook{ .task = fixture.tasks[1u], .hook = fixture.hook(99u) };
    ASSERT_TRUE(resolved.resolve(declarations, plan, plan.allPacketRange(), Span<const Core::GpuTaskGraphTaskTimingTicket>(&singleTiming, 1u), Span<const Core::GpuTaskGraphTaskSubmissionHook>(&singleHook, 1u)));
    hook = resolved.collectPacket(plan.packetIdAt(0u), packetTickets);
    ASSERT_EQ(packetTickets.size(), 1u);
    EXPECT_EQ(packetTickets[0u].get(), fixture.tickets[0u].get());
    ASSERT_NE(hook, nullptr);
    EXPECT_EQ(hook->identity, 99u);
    hook = resolved.collectPacket(plan.packetIdAt(15u), packetTickets);
    EXPECT_TRUE(packetTickets.empty());
    EXPECT_EQ(hook, nullptr);
    EXPECT_TRUE(resolved.validateOwnedTimingTicket(plan.packetIdAt(0u), fixture.tickets[3u].get()));

    ASSERT_TRUE(resolved.resolve(declarations, plan, plan.allPacketRange(), {}, {}));
    hook = resolved.collectPacket(plan.packetIdAt(0u), packetTickets);
    EXPECT_TRUE(packetTickets.empty());
    EXPECT_EQ(hook, nullptr);
    EXPECT_TRUE(resolved.validateOwnedTimingTicket(stalePacket, fixture.tickets[0u].get()));
    EXPECT_EQ(fixture.hookInvocationCount, 0u);
}

TEST(GpuTaskSubmissionBindings, RejectsLateDuplicateAnchorsCrossPacketTicketsAndMergedHookCollisions){
    SubmissionBindingFixture fixture;
    ASSERT_TRUE(fixture.prepare(48u, 3u));
    const Core::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Core::GpuCompiledGraph::ReadView plan(fixture.compileState.compiledGraph);
    const Core::GpuSubmissionPacketRange range = plan.allPacketRange();
    Core::GraphicsVector<Core::GpuTaskGraphTaskTimingTicket> timingBindings(fixture.testArena.arena);
    Core::GraphicsVector<Core::GpuTaskGraphTaskSubmissionHook> hookBindings(fixture.testArena.arena);
    timingBindings.reserve(fixture.tasks.size());
    hookBindings.reserve(plan.packetCount());
    for(usize index = 0u; index < fixture.tasks.size(); ++index)
        timingBindings.push_back({ .task = fixture.tasks[index], .timingTicket = fixture.tickets[index].get() });
    for(usize index = 0u; index < plan.packetCount(); ++index)
        hookBindings.push_back({ .task = fixture.tasks[index * 3u], .hook = fixture.hook(index + 1u) });
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    Core::GpuTaskSubmissionDetail::TaskSubmissionBindings resolved(scratch);
    const Core::GpuTaskGraphTaskTimingTicket lastTiming = timingBindings.back();
    timingBindings.back().task = timingBindings.front().task;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, Span<const Core::GpuTaskGraphTaskTimingTicket>(timingBindings.data(), timingBindings.size()), {}));
    timingBindings.back() = lastTiming;
    timingBindings.back().timingTicket = timingBindings.front().timingTicket;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, Span<const Core::GpuTaskGraphTaskTimingTicket>(timingBindings.data(), timingBindings.size()), {}));
    timingBindings.back() = lastTiming;
    const Core::GpuTaskGraphTaskSubmissionHook lastHook = hookBindings.back();
    hookBindings.back().task = fixture.tasks[1u];
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, {}, Span<const Core::GpuTaskGraphTaskSubmissionHook>(hookBindings.data(), hookBindings.size())));
    hookBindings.back().task = hookBindings.front().task;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, {}, Span<const Core::GpuTaskGraphTaskSubmissionHook>(hookBindings.data(), hookBindings.size())));
    hookBindings.back() = lastHook;
    ASSERT_TRUE(resolved.resolve(declarations, plan, range, Span<const Core::GpuTaskGraphTaskTimingTicket>(timingBindings.data(), timingBindings.size()), Span<const Core::GpuTaskGraphTaskSubmissionHook>(hookBindings.data(), hookBindings.size())));
    EXPECT_EQ(fixture.hookInvocationCount, 0u);
}

TEST(GpuTaskSubmissionBindings, RejectsStaleMissingAndOutOfRangeAnchorsAndMalformedBindings){
    SubmissionBindingFixture fixture;
    ASSERT_TRUE(fixture.prepare(2u));
    const Core::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Core::GpuCompiledGraph::ReadView plan(fixture.compileState.compiledGraph);
    const Core::GpuSubmissionPacketRange range = plan.allPacketRange();
    Core::GpuTaskGraphTaskTimingTicket timing{ .task = fixture.tasks[1u], .timingTicket = fixture.tickets[1u].get() };
    Core::GpuTaskGraphTaskSubmissionHook hook{ .task = fixture.tasks[1u], .hook = fixture.hook(2u) };
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    Core::GpuTaskSubmissionDetail::TaskSubmissionBindings resolved(scratch);
    Core::GpuSubmissionPacketRange prefix = range;
    prefix.packetCount = 1u;
    EXPECT_FALSE(resolved.resolve(declarations, plan, prefix, Span<const Core::GpuTaskGraphTaskTimingTicket>(&timing, 1u), {}));
    EXPECT_FALSE(resolved.resolve(declarations, plan, prefix, {}, Span<const Core::GpuTaskGraphTaskSubmissionHook>(&hook, 1u)));
    ++timing.task.generation;
    ++hook.task.generation;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, Span<const Core::GpuTaskGraphTaskTimingTicket>(&timing, 1u), {}));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, {}, Span<const Core::GpuTaskGraphTaskSubmissionHook>(&hook, 1u)));
    timing.task = fixture.tasks[1u];
    hook.task = fixture.tasks[1u];
    timing.task.index = static_cast<u32>(fixture.tasks.size());
    hook.task.index = static_cast<u32>(fixture.tasks.size());
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, Span<const Core::GpuTaskGraphTaskTimingTicket>(&timing, 1u), {}));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, {}, Span<const Core::GpuTaskGraphTaskSubmissionHook>(&hook, 1u)));
    timing.task = fixture.tasks[1u];
    hook.task = fixture.tasks[1u];
    timing.timingTicket = nullptr;
    hook.hook.invoke = nullptr;
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, Span<const Core::GpuTaskGraphTaskTimingTicket>(&timing, 1u), {}));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, {}, Span<const Core::GpuTaskGraphTaskSubmissionHook>(&hook, 1u)));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, Span<const Core::GpuTaskGraphTaskTimingTicket>(static_cast<const Core::GpuTaskGraphTaskTimingTicket*>(nullptr), 1u), {}));
    EXPECT_FALSE(resolved.resolve(declarations, plan, range, {}, Span<const Core::GpuTaskGraphTaskSubmissionHook>(static_cast<const Core::GpuTaskGraphTaskSubmissionHook*>(nullptr), 1u)));
    EXPECT_EQ(fixture.hookInvocationCount, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

