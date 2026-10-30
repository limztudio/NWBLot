// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/scheduler_callback_bindings.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_callback_binding_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using Graphics::GpuTaskSubmissionDetail::TaskCallbackBindings;

inline constexpr usize s_InlineCount = 8u;
inline constexpr usize s_IndexedCount = 9u;
inline constexpr usize s_MergeGroupSize = 2u;
inline constexpr usize s_ExtraRangeTasks = 4u;
inline constexpr usize s_ExcludedPacketCount = 1u;

inline constexpr usize s_NumberBufferBytes = 32u;

inline constexpr u64 s_AcceptedTokenValue = 7u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InvocationState{
    Vector<u32, Graphics::Alloc::ScratchArena>* order = nullptr;
    u64 count = 0u;
};

struct CallbackContext{
    InvocationState& state;
    u32 taskIndex;
};

struct CallbackGraph{
public:
    CallbackGraph(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch);


public:
    [[nodiscard]] b8 prepare(usize taskCount);


public:
    Graphics::GpuTaskGraph graph;
    SingleQueueCompile compilation;
    Vector<Graphics::GpuTaskId, Graphics::Alloc::ScratchArena> tasks;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CallbackGraph::CallbackGraph(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch)
    : graph(testArena.arena)
    , compilation(testArena)
    , tasks(scratch)
{}

b8 CallbackGraph::prepare(const usize taskCount){
    tasks.reserve(taskCount);
    for(usize index = 0u; index < taskCount; ++index){
        char suffix[s_NumberBufferBytes] = {};
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.mergeWithPrevious = index % s_MergeGroupSize != 0u;
        const auto derivedNameResult = DeriveName(Name("tests/callback_bindings/task/"), FormatDecimal(index, suffix));
        if(!derivedNameResult)
            return false;
        const auto taskResult = AddTaskWithCommands(graph, *derivedNameResult, "Callback Binding Task", GraphicsCommands(), scheduling, {}, scheduling.mergeWithPrevious ? &tasks.back() : nullptr, scheduling.mergeWithPrevious ? 1u : 0u);
        if(!taskResult)
            return false;
        const Graphics::GpuTaskId task = *taskResult;
        if(!task.valid())
            return false;
        tasks.push_back(task);
    }
    if(!compilation.compile(graph))
        return false;
    tasks.clear();
    const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    for(usize packetIndex = 0u; packetIndex < plan.packetCount(); ++packetIndex){
        const Graphics::GpuCompiledPacketView packet = plan.packet(plan.packetIdAt(packetIndex));
        if(!packet.valid())
            return false;
        for(usize taskIndex = 0u; taskIndex < packet.plan->taskCount; ++taskIndex)
            tasks.push_back(packet.tasks[taskIndex]);
    }
    return tasks.size() == taskCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RecordInvocation(CallbackContext& context){
    ++context.state.count;
    if(context.state.order)
        context.state.order->push_back(context.taskIndex);
}

b8 InvokeRecorded(void* const context, const Graphics::CommandListResourceStateHandoff*){
    RecordInvocation(*MakeNotNull(static_cast<CallbackContext*>(context)));
    return true;
}

b8 InvokeAccepted(void* const context, const Graphics::QueueSubmissionToken& token){
    if(token.value != s_AcceptedTokenValue)
        return false;
    RecordInvocation(*MakeNotNull(static_cast<CallbackContext*>(context)));
    return true;
}

template<typename Callback>
[[nodiscard]] Callback MakeCallback(const Graphics::GpuTaskId task, CallbackContext& context)noexcept(noexcept(Callback{
    .task = task,
    .context = &context,
    .invoke = DeclVal<Conditional_T<IsSame_V<Callback, Graphics::GpuTaskGraphTaskRecordedCallback>, decltype(&InvokeRecorded), decltype(&InvokeAccepted)>>(),
})){
    if constexpr(IsSame_V<Callback, Graphics::GpuTaskGraphTaskRecordedCallback>)
        return { .task = task, .context = &context, .invoke = InvokeRecorded };
    else
        return { .task = task, .context = &context, .invoke = InvokeAccepted };
}

template<typename Callback>
[[nodiscard]] b8 InvokeBoundCallback(const Callback& callback)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(
    callback.invoke(callback.context, DeclVal<Conditional_T<IsSame_V<Callback, Graphics::GpuTaskGraphTaskRecordedCallback>, decltype(nullptr), const Graphics::QueueSubmissionToken&>>())
))){
    if constexpr(IsSame_V<Callback, Graphics::GpuTaskGraphTaskRecordedCallback>)
        return callback.invoke(callback.context, nullptr);
    else{
        const Graphics::QueueSubmissionToken token{ .value = s_AcceptedTokenValue, .queue = Graphics::CommandQueue::Graphics };
        return callback.invoke(callback.context, token);
    }
}

template<typename Callback>
void CheckReorderedBindings(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch, const usize callbackCount){
    CallbackGraph fixture(testArena, scratch);
    ASSERT_TRUE(fixture.prepare(callbackCount));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.compilation.compiledGraph);
    ASSERT_LT(plan.packetCount(), fixture.tasks.size());
    Vector<u32, Graphics::Alloc::ScratchArena> order(scratch);
    order.reserve(callbackCount);
    InvocationState state{ .order = &order };
    Vector<CallbackContext, Graphics::Alloc::ScratchArena> contexts(scratch);
    Vector<Callback, Graphics::Alloc::ScratchArena> callbacks(scratch);
    contexts.reserve(callbackCount);
    callbacks.reserve(callbackCount);
    for(usize index = 0u; index < callbackCount; ++index){
        const Graphics::GpuTaskId task = fixture.tasks[callbackCount - 1u - index];
        contexts.push_back({ state, task.index });
        callbacks.push_back(MakeCallback<Callback>(task, contexts.back()));
    }
    TaskCallbackBindings<Callback> bindings(scratch);
    ASSERT_TRUE(bindings.resolve(declarations, plan, plan.allPacketRange(), callbacks.data(), callbacks.size()));
    const ArenaMemoryStats before = scratch.memoryStats();
    for(const Graphics::GpuTaskId task : fixture.tasks){
        const Callback* const callback = bindings.find(task);
        ASSERT_NE(callback, nullptr);
        ASSERT_TRUE(InvokeBoundCallback(*callback));
    }
    ExpectMemoryStatsEqual(before, scratch.memoryStats());
    ASSERT_EQ(order.size(), callbackCount);
    for(usize index = 0u; index < callbackCount; ++index)
        EXPECT_EQ(order[index], fixture.tasks[index].index);
    EXPECT_EQ(state.count, callbackCount);
    Graphics::GpuTaskId stale = fixture.tasks.front();
    ++stale.generation;
    EXPECT_EQ(bindings.find(stale), nullptr);
    EXPECT_EQ(bindings.find({}), nullptr);
}

template<typename Callback>
void CheckInvalidBindings(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch, const usize callbackCount){
    CallbackGraph fixture(testArena, scratch);
    ASSERT_TRUE(fixture.prepare(callbackCount + s_ExtraRangeTasks));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.compilation.compiledGraph);
    ASSERT_GT(plan.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketRange range = plan.packetRange(plan.packetIdAt(0u), plan.packetIdAt(plan.packetCount() - s_ExcludedPacketCount - 1u));
    InvocationState state;
    Vector<CallbackContext, Graphics::Alloc::ScratchArena> contexts(scratch);
    Vector<Callback, Graphics::Alloc::ScratchArena> callbacks(scratch);
    contexts.reserve(callbackCount);
    callbacks.reserve(callbackCount);
    for(usize index = 0u; index < callbackCount; ++index){
        const Graphics::GpuTaskId task = fixture.tasks[index];
        contexts.push_back({ state, task.index });
        callbacks.push_back(MakeCallback<Callback>(task, contexts.back()));
    }
    TaskCallbackBindings<Callback> bindings(scratch);
    const Callback saved = callbacks.back();
    const auto expectRejectedAndReset = [&](){
        EXPECT_FALSE(bindings.resolve(declarations, plan, range, callbacks.data(), callbacks.size()));
        EXPECT_EQ(bindings.find(callbacks.front().task), nullptr);
        callbacks.back() = saved;
        EXPECT_TRUE(bindings.resolve(declarations, plan, range, callbacks.data(), callbacks.size()));
        EXPECT_NE(bindings.find(saved.task), nullptr);
    };
    ASSERT_TRUE(bindings.resolve(declarations, plan, range, callbacks.data(), callbacks.size()));
    callbacks.back().task = callbacks.front().task;
    expectRejectedAndReset();
    ++callbacks.back().task.generation;
    expectRejectedAndReset();
    callbacks.back().invoke = nullptr;
    expectRejectedAndReset();
    callbacks.back().task = fixture.tasks.back();
    expectRejectedAndReset();
    callbacks.back().task.index = static_cast<u32>(fixture.tasks.size());
    expectRejectedAndReset();
    EXPECT_EQ(bindings.find(fixture.tasks.back()), nullptr);
    EXPECT_FALSE(bindings.resolve(declarations, plan, range, nullptr, callbackCount));
    EXPECT_EQ(bindings.find(saved.task), nullptr);
    EXPECT_TRUE(bindings.resolve(declarations, plan, range, nullptr, 0u));
    EXPECT_EQ(bindings.find(saved.task), nullptr);
    const ArenaMemoryStats beforeOversized = scratch.memoryStats();
    EXPECT_FALSE(bindings.resolve(declarations, plan, range, callbacks.data(), Limit<usize>::s_Max));
    EXPECT_FALSE(bindings.resolve(declarations, plan, range, callbacks.data(), fixture.tasks.size() + 1u));
    ExpectMemoryStatsEqual(beforeOversized, scratch.memoryStats());
    EXPECT_EQ(bindings.find(saved.task), nullptr);
    EXPECT_FALSE(bindings.resolve(declarations, plan, {}, callbacks.data(), callbacks.size()));
    EXPECT_EQ(bindings.find(saved.task), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskCallbackBindings, ReversedBindingsInvokeInMergedCompiledTaskOrderWithoutLookupAllocation){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    constexpr usize s_Counts[] = { s_InlineCount, s_IndexedCount };
    for(const usize count : s_Counts){
        ASSERT_NO_FATAL_FAILURE(CheckReorderedBindings<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, count));
        ASSERT_NO_FATAL_FAILURE(CheckReorderedBindings<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, count));
    }
}

TEST(GpuTaskCallbackBindings, FailedAdmissionClearsPriorBindingsForBothLookupPaths){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    constexpr usize s_Counts[] = { s_InlineCount, s_IndexedCount };
    for(const usize count : s_Counts){
        ASSERT_NO_FATAL_FAILURE(CheckInvalidBindings<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, count));
        ASSERT_NO_FATAL_FAILURE(CheckInvalidBindings<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, count));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

