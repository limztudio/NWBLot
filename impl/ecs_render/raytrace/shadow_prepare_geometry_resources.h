// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareGeometryInputs{
    Span<const PreparedMeshBlasBuild> blasBuilds{};
    Span<const Core::GpuGraphResourceId> traceResources{};
    b8 blasBuildsGraphOwned = false;
};

// One declaration owns selection and scratch; inputs stay frozen, phases run once.
class ShadowPrepareGeometryResources final : NoCopy{
public:
    using ResourceVector = Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>;


private:
    struct BufferRequest{
        const Core::BufferHandle* source = nullptr;
        Core::GpuGraphResourceId resource;
        b8 listedForTrace = false;
        b8 selected = false;
    };
    struct InlineRequest{
        Core::Buffer* buffer = nullptr;
        BufferRequest request;
    };


private:
    using RequestIndex = HashMap<NotNull<Core::Buffer*>, BufferRequest, Core::Alloc::ScratchArena>;
    using ResourceIndex = HashMap<u32, NotNull<BufferRequest*>, Core::Alloc::ScratchArena>;
    using NameReference = NotNull<const NameHash*>;


private:
    struct NameReferenceHash{
        [[nodiscard]] usize operator()(const NameReference& identity)const noexcept{
            return Hasher<NameHash>{}(*identity);
        }
    };
    struct NameReferenceEqual{
        [[nodiscard]] b8 operator()(const NameReference& lhs, const NameReference& rhs)const noexcept{
            return *lhs == *rhs;
        }
    };


private:
    using NameIndex = HashSet<NameReference, Core::Alloc::ScratchArena, NameReferenceHash, NameReferenceEqual>;


private:
    static constexpr usize s_InlineCount = 32u;
    static constexpr usize s_PromotedIndexCapacity = s_InlineCount * 4u;


public:
    ShadowPrepareGeometryResources(const ShadowPrepareGeometryInputs& inputs, Core::Alloc::ScratchArena& scratchArena)noexcept;
    ShadowPrepareGeometryResources(ShadowPrepareGeometryResources&&) = delete;


public:
    // Collect identities without reading graph state; size outputs from distinct buffers.
    void prepareStorage(b8 blasInputStatesGraphOwned);
    [[nodiscard]] b8 gatherBuildInputs(const Core::GpuTaskGraph& graph);
    [[nodiscard]] b8 gatherRemainingTraceResources()noexcept;
    [[nodiscard]] b8 isPreparedMeshBlasBuild(const Name& meshName)const;


private:
    void addRequest(const Core::BufferHandle& buffer);
    [[nodiscard]] BufferRequest* findRequest(Core::Buffer* buffer)noexcept;
    void promoteRequests();
    [[nodiscard]] b8 resolveRequests(const Core::GpuTaskGraph& graph);
    [[nodiscard]] BufferRequest* findResolvedRequest(Core::GpuGraphResourceId resource)noexcept;
    [[nodiscard]] b8 appendBuildInput(const Core::BufferHandle& buffer)noexcept;
    void clearBuildInputs()noexcept;
    void prepareNames()const;


public:
    ResourceVector m_blasBuildInputs;
    ResourceVector m_remainingTraceGeometry;


private:
    const ShadowPrepareGeometryInputs m_inputs;
    Core::Alloc::ScratchArena& m_scratchArena;
    InlineRequest m_inlineRequests[s_InlineCount] = {};
    usize m_inlineRequestCount = 0u;
    Optional<RequestIndex> m_requests;
    // Request storage is complete before the index retains pointers; it never grows after.
    Optional<ResourceIndex> m_resources;
    u64 m_graphGeneration = 0u;
    // Prepared build names stay frozen and outlive this operation; membership borrows them.
    mutable const NameHash* m_inlineNames[s_InlineCount];
    mutable usize m_inlineNameCount = 0u;
    mutable Optional<NameIndex> m_names;
    b8 m_preparedBlasPolicy = false;
    b8 m_storagePrepared = false;
    b8 m_inputsGathered = false;
    mutable b8 m_namesPrepared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

