// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareGeometryInputs{
    const PreparedMeshBlasBuildVector& blasBuilds;
    const PreparedMeshSwBvhBuildVector& softwareBuilds;
    const Core::GpuGraphResourceId* traceResources = nullptr;
    usize traceResourceCount = 0u;
    bool blasBuildsGraphOwned = false;
};

// One declaration owns selection and scratch; inputs stay frozen, phases run once.
class ShadowPrepareGeometryResources final : NoCopy{
private:
    struct BufferRequest{
        const Core::BufferHandle* source = nullptr;
        Core::GpuGraphResourceId resource;
        u8 requestedRoles = 0u;
        u8 selectedRoles = 0u;
        bool listedForTrace = false;
    };
    struct InlineRequest{
        Core::Buffer* buffer = nullptr;
        BufferRequest request;
    };
    using RequestIndex = HashMap<Core::Buffer*, BufferRequest, Core::Alloc::ScratchArena>;
    using ResourceIndex = HashMap<u32, BufferRequest*, Core::Alloc::ScratchArena>;
    using NameReference = NotNull<const NameHash*>;
    struct NameReferenceHash{
        [[nodiscard]] usize operator()(const NameReference& identity)const noexcept{
            return Hasher<NameHash>{}(*identity);
        }
    };
    struct NameReferenceEqual{
        [[nodiscard]] bool operator()(const NameReference& lhs, const NameReference& rhs)const noexcept{
            return *lhs == *rhs;
        }
    };
    using NameIndex = HashSet<NameReference, NameReferenceHash, NameReferenceEqual, Core::Alloc::ScratchArena>;


private:
    static constexpr usize s_InlineCount = 32u;
    static constexpr u8 s_BlasRole = 1u;
    static constexpr u8 s_SoftwareRole = 2u;


public:
    using ResourceVector = Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>;


public:
    ShadowPrepareGeometryResources(const ShadowPrepareGeometryInputs& inputs, Core::Alloc::ScratchArena& scratchArena);
    ShadowPrepareGeometryResources(ShadowPrepareGeometryResources&&) = delete;


public:
    // Collect identities without reading graph state; size outputs from distinct buffers.
    void prepareStorage(bool blasInputStatesGraphOwned, bool softwareInputStatesGraphOwned);
    void gatherBuildInputs(
        const Core::GpuTaskGraph& graph,
        bool& blasInputStatesGraphOwned,
        bool& softwareInputStatesGraphOwned
    );
    [[nodiscard]] bool gatherRemainingTraceResources();
    [[nodiscard]] bool isPreparedMeshBlasBuild(const Name& meshName)const;


private:
    void addRequest(const Core::BufferHandle& buffer, u8 role);
    [[nodiscard]] BufferRequest* findRequest(Core::Buffer* buffer);
    void promoteRequests();
    [[nodiscard]] bool resolveRequests(const Core::GpuTaskGraph& graph);
    [[nodiscard]] BufferRequest* findResolvedRequest(Core::GpuGraphResourceId resource);
    [[nodiscard]] bool appendBuildInput(ResourceVector& resources, const Core::BufferHandle& buffer, u8 role);
    void clearBuildInputs()noexcept;
    void prepareNames()const;


public:
    ResourceVector m_blasBuildInputs;
    ResourceVector m_softwareTailInputs;
    ResourceVector m_remainingTraceGeometry;


private:
    const ShadowPrepareGeometryInputs m_inputs;
    Core::Alloc::ScratchArena& m_scratchArena;
    InlineRequest m_inlineRequests[s_InlineCount] = {};
    usize m_inlineRequestCount = 0u;
    Optional<RequestIndex> m_requests;
    // Request storage is complete before the index retains pointers; it never grows after.
    Optional<ResourceIndex> m_resources;
    usize m_blasRequestCount = 0u;
    usize m_softwareRequestCount = 0u;
    u64 m_graphGeneration = 0u;
    bool m_preparedBlasPolicy = false;
    bool m_preparedSoftwarePolicy = false;
    bool m_storagePrepared = false;
    bool m_inputsGathered = false;
    // Prepared build names stay frozen and outlive this operation; membership borrows them.
    mutable const NameHash* m_inlineNames[s_InlineCount];
    mutable usize m_inlineNameCount = 0u;
    mutable Optional<NameIndex> m_names;
    mutable bool m_namesPrepared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

