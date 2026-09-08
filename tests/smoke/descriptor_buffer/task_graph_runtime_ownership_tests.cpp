// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/gpu_task_graph_read_views.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/task/cpu_task.h>
#include <core/common/module.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <core/graphics/vulkan/backend.h>
#include <global/global.h>
#include <global/termination.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core{


class GpuGraphSubmissionTransactionGateTestAccess final{
public:
    struct Snapshot{
        u32 readerCount = 0u;
        u32 writerReservationCount = 0u;
        bool writerActive = false;
    };

    struct ArtifactMembershipResults{
        Snapshot afterOwner;
        Snapshot afterNested;
        Snapshot afterOwnerThroughNested;
        Snapshot afterForeignThread;
        bool artifactOwnerValid = false;
        bool ownerBorrowAccepted = false;
        bool nestedArtifactValid = false;
        bool nestedBorrowAccepted = false;
        bool ownerThroughNestedAccepted = false;
        bool foreignThreadBorrowAccepted = false;
    };


public:
    [[nodiscard]] static Snapshot snapshot(const GpuGraphSubmissionTransaction& transaction)noexcept{
        constexpr u32 writerBit = 1u << 31u;
        constexpr u32 readerMask = writerBit - 1u;
        const u32 state = transaction.m_submissionGateState.load(MemoryOrder::acquire);
        return Snapshot{
            .readerCount = state & readerMask,
            .writerReservationCount = transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire),
            .writerActive = (state & writerBit) != 0u,
        };
    }
    [[nodiscard]] static bool waitForWriterReservationCount(
        const GpuGraphSubmissionTransaction& transaction,
        const u32 expectedCount
    )noexcept{
        const Timer begin = TimerNow();
        while(
            transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire) != expectedCount
            && DurationInSeconds<f64>(TimerNow(), begin) < 5.0
        )
            YieldThread();
        return transaction.m_submissionGateWriterCount.load(MemoryOrder::acquire) == expectedCount;
    }

    template<typename Callback>
    static void withOrdinaryOperation(
        const GpuGraphSubmissionTransaction& transaction,
        Callback&& callback
    ){
        GpuGraphSubmissionTransaction::SubmissionOperation operation(
            transaction,
            GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket
        );
        callback(operation.valid());
    }

    template<typename Callback>
    static void withWaitExclusiveOperation(
        const GpuGraphSubmissionTransaction& transaction,
        Callback&& callback
    ){
        GpuGraphSubmissionTransaction::SubmissionOperation operation(
            transaction,
            GpuGraphSubmissionTransaction::SubmissionOperationMode::WaitExclusiveBarrier
        );
        callback(operation.valid());
    }

    template<typename Callback>
    static void withPendingWriterReservation(
        const GpuGraphSubmissionTransaction& transaction,
        Callback&& callback
    ){
        GpuGraphSubmissionTransaction::SubmissionWriterReservation reservation;
        if(!reservation.acquire(transaction, false))
            TerminateInvariant();
        callback();
    }

    [[nodiscard]] static ArtifactMembershipResults exerciseArtifactMembership(
        const GpuRecordedGraph& recordedGraph,
        const GpuGraphSubmissionTransaction& transaction
    ){
        ArtifactMembershipResults results;
        GpuRecordedGraph::ArtifactOperation artifactOwner(
            recordedGraph,
            GpuRecordedGraph::ArtifactOperationMode::Read
        );
        results.artifactOwnerValid = artifactOwner.valid();
        {
            GpuGraphSubmissionTransaction::SubmissionOperation operation(
                transaction,
                GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket,
                &artifactOwner
            );
            results.ownerBorrowAccepted = operation.valid();
        }
        results.afterOwner = snapshot(transaction);

        {
            GpuRecordedGraph::ArtifactOperation nestedArtifact(
                recordedGraph,
                GpuRecordedGraph::ArtifactOperationMode::Read
            );
            results.nestedArtifactValid = nestedArtifact.valid();
            {
                GpuGraphSubmissionTransaction::SubmissionOperation operation(
                    transaction,
                    GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket,
                    &nestedArtifact
                );
                results.nestedBorrowAccepted = operation.valid();
            }
            results.afterNested = snapshot(transaction);
            {
                GpuGraphSubmissionTransaction::SubmissionOperation operation(
                    transaction,
                    GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket,
                    &artifactOwner
                );
                results.ownerThroughNestedAccepted = operation.valid();
            }
            results.afterOwnerThroughNested = snapshot(transaction);
        }

        JoiningThread foreignThread([&](){
            GpuGraphSubmissionTransaction::SubmissionOperation operation(
                transaction,
                GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket,
                &artifactOwner
            );
            results.foreignThreadBorrowAccepted = operation.valid();
        });
        foreignThread.join();
        results.afterForeignThread = snapshot(transaction);
        return results;
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_runtime_ownership_tests{


struct TimedRecordSentinel{};

inline constexpr Core::GpuTimingScopeDefinition s_ThrowingSplitAsyncTimingScope(
    "tests/task_graph/throwing_split_timing_async"
);
inline constexpr Core::GpuTimingScopeDefinition s_ThrowingSplitVisibilityTimingScope(
    "tests/task_graph/throwing_split_timing_visibility"
);


static void SignalFlagExactlyOnce(AtomicFlag& flag)noexcept{
    if(flag.test_and_set(MemoryOrder::release))
        TerminateInvariant();
    flag.notify_all();
}

static void ReleaseFlagIdempotently(AtomicFlag& flag)noexcept{
    const bool alreadyReleased = flag.test_and_set(MemoryOrder::release);
    if(!alreadyReleased)
        flag.notify_all();
}


struct RecordableTask{
    struct Payload{
        bool* recorded = nullptr;
        u32* discardedCount = nullptr;
        const bool* shouldRecord = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool recorded = commandList.isRecording() && (!payload.shouldRecord || *payload.shouldRecord);
        if(payload.recorded)
            *payload.recorded = recorded;
        return recorded;
    }

    static void discarded(Payload& payload)noexcept{
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


struct ThrowingSplitTimingTask{
    struct Payload{
        Core::Graphics* graphics = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* visibilityTiming = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.graphics
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.visibilityTiming
            || payload.asyncTiming->has_value()
            || payload.visibilityTiming->has_value()
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.asyncTiming->emplace(
            payload.graphics->gpuTiming(),
            s_ThrowingSplitAsyncTimingScope,
            payload.graphics->getDevice(),
            commandList
        );
        payload.visibilityTiming->emplace(
            payload.graphics->gpuTiming(),
            s_ThrowingSplitVisibilityTimingScope,
            payload.graphics->getDevice(),
            commandList
        );
        throw TimedRecordSentinel{};
    }

    static void discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.visibilityTiming);
        Core::DiscardGpuTimingMeasure(payload.asyncTiming);
    }
};


static void RecordThrowingSplitTimingTask(HeadlessGraphicsScope& graphicsScope){
    Core::Graphics& graphics = graphicsScope.graphics();
    Core::GraphicsBackend::Device& device = graphics.getDevice();
    Core::GpuTimingSubmissionTicket timingTicket(graphics.gpuTiming());
    Optional<Core::GpuTimingMeasure> asyncTiming;
    Optional<Core::GpuTimingMeasure> visibilityTiming;
    Core::GpuTaskGraph graph(graphicsScope.arena());
    const Core::GpuTaskId task = graph.addTask<ThrowingSplitTimingTask>(
        Core::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/throwing_split_timing"))
            .setMarkerLabel("Throwing Split Timing")
            .setQueue(Core::GpuQueueRequest{
                Core::GpuQueueCapability::Graphics,
                Core::GpuQueuePreference::Graphics,
                false,
                false,
            }),
        ThrowingSplitTimingTask::Payload{
            .graphics = &graphics,
            .timingTicket = &timingTicket,
            .asyncTiming = &asyncTiming,
            .visibilityTiming = &visibilityTiming,
        }
    );
    ASSERT_TRUE(task.valid());

    Core::GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    Core::GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    Core::GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Core::Alloc::ScratchArena compileScratch(Name("tests/task_graph/throwing_split_timing_compile"));
    const Core::GpuTaskGraphCompiler compiler;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }

    Core::GpuSubmissionPacketRange packetRange;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        packetRange = views.compiled.allPacketRange();
        ASSERT_TRUE(packetRange.valid());
    }

    Core::GpuRecordedGraph recordedGraph(graphicsScope.arena());
    const Core::GpuNativePacketRecorder recorder(device, graphics.gpuTiming());
    const bool recorded = recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        recordedGraph
    );
    EXPECT_FALSE(recorded);
}


struct DiscardObserverSentinel{};


struct ThrowingDiscardTask{
    struct Payload{
        AtomicFlag* entered = nullptr;
        AtomicFlag* release = nullptr;
        u32* discardedCount = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(payload);
        static_cast<void>(context);
        return commandList.isRecording();
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
        if(payload.entered)
            SignalFlagExactlyOnce(*payload.entered);
        if(payload.release){
            while(!payload.release->test(MemoryOrder::acquire))
                payload.release->wait(false, MemoryOrder::acquire);
        }
        throw DiscardObserverSentinel{};
    }
};


struct BlockingRecordTask{
    struct Payload{
        AtomicFlag* entered = nullptr;
        AtomicFlag* release = nullptr;
        u32* discardedCount = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.entered || !payload.release || !commandList.isRecording())
            return false;
        SignalFlagExactlyOnce(*payload.entered);
        while(!payload.release->test(MemoryOrder::acquire))
            payload.release->wait(false, MemoryOrder::acquire);
        return true;
    }

    static void discarded(Payload& payload)noexcept{
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


struct BlockingDeclarationTask{
    struct Payload{
        AtomicFlag* entered = nullptr;
        AtomicFlag* release = nullptr;

        Payload(AtomicFlag& enteredFlag, AtomicFlag& releaseFlag)noexcept
            : entered(&enteredFlag)
            , release(&releaseFlag)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other)noexcept
            : entered(other.entered)
            , release(other.release)
        {
            if(!entered || !release)
                TerminateInvariant();
            SignalFlagExactlyOnce(*entered);
            while(!release->test(MemoryOrder::acquire))
                release->wait(false, MemoryOrder::acquire);
        }
        Payload& operator=(const Payload&) = delete;
        Payload& operator=(Payload&&) = delete;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(payload);
        static_cast<void>(context);
        return commandList.isRecording();
    }
};


struct AcceptedCallbackBlocker{
    AtomicFlag entered;
    AtomicFlag release;
    Core::QueueSubmissionToken token;
};


[[nodiscard]] static bool BlockAcceptedCallback(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    AcceptedCallbackBlocker* const context = static_cast<AcceptedCallbackBlocker*>(rawContext);
    if(!context || !token.valid())
        return false;

    context->token = token;
    SignalFlagExactlyOnce(context->entered);
    while(!context->release.test(MemoryOrder::acquire))
        context->release.wait(false, MemoryOrder::acquire);
    return true;
}


[[nodiscard]] static bool WaitForFlag(const AtomicFlag& flag)noexcept{
    const Timer begin = TimerNow();
    while(!flag.test(MemoryOrder::acquire) && DurationInSeconds<f64>(TimerNow(), begin) < 5.0)
        YieldThread();
    return flag.test(MemoryOrder::acquire);
}


class JoiningThreadGroup final : NoCopy{
public:
    explicit JoiningThreadGroup(AtomicFlag& releaseSignal)noexcept{ addReleaseSignal(releaseSignal); }
    ~JoiningThreadGroup()noexcept{
        // JoiningThread members join after this destructor body; publish release first so every parked worker can exit.
        release();
    }


public:
    void addReleaseSignal(AtomicFlag& releaseSignal)noexcept{
        if(m_releaseSignalCount >= LengthOf(m_releaseSignals))
            TerminateInvariant();
        m_releaseSignals[m_releaseSignalCount++] = &releaseSignal;
    }

    template<typename Callback>
    usize start(Callback&& callback){
        if(m_threadCount >= LengthOf(m_threads))
            TerminateInvariant();
        const usize threadIndex = m_threadCount;
        m_threads[threadIndex] = JoiningThread(Forward<Callback>(callback));
        ++m_threadCount;
        return threadIndex;
    }

    void join(const usize threadIndex){
        if(threadIndex >= m_threadCount)
            TerminateInvariant();
        if(m_threads[threadIndex].joinable())
            m_threads[threadIndex].join();
    }
    void releaseAndJoin(){
        release();
        for(usize threadIndex = 0u; threadIndex < m_threadCount; ++threadIndex)
            join(threadIndex);
    }


private:
    void release()noexcept{
        for(usize signalIndex = 0u; signalIndex < m_releaseSignalCount; ++signalIndex)
            ReleaseFlagIdempotently(*m_releaseSignals[signalIndex]);
    }


private:
    AtomicFlag* m_releaseSignals[4u] = {};
    JoiningThread m_threads[4u];
    usize m_releaseSignalCount = 0u;
    usize m_threadCount = 0u;
};


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
using namespace __hidden_task_graph_runtime_ownership_tests;


static_assert(!IsConstructible_V<GpuTaskGraph::DeclarationReadView, const GpuTaskGraph::DeclarationReadView&>);
static_assert(!IsConstructible_V<GpuTaskGraph::DeclarationReadView, GpuTaskGraph::DeclarationReadView&&>);
static_assert(!IsAssignable_V<GpuTaskGraph::DeclarationReadView&, const GpuTaskGraph::DeclarationReadView&>);
static_assert(!IsAssignable_V<GpuTaskGraph::DeclarationReadView&, GpuTaskGraph::DeclarationReadView&&>);
static_assert(!IsConstructible_V<GpuCompiledGraph::ReadView, const GpuCompiledGraph::ReadView&>);
static_assert(!IsConstructible_V<GpuCompiledGraph::ReadView, GpuCompiledGraph::ReadView&&>);
static_assert(!IsAssignable_V<GpuCompiledGraph::ReadView&, const GpuCompiledGraph::ReadView&>);
static_assert(!IsAssignable_V<GpuCompiledGraph::ReadView&, GpuCompiledGraph::ReadView&&>);


// A split timing owner lives outside the packet recorder that creates its opening command list. If its task throws,
// graph ownership recovers the native marker stack before destroying that list; the later timing unwind must never
// follow its stale CommandList reference back into the destroyed opener.
TEST(TaskGraphRuntimeOwnershipTest, ThrowingSplitTimingUnwindRelinquishesDestroyedOpeningCommandList){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Throwing split timing unwind: no usable headless Vulkan device on this host.";

    EXPECT_THROW(RecordThrowingSplitTimingTask(graphicsScope), TimedRecordSentinel);
    EXPECT_FALSE(logger.sawMessageContaining(NWB_TEXT("Ignoring an unmatched command-list marker end")));
}


TEST(TaskGraphRuntimeOwnershipTest, DeclarationReadViewRejectsResetAndMutationUntilRelease){
    TestArena<struct DeclarationReadViewAdmissionTag> testArena;
    GpuTaskGraph graph(testArena.arena);
    const GpuTaskDesc taskDesc = GpuTaskDesc{}
        .setIdentity(Name("tests/task_graph/declaration_read_admission"))
        .setMarkerLabel("Declaration Read Admission")
    ;
    const GpuTaskId task = graph.addTask(taskDesc);
    ASSERT_TRUE(task.valid());

    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        const GpuTaskGraphTaskView taskView = declarations.taskAt(task.index);
        ASSERT_EQ(taskView.id, task);
        const auto* const markerData = taskView.markerLabel.data();
        ASSERT_NE(markerData, nullptr);

        EXPECT_FALSE(graph.tryReset());
        EXPECT_FALSE(graph.addTask(GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/declaration_read_rejected_mutation"))
            .setMarkerLabel("Declaration Read Rejected Mutation")
        ).valid());

        const GpuTaskGraphTaskView stableTaskView = declarations.taskAt(task.index);
        EXPECT_EQ(stableTaskView.id, task);
        EXPECT_EQ(stableTaskView.markerLabel.data(), markerData);
        EXPECT_EQ(stableTaskView.markerLabel.size(), taskView.markerLabel.size());
    }

    EXPECT_TRUE(graph.tryReset());
}


TEST(TaskGraphRuntimeOwnershipTest, TryDeclarationReadRejectsActiveMutationWithoutWaiting){
    TestArena<struct TryDeclarationReadAdmissionTag> testArena;
    GpuTaskGraph graph(testArena.arena);
    AtomicFlag mutationEntered;
    AtomicFlag releaseMutation;
    GpuTaskId task;
    JoiningThreadGroup threads(releaseMutation);
    threads.start([&](){
        task = graph.addTask<BlockingDeclarationTask>(
            GpuTaskDesc{}
                .setIdentity(Name("tests/task_graph/try_declaration_read_active_mutation"))
                .setMarkerLabel("Try Declaration Read Active Mutation"),
            BlockingDeclarationTask::Payload(mutationEntered, releaseMutation)
        );
    });
    const bool entered = WaitForFlag(mutationEntered);
    if(!entered){
        threads.releaseAndJoin();
        ASSERT_TRUE(entered);
        return;
    }

    const Timer begin = TimerNow();
    const GpuTaskGraph::DeclarationReadView declarations = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
    const f64 elapsedSeconds = DurationInSeconds<f64>(TimerNow(), begin);
    EXPECT_FALSE(declarations.valid());
    EXPECT_LT(elapsedSeconds, 1.0);

    threads.releaseAndJoin();
    EXPECT_TRUE(task.valid());
    EXPECT_TRUE(graph.tryReset());
}


// Public try-style transaction operations must never wait behind a callback holding ordinary submission admission.
// Both calls race the same parked accepted callback so the regression also proves that a failed writer reservation
// cannot perturb the in-flight packet's tokens, counters, or graph binding.
TEST(TaskGraphRuntimeOwnershipTest, TransactionTryOperationsRejectBeforeAcceptedCallbackRelease){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Transaction try admission: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    GpuTaskGraph graph(graphicsScope.arena());
    bool firstRecorded = false;
    bool secondRecorded = false;
    u32 secondDiscardedCount = 0u;
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuTaskId firstTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/runtime_ownership_try_operation_first"))
            .setMarkerLabel("Runtime Ownership Try Operation First")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling),
        RecordableTask::Payload{ .recorded = &firstRecorded }
    );
    const GpuTaskId secondTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/runtime_ownership_try_operation_second"))
            .setMarkerLabel("Runtime Ownership Try Operation Second")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling),
        RecordableTask::Payload{
            .recorded = &secondRecorded,
            .discardedCount = &secondDiscardedCount,
        }
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/runtime_ownership_try_operation_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId firstPacket;
    GpuSubmissionPacketId secondPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        firstPacket = views.compiled.packetForTask(firstTask);
        secondPacket = views.compiled.packetForTask(secondTask);
    }
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(firstPacket, secondPacket);

    GpuRecordedGraph recordedGraph(graphicsScope.arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        firstTask,
        secondTask,
        recordedGraph
    ));
    ASSERT_TRUE(firstRecorded);
    ASSERT_TRUE(secondRecorded);

    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    AcceptedCallbackBlocker callbackBlocker;
    const GpuTaskGraphTaskAcceptedCallback callback{
        .task = firstTask,
        .context = &callbackBlocker,
        .invoke = &BlockAcceptedCallback,
    };

    bool submissionResult = false;
    JoiningThreadGroup threads(callbackBlocker.release);
    threads.start([&](){
        Alloc::ScratchArena submissionScratch(Name("tests/task_graph/runtime_ownership_try_operation_submit"));
        submissionResult = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            firstTask,
            firstTask,
            nullptr,
            0u,
            nullptr,
            0u,
            transaction,
            submissionScratch,
            nullptr,
            &callback,
            1u
        );
    });
    const bool callbackEntered = WaitForFlag(callbackBlocker.entered);
    if(!callbackEntered){
        threads.releaseAndJoin();
        ASSERT_TRUE(callbackEntered);
        return;
    }

    const u64 attemptGeneration = recordedGraph.recordingAttemptGeneration();
    const GpuTaskGraphSubmissionStatistics statisticsBeforeTry = transaction.submissionStatistics();
    EXPECT_FALSE(transaction.packetToken(firstPacket).valid());
    EXPECT_FALSE(transaction.packetToken(secondPacket).valid());

    bool resetResult = true;
    bool discardResult = true;
    AtomicFlag resetReturned;
    AtomicFlag discardReturned;
    threads.start([&](){
        resetResult = transaction.tryReset(compiledGraph);
        SignalFlagExactlyOnce(resetReturned);
    });
    threads.start([&](){
        discardResult = transaction.discardUnaccepted(graph, compiledGraph, attemptGeneration);
        SignalFlagExactlyOnce(discardReturned);
    });
    const bool resetReturnedBeforeRelease = WaitForFlag(resetReturned);
    const bool discardReturnedBeforeRelease = WaitForFlag(discardReturned);
    if(!resetReturnedBeforeRelease || !discardReturnedBeforeRelease){
        threads.releaseAndJoin();
        EXPECT_TRUE(resetReturnedBeforeRelease);
        EXPECT_TRUE(discardReturnedBeforeRelease);
        return;
    }

    const GpuTaskGraphSubmissionStatistics statisticsAfterTry = transaction.submissionStatistics();
    EXPECT_TRUE(resetReturnedBeforeRelease);
    EXPECT_TRUE(discardReturnedBeforeRelease);
    EXPECT_FALSE(resetResult);
    EXPECT_FALSE(discardResult);
    EXPECT_FALSE(transaction.packetToken(firstPacket).valid());
    EXPECT_FALSE(transaction.packetToken(secondPacket).valid());
    EXPECT_EQ(statisticsAfterTry.acceptedPacketCount, statisticsBeforeTry.acceptedPacketCount);
    EXPECT_EQ(statisticsAfterTry.rejectedPacketCount, statisticsBeforeTry.rejectedPacketCount);
    EXPECT_EQ(statisticsAfterTry.nativeSubmissionCount, statisticsBeforeTry.nativeSubmissionCount);

    threads.releaseAndJoin();

    EXPECT_TRUE(submissionResult);
    EXPECT_TRUE(callbackBlocker.token.valid());
    EXPECT_TRUE(transaction.packetToken(firstPacket).valid());
    EXPECT_FALSE(transaction.packetToken(secondPacket).valid());
    EXPECT_EQ(transaction.submissionStatistics().acceptedPacketCount, 1u);
    EXPECT_TRUE(transaction.discardUnaccepted(graph, compiledGraph, attemptGeneration));
    EXPECT_EQ(secondDiscardedCount, 1u);
    EXPECT_EQ(transaction.submissionStatistics().rejectedPacketCount, 1u);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
}


// A discard observer can throw while another artifact owns a packet recording claim. Exception close is published
// before the transaction writer releases, so a queued operation cannot claim a third packet; cleanup then waits
// without the writer, lets the existing recorder publish, and terminalizes the exact original attempt before the
// observer exception reaches the entry boundary.
TEST(TaskGraphRuntimeOwnershipTest, ThrowingDiscardWaitsForExistingRecorderAfterClosingNewAdmission){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Submission exception close: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    GpuTaskGraph graph(graphicsScope.arena());
    AtomicFlag recordEntered;
    AtomicFlag releaseRecord;
    AtomicFlag discardEntered;
    AtomicFlag releaseDiscard;
    u32 throwingDiscardCount = 0u;
    u32 probeDiscardCount = 0u;
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest queue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskId throwingTask = graph.addTask<ThrowingDiscardTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/throwing_discard_close"))
            .setMarkerLabel("Throwing Discard Close")
            .setQueue(queue)
            .setScheduling(scheduling),
        ThrowingDiscardTask::Payload{
            .entered = &discardEntered,
            .release = &releaseDiscard,
            .discardedCount = &throwingDiscardCount,
        }
    );
    const GpuTaskId recordingTask = graph.addTask<BlockingRecordTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/throwing_discard_recording"))
            .setMarkerLabel("Throwing Discard Recording")
            .setQueue(queue)
            .setScheduling(scheduling),
        BlockingRecordTask::Payload{
            .entered = &recordEntered,
            .release = &releaseRecord,
        }
    );
    const GpuTaskId probeTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/throwing_discard_probe"))
            .setMarkerLabel("Throwing Discard Probe")
            .setQueue(queue)
            .setScheduling(scheduling),
        RecordableTask::Payload{ .discardedCount = &probeDiscardCount }
    );
    ASSERT_TRUE(throwingTask.valid());
    ASSERT_TRUE(recordingTask.valid());
    ASSERT_TRUE(probeTask.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/throwing_discard_close_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }

    GpuRecordedGraph recordedGraph(graphicsScope.arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        throwingTask,
        throwingTask,
        recordedGraph
    ));
    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    ASSERT_NE(recordingAttemptGeneration, 0u);

    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    transaction.reset(compiledGraph);
    bool recordingResult = false;
    bool queuedWriterObserved = false;
    bool resetResultWhileClosing = true;
    bool resetReturnedBeforeRecorderRelease = false;
    GpuTaskGraphSubmissionStatistics statisticsBeforeClosingReset;
    GpuTaskGraphSubmissionStatistics statisticsAfterClosingReset;
    AtomicFlag closingResetReturned;
    JoiningThreadGroup threads(releaseRecord);
    threads.addReleaseSignal(discardEntered);
    threads.addReleaseSignal(releaseDiscard);
    threads.start([&](){
        recordingResult = recorder.recordTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordingTask,
            recordingTask,
            recordedGraph
        );
    });
    const bool recorderParked = WaitForFlag(recordEntered);
    if(!recorderParked){
        threads.releaseAndJoin();
        ASSERT_TRUE(recorderParked);
        return;
    }

    threads.start([&](){
        while(!discardEntered.test(MemoryOrder::acquire))
            discardEntered.wait(false, MemoryOrder::acquire);
        transaction.rejectTask(graph, compiledGraph, probeTask, recordingAttemptGeneration);
        statisticsBeforeClosingReset = transaction.submissionStatistics();
        resetResultWhileClosing = transaction.tryReset(compiledGraph);
        statisticsAfterClosingReset = transaction.submissionStatistics();
        SignalFlagExactlyOnce(closingResetReturned);
    });
    threads.start([&](){
        queuedWriterObserved = GpuGraphSubmissionTransactionGateTestAccess::waitForWriterReservationCount(
            transaction,
            2u
        );
        ReleaseFlagIdempotently(releaseDiscard);
        resetReturnedBeforeRecorderRelease = WaitForFlag(closingResetReturned);
        ReleaseFlagIdempotently(releaseRecord);
    });

    EXPECT_THROW({
        const bool unexpectedDiscardResult = transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordingAttemptGeneration
        );
        EXPECT_TRUE(unexpectedDiscardResult);
    }, DiscardObserverSentinel);
    threads.releaseAndJoin();

    EXPECT_TRUE(recordingResult);
    EXPECT_TRUE(queuedWriterObserved);
    EXPECT_TRUE(resetReturnedBeforeRecorderRelease);
    EXPECT_FALSE(resetResultWhileClosing);
    EXPECT_EQ(statisticsAfterClosingReset.graphGeneration, statisticsBeforeClosingReset.graphGeneration);
    EXPECT_EQ(statisticsAfterClosingReset.planGeneration, statisticsBeforeClosingReset.planGeneration);
    EXPECT_EQ(
        statisticsAfterClosingReset.recordingAttemptGeneration,
        statisticsBeforeClosingReset.recordingAttemptGeneration
    );
    EXPECT_EQ(statisticsAfterClosingReset.acceptedPacketCount, statisticsBeforeClosingReset.acceptedPacketCount);
    EXPECT_EQ(statisticsAfterClosingReset.rejectedPacketCount, statisticsBeforeClosingReset.rejectedPacketCount);
    EXPECT_EQ(statisticsAfterClosingReset.nativeSubmissionCount, statisticsBeforeClosingReset.nativeSubmissionCount);
    EXPECT_EQ(throwingDiscardCount, 1u);
    EXPECT_EQ(probeDiscardCount, 0u);
    EXPECT_EQ(transaction.submissionStatistics().rejectedPacketCount, 3u);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
}


// Rejecting one packet must not partially publish transaction or graph state while that packet is still Recording.
// Once its exact recording claim completes, the same bound transaction can discard both unaccepted packets and all
// three owners become independently resettable.
TEST(TaskGraphRuntimeOwnershipTest, LiveRecordingRejectAndDiscardRemainFailureAtomicUntilClaimCompletes){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Live recording rejection: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    GpuTaskGraph graph(graphicsScope.arena());
    AtomicFlag recordingEntered;
    AtomicFlag releaseRecording;
    bool terminalTaskRecorded = false;
    u32 recordingTaskDiscardedCount = 0u;
    u32 terminalTaskDiscardedCount = 0u;
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest queue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskId recordingTask = graph.addTask<BlockingRecordTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/live_recording_reject"))
            .setMarkerLabel("Live Recording Reject")
            .setQueue(queue)
            .setScheduling(scheduling),
        BlockingRecordTask::Payload{
            .entered = &recordingEntered,
            .release = &releaseRecording,
            .discardedCount = &recordingTaskDiscardedCount,
        }
    );
    const GpuTaskId terminalTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/live_recording_terminal"))
            .setMarkerLabel("Live Recording Terminal")
            .setQueue(queue)
            .setScheduling(scheduling),
        RecordableTask::Payload{
            .recorded = &terminalTaskRecorded,
            .discardedCount = &terminalTaskDiscardedCount,
        }
    );
    ASSERT_TRUE(recordingTask.valid());
    ASSERT_TRUE(terminalTask.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/live_recording_reject_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId recordingPacket;
    GpuSubmissionPacketId terminalPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        recordingPacket = views.compiled.packetForTask(recordingTask);
        terminalPacket = views.compiled.packetForTask(terminalTask);
    }
    ASSERT_TRUE(recordingPacket.valid());
    ASSERT_TRUE(terminalPacket.valid());
    ASSERT_NE(recordingPacket, terminalPacket);

    GpuRecordedGraph recordedGraph(graphicsScope.arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        terminalTask,
        terminalTask,
        recordedGraph
    ));
    ASSERT_TRUE(terminalTaskRecorded);
    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    ASSERT_NE(recordingAttemptGeneration, 0u);

    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    transaction.reset(compiledGraph);
    bool recordingResult = false;
    JoiningThreadGroup threads(releaseRecording);
    threads.start([&](){
        recordingResult = recorder.recordTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordingTask,
            recordingTask,
            recordedGraph
        );
    });
    const bool recordingParked = WaitForFlag(recordingEntered);
    if(!recordingParked){
        threads.releaseAndJoin();
        ASSERT_TRUE(recordingParked);
        return;
    }

    const GpuTaskGraphSubmissionStatistics statisticsBeforeReject = transaction.submissionStatistics();
    transaction.rejectTask(graph, compiledGraph, recordingTask, recordingAttemptGeneration);
    const GpuTaskGraphSubmissionStatistics statisticsAfterReject = transaction.submissionStatistics();
    EXPECT_EQ(recordingTaskDiscardedCount, 0u);
    EXPECT_EQ(terminalTaskDiscardedCount, 0u);
    EXPECT_FALSE(transaction.packetToken(recordingPacket).valid());
    EXPECT_FALSE(transaction.packetToken(terminalPacket).valid());
    EXPECT_EQ(statisticsAfterReject.acceptedPacketCount, statisticsBeforeReject.acceptedPacketCount);
    EXPECT_EQ(statisticsAfterReject.rejectedPacketCount, statisticsBeforeReject.rejectedPacketCount);
    EXPECT_EQ(statisticsAfterReject.nativeSubmissionCount, statisticsBeforeReject.nativeSubmissionCount);
    EXPECT_FALSE(transaction.discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration));
    EXPECT_EQ(transaction.submissionStatistics().rejectedPacketCount, 0u);
    EXPECT_EQ(recordingTaskDiscardedCount, 0u);
    EXPECT_EQ(terminalTaskDiscardedCount, 0u);

    threads.releaseAndJoin();
    EXPECT_TRUE(recordingResult);
    EXPECT_TRUE(transaction.discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration));
    EXPECT_EQ(transaction.submissionStatistics().rejectedPacketCount, 2u);
    EXPECT_EQ(recordingTaskDiscardedCount, 1u);
    EXPECT_EQ(terminalTaskDiscardedCount, 1u);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
}


// Reset publishes an empty exact-plan artifact without requiring a timing recorder. Retired timed storage may reuse
// its ticket for the same recorder, rebuild it for a different recorder, and must clear it before a later untimed
// packet reuses that physical slot.
TEST(TaskGraphRuntimeOwnershipTest, TimedArtifactResetAndRecorderChangesPreserveExactTicketOwnership){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Timed artifact reuse: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    GpuTimingRecorder& primaryTiming = graphicsScope.graphics().gpuTiming();
    graphicsScope.setGpuTimingEnabled(true);
    Perf::TimingRecorder alternateTimingSink(graphicsScope.arena());
    alternateTimingSink.setEnabled(true);
    GpuTimingRecorder alternateTiming(graphicsScope.arena(), alternateTimingSink);
    alternateTiming.setQueryCollectionEnabled(true);

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest queue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskGraph timedGraph(graphicsScope.arena());
    const GpuTaskId timedTask = timedGraph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/timed_artifact_reuse"))
            .setMarkerLabel("Timed Artifact Reuse")
            .setQueue(queue)
            .setScheduling(scheduling)
            .setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::PacketOnly }),
        RecordableTask::Payload{}
    );
    ASSERT_TRUE(timedTask.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/timed_artifact_reuse_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(timedGraph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId timedPacket;
    {
        const GpuTaskGraphReadViews views(timedGraph, compiledGraph);
        ASSERT_TRUE(views.valid());
        timedPacket = views.compiled.packetForTask(timedTask);
        ASSERT_TRUE(timedPacket.valid());
        const GpuCompiledPacketView packet = views.compiled.packet(timedPacket);
        ASSERT_TRUE(packet.valid());
        ASSERT_TRUE(packet.plan->recordsTiming);
    }

    GpuRecordedGraph recordedGraph(graphicsScope.arena());
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    const auto discardRecordedAttempt = [&](GpuTaskGraph& graph){
        ASSERT_TRUE(transaction.tryReset(compiledGraph));
        ASSERT_TRUE(transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        ));
        ASSERT_TRUE(transaction.tryReset(compiledGraph));
    };

    const GpuNativePacketRecorder primaryRecorder(device, primaryTiming);
    ASSERT_TRUE(primaryRecorder.recordTaskRangeInCompileOrder(
        timedGraph,
        compiledGraph,
        timedTask,
        timedTask,
        recordedGraph
    ));
    discardRecordedAttempt(timedGraph);
    ASSERT_TRUE(recordedGraph.tryReset(compiledGraph));
    ASSERT_TRUE(primaryRecorder.recordTaskRangeInCompileOrder(
        timedGraph,
        compiledGraph,
        timedTask,
        timedTask,
        recordedGraph
    ));
    discardRecordedAttempt(timedGraph);
    ASSERT_TRUE(recordedGraph.tryReset(compiledGraph));

    const GpuNativePacketRecorder alternateRecorder(device, alternateTiming);
    ASSERT_TRUE(alternateRecorder.recordTaskRangeInCompileOrder(
        timedGraph,
        compiledGraph,
        timedTask,
        timedTask,
        recordedGraph
    ));
    discardRecordedAttempt(timedGraph);
    ASSERT_TRUE(recordedGraph.tryReset(compiledGraph));
    ASSERT_TRUE(timedGraph.tryReset());

    GpuTaskGraph untimedGraph(graphicsScope.arena());
    const GpuTaskId untimedTask = untimedGraph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/untimed_artifact_reuse"))
            .setMarkerLabel("Untimed Artifact Reuse")
            .setQueue(queue)
            .setScheduling(scheduling),
        RecordableTask::Payload{}
    );
    ASSERT_TRUE(untimedTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(untimedGraph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId untimedPacket;
    {
        const GpuTaskGraphReadViews views(untimedGraph, compiledGraph);
        ASSERT_TRUE(views.valid());
        untimedPacket = views.compiled.packetForTask(untimedTask);
        ASSERT_TRUE(untimedPacket.valid());
        const GpuCompiledPacketView packet = views.compiled.packet(untimedPacket);
        ASSERT_TRUE(packet.valid());
        ASSERT_FALSE(packet.plan->recordsTiming);
    }
    const GpuNativePacketRecorder untimedRecorder(device);
    ASSERT_TRUE(untimedRecorder.recordTaskRangeInCompileOrder(
        untimedGraph,
        compiledGraph,
        untimedTask,
        untimedTask,
        recordedGraph
    ));
    discardRecordedAttempt(untimedGraph);
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(untimedGraph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
    graphicsScope.setGpuTimingEnabled(false);
}


TEST(TaskGraphRuntimeOwnershipTest, TimedDuplicateAndAliasedScopesReserveAllOccurrencesAcrossDisjointRanges){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Timing scope occurrences: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "Timing scope occurrences: the graphics queue does not support timestamps.";

    Perf::TimingRecorder timingSink(graphicsScope.arena());
    timingSink.setEnabled(true);
    GpuTimingRecorder timing(graphicsScope.arena(), timingSink);
    timing.setQueryCollectionEnabled(true);
    GpuTaskGraph graph(graphicsScope.arena());
    const Name sharedIdentity("tests/task_graph/shared_timing_occurrence");
    const Name packetIdentity = GpuTaskPacketTimingScopeName(sharedIdentity);
    const Name taskIdentities[]{
        sharedIdentity, sharedIdentity, packetIdentity, packetIdentity,
        sharedIdentity, sharedIdentity, sharedIdentity,
    };
    const GpuTaskTimingPolicy::Enum timingPolicies[]{
        GpuTaskTimingPolicy::Task, GpuTaskTimingPolicy::Task,
        GpuTaskTimingPolicy::Task, GpuTaskTimingPolicy::Task,
        GpuTaskTimingPolicy::PacketOnly, GpuTaskTimingPolicy::None, GpuTaskTimingPolicy::None,
    };
    GpuTaskId tasks[7]{};
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest queue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        GpuTaskDesc desc;
        desc
            .setIdentity(taskIdentities[taskIndex])
            .setMarkerLabel("Shared Timing Occurrence")
            .setQueue(queue)
            .setScheduling(scheduling)
            .setTimingMetadata(GpuTaskTimingMetadata{ .policy = timingPolicies[taskIndex] })
        ;
        if(taskIndex != 0u)
            desc.setDependencies(&tasks[taskIndex - 1u], 1u);
        tasks[taskIndex] = graph.addTask<RecordableTask>(desc, RecordableTask::Payload{});
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/shared_timing_occurrences_compile"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions options;
    options.packetTimingEnvelope.firstTask = tasks[4u];
    options.packetTimingEnvelope.lastTask = tasks[5u];
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch,
            options
        ));
    }
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), LengthOf(tasks));
    }

    GpuRecordedGraph recordedGraph(graphicsScope.arena());
    const GpuNativePacketRecorder recorder(device, timing);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(graph, compiledGraph, tasks[0u], tasks[0u], recordedGraph));
    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    ASSERT_NE(recordingAttemptGeneration, 0u);
    const GpuTimingRecorderStatistics prefixStatistics = timing.statistics(device);
    // Four task scopes and six packet scopes share three identities. Preparation must include the still-unrecorded
    // suffix, the packet-only policy, and the envelope-only packet, while excluding the final untimed packet.
    EXPECT_EQ(prefixStatistics.preparedScopeCount, 3u);
    ASSERT_EQ(timingSink.scopeCount(), 3u);
    EXPECT_EQ(timingSink.scopeNameAt(0u), packetIdentity);
    EXPECT_EQ(timingSink.scopeNameAt(1u), sharedIdentity);
    EXPECT_EQ(timingSink.scopeNameAt(2u), GpuTaskPacketTimingScopeName(packetIdentity));
    EXPECT_EQ(prefixStatistics.requestedQueryCount, 10u * s_MaxFramesInFlight);
    EXPECT_EQ(prefixStatistics.materializedQueryCount, prefixStatistics.requestedQueryCount);
    EXPECT_EQ(prefixStatistics.recordedScopeCount, 2u);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(graph, compiledGraph, tasks[1u], tasks[6u], recordedGraph));
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), recordingAttemptGeneration);
    const GpuTimingRecorderStatistics recordedStatistics = timing.statistics(device);
    EXPECT_EQ(recordedStatistics.preparedScopeCount, prefixStatistics.preparedScopeCount);
    EXPECT_EQ(recordedStatistics.requestedQueryCount, prefixStatistics.requestedQueryCount);
    EXPECT_EQ(recordedStatistics.materializedQueryCount, prefixStatistics.materializedQueryCount);
    EXPECT_EQ(recordedStatistics.queryMaterializationFailureCount, 0u);
    EXPECT_EQ(recordedStatistics.scopeAttemptCount, 10u);
    EXPECT_EQ(recordedStatistics.recordedScopeCount, 10u);
    EXPECT_EQ(recordedStatistics.beginFailureCount, 0u);

    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    ASSERT_TRUE(transaction.discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration));
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    EXPECT_EQ(timing.statistics(device).discardedScopeCount, 10u);
    EXPECT_TRUE(graph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
    timing.setQueryCollectionEnabled(false);
}

// An untimed plan has no timing-recorder identity. Independent recorder facades may therefore complete disjoint
// ranges of the same attempt without a meaningless pointer mismatch.
TEST(TaskGraphRuntimeOwnershipTest, UntimedDisjointRangesShareOneAttemptAcrossRecorderFacades){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Untimed recorder identity: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    GpuTimingRecorder& timingRecorder = graphicsScope.graphics().gpuTiming();
    const GpuQueueRequest queue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    GpuTaskGraph graph(graphicsScope.arena());
    const GpuTaskId firstTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/untimed_mixed_recorder_first"))
            .setMarkerLabel("Untimed Mixed Recorder First")
            .setQueue(queue)
            .setScheduling(scheduling),
        RecordableTask::Payload{}
    );
    const GpuTaskId secondTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/untimed_mixed_recorder_second"))
            .setMarkerLabel("Untimed Mixed Recorder Second")
            .setQueue(queue)
            .setScheduling(scheduling),
        RecordableTask::Payload{}
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/untimed_mixed_recorder_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
        const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
        ASSERT_NE(firstPacket, secondPacket);
        const GpuCompiledPacketView firstPacketView = views.compiled.packet(firstPacket);
        const GpuCompiledPacketView secondPacketView = views.compiled.packet(secondPacket);
        ASSERT_TRUE(firstPacketView.valid());
        ASSERT_TRUE(secondPacketView.valid());
        ASSERT_FALSE(firstPacketView.plan->recordsTiming);
        ASSERT_FALSE(secondPacketView.plan->recordsTiming);
    }

    GpuRecordedGraph recordedGraph(graphicsScope.arena());
    const GpuNativePacketRecorder timingAwareRecorder(device, timingRecorder);
    const GpuNativePacketRecorder legacyRecorder(device);
    ASSERT_TRUE(timingAwareRecorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        firstTask,
        firstTask,
        recordedGraph
    ));
    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    ASSERT_NE(recordingAttemptGeneration, 0u);
    ASSERT_TRUE(legacyRecorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        secondTask,
        secondTask,
        recordedGraph
    ));
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), recordingAttemptGeneration);

    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    ASSERT_TRUE(transaction.discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration));
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
}


// A paused production writer reservation must bar tryReset without perturbing the transaction. Once that reservation
// releases, a separate WaitExclusive operation proves normal writer claim/release and leaves the gate fully empty.
TEST(TaskGraphRuntimeOwnershipTest, TrySubmissionWriterDoesNotOvertakePendingBlockingWriter){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Submission writer ordering: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    GpuTaskGraph graph(graphicsScope.arena());
    const GpuTaskId task = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/pending_submission_writer"))
            .setMarkerLabel("Pending Submission Writer")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            }),
        RecordableTask::Payload{}
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/pending_submission_writer_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }

    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmissionStatistics originalStatistics = transaction.submissionStatistics();
    AtomicFlag releaseReader;
    AtomicFlag readerEntered;
    AtomicFlag releaseReservation;
    AtomicFlag reservationEntered;
    AtomicFlag releaseWriter;
    AtomicFlag writerClaimed;
    bool readerValid = false;
    bool reservationValid = false;
    bool writerValid = false;
    JoiningThreadGroup threads(releaseReader);
    threads.addReleaseSignal(releaseReservation);
    threads.addReleaseSignal(releaseWriter);
    const usize readerThreadIndex = threads.start([&](){
        GpuGraphSubmissionTransactionGateTestAccess::withOrdinaryOperation(
            transaction,
            [&](const bool valid){
                readerValid = valid;
                SignalFlagExactlyOnce(readerEntered);
                while(!releaseReader.test(MemoryOrder::acquire))
                    releaseReader.wait(false, MemoryOrder::acquire);
            }
        );
    });
    if(!WaitForFlag(readerEntered)){
        threads.releaseAndJoin();
        ASSERT_TRUE(readerEntered.test(MemoryOrder::acquire));
        return;
    }

    const usize writerThreadIndex = threads.start([&](){
        GpuGraphSubmissionTransactionGateTestAccess::withPendingWriterReservation(
            transaction,
            [&](){
                reservationValid = true;
                SignalFlagExactlyOnce(reservationEntered);
                while(!releaseReservation.test(MemoryOrder::acquire))
                    releaseReservation.wait(false, MemoryOrder::acquire);
            }
        );
        GpuGraphSubmissionTransactionGateTestAccess::withWaitExclusiveOperation(
            transaction,
            [&](const bool valid){
                writerValid = valid;
                SignalFlagExactlyOnce(writerClaimed);
                while(!releaseWriter.test(MemoryOrder::acquire))
                    releaseWriter.wait(false, MemoryOrder::acquire);
            }
        );
    });
    if(!WaitForFlag(reservationEntered)){
        threads.releaseAndJoin();
        ASSERT_TRUE(reservationEntered.test(MemoryOrder::acquire));
        return;
    }

    ReleaseFlagIdempotently(releaseReader);
    threads.join(readerThreadIndex);
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot pendingWriterState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_TRUE(readerValid);
    EXPECT_TRUE(reservationValid);
    EXPECT_EQ(pendingWriterState.readerCount, 0u);
    EXPECT_EQ(pendingWriterState.writerReservationCount, 1u);
    EXPECT_FALSE(pendingWriterState.writerActive);
    EXPECT_FALSE(transaction.tryReset(compiledGraph));
    EXPECT_EQ(transaction.submissionStatistics().graphGeneration, originalStatistics.graphGeneration);
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot afterTryState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_EQ(afterTryState.readerCount, 0u);
    EXPECT_EQ(afterTryState.writerReservationCount, 1u);
    EXPECT_FALSE(afterTryState.writerActive);

    ReleaseFlagIdempotently(releaseReservation);
    const bool writerClaimedAfterReservation = WaitForFlag(writerClaimed);
    if(!writerClaimedAfterReservation){
        threads.releaseAndJoin();
        EXPECT_TRUE(writerClaimedAfterReservation);
        return;
    }
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot activeWriterState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_TRUE(writerValid);
    EXPECT_TRUE(activeWriterState.writerActive);
    EXPECT_EQ(activeWriterState.writerReservationCount, 1u);

    ReleaseFlagIdempotently(releaseWriter);
    threads.join(writerThreadIndex);
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot releasedState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_EQ(releasedState.readerCount, 0u);
    EXPECT_EQ(releasedState.writerReservationCount, 0u);
    EXPECT_FALSE(releasedState.writerActive);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
}


// Independent readers share the gate. Once a blocking writer reserves ownership, a later reader waits instead of
// overtaking it; draining the original readers lets the writer claim/release before that late reader can enter.
TEST(TaskGraphRuntimeOwnershipTest, ConcurrentSubmissionReadersYieldToPendingWriterWithoutLeakingAdmission){
    Alloc::GlobalArena arena(Name("tests/task_graph/submission_gate_reader_writer"));
    GpuGraphSubmissionTransaction transaction(arena);
    AtomicFlag releaseReaders;
    AtomicFlag firstReaderEntered;
    AtomicFlag secondReaderEntered;
    AtomicFlag releaseWriter;
    AtomicFlag writerClaimed;
    AtomicFlag lateReaderStarted;
    AtomicFlag lateReaderEntered;
    bool firstReaderValid = false;
    bool secondReaderValid = false;
    bool writerValid = false;
    bool lateReaderValid = true;
    JoiningThreadGroup threads(releaseReaders);
    threads.addReleaseSignal(releaseWriter);
    const usize firstReaderThread = threads.start([&](){
        GpuGraphSubmissionTransactionGateTestAccess::withOrdinaryOperation(
            transaction,
            [&](const bool valid){
                firstReaderValid = valid;
                SignalFlagExactlyOnce(firstReaderEntered);
                while(!releaseReaders.test(MemoryOrder::acquire))
                    releaseReaders.wait(false, MemoryOrder::acquire);
            }
        );
    });
    const usize secondReaderThread = threads.start([&](){
        GpuGraphSubmissionTransactionGateTestAccess::withOrdinaryOperation(
            transaction,
            [&](const bool valid){
                secondReaderValid = valid;
                SignalFlagExactlyOnce(secondReaderEntered);
                while(!releaseReaders.test(MemoryOrder::acquire))
                    releaseReaders.wait(false, MemoryOrder::acquire);
            }
        );
    });
    const bool bothReadersEntered = WaitForFlag(firstReaderEntered) && WaitForFlag(secondReaderEntered);
    if(!bothReadersEntered){
        threads.releaseAndJoin();
        EXPECT_TRUE(bothReadersEntered);
        return;
    }
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot concurrentReaderState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_EQ(concurrentReaderState.readerCount, 2u);
    EXPECT_EQ(concurrentReaderState.writerReservationCount, 0u);
    EXPECT_FALSE(concurrentReaderState.writerActive);

    const usize writerThread = threads.start([&](){
        GpuGraphSubmissionTransactionGateTestAccess::withWaitExclusiveOperation(
            transaction,
            [&](const bool valid){
                writerValid = valid;
                SignalFlagExactlyOnce(writerClaimed);
                while(!releaseWriter.test(MemoryOrder::acquire))
                    releaseWriter.wait(false, MemoryOrder::acquire);
            }
        );
    });
    const bool writerReserved = GpuGraphSubmissionTransactionGateTestAccess::waitForWriterReservationCount(
        transaction,
        1u
    );
    if(!writerReserved){
        threads.releaseAndJoin();
        EXPECT_TRUE(writerReserved);
        return;
    }

    const usize lateReaderThread = threads.start([&](){
        SignalFlagExactlyOnce(lateReaderStarted);
        GpuGraphSubmissionTransactionGateTestAccess::withOrdinaryOperation(
            transaction,
            [&](const bool valid){
                lateReaderValid = valid;
                SignalFlagExactlyOnce(lateReaderEntered);
            }
        );
    });
    const bool lateReaderStartedWhileWriterPending = WaitForFlag(lateReaderStarted);
    if(!lateReaderStartedWhileWriterPending){
        threads.releaseAndJoin();
        EXPECT_TRUE(lateReaderStartedWhileWriterPending);
        return;
    }
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot pendingWriterState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_EQ(pendingWriterState.readerCount, 2u);
    EXPECT_EQ(pendingWriterState.writerReservationCount, 1u);
    EXPECT_FALSE(pendingWriterState.writerActive);

    ReleaseFlagIdempotently(releaseReaders);
    threads.join(firstReaderThread);
    threads.join(secondReaderThread);
    const bool writerClaimedAfterReaders = WaitForFlag(writerClaimed);
    if(!writerClaimedAfterReaders){
        threads.releaseAndJoin();
        EXPECT_TRUE(writerClaimedAfterReaders);
        return;
    }
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot activeWriterState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_TRUE(firstReaderValid);
    EXPECT_TRUE(secondReaderValid);
    EXPECT_TRUE(writerValid);
    EXPECT_EQ(activeWriterState.readerCount, 0u);
    EXPECT_EQ(activeWriterState.writerReservationCount, 1u);
    EXPECT_TRUE(activeWriterState.writerActive);
    EXPECT_FALSE(lateReaderEntered.test(MemoryOrder::acquire));

    ReleaseFlagIdempotently(releaseWriter);
    threads.join(writerThread);
    threads.join(lateReaderThread);
    EXPECT_TRUE(lateReaderEntered.test(MemoryOrder::acquire));
    EXPECT_TRUE(lateReaderValid);
    const GpuGraphSubmissionTransactionGateTestAccess::Snapshot releasedState =
        GpuGraphSubmissionTransactionGateTestAccess::snapshot(transaction)
    ;
    EXPECT_EQ(releasedState.readerCount, 0u);
    EXPECT_EQ(releasedState.writerReservationCount, 0u);
    EXPECT_FALSE(releasedState.writerActive);
}


// Borrowed artifact admission is an exact lexical capability: the owning token succeeds, a nested wrapper cannot
// escalate, that owner remains valid through same-artifact nesting, and another thread cannot reuse its address.
TEST(TaskGraphRuntimeOwnershipTest, SubmissionBorrowRequiresExactOwningArtifactScopeMembership){
    Alloc::GlobalArena arena(Name("tests/task_graph/submission_artifact_membership"));
    GpuRecordedGraph recordedGraph(arena);
    GpuGraphSubmissionTransaction transaction(arena);
    const GpuGraphSubmissionTransactionGateTestAccess::ArtifactMembershipResults results =
        GpuGraphSubmissionTransactionGateTestAccess::exerciseArtifactMembership(recordedGraph, transaction)
    ;
    EXPECT_TRUE(results.artifactOwnerValid);
    EXPECT_TRUE(results.ownerBorrowAccepted);
    EXPECT_TRUE(results.nestedArtifactValid);
    EXPECT_FALSE(results.nestedBorrowAccepted);
    EXPECT_TRUE(results.ownerThroughNestedAccepted);
    EXPECT_FALSE(results.foreignThreadBorrowAccepted);
    const auto expectEmptyGate = [](const GpuGraphSubmissionTransactionGateTestAccess::Snapshot& snapshot){
        EXPECT_EQ(snapshot.readerCount, 0u);
        EXPECT_EQ(snapshot.writerReservationCount, 0u);
        EXPECT_FALSE(snapshot.writerActive);
    };
    expectEmptyGate(results.afterOwner);
    expectEmptyGate(results.afterNested);
    expectEmptyGate(results.afterOwnerThroughNested);
    expectEmptyGate(results.afterForeignThread);
}


// The plan gate admits independent readers concurrently and never waits in a reset/recompile writer. A rejected
// writer must leave the old publication and every existing reader claim intact; releasing the final reader then
// allows the next compile to publish a distinct plan generation.
TEST(TaskGraphRuntimeOwnershipTest, ConcurrentPlanReadersRejectWritersWithoutPoisoningPublication){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Compiled-plan admission: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    GpuTaskGraph graph(graphicsScope.arena());
    bool recorded = false;
    const GpuTaskId task = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/concurrent_plan_readers"))
            .setMarkerLabel("Concurrent Plan Readers")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            }),
        RecordableTask::Payload{ .recorded = &recorded }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/concurrent_plan_readers_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    u64 originalPlanGeneration = 0u;
    GpuSubmissionPacketId originalPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        originalPlanGeneration = views.compiled.planGeneration();
        originalPacket = views.compiled.packetForTask(task);
    }
    ASSERT_NE(originalPlanGeneration, 0u);
    ASSERT_TRUE(originalPacket.valid());

    AtomicFlag releaseReaders;
    AtomicFlag firstReaderEntered;
    AtomicFlag secondReaderEntered;
    bool firstReaderValid = false;
    bool secondReaderValid = false;
    JoiningThreadGroup threads(releaseReaders);
    threads.start([&](){
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        firstReaderValid = views.valid();
        SignalFlagExactlyOnce(firstReaderEntered);
        while(!releaseReaders.test(MemoryOrder::acquire))
            releaseReaders.wait(false, MemoryOrder::acquire);
    });
    threads.start([&](){
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        secondReaderValid = views.valid();
        SignalFlagExactlyOnce(secondReaderEntered);
        while(!releaseReaders.test(MemoryOrder::acquire))
            releaseReaders.wait(false, MemoryOrder::acquire);
    });
    const bool firstReaderEnteredBeforeWriter = WaitForFlag(firstReaderEntered);
    const bool secondReaderEnteredBeforeWriter = WaitForFlag(secondReaderEntered);
    if(!firstReaderEnteredBeforeWriter || !secondReaderEnteredBeforeWriter){
        threads.releaseAndJoin();
        ASSERT_TRUE(firstReaderEnteredBeforeWriter);
        ASSERT_TRUE(secondReaderEnteredBeforeWriter);
        return;
    }

    bool resetResult = true;
    AtomicFlag resetReturned;
    const usize resetThreadIndex = threads.start([&](){
        resetResult = compiledGraph.tryReset();
        SignalFlagExactlyOnce(resetReturned);
    });
    const bool resetReturnedBeforeReaderRelease = WaitForFlag(resetReturned);
    if(!resetReturnedBeforeReaderRelease){
        threads.releaseAndJoin();
        EXPECT_TRUE(resetReturnedBeforeReaderRelease);
        return;
    }
    threads.join(resetThreadIndex);

    EXPECT_TRUE(firstReaderValid);
    EXPECT_TRUE(secondReaderValid);
    EXPECT_FALSE(resetResult);
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    EXPECT_EQ(analysis.diagnostic().status, GpuTaskGraphAnalysisStatus::OutputPlanInUse);

    threads.releaseAndJoin();
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_EQ(views.compiled.planGeneration(), originalPlanGeneration);
        EXPECT_EQ(views.compiled.packetForTask(task), originalPacket);
    }

    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_NE(views.compiled.planGeneration(), originalPlanGeneration);
    }
    EXPECT_TRUE(graph.tryReset());
}


// Two independent timed packets share one ready frontier. An explicit false result must relinquish its timing
// reservation without revoking the successfully published peer, and both cached slots must remain reusable.
TEST(TaskGraphRuntimeOwnershipTest, TimedReadyFrontierFalseResultPreservesPeerAndReusesTicketReservations){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope graphicsScope;
    if(!graphicsScope.initialize())
        GTEST_SKIP() << "Timed ready-frontier recovery: no usable headless Vulkan device on this host.";

    GraphicsBackend::Device& device = graphicsScope.graphics().getDevice();
    graphicsScope.setGpuTimingEnabled(true);
    bool failedPeerShouldRecord = false;
    u32 successfulPeerDiscardedCount = 0u;
    u32 failedPeerDiscardedCount = 0u;
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;
    const GpuQueueRequest queue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskTimingMetadata timing{ .policy = GpuTaskTimingPolicy::PacketOnly };
    GpuTaskGraph graph(graphicsScope.arena());
    const GpuTaskId successfulTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/timed_ready_frontier_success"))
            .setMarkerLabel("Timed Ready Frontier Success")
            .setQueue(queue)
            .setScheduling(scheduling)
            .setTimingMetadata(timing),
        RecordableTask::Payload{ .discardedCount = &successfulPeerDiscardedCount }
    );
    const GpuTaskId failedTask = graph.addTask<RecordableTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/timed_ready_frontier_retry"))
            .setMarkerLabel("Timed Ready Frontier Retry")
            .setQueue(queue)
            .setScheduling(scheduling)
            .setTimingMetadata(timing),
        RecordableTask::Payload{
            .discardedCount = &failedPeerDiscardedCount,
            .shouldRecord = &failedPeerShouldRecord,
        }
    );
    ASSERT_TRUE(successfulTask.valid());
    ASSERT_TRUE(failedTask.valid());

    GpuTaskGraphAnalysis analysis(graphicsScope.arena());
    GpuTaskGraphQueueAssignments assignments(graphicsScope.arena());
    GpuCompiledGraph compiledGraph(graphicsScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/task_graph/timed_ready_frontier_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.compile(
            declarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId successfulPacket;
    GpuSubmissionPacketId failedPacketExpected;
    GpuSubmissionPacketRange allPackets;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        successfulPacket = views.compiled.packetForTask(successfulTask);
        failedPacketExpected = views.compiled.packetForTask(failedTask);
        allPackets = views.compiled.allPacketRange();
        ASSERT_TRUE(successfulPacket.valid());
        ASSERT_TRUE(failedPacketExpected.valid());
        ASSERT_NE(successfulPacket, failedPacketExpected);
        const GpuCompiledPacketView successfulPacketView = views.compiled.packet(successfulPacket);
        const GpuCompiledPacketView failedPacketView = views.compiled.packet(failedPacketExpected);
        ASSERT_TRUE(successfulPacketView.valid());
        ASSERT_TRUE(failedPacketView.valid());
        ASSERT_TRUE(successfulPacketView.plan->recordsTiming);
        ASSERT_TRUE(failedPacketView.plan->recordsTiming);
        ASSERT_EQ(successfulPacketView.plan->recordingFrontier, 0u);
        ASSERT_EQ(failedPacketView.plan->recordingFrontier, 0u);
    }

    CpuTaskScheduler recordingWorkers(1u);
    GpuRecordedGraph recordedGraph(graphicsScope.arena());
    const GpuNativePacketRecorder recorder(device, graphicsScope.graphics().gpuTiming());
    GpuSubmissionPacketId failedPacket;
    EXPECT_FALSE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        allPackets,
        recordedGraph,
        recordingWorkers,
        &failedPacket
    ));
    EXPECT_EQ(failedPacket, failedPacketExpected);
    EXPECT_TRUE(recordedGraph.packetSnapshot(successfulPacket).has_value());
    EXPECT_FALSE(recordedGraph.packetSnapshot(failedPacketExpected).has_value());
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        const GpuTaskGraphRecordingStatistics partialStatistics = recordedGraph.recordingStatistics(
            compiledGraph,
            views.compiled
        );
        ASSERT_TRUE(partialStatistics.valid());
        EXPECT_EQ(partialStatistics.packetCount, 1u);
    }

    GpuGraphSubmissionTransaction transaction(graphicsScope.arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    ASSERT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_EQ(successfulPeerDiscardedCount, 1u);
    EXPECT_EQ(failedPeerDiscardedCount, 1u);
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    ASSERT_TRUE(recordedGraph.tryReset(compiledGraph));

    failedPeerShouldRecord = true;
    failedPacket = {};
    EXPECT_TRUE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        allPackets,
        recordedGraph,
        recordingWorkers,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_TRUE(recordedGraph.packetSnapshot(successfulPacket).has_value());
    EXPECT_TRUE(recordedGraph.packetSnapshot(failedPacketExpected).has_value());
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        const GpuTaskGraphRecordingStatistics retryStatistics = recordedGraph.recordingStatistics(
            compiledGraph,
            views.compiled
        );
        ASSERT_TRUE(retryStatistics.valid());
        EXPECT_EQ(retryStatistics.packetCount, 2u);
    }

    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    ASSERT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_EQ(successfulPeerDiscardedCount, 2u);
    EXPECT_EQ(failedPeerDiscardedCount, 2u);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
    graphicsScope.setGpuTimingEnabled(false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

