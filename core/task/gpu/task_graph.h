// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_contract.h"
#include "task_desc.h"
#include "task_graph_declaration_view.h"
#include "task_graph_nodes.h"
#include "task_graph_resource_import_index.h"
#include "task_graph_packet_claims.h"
#include "task_graph_submission_binding.h"

#include <core/alloc/scratch.h>

#include <global/arena_object.h>
#include <global/binary.h>
#include <global/sync.h>
#include <global/termination.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuCompiledBarrier;

class GpuNativePacketRecorder;
class GpuGraphSubmissionTransaction;
class GpuRecordedGraph;
class GpuTaskScheduler;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{
    struct CompilerGraphAccess;
};


class GpuTaskGraph final : NoCopy{
    friend class GpuTaskGraphDeclarationReadView;
    friend struct GpuTaskGraphCompilerDetail::CompilerGraphAccess;
    friend class GpuRecordingAttemptScope;
    friend class GpuPacketRecordingLease;
    friend class GpuTaskGraphCompiler;
    friend class GpuTaskGraphQueueAssignmentTelemetryTracker;
    friend class GpuNativePacketRecorder;
    friend class GpuGraphSubmissionTransaction;
    friend class GpuRecordedGraph;
    friend class GpuTaskScheduler;


public:
    using DeclarationReadView = GpuTaskGraphDeclarationReadView;

    class PayloadLifetimeScope final : NoCopy{
    public:
        explicit PayloadLifetimeScope(GpuTaskGraph& graph)noexcept
            : m_graph(graph)
        {}
        ~PayloadLifetimeScope()noexcept(false);


    private:
        GpuTaskGraph& m_graph;
    };


private:
    struct RecordTaskFailure final{
        b8 recordThunkInvoked;
    };

    class RecordThunkClaimGuard;
    class RecordingAttemptResolutionGuard;

    template<typename PayloadT>
    using ProvisionalPayloadOwner = GpuTaskGraphDetail::ProvisionalPayloadOwner<PayloadT>;
    using TaskLifecycleState = GpuTaskGraphDetail::TaskLifecycleState;
    using SubmissionBindingState = GpuTaskGraphDetail::SubmissionBindingState;

    class DeclarationMutationScope final : NoCopy{
    public:
        explicit DeclarationMutationScope(GpuTaskGraph& graph)noexcept;
        ~DeclarationMutationScope()noexcept;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return m_valid; }

        [[nodiscard]] b8 validFor(const GpuTaskGraph& graph)const noexcept{ return m_valid && &m_graph == &graph; }


    private:
        void acquireSlow(u32 threadId, u64 state)noexcept;
        void releaseSlow()noexcept;


    private:
        GpuTaskGraph& m_graph;
        b8 m_valid = false;
        b8 m_isRootWriter = false;
    };

    class DiscardNotificationScope final : NoCopy{
    public:
        explicit DiscardNotificationScope(const GpuTaskGraph& graph)noexcept;
        ~DiscardNotificationScope()noexcept;


    public:
        void complete()noexcept;
        void activateWithinLock()noexcept;
        void adoptPacketRecordingClaimWithinLock()noexcept;


    private:
        const GpuTaskGraph& m_graph;
        b8 m_active = false;
        b8 m_ownsPacketRecordingClaim = false;
    };

    class TaskPayloadDestroyScope final : NoCopy{
    public:
        explicit TaskPayloadDestroyScope(GpuTaskGraph& graph)noexcept;
        ~TaskPayloadDestroyScope()noexcept;


    public:
        void activateWithinLock()noexcept;


    private:
        GpuTaskGraph& m_graph;
        b8 m_active = false;
    };

    class ResetCompletionScope final : NoCopy{
    public:
        explicit ResetCompletionScope(GpuTaskGraph& graph)noexcept;
        ~ResetCompletionScope()noexcept;


    public:
        void activateWithinLock()noexcept;
        void markPayloadsDestroyed()noexcept{ m_payloadsAlreadyDestroyed = true; }
        void complete()noexcept;


    private:
        GpuTaskGraph& m_graph;
        b8 m_active = false;
        b8 m_payloadsAlreadyDestroyed = false;
    };


private:
    template<typename KeyT>
    struct TypedPointerHasher{
        [[nodiscard]] usize operator()(const KeyT& key)const noexcept{
            usize hash = Hasher<const void*>{}(key.pointer);
            HashCombine(hash, static_cast<u8>(key.type));
            return hash;
        }
    };

    using GpuTaskNode = GpuTaskGraphDetail::GpuTaskNode;
    using GpuGraphResourceNode = GpuTaskGraphDetail::GpuGraphResourceNode;
    using ResourcePointerKey = GpuTaskGraphDetail::ResourcePointerKey;
    using ResourceImportMatch = GpuTaskGraphDetail::ResourceImportMatch;
    using ResourceBinding = GpuTaskGraphDetail::ResourceBinding;
    using GpuGraphResourceVersionNode = GpuTaskGraphDetail::GpuGraphResourceVersionNode;
    using GpuGraphResourceSetNode = GpuTaskGraphDetail::GpuGraphResourceSetNode;
    using GpuGraphPipelineNode = GpuTaskGraphDetail::GpuGraphPipelineNode;
    using PipelinePointerKey = GpuTaskGraphDetail::PipelinePointerKey;
    using PipelinePointerHasher = TypedPointerHasher<PipelinePointerKey>;
    using PipelinePointerEqual = GpuTaskGraphDetail::PipelinePointerEqual;
    using PipelineImportMatch = GpuTaskGraphDetail::PipelineImportMatch;
    using PipelineBinding = GpuTaskGraphDetail::PipelineBinding;
    using GpuExternalCompletionNode = GpuTaskGraphDetail::GpuExternalCompletionNode;
    using GpuUploadBlobStorageBlock = GpuTaskGraphDetail::GpuUploadBlobStorageBlock;
    using GpuUploadBlobNode = GpuTaskGraphDetail::GpuUploadBlobNode;
    using MarkerLabelRange = GpuTaskGraphDetail::MarkerLabelRange;
    struct TaskPayloadPoolBlock{
        u8* data = nullptr;
        usize size = 0u;
        usize used = 0u;
    };
    using ImportIdentityIndex = HashMap<NameHash, u32, GraphicsArena>;
    using PipelinePointerIndex = HashMap<PipelinePointerKey, u32, GraphicsArena, PipelinePointerHasher, PipelinePointerEqual>;


private:
    static constexpr usize s_InlineImportIndexCount = 32u;
    static constexpr usize s_InitialPointerIndexBucketCount = 2u;
    static constexpr u32 s_InvalidImportIndex = Limit<u32>::s_Max;
    static constexpr usize s_PayloadPoolBlockBytes = 4096u;
    static constexpr usize s_PayloadPoolMaxPayloadBytes = 4096u;
    static constexpr usize s_PayloadPoolMaxAlign = 64u;
    static inline constexpr Name s_DeclarationScratchArena{ "core/task/gpu/declaration_scratch" };


private:
    [[nodiscard]] static u64 AllocateGeneration()noexcept;
    [[nodiscard]] static u64 AllocateGenerationBlock(u64 count)noexcept;
    [[nodiscard]] static PipelinePointerKey MakePipelinePointerKey(const GpuGraphPipelineNode& pipeline)noexcept;

    [[nodiscard]] static Expected<GpuCompiledPacketView> ResolvePacketView(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet)noexcept;
    [[nodiscard]] static Expected<GpuCompiledPacketView> ResolveLeasedPacketView(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, const GpuPacketRecordingLease& lease)noexcept;
    [[nodiscard]] static Expected<GpuCompiledPacketView> ResolveAttemptPacketView(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        b8 leaseValid
    )noexcept;

    template<typename TaskT>
    static b8 RecordTaskPayload(const void* const payload, CommandList& commandList, const GpuTaskRecordContext& context)noexcept(IsSame_V<decltype(TaskT::Record(*static_cast<const typename TaskT::Payload*>(payload), commandList, context)), b8>
        && noexcept(TaskT::Record(*static_cast<const typename TaskT::Payload*>(payload), commandList, context))){
        using Payload = typename TaskT::Payload;
        return TaskT::Record(*MakeNotNull(static_cast<const Payload*>(payload)), commandList, context);
    }

    template<typename TaskT>
    static void AcceptTaskPayload(void* const payload, const QueueSubmissionToken& token)noexcept(noexcept(TaskT::Accepted(*static_cast<typename TaskT::Payload*>(payload), token))){
        using Payload = typename TaskT::Payload;
        TaskT::Accepted(*MakeNotNull(static_cast<Payload*>(payload)), token);
    }

    template<typename TaskT>
    static void DiscardTaskPayload(void* const payload)noexcept(noexcept(TaskT::Discarded(*static_cast<typename TaskT::Payload*>(payload)))){
        using Payload = typename TaskT::Payload;
        TaskT::Discarded(*MakeNotNull(static_cast<Payload*>(payload)));
    }

    template<typename PayloadT>
    static void DestroyTaskPayload(GraphicsArena& arena, void* payload)noexcept{
        static_assert(noexcept(DestroyArenaObjectNoexcept(arena, static_cast<PayloadT*>(payload))));
        DestroyArenaObjectNoexcept(arena, static_cast<PayloadT*>(payload));
    }

    template<typename PayloadT>
    static void DestroyPooledTaskPayload(GraphicsArena&, void* payload)noexcept{
        static_assert(IsNothrowDestructible_V<PayloadT>);
        static_cast<PayloadT*>(payload)->~PayloadT();
    }

    [[nodiscard]] void* allocatePooledTaskPayload(const usize byteSize, const usize alignment){
        if(m_payloadPoolCurrentBlock < m_payloadPoolBlocks.size()){
            TaskPayloadPoolBlock& block = m_payloadPoolBlocks[m_payloadPoolCurrentBlock];
            const usize alignMask = alignment - 1u;
            const usize alignedOffset = (block.used + alignMask) & ~alignMask;
            if(alignedOffset <= block.size && byteSize <= block.size - alignedOffset){
                block.used = alignedOffset + byteSize;
                return block.data + alignedOffset;
            }
        }
        return allocatePooledTaskPayloadSlow(byteSize, alignment);
    }
    [[nodiscard]] void* allocatePooledTaskPayloadSlow(usize byteSize, usize alignment);
    void rewindTaskPayloadPool()noexcept;


public:
    explicit GpuTaskGraph(GraphicsArena& arena)
        : m_arena(arena)
        , m_declarationScratch(s_DeclarationScratchArena)
        , m_tasks(arena)
        , m_dependencies(arena)
        , m_externalDependencies(arena)
        , m_externalStateSources(arena)
        , m_externalStateSnapshots(arena)
        , m_resourceUses(arena)
        , m_resourceVersionUses(arena)
        , m_resources(arena)
        , m_resourceImportIndexes(arena)
        , m_resourceVersions(arena)
        , m_initialOwnerHandoffSources(arena)
        , m_initialOwnerHandoffStateSnapshots(arena)
        , m_reusableStateSnapshots(arena)
        , m_queueFamilyIndices(arena)
        , m_resourceSets(arena)
        , m_resourceSetMembers(arena)
        , m_pipelines(arena)
        , m_externalCompletions(arena)
        , m_uploadBlobs(arena)
        , m_uploadBlobStorage(arena)
        , m_overalignedUploadBlobs(arena)
        , m_markerText(arena)
        , m_payloadPoolBlocks(arena)
        , m_generation(AllocateGeneration())
        , m_declarationRevision(AllocateGeneration())
        , m_activeRecordingAttemptGeneration(AllocateGeneration())
    {}
    ~GpuTaskGraph()noexcept(false);


public:
    [[nodiscard]] Expected<GpuTaskId> addTask(const GpuTaskDesc& desc, const GpuTaskCommandRequirements& commands = {});
    [[nodiscard]] b8 setNormalExecutionPrelude(const GpuTaskId& task);

    [[nodiscard]] Expected<GpuTaskId> addCopyBufferTask(const GpuTaskDesc& desc, const GpuCopyBufferTaskDesc& copyDesc);

    [[nodiscard]] Expected<GpuTaskId> addCopyBufferRegions(const GpuTaskDesc& desc, const Span<const GpuCopyBufferTaskRegion> regions){
        return addCopyBufferTask(desc, { .regions = regions.data(), .regionCount = regions.size() });
    }

    [[nodiscard]] Expected<GpuTaskId> addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc);

    [[nodiscard]] Expected<GpuTaskId> addCopyTextureRegions(const GpuTaskDesc& desc, const Span<const GpuCopyTextureTaskRegion> regions, QueueSubmissionToken& acceptedToken){
        return addCopyTextureTask(desc, { .regions = regions.data(), .regionCount = regions.size(), .acceptedToken = &acceptedToken });
    }

    [[nodiscard]] Expected<GpuTaskId> addResolveTextureTask(const GpuTaskDesc& desc, const GpuResolveTextureTaskDesc& resolveDesc);

    [[nodiscard]] Expected<GpuUploadBlobId> copyUploadData(const void* data, usize byteSize, usize alignment = alignof(u8));

    [[nodiscard]] Expected<GpuUploadBlobId> copyAlignedUploadBytes(const BinaryByteView bytes, const usize alignment){
        return copyUploadData(bytes.data(), bytes.size(), alignment);
    }

    template<typename T>
    [[nodiscard]] Expected<GpuUploadBlobId> copyUploadElements(const Span<const T> elements){
        static_assert(IsTriviallyCopyable_V<T>, "GPU upload elements must be trivially copyable");
        if(elements.size() > Limit<usize>::s_Max / sizeof(T))
            return MakeUnexpected(Failure{});
        return copyUploadData(elements.data(), elements.size() * sizeof(T), alignof(T));
    }

    template<typename T>
    [[nodiscard]] Expected<GpuUploadBlobId> copyUploadValue(const T& value){
        return copyUploadElements<T>({ &value, 1u });
    }

    template<typename AlignmentT>
    [[nodiscard]] Expected<GpuUploadBlobId> copyUploadBytes(Span<const u8> bytes){
        return copyUploadData(bytes.data(), bytes.size(), alignof(AlignmentT));
    }

    [[nodiscard]] Expected<GpuTaskId> addUploadBufferTask(const GpuTaskDesc& desc, const GpuUploadBufferTaskDesc& uploadDesc);
    [[nodiscard]] Expected<GpuTaskId> addUploadTextureTask(const GpuTaskDesc& desc, const GpuUploadTextureTaskDesc& uploadDesc);

    [[nodiscard]] Expected<GpuTaskId> addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc);

    [[nodiscard]] Expected<GpuTaskId> addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc);

    [[nodiscard]] Expected<GpuTaskId> addClearTextureRectUIntTask(const GpuTaskDesc& desc, const GpuClearTextureRectUIntTaskDesc& clearDesc);

    template<typename TaskT>
    [[nodiscard]] Expected<GpuTaskId> addTask(const GpuTaskDesc& desc, typename TaskT::Payload&& payload){
        using Payload = typename TaskT::Payload;
        return declareTaskWithPayload<TaskT>(desc, [&](void* const storage){
            if constexpr(::LeadingArenaConstructible<Payload> && IsConstructible_V<Payload, GraphicsArena&, Payload&&>)
                return new(storage) Payload(m_arena, Move(payload));
            else
                return new(storage) Payload(Move(payload));
        });
    }

    template<typename Payload, typename Factory>
    [[nodiscard]] Expected<GpuTaskId> addPayloadTaskFrom(const GpuTaskDesc& desc, Factory&& factory){
        return declareTaskWithPayload<GpuGraphTaskContract::MemberTask<Payload>>(desc, [&](void* const storage){
            if constexpr(::LeadingArenaConstructible<Payload> && IsConstructible_V<Payload, GraphicsArena&, Payload&&>)
                return new(storage) Payload(m_arena, factory());
            else
                return new(storage) Payload(factory());
        });
    }

    template<typename Payload>
    [[nodiscard]] Expected<GpuTaskId> addPayloadTask(const GpuTaskDesc& desc, Payload&& payload) requires (!IsLValueReference<Payload>::value){
        return addTask<GpuGraphTaskContract::MemberTask<Payload>>(desc, Move(payload));
    }

    template<typename Payload>
    [[nodiscard]] Expected<GpuTaskId> addPreparedPayloadTask(const GpuTaskDesc& desc, Payload& payload) requires (!IsConst_V<Payload>){
        return addPayloadTask(desc, Move(payload));
    }

    template<typename TaskT>
    Expected<GpuTaskId> rejectInvalidBuiltinPayload(ProvisionalPayloadOwner<typename TaskT::Payload>& payload){
        using Payload = typename TaskT::Payload;
        discardAndDestroyUnappendedPayload(payload.release(), &DiscardTaskPayload<TaskT>, &DestroyTaskPayload<Payload>);
        return MakeUnexpected(Failure{});
    }

    template<typename TaskT>
    [[nodiscard]] Expected<GpuTaskId> appendBuiltinTaskWithinMutation(
        ProvisionalPayloadOwner<typename TaskT::Payload>& payload,
        const GpuTaskDesc& resolvedDesc,
        const DeclarationMutationScope& mutation,
        const GpuTaskCommandRequirements& commands = { .requiredCapabilities = GpuQueueCapability::Transfer }
    ){
        using Payload = typename TaskT::Payload;
        const auto task = appendTaskWithinMutation(resolvedDesc, commands, &RecordTaskPayload<TaskT>, &AcceptTaskPayload<TaskT>, &DiscardTaskPayload<TaskT>, &DestroyTaskPayload<Payload>, sizeof(Payload), mutation, payload.get());
        if(task)
            payload.publish();
        else
            discardAndDestroyUnappendedPayload(payload.release(), &DiscardTaskPayload<TaskT>, &DestroyTaskPayload<Payload>);
        return task;
    }

    [[nodiscard]] Expected<GpuGraphResourceId> importResource(const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importAccelStruct(const RayTracingAccelStructHandle& accelStruct, const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importHazardDomain(const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceVersionId> declareResourceVersion(const GpuGraphResourceVersionDesc& desc){
        return appendResourceVersion(desc);
    }
    [[nodiscard]] Expected<GpuGraphResourceSetId> importResourceSet(const GpuGraphResourceSetDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importPipeline(const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importGraphicsPipeline(const GraphicsPipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importComputePipeline(const ComputePipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuExternalCompletionId> importExternalCompletion(const GpuExternalCompletionDesc& desc);
    [[nodiscard]] b8 declarePresentEndpoint(const GpuPresentEndpoint& endpoint);

    [[nodiscard]] b8 tryReset();
    void reset();


private:
    template<typename TaskT, typename ConstructPayloadAt>
    [[nodiscard]] Expected<GpuTaskId> declareTaskWithPayload(const GpuTaskDesc& desc, ConstructPayloadAt&& constructAt){
        using Payload = typename TaskT::Payload;
        static_assert(IsNothrowDestructible_V<Payload>, "GPU task payload cleanup must remain no-throw");
        DeclarationMutationScope mutation(*this);
        if(!mutation.valid())
            return MakeUnexpected(Failure{});

        constexpr b8 s_UsePool = sizeof(Payload) <= s_PayloadPoolMaxPayloadBytes && alignof(Payload) <= s_PayloadPoolMaxAlign;
        void* rawStorage = nullptr;
        if constexpr(s_UsePool){
            rawStorage = allocatePooledTaskPayload(sizeof(Payload), alignof(Payload));
        }
        else{
            rawStorage = m_arena.allocate(alignof(Payload), sizeof(Payload));
        }
        if(!rawStorage)
            return MakeUnexpected(Failure{});

        Payload* payloadObject = nullptr;
        struct UnappendedStorageGuard final : NoCopy{
            GraphicsArena& arena;
            void* raw = nullptr;
            Payload* object = nullptr;
            GpuTaskPayloadDestroyThunk destroy = nullptr;
            b8 usePool = false;

            UnappendedStorageGuard(
                GraphicsArena& inArena,
                void* const inRaw,
                Payload* const inObject,
                const GpuTaskPayloadDestroyThunk inDestroy,
                const b8 inUsePool
            )noexcept
                : arena(inArena)
                , raw(inRaw)
                , object(inObject)
                , destroy(inDestroy)
                , usePool(inUsePool)
            {}
            ~UnappendedStorageGuard()noexcept{
                if(object && destroy)
                    destroy(arena, object);
                else if(raw && !usePool)
                    arena.deallocate(raw, alignof(Payload), sizeof(Payload));
            }
            Payload* release()noexcept{
                Payload* const released = object;
                raw = nullptr;
                object = nullptr;
                return released;
            }
        };
        constexpr GpuTaskPayloadDestroyThunk s_DestroyThunk = s_UsePool
            ? (std::is_trivially_destructible_v<Payload> ? nullptr : &DestroyPooledTaskPayload<Payload>)
            : &DestroyTaskPayload<Payload>;
        UnappendedStorageGuard guard{ m_arena, rawStorage, nullptr, s_DestroyThunk, s_UsePool };
        payloadObject = constructAt(rawStorage);
        guard.object = payloadObject;

        GpuTaskRecordThunk recordPayload = nullptr;
        if constexpr(GpuGraphTaskContract::RecordApi<TaskT>)
            recordPayload = &RecordTaskPayload<TaskT>;

        GpuTaskAcceptedThunk acceptPayload = nullptr;
        if constexpr(GpuGraphTaskContract::AcceptedApi<TaskT>)
            acceptPayload = &AcceptTaskPayload<TaskT>;

        GpuTaskDiscardedThunk discardPayload = nullptr;
        if constexpr(GpuGraphTaskContract::DiscardedApi<TaskT>)
            discardPayload = &DiscardTaskPayload<TaskT>;

        const auto task = appendTaskWithinMutation(desc, GpuGraphTaskContract::CommandRequirements<TaskT>(*payloadObject), recordPayload, acceptPayload, discardPayload, s_DestroyThunk, sizeof(Payload), mutation, payloadObject);
        if(task)
            guard.release();
        else
            discardAndDestroyUnappendedPayload(guard.release(), discardPayload, s_DestroyThunk);
        return task;
    }


    [[nodiscard]] u64 recordingAttemptGeneration()const noexcept;
    [[nodiscard]] b8 beginRecordingAttempt(const GpuCompiledGraph& compiledGraph, GpuSubmissionPacketId packet, const DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, GpuRecordingAttemptScope& outAttempt)const noexcept;
    void cancelRecordingAttempt(GpuRecordingAttemptScope& attempt)const noexcept;
    void completeRecordingPreparation(GpuRecordingAttemptScope& attempt)const noexcept;
    [[nodiscard]] b8 matchesRecordingAttempt(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] b8 resolveRecordingAttemptIfTerminal(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] b8 bindSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding, const GpuRecordingAttemptScope* preparationAttempt = nullptr)const noexcept;
    [[nodiscard]] b8 matchesSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 resolveSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 beginSubmissionExceptionClosing(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 waitForSubmissionExceptionRecordingClaims(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;


private:
    static constexpr usize s_ReaderStripeCount = 16u;
    static constexpr u64 s_AdmissionReaderStripesDirtyBit = 1ull << 59u;
    static constexpr u64 s_AdmissionWriterBit = 1ull << 60u;
    static constexpr u64 s_AdmissionWriterMask = s_AdmissionWriterBit;
    static constexpr u64 s_AdmissionWaiterBit = 1ull << 61u;
    static constexpr u64 s_AdmissionPlanActiveBit = 1ull << 62u;
    static constexpr u64 s_AdmissionTeardownBit = 1ull << 63u;

    struct alignas(64) DeclarationReaderStripe{
        Atomic<u32> count{ 0u };
    };


private:
    [[nodiscard]] static u32 CachedCurrentThreadId()noexcept{
        thread_local const u32 id = []()noexcept{
            const u32 tid = CurrentThreadId();
            return tid != 0u ? tid : 1u;
        }();
        return id;
    }

    [[nodiscard]] static u8 LocalReaderStripeIndex()noexcept{
        static Atomic<u32> s_NextStripe{ 0u };
        thread_local const u8 stripe = static_cast<u8>(s_NextStripe.fetch_add(1u, MemoryOrder::relaxed) & (s_ReaderStripeCount - 1u));
        return stripe;
    }

    [[nodiscard]] b8 hasAnyActiveDeclarationReader(const MemoryOrder order)const noexcept{
        for(usize stripeIndex = 0u; stripeIndex < s_ReaderStripeCount; ++stripeIndex){
            if(m_readerStripes[stripeIndex].count.load(order) != 0u)
                return true;
        }
        return false;
    }

    [[nodiscard]] b8 isTeardownInProgress(const MemoryOrder order = MemoryOrder::acquire)const noexcept{
        return (m_declarationAdmissionState.load(order) & s_AdmissionTeardownBit) != 0u;
    }

    [[nodiscard]] b8 tryBeginTeardownWithinLock()const noexcept{
        u64 state = m_declarationAdmissionState.load(MemoryOrder::acquire);
        for(;;){
            if((state & (s_AdmissionTeardownBit | s_AdmissionWriterBit)) != 0u)
                return false;
            const b8 dirty = (state & s_AdmissionReaderStripesDirtyBit) != 0u;
            if(dirty && hasAnyActiveDeclarationReader(MemoryOrder::relaxed))
                return false;
            if(m_declarationAdmissionState.compare_exchange_weak(state, state | s_AdmissionTeardownBit, MemoryOrder::seq_cst, MemoryOrder::acquire)){
                if(dirty){
                    if(hasAnyActiveDeclarationReader(MemoryOrder::seq_cst)){
                        m_declarationAdmissionState.fetch_and(~s_AdmissionTeardownBit, MemoryOrder::acq_rel);
                        return false;
                    }
                    m_declarationAdmissionState.fetch_and(~s_AdmissionReaderStripesDirtyBit, MemoryOrder::release);
                }
                return true;
            }
        }
    }

    [[nodiscard]] b8 matchesPacketAttemptWithinLock(const GpuCompiledGraph& compiledGraph, const u64 planGeneration, const u64 recordingAttemptGeneration)const noexcept{
        return
            !isTeardownInProgress(MemoryOrder::relaxed)
            && m_activeCompiledGraph == &compiledGraph
            && m_activeRecordingPlanGeneration.load(MemoryOrder::relaxed) == planGeneration
            && m_activeRecordingAttemptGeneration.load(MemoryOrder::relaxed) == recordingAttemptGeneration
        ;
    }

    [[nodiscard]] Expected<void, RecordTaskFailure> recordTask(const GpuTaskId& taskID, CommandList& commandList, const GpuTaskRecordContext& context, const GpuPacketRecordingLease& lease)const;

    void discardPacketTasksWithinLock(Span<const GpuTaskId> tasks, u64 notificationGeneration = 0u)const noexcept;

    [[nodiscard]] b8 beginPacketRecording(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        GpuPacketRecordingLease& outLease,
        GpuPacketRecordingAccess* outAccess = nullptr
    )const noexcept;
    [[nodiscard]] b8 beginPacketRecordingBatch(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        Span<const GpuSubmissionPacketId> packets,
        u64 recordingAttemptGeneration,
        GpuPacketRecordingLease* outLeases,
        GpuPacketRecordingAccess* outAccesses
    )const noexcept;
    [[nodiscard]] b8 completePacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const noexcept;
    [[nodiscard]] b8 completePacketRecordingBatch(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        Span<const GpuSubmissionPacketId> packets,
        GpuPacketRecordingLease* const* leases
    )const noexcept;
    void rollbackUnstartedPacketRecordingBatch(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        Span<const GpuSubmissionPacketId> packets,
        GpuPacketRecordingLease* const* leases
    )const noexcept;
    [[nodiscard]] b8 deferPacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease, GpuPacketRecordingAbort& outAbort)const noexcept;
    [[nodiscard]] b8 completePacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuPacketRecordingAbort& abort)const;
    [[nodiscard]] b8 abandonPacketRecordingWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const noexcept;
    [[nodiscard]] b8 abandonPacketRecordingAbortWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuPacketRecordingAbort& abort)const noexcept;
    void abortPacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const;
    [[nodiscard]] b8 packetReadyForSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] b8 packetsReadyForSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketRange range, u64 recordingAttemptGeneration)const noexcept;


private:
    [[nodiscard]] b8 beginPacketSubmission(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding,
        GpuPacketSubmissionLease& outLease
    )const noexcept;
    [[nodiscard]] usize beginPacketSubmissionBatch(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        Span<const GpuSubmissionPacketId> packets,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding,
        GpuPacketSubmissionLease* const* outLeases
    )const noexcept;
    void beginPacketSubmissionAcceptance(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, const QueueSubmissionToken& token, GpuPacketSubmissionLease& lease)const noexcept;
    void beginPacketSubmissionAcceptanceBatch(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, Span<const GpuSubmissionPacketId> packets, const QueueSubmissionReceipt* receipts, GpuPacketSubmissionLease* const* leases)const noexcept;
    void notifyPacketSubmissionAccepted(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, const QueueSubmissionToken& token, const GpuPacketSubmissionLease& lease)const;
    void completePacketSubmissionAcceptance(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const noexcept;
    void completePacketSubmissionAcceptanceBatch(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, Span<const GpuSubmissionPacketId> packets, GpuPacketSubmissionLease* const* leases)const noexcept;
    [[nodiscard]] b8 acceptPacketSubmissionBatchWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        Span<const GpuSubmissionPacketId> packets,
        const QueueSubmissionReceipt* receipts,
        GpuPacketSubmissionLease* const* leases
    )const noexcept;
    void abortPacketSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const;
    [[nodiscard]] b8 abandonPacketSubmissionWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const noexcept;
    [[nodiscard]] b8 discardUnacceptedPacket(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const;
    [[nodiscard]] b8 abandonUnacceptedPacketWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;


private:
    [[nodiscard]] b8 applyCompiledBarrier(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPacketRecordingAccess& recordingAccess,
        const GpuTaskId& task,
        const GpuCompiledBarrier& barrier,
        CommandList& commandList
    )const;
    [[nodiscard]] b8 applyCompiledBarriers(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPacketRecordingAccess& recordingAccess,
        const GpuTaskId& task,
        Span<const GpuCompiledBarrier> barriers,
        CommandList& commandList
    )const;
    [[nodiscard]] b8 applyCompiledBarrierValidated(
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuCompiledBarrier& barrier,
        CommandList& commandList
    )const;
    [[nodiscard]] b8 seedTaskRetainedResourceStates(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPacketRecordingAccess& recordingAccess, const GpuTaskId& taskID, CommandList& commandList)const;


private:
    [[nodiscard]] b8 validForDeviceGeneration(u16 deviceGeneration)const noexcept;
    [[nodiscard]] b8 validTask(const GpuTaskId& id)const noexcept{
        return id.valid() && id.generation == m_generation && id.index < m_tasks.size();
    }

    [[nodiscard]] b8 validResource(const GpuGraphResourceId& id)const noexcept{
        return id.valid() && id.generation == m_generation && id.index < m_resources.size();
    }

    [[nodiscard]] b8 validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept{
        return id.valid() && id.generation == m_generation && id.index < m_resourceVersions.size();
    }

    [[nodiscard]] b8 validResourceSet(const GpuGraphResourceSetId& id)const noexcept{
        return id.valid() && id.generation == m_generation && id.index < m_resourceSets.size();
    }

    [[nodiscard]] b8 validUploadBlob(const GpuUploadBlobId& id)const noexcept{
        return id.valid() && id.generation == m_generation && id.index < m_uploadBlobs.size() && m_uploadBlobs[id.index].dataSize != 0u;
    }

    [[nodiscard]] b8 validPipeline(const GpuGraphPipelineId& id)const noexcept{
        return id.valid() && id.generation == m_generation && id.index < m_pipelines.size();
    }

    [[nodiscard]] b8 validExternalCompletion(const GpuExternalCompletionId& id)const noexcept{
        return id.valid() && id.generation == m_generation && id.index < m_externalCompletions.size();
    }
    [[nodiscard]] GpuTaskGraphTaskView taskAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphResourceView resourceAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphResourceVersionView resourceVersionAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphResourceSetView resourceSetAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphPipelineView pipelineAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphExternalCompletionView externalCompletionAt(usize index)const noexcept;
    [[nodiscard]] const QueueSubmissionToken* externalCompletionToken(const GpuExternalCompletionId& completion)const noexcept;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] RayTracingAccelStruct* accelStructForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] Expected<BinaryByteView> uploadBlobData(const GpuUploadBlobId& blob)const noexcept;
    [[nodiscard]] GraphicsPipeline* graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;

    [[nodiscard]] Expected<GpuTaskId> appendTaskWithinMutation(
        const GpuTaskDesc& desc,
        const GpuTaskCommandRequirements& commands,
        GpuTaskRecordThunk recordPayload,
        GpuTaskAcceptedThunk acceptPayload,
        GpuTaskDiscardedThunk discardPayload,
        GpuTaskPayloadDestroyThunk destroyPayload,
        usize payloadObjectSize,
        const DeclarationMutationScope& mutationAccess,
        void* payload
    );
    void discardAndDestroyUnappendedPayload(void* payload, GpuTaskDiscardedThunk discardPayload, GpuTaskPayloadDestroyThunk destroyPayload);
    [[nodiscard]] u32 findPipelineIdentity(const Name& identity)const noexcept;
    [[nodiscard]] PipelineImportMatch findPipelineImportMatch(const Name& identity, const PipelinePointerKey& pointer)const noexcept;
    [[nodiscard]] u32 findExternalCompletionIdentity(const Name& identity)const noexcept;
    void preparePipelineIndexes(const PipelinePointerKey& pendingPointer);
    void prepareExternalCompletionIndex();
    [[nodiscard]] Expected<GpuGraphResourceId> appendResourceWithinMutation(const GpuGraphResourceDesc& desc, const ResourceQueueAdmissionSnapshot* queueAdmission, const ResourceBinding& binding, const DeclarationMutationScope& mutationAccess);
    [[nodiscard]] Expected<GpuGraphResourceVersionId> appendResourceVersion(const GpuGraphResourceVersionDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceSetId> appendResourceSet(const GpuGraphResourceSetDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> appendPipeline(const GpuGraphPipelineDesc& desc, const PipelineBinding& binding);
    [[nodiscard]] Expected<GpuExternalCompletionId> appendExternalCompletion(const GpuExternalCompletionDesc& desc);
    [[nodiscard]] const GpuUploadBlobNode* findUploadBlob(const GpuUploadBlobId& blob)const noexcept;


    [[nodiscard]] MarkerLabelRange appendMarkerLabel(AStringView text);
    [[nodiscard]] AStringView markerLabel(u32 offset, u32 size)const noexcept;
    void bumpDeclarationRevision()noexcept{
        if(m_declarationRevision == Limit<u64>::s_Max)
            FOUNDATION_FATAL_ASSERT_MSG(false, "GPU task graph declaration revision space is exhausted");
        ++m_declarationRevision;
    }
    [[nodiscard]] GlobalUniquePtr<CommandListResourceStateHandoff> acquireStateSnapshot();
    void recycleOrDestroyStateSnapshot(NotNull<CommandListResourceStateHandoff*> states)noexcept;
    void destroyReusableStateSnapshots()noexcept;
    [[nodiscard]] b8 retireTaskPayloads();
    [[nodiscard]] b8 destroyTaskPayloads();
    [[nodiscard]] b8 destroyTaskPayloadsWithoutCallbacks()noexcept;
    void releasePacketRecordingClaimWithinLock(u32 releasedCount = 1u)const noexcept;
    void destroyTaskPayloadObjects()noexcept;
    void destroyTaskStateSnapshots()noexcept;
    void destroyResourceStateSnapshots()noexcept;
    void completeResetWithinLock()noexcept;
    void completeResetWithoutCallbacks(b8 payloadsAlreadyDestroyed = false)noexcept;


private:
    GraphicsArena& m_arena;
    Alloc::ScratchArena m_declarationScratch;
    mutable Atomic<u32> m_writerOwnerThread{ 0u };
    mutable u32 m_writerRecursionDepth = 0u;
    mutable Futex m_lifecycleMutex;
    GraphicsVector<GpuTaskNode> m_tasks;
    GraphicsVector<GpuTaskId> m_dependencies;
    GpuTaskId m_normalExecutionPrelude;
    GraphicsVector<GpuExternalCompletionId> m_externalDependencies;
    GraphicsVector<GpuTaskExternalStateSource> m_externalStateSources;
    GraphicsVector<NotNull<CommandListResourceStateHandoff*>> m_externalStateSnapshots;
    GraphicsVector<GpuTaskResourceUse> m_resourceUses;
    GraphicsVector<GpuTaskResourceVersionUse> m_resourceVersionUses;
    GraphicsVector<GpuGraphResourceNode> m_resources;
    GpuGraphResourceImportIndex m_resourceImportIndexes;
    GraphicsVector<GpuGraphResourceVersionNode> m_resourceVersions;
    GraphicsVector<GpuTaskGraphInitialOwnerHandoffSourceView> m_initialOwnerHandoffSources;
    GraphicsVector<NotNull<CommandListResourceStateHandoff*>> m_initialOwnerHandoffStateSnapshots;
    GraphicsVector<NotNull<CommandListResourceStateHandoff*>> m_reusableStateSnapshots;
    GraphicsVector<u32> m_queueFamilyIndices;
    GraphicsVector<GpuGraphResourceSetNode> m_resourceSets;
    GraphicsVector<GpuGraphResourceId> m_resourceSetMembers;
    GraphicsVector<GpuGraphPipelineNode> m_pipelines;
    Optional<ImportIdentityIndex> m_pipelineIdentityIndex;
    Optional<PipelinePointerIndex> m_pipelinePointerIndex;
    GraphicsVector<GpuExternalCompletionNode> m_externalCompletions;
    Optional<ImportIdentityIndex> m_externalCompletionIdentityIndex;
    GraphicsVector<GpuUploadBlobNode> m_uploadBlobs;
    GraphicsVector<GpuUploadBlobStorageBlock> m_uploadBlobStorage;
    GraphicsVector<GraphicsBytes> m_overalignedUploadBlobs;
    GraphicsBytes m_markerText;
    GpuPresentEndpoint m_presentEndpoint;
    usize m_uploadBlobStorageByteSize = 0u;
    GraphicsVector<TaskPayloadPoolBlock> m_payloadPoolBlocks;
    usize m_payloadPoolCurrentBlock = 0u;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    mutable Atomic<u64> m_activeRecordingAttemptGeneration{ 0u };
    mutable Atomic<u64> m_activeRecordingPlanGeneration{ 0u };
    mutable u64 m_activeRecordingPreparationSerial = 0u;
    mutable const GpuCompiledGraph* m_activeCompiledGraph = nullptr;
    mutable GpuGraphSubmissionBinding m_activeSubmissionBinding;
    alignas(64) mutable Atomic<u64> m_declarationAdmissionState{ 0u };
    mutable Array<DeclarationReaderStripe, s_ReaderStripeCount> m_readerStripes{};
    mutable u32 m_activeDiscardNotificationCount = 0u;
    mutable u32 m_exceptionRecordingClaimWaiters = 0u;
    mutable Atomic<u32> m_activePacketRecordingClaimCount{ 0u };
    mutable SubmissionBindingState::Enum m_submissionBindingState = SubmissionBindingState::None;
    u16 m_externalCompletionDeviceGeneration = 0u;
    b8 m_hasPresentEndpoint = false;
    b8 m_hasTaskPayloads = false;
    b8 m_hasNonTrivialTaskPayloads = false;
    b8 m_hasDiscardCallbacks = false;
    b8 m_hasAcceptedQueueFrontierOrRecoveryTask = false;
    mutable b8 m_hasTaskLifecycleActivity = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline GpuTaskGraph::DeclarationMutationScope::DeclarationMutationScope(GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{
    static_assert(Atomic<u64>::is_always_lock_free, "GpuTaskGraph declaration admission atomic must remain lock-free");

    const u32 threadId = GpuTaskGraph::CachedCurrentThreadId();
    if(m_graph.m_writerOwnerThread.load(MemoryOrder::relaxed) == threadId){
        ++m_graph.m_writerRecursionDepth;
        m_valid = true;
        return;
    }

    u64 expected = 0u;
    if(m_graph.m_declarationAdmissionState.compare_exchange_strong(expected, GpuTaskGraph::s_AdmissionWriterBit, MemoryOrder::seq_cst, MemoryOrder::acquire)){
        m_graph.m_writerOwnerThread.store(threadId, MemoryOrder::relaxed);
        m_graph.m_writerRecursionDepth = 1u;
        m_valid = true;
        m_isRootWriter = true;
        return;
    }

    acquireSlow(threadId, expected);
}

inline GpuTaskGraph::DeclarationMutationScope::~DeclarationMutationScope()noexcept{
    if(!m_valid)
        return;

    if(!m_isRootWriter){
        if(m_graph.m_writerRecursionDepth == 0u)
            FOUNDATION_FATAL_ASSERT_MSG(false, "GpuTaskGraph declaration mutation recursion depth must stay balanced");
        --m_graph.m_writerRecursionDepth;
        return;
    }

    m_graph.m_writerRecursionDepth = 0u;
    m_graph.m_writerOwnerThread.store(0u, MemoryOrder::relaxed);
    u64 expected = GpuTaskGraph::s_AdmissionWriterBit;
    if(!m_graph.m_declarationAdmissionState.compare_exchange_strong(expected, 0u, MemoryOrder::release, MemoryOrder::relaxed))
        releaseSlow();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

