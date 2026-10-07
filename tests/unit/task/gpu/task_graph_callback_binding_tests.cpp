// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/scheduler_callback_bindings.h>

#include <global/timer.h>


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
inline constexpr usize s_DesignCount = 2u;
inline constexpr usize s_NumberBufferBytes = 32u;
inline constexpr usize s_MediumCallbackCount = 64u;
inline constexpr usize s_LargeCallbackCount = 1024u;
inline constexpr usize s_HugeCallbackCount = 4096u;
inline constexpr usize s_Warmups = 2u;
inline constexpr usize s_SampleCount = 8u;
inline constexpr u64 s_AcceptedTokenValue = 7u;
inline constexpr AStringView s_ScalarSampleKeys[] = {
    "scalar_sample_0_ns", "scalar_sample_1_ns", "scalar_sample_2_ns", "scalar_sample_3_ns",
    "scalar_sample_4_ns", "scalar_sample_5_ns", "scalar_sample_6_ns", "scalar_sample_7_ns"
};
inline constexpr AStringView s_IndexedSampleKeys[] = {
    "indexed_sample_0_ns", "indexed_sample_1_ns", "indexed_sample_2_ns", "indexed_sample_3_ns",
    "indexed_sample_4_ns", "indexed_sample_5_ns", "indexed_sample_6_ns", "indexed_sample_7_ns"
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InvocationState{
    Vector<u32, Graphics::Alloc::ScratchArena>* m_order = nullptr;
    u64 m_count = 0u;
    u64 m_checksum = 0u;
};

struct CallbackContext{
    InvocationState& m_state;
    u32 m_taskIndex;
};

struct CallbackGraph{
    Graphics::GpuTaskGraph m_graph;
    SingleQueueCompile m_compilation;
    Vector<Graphics::GpuTaskId, Graphics::Alloc::ScratchArena> m_tasks;


    CallbackGraph(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch);


    [[nodiscard]] bool prepare(usize taskCount);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CallbackGraph::CallbackGraph(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch)
    : m_graph(testArena.arena)
    , m_compilation(testArena)
    , m_tasks(scratch)
{}

bool CallbackGraph::prepare(const usize taskCount){
    m_tasks.reserve(taskCount);
    for(usize index = 0u; index < taskCount; ++index){
        char suffix[s_NumberBufferBytes] = {};
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.mergeWithPrevious = index % s_MergeGroupSize != 0u;
        const Graphics::GpuTaskId task = AddTaskWithCommands(
            m_graph,
            DeriveName(Name("tests/callback_bindings/task/"), FormatDecimal(index, suffix)),
            "Callback Binding Task",
            GraphicsCommands(),
            scheduling,
            {},
            scheduling.mergeWithPrevious ? &m_tasks.back() : nullptr,
            scheduling.mergeWithPrevious ? 1u : 0u
        );
        if(!task.valid())
            return false;
        m_tasks.push_back(task);
    }
    if(!m_compilation.compile(m_graph))
        return false;
    m_tasks.clear();
    const Graphics::GpuCompiledGraph::ReadView plan(m_compilation.compiledGraph);
    for(usize packetIndex = 0u; packetIndex < plan.packetCount(); ++packetIndex){
        const Graphics::GpuCompiledPacketView packet = plan.packet(plan.packetIdAt(packetIndex));
        if(!packet.valid())
            return false;
        for(usize taskIndex = 0u; taskIndex < packet.plan->taskCount; ++taskIndex)
            m_tasks.push_back(packet.tasks[taskIndex]);
    }
    return m_tasks.size() == taskCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RecordInvocation(CallbackContext& context){
    ++context.m_state.m_count;
    context.m_state.m_checksum += context.m_taskIndex + 1u;
    if(context.m_state.m_order)
        context.m_state.m_order->push_back(context.m_taskIndex);
}

bool InvokeRecorded(void* const context, const Graphics::CommandListResourceStateHandoff*){
    RecordInvocation(*static_cast<CallbackContext*>(context));
    return true;
}

bool InvokeAccepted(void* const context, const Graphics::QueueSubmissionToken& token){
    if(token.value != s_AcceptedTokenValue)
        return false;
    RecordInvocation(*static_cast<CallbackContext*>(context));
    return true;
}

template<typename Callback>
[[nodiscard]] Callback MakeCallback(const Graphics::GpuTaskId task, CallbackContext& context){
    if constexpr(IsSame_V<Callback, Graphics::GpuTaskGraphTaskRecordedCallback>)
        return { .task = task, .context = &context, .invoke = InvokeRecorded };
    else
        return { .task = task, .context = &context, .invoke = InvokeAccepted };
}

template<typename Callback>
[[nodiscard]] bool InvokeBoundCallback(const Callback& callback){
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
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.m_graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.m_compilation.compiledGraph);
    ASSERT_LT(plan.packetCount(), fixture.m_tasks.size());
    Vector<u32, Graphics::Alloc::ScratchArena> order(scratch);
    order.reserve(callbackCount);
    InvocationState state{ .m_order = &order };
    Vector<CallbackContext, Graphics::Alloc::ScratchArena> contexts(scratch);
    Vector<Callback, Graphics::Alloc::ScratchArena> callbacks(scratch);
    contexts.reserve(callbackCount);
    callbacks.reserve(callbackCount);
    for(usize index = 0u; index < callbackCount; ++index){
        const Graphics::GpuTaskId task = fixture.m_tasks[callbackCount - 1u - index];
        contexts.push_back({ state, task.index });
        callbacks.push_back(MakeCallback<Callback>(task, contexts.back()));
    }
    TaskCallbackBindings<Callback> bindings(scratch);
    ASSERT_TRUE(bindings.resolve(declarations, plan, plan.allPacketRange(), callbacks.data(), callbacks.size()));
    const ArenaMemoryStats before = scratch.memoryStats();
    for(const Graphics::GpuTaskId task : fixture.m_tasks){
        const Callback* const callback = bindings.find(task);
        ASSERT_NE(callback, nullptr);
        ASSERT_TRUE(InvokeBoundCallback(*callback));
    }
    ExpectMemoryStatsEqual(before, scratch.memoryStats());
    ASSERT_EQ(order.size(), callbackCount);
    for(usize index = 0u; index < callbackCount; ++index)
        EXPECT_EQ(order[index], fixture.m_tasks[index].index);
    EXPECT_EQ(state.m_count, callbackCount);
    Graphics::GpuTaskId stale = fixture.m_tasks.front();
    ++stale.generation;
    EXPECT_EQ(bindings.find(stale), nullptr);
    EXPECT_EQ(bindings.find({}), nullptr);
}

template<typename Callback>
void CheckInvalidBindings(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch, const usize callbackCount){
    CallbackGraph fixture(testArena, scratch);
    ASSERT_TRUE(fixture.prepare(callbackCount + s_ExtraRangeTasks));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.m_graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.m_compilation.compiledGraph);
    ASSERT_GT(plan.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketRange range = plan.packetRange(
        plan.packetIdAt(0u), plan.packetIdAt(plan.packetCount() - s_ExcludedPacketCount - 1u)
    );
    InvocationState state;
    Vector<CallbackContext, Graphics::Alloc::ScratchArena> contexts(scratch);
    Vector<Callback, Graphics::Alloc::ScratchArena> callbacks(scratch);
    contexts.reserve(callbackCount);
    callbacks.reserve(callbackCount);
    for(usize index = 0u; index < callbackCount; ++index){
        const Graphics::GpuTaskId task = fixture.m_tasks[index];
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
    callbacks.back().task = fixture.m_tasks.back();
    expectRejectedAndReset();
    callbacks.back().task.index = static_cast<u32>(fixture.m_tasks.size());
    expectRejectedAndReset();
    EXPECT_EQ(bindings.find(fixture.m_tasks.back()), nullptr);
    EXPECT_FALSE(bindings.resolve(declarations, plan, range, nullptr, callbackCount));
    EXPECT_EQ(bindings.find(saved.task), nullptr);
    EXPECT_TRUE(bindings.resolve(declarations, plan, range, nullptr, 0u));
    EXPECT_EQ(bindings.find(saved.task), nullptr);
    const ArenaMemoryStats beforeOversized = scratch.memoryStats();
    EXPECT_FALSE(bindings.resolve(declarations, plan, range, callbacks.data(), Limit<usize>::s_Max));
    EXPECT_FALSE(bindings.resolve(declarations, plan, range, callbacks.data(), fixture.m_tasks.size() + 1u));
    ExpectMemoryStatsEqual(beforeOversized, scratch.memoryStats());
    EXPECT_EQ(bindings.find(saved.task), nullptr);
    EXPECT_FALSE(bindings.resolve(declarations, plan, {}, callbacks.data(), callbacks.size()));
    EXPECT_EQ(bindings.find(saved.task), nullptr);
}

// Preserve the original validation and full-array dispatch work as the benchmark reference.
template<typename Callback>
[[nodiscard]] bool ScalarResolve(
    const Graphics::GpuTaskGraph::DeclarationReadView& declarations,
    const Graphics::GpuCompiledGraph::ReadView& plan,
    const Graphics::GpuSubmissionPacketRange& range,
    const Vector<Callback, Graphics::Alloc::ScratchArena>& callbacks
){
    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize index = 0u; index < callbacks.size(); ++index){
        const Callback& callback = callbacks[index];
        if(!callback.invoke || !declarations.validTask(callback.task) || !plan.findTask(callback.task).valid())
            return false;
        const Graphics::GpuSubmissionPacketId packet = plan.packetForTask(callback.task);
        if(!packet.valid() || packet.index < range.first.index || packet.index >= rangeEnd)
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(callbacks[previous].task == callback.task)
                return false;
        }
    }
    return true;
}

template<typename Callback>
void BenchmarkCallbacks(TestArena& testArena, Graphics::Alloc::ScratchArena& scratch, const usize callbackCount){
    CallbackGraph fixture(testArena, scratch);
    ASSERT_TRUE(fixture.prepare(callbackCount));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(fixture.m_graph);
    const Graphics::GpuCompiledGraph::ReadView plan(fixture.m_compilation.compiledGraph);
    const Graphics::GpuSubmissionPacketRange range = plan.allPacketRange();
    InvocationState state;
    Vector<CallbackContext, Graphics::Alloc::ScratchArena> contexts(scratch);
    Vector<Callback, Graphics::Alloc::ScratchArena> callbacks(scratch);
    contexts.reserve(callbackCount);
    callbacks.reserve(callbackCount);
    u64 expectedChecksum = 0u;
    for(usize index = 0u; index < callbackCount; ++index){
        const Graphics::GpuTaskId task = fixture.m_tasks[callbackCount - 1u - index];
        contexts.push_back({ state, task.index });
        callbacks.push_back(MakeCallback<Callback>(task, contexts.back()));
        expectedChecksum += task.index + 1u;
    }
    for(usize sample = 0u; sample < s_Warmups + s_SampleCount; ++sample){
        for(usize step = 0u; step < s_DesignCount; ++step){
            const bool indexed = (sample + step) % s_DesignCount != 0u;
            state.m_count = 0u;
            state.m_checksum = 0u;
            TaskCallbackBindings<Callback> bindings(scratch);
            const Timer begin = TimerNow();
            bool succeeded = indexed
                ? bindings.resolve(declarations, plan, range, callbacks.data(), callbacks.size())
                : ScalarResolve(declarations, plan, range, callbacks)
            ;
            for(const Graphics::GpuTaskId task : fixture.m_tasks){
                if(indexed){
                    const Callback* const callback = bindings.find(task);
                    succeeded = callback && InvokeBoundCallback(*callback) && succeeded;
                }
                else{
                    for(const Callback& callback : callbacks){
                        if(callback.task == task)
                            succeeded = InvokeBoundCallback(callback) && succeeded;
                    }
                }
            }
            const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
            ASSERT_TRUE(succeeded);
            ASSERT_EQ(state.m_count, callbackCount);
            ASSERT_EQ(state.m_checksum, expectedChecksum);
            if(sample >= s_Warmups){
                const AStringView key = indexed
                    ? s_IndexedSampleKeys[sample - s_Warmups]
                    : s_ScalarSampleKeys[sample - s_Warmups]
                ;
                RecordUnsignedTestProperty(key, elapsed);
            }
        }
    }
    RecordUnsignedTestProperty("callback_count", callbackCount);
    RecordUnsignedTestProperty("checksum", expectedChecksum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskCallbackBindings, ReversedBindingsInvokeInMergedCompiledTaskOrderWithoutLookupAllocation){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    constexpr usize s_Counts[] = { s_InlineCount, s_IndexedCount };
    for(const usize count : s_Counts){
        CheckReorderedBindings<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, count);
        CheckReorderedBindings<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, count);
    }
}

TEST(GpuTaskCallbackBindings, FailedAdmissionClearsPriorBindingsForBothLookupPaths){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    constexpr usize s_Counts[] = { s_InlineCount, s_IndexedCount };
    for(const usize count : s_Counts){
        CheckInvalidBindings<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, count);
        CheckInvalidBindings<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, count);
    }
}

TEST(GpuTaskCallbackBindings, DISABLED_RecordedBenchmark8Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, s_InlineCount);
}

TEST(GpuTaskCallbackBindings, DISABLED_RecordedBenchmark64Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, s_MediumCallbackCount);
}

TEST(GpuTaskCallbackBindings, DISABLED_RecordedBenchmark1024Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, s_LargeCallbackCount);
}

TEST(GpuTaskCallbackBindings, DISABLED_RecordedBenchmark4096Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskRecordedCallback>(testArena, scratch, s_HugeCallbackCount);
}

TEST(GpuTaskCallbackBindings, DISABLED_AcceptedBenchmark8Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, s_InlineCount);
}

TEST(GpuTaskCallbackBindings, DISABLED_AcceptedBenchmark64Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, s_MediumCallbackCount);
}

TEST(GpuTaskCallbackBindings, DISABLED_AcceptedBenchmark1024Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, s_LargeCallbackCount);
}

TEST(GpuTaskCallbackBindings, DISABLED_AcceptedBenchmark4096Callbacks){
    TestArena testArena;
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    BenchmarkCallbacks<Graphics::GpuTaskGraphTaskAcceptedCallback>(testArena, scratch, s_HugeCallbackCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

